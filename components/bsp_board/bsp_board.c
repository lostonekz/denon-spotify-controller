#include "bsp_board.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "bsp_pins.h"

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static bsp_io_expander_t       s_io_expander;
static bool                    s_io_expander_ready = false;

#define BSP_BOARD_I2C_RECOVER_PULSES 9   /* worst case: a slave mid-byte holds SDA low for
                                            * up to 9 bits (8 data + 1 ack) -- clocking 9
                                            * times always lets it finish and release */
#define BSP_BOARD_I2C_RECOVER_DELAY_US 5   /* ~100kHz bit-bang rate, well under any slave's
                                             * own timeout */

void bsp_board_i2c_recover(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_I2C_SDA) | (1ULL << BSP_I2C_SCL),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(BSP_I2C_SCL, 1);
    gpio_set_level(BSP_I2C_SDA, 1);
    esp_rom_delay_us(BSP_BOARD_I2C_RECOVER_DELAY_US);

    if (gpio_get_level(BSP_I2C_SDA) == 1) {
        return;   /* not actually wedged -- SDA already idle-high */
    }

    for (int i = 0; i < BSP_BOARD_I2C_RECOVER_PULSES && gpio_get_level(BSP_I2C_SDA) == 0; i++) {
        gpio_set_level(BSP_I2C_SCL, 0);
        esp_rom_delay_us(BSP_BOARD_I2C_RECOVER_DELAY_US);
        gpio_set_level(BSP_I2C_SCL, 1);
        esp_rom_delay_us(BSP_BOARD_I2C_RECOVER_DELAY_US);
    }

    /* Manual STOP: SDA low-to-high while SCL is high. */
    gpio_set_level(BSP_I2C_SDA, 0);
    esp_rom_delay_us(BSP_BOARD_I2C_RECOVER_DELAY_US);
    gpio_set_level(BSP_I2C_SCL, 1);
    esp_rom_delay_us(BSP_BOARD_I2C_RECOVER_DELAY_US);
    gpio_set_level(BSP_I2C_SDA, 1);
    esp_rom_delay_us(BSP_BOARD_I2C_RECOVER_DELAY_US);
}

esp_err_t bsp_board_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;   /* already installed */
    }

    bsp_board_i2c_recover();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = BSP_I2C_PORT,
        .sda_io_num        = BSP_I2C_SDA,
        .scl_io_num        = BSP_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_io_expander_init(&s_io_expander, s_i2c_bus);
    if (err != ESP_OK) {
        return err;
    }
    s_io_expander_ready = true;
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_board_i2c_bus(void)
{
    return s_i2c_bus;
}

bsp_io_expander_t *bsp_board_io_expander(void)
{
    return s_io_expander_ready ? &s_io_expander : NULL;
}
