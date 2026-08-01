#include "hal_touch.h"

#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_gt911.h"

#include "bsp_pins.h"

#define HAL_TOUCH_PROBE_TIMEOUT_MS 100

esp_err_t hal_touch_init(hal_touch_t *touch, i2c_master_bus_handle_t i2c_bus, bsp_io_expander_t *expander)
{
    (void)expander;   /* touch reset already released by
                        * bsp_io_expander_power_up_display(), run once from
                        * hal_display_init() before this is called -- the
                        * real hardware shares one reset line between LCD
                        * and touch, there is no independent touch reset. */

    /* No dedicated INT pin exists on the real board (confirmed against the
     * real vendor source: int_gpio_num = GPIO_NUM_NC) and therefore no
     * INT-pin address-strapping trick either -- an earlier draft of this
     * driver held BSP_TOUCH_INT low across reset to force address 0x5D,
     * using a GPIO (38) that turned out to actually be this board's real
     * HSYNC pin. The real approach is simpler: just probe both possible
     * addresses and use whichever one ACKs, exactly as the real vendor's
     * own bsp_touch_new() does. */
    uint8_t dev_addr;
    if (i2c_master_probe(i2c_bus, BSP_TOUCH_I2C_ADDR, HAL_TOUCH_PROBE_TIMEOUT_MS) == ESP_OK) {
        dev_addr = BSP_TOUCH_I2C_ADDR;
    } else if (i2c_master_probe(i2c_bus, BSP_TOUCH_I2C_ADDR_BACKUP, HAL_TOUCH_PROBE_TIMEOUT_MS) == ESP_OK) {
        dev_addr = BSP_TOUCH_I2C_ADDR_BACKUP;
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.dev_addr = dev_addr;
    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &touch->io);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_touch_config_t touch_config = {
        .x_max          = BSP_LCD_H_RES,
        .y_max           = BSP_LCD_V_RES,
        .rst_gpio_num  = GPIO_NUM_NC,   /* shared with LCD reset, already released */
        .int_gpio_num   = GPIO_NUM_NC,   /* no INT pin on the real board */
        .levels.reset      = 0,
        .levels.interrupt = 0,
    };
    err = esp_lcd_touch_new_i2c_gt911(touch->io, &touch_config, &touch->handle);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(touch->io);
        touch->io = NULL;
        return err;
    }
    return ESP_OK;
}
