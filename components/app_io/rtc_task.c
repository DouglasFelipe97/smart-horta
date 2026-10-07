#include "rtc_task.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "app_model.h"
#include "app_pin_config.h"

#define DS3231_I2C_ADDR          0x68
#define DS3231_REG_SECONDS       0x00

static const char *TAG_RTC = "RTC_TASK";

static uint8_t to_bcd(uint8_t val)
{
    return (uint8_t)(((val / 10U) << 4U) | (val % 10U));
}

static uint8_t from_bcd(uint8_t val)
{
    return (uint8_t)(((val >> 4U) * 10U) + (val & 0x0FU));
}

static esp_err_t ds3231_read_datetime(app_datetime_t *dt)
{
    if (dt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = DS3231_REG_SECONDS;
    uint8_t data[7] = {0};

    esp_err_t err = i2c_master_write_read_device(
        APP_RTC_I2C_PORT,
        DS3231_I2C_ADDR,
        &reg,
        1,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
    if (err != ESP_OK) {
        return err;
    }

    dt->second = from_bcd((uint8_t)(data[0] & 0x7F));
    dt->minute = from_bcd((uint8_t)(data[1] & 0x7F));
    dt->hour = from_bcd((uint8_t)(data[2] & 0x3F));
    dt->day = from_bcd((uint8_t)(data[4] & 0x3F));
    dt->month = from_bcd((uint8_t)(data[5] & 0x1F));
    dt->year = (uint16_t)(2000U + from_bcd(data[6]));

    return ESP_OK;
}

static esp_err_t ds3231_write_datetime(const app_datetime_t *dt)
{
    if (dt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[8] = {
        DS3231_REG_SECONDS,
        to_bcd(dt->second),
        to_bcd(dt->minute),
        to_bcd(dt->hour),
        1, // dia da semana (fixo por enquanto)
        to_bcd(dt->day),
        to_bcd(dt->month),
        to_bcd((uint8_t)(dt->year % 100U)),
    };

    return i2c_master_write_to_device(
        APP_RTC_I2C_PORT,
        DS3231_I2C_ADDR,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
}

void rtc_read_task(void *pvParameters)
{
    (void)pvParameters;

    // Task dedicada para leitura periodica do RTC.
    // Mantem o relogio do app sincronizado com o modulo DS3231.
    while (1) {
        app_datetime_t dt;
        esp_err_t err = ds3231_read_datetime(&dt);
        if (err == ESP_OK) {
            app_model_set_datetime(&dt);
        } else {
            ESP_LOGW(TAG_RTC, "Falha ao ler DS3231: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void rtc_write_task(void *pvParameters)
{
    (void)pvParameters;

    // Task separada para escrita sob demanda.
    // Ela quase nao roda: so atua quando o usuario salva ajuste no menu.
    while (1) {
        app_datetime_t dt;
        if (app_model_take_rtc_write_request(&dt)) {
            esp_err_t err = ds3231_write_datetime(&dt);
            if (err == ESP_OK) {
                ESP_LOGI(TAG_RTC, "DS3231 atualizado via menu");
            } else {
                ESP_LOGE(TAG_RTC, "Falha ao escrever DS3231: %s", esp_err_to_name(err));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
