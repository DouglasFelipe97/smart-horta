#include "app_pcf8574.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "app_pin_config.h"

#define PCF8574_WRITE_TIMEOUT_TICKS pdMS_TO_TICKS(100)

static uint8_t output_levels = 0xFFU;
static bool initialized;
static SemaphoreHandle_t output_mutex;

esp_err_t app_pcf8574_init(void)
{
    if (output_mutex == NULL) {
        output_mutex = xSemaphoreCreateMutex();
        if (output_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const uint8_t initial_levels = 0xFFU;
    esp_err_t err = i2c_master_write_to_device(
        APP_RTC_I2C_PORT,
        APP_PCF8574_I2C_ADDR,
        &initial_levels,
        sizeof(initial_levels),
        PCF8574_WRITE_TIMEOUT_TICKS
    );
    if (err == ESP_OK) {
        output_levels = initial_levels;
        initialized = true;
    }

    return err;
}

esp_err_t app_pcf8574_write_output(app_pcf8574_output_t output, bool high)
{
    if ((unsigned)output >= (unsigned)APP_OUTPUT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized || output_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(output_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t updated_levels = output_levels;
    if (high) {
        updated_levels |= (uint8_t)(1U << output);
    } else {
        updated_levels &= (uint8_t)~(1U << output);
    }

    esp_err_t err = i2c_master_write_to_device(
        APP_RTC_I2C_PORT,
        APP_PCF8574_I2C_ADDR,
        &updated_levels,
        sizeof(updated_levels),
        PCF8574_WRITE_TIMEOUT_TICKS
    );
    if (err == ESP_OK) {
        output_levels = updated_levels;
    }

    xSemaphoreGive(output_mutex);
    return err;
}