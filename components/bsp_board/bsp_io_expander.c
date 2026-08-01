#include "bsp_io_expander.h"

#include "esp_log.h"

#include "bsp_pins.h"

static const char *TAG = "bsp_io_expander";

#define CH32V003_XFER_TIMEOUT_MS 50
#define CH32V003_MAX_ATTEMPTS    3
#define CH32V003_RETRY_DELAY_MS  5

#define BSP_IO_EXPANDER_POWER_UP_HOLD_MS    200
#define BSP_IO_EXPANDER_POWER_UP_RELEASE_MS 200

esp_err_t bsp_io_expander_write_reg(bsp_io_expander_t *exp, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = { reg, value };
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < CH32V003_MAX_ATTEMPTS; attempt++) {
        err = i2c_master_transmit(exp->dev, payload, sizeof(payload), CH32V003_XFER_TIMEOUT_MS);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(CH32V003_RETRY_DELAY_MS));
    }
    ESP_LOGW(TAG, "write reg 0x%02x=0x%02x failed after %d attempts: %s",
             reg, value, CH32V003_MAX_ATTEMPTS, esp_err_to_name(err));
    return err;
}

esp_err_t bsp_io_expander_init(bsp_io_expander_t *exp, i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_IO_EXPANDER_ADDR,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &exp->dev);
    if (err != ESP_OK) {
        return err;
    }

    exp->lock = xSemaphoreCreateMutex();
    if (exp->lock == NULL) {
        i2c_master_bus_rm_device(exp->dev);
        return ESP_ERR_NO_MEM;
    }

    /* Every pin output except RTC_INT (input, unused). Output register
     * starts at 0x00 -- every reset line this project drives through the
     * expander (LCD, touch) is active-low, so "held in reset until
     * explicitly released" is the correct, safe power-up default. */
    err = bsp_io_expander_write_reg(exp, BSP_IO_EXPANDER_REG_DIRECTION, BSP_EXP_DIRECTION);
    if (err != ESP_OK) {
        vSemaphoreDelete(exp->lock);
        i2c_master_bus_rm_device(exp->dev);
        return err;
    }

    exp->output_shadow = 0x00;
    err = bsp_io_expander_write_reg(exp, BSP_IO_EXPANDER_REG_OUTPUT, exp->output_shadow);
    if (err != ESP_OK) {
        vSemaphoreDelete(exp->lock);
        i2c_master_bus_rm_device(exp->dev);
        return err;
    }
    return ESP_OK;
}

esp_err_t bsp_io_expander_set_output_bits(bsp_io_expander_t *exp, uint8_t bits, bool level)
{
    xSemaphoreTake(exp->lock, portMAX_DELAY);
    uint8_t next = level ? (uint8_t)(exp->output_shadow | bits)
                          : (uint8_t)(exp->output_shadow & ~bits);
    esp_err_t err = bsp_io_expander_write_reg(exp, BSP_IO_EXPANDER_REG_OUTPUT, next);
    if (err == ESP_OK) {
        exp->output_shadow = next;
    }
    xSemaphoreGive(exp->lock);
    return err;
}

esp_err_t bsp_io_expander_power_up_display(bsp_io_expander_t *exp)
{
    /* Assert reset on LCD+touch, buzzer off. Ported from the real vendor's
     * bsp_display_new() -- both lines come out of reset TOGETHER, later,
     * not independently. */
    esp_err_t err = bsp_io_expander_set_output_bits(exp,
        BSP_EXP_BIT_BUZZER_EN | BSP_EXP_BIT_LCD_RESET | BSP_EXP_BIT_TOUCH_RESET, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(BSP_IO_EXPANDER_POWER_UP_HOLD_MS));

    /* Release LCD+touch reset and enable system power together. */
    err = bsp_io_expander_set_output_bits(exp,
        BSP_EXP_BIT_SYS_EN | BSP_EXP_BIT_LCD_RESET | BSP_EXP_BIT_TOUCH_RESET, true);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(BSP_IO_EXPANDER_POWER_UP_RELEASE_MS));
    return ESP_OK;
}

esp_err_t bsp_io_expander_set_pwm(bsp_io_expander_t *exp, uint8_t value)
{
    xSemaphoreTake(exp->lock, portMAX_DELAY);
    esp_err_t err = bsp_io_expander_write_reg(exp, BSP_IO_EXPANDER_REG_PWM, value);
    xSemaphoreGive(exp->lock);
    return err;
}
