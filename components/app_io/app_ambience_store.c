#include "app_ambience_store.h"

#include <string.h>
#include "app_storage.h"
#include "at25sf321b.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_AMBIENCE_RECORD_ADDRESS (AT25SF321B_CAPACITY_BYTES - (2U * AT25SF321B_SECTOR_SIZE))
#define APP_AMBIENCE_CURVE_BYTES 11U
#define APP_AMBIENCE_HEADER_SIZE 7U
#define APP_AMBIENCE_CRC_OFFSET (APP_AMBIENCE_HEADER_SIZE + (APP_AMBIENCE_CURVE_COUNT * APP_AMBIENCE_CURVE_BYTES))
#define APP_AMBIENCE_RECORD_SIZE (APP_AMBIENCE_CRC_OFFSET + 4U)
#define APP_AMBIENCE_RECORD_VERSION 2U

static const char *TAG_AMBIENCE_STORE = "AMBIENCE_STORE";
static const uint8_t s_ambience_magic[4] = {'S', 'H', 'A', 'M'};

static uint32_t calculate_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

static bool curves_are_valid(const app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT])
{
    if (curves == NULL) {
        return false;
    }

    for (uint8_t i = 0; i < APP_AMBIENCE_CURVE_COUNT; i++) {
        if (curves[i].age_days > 999U ||
            curves[i].air_temperature_x10 < -100 || curves[i].air_temperature_x10 > 600 ||
            curves[i].soil_temperature_x10 < -100 || curves[i].soil_temperature_x10 > 600 ||
            curves[i].air_humidity_percent > 100U || curves[i].soil_humidity_percent > 100U ||
            curves[i].sprinkler_count > 2U || curves[i].irrigator_count > 2U ||
            curves[i].exhaust_fan_count > 2U) {
            return false;
        }
        if (i > 0U && curves[i].age_days <= curves[i - 1U].age_days) {
            return false;
        }
    }
    return true;
}

static void encode_u16(uint8_t *record, size_t *offset, uint16_t value)
{
    record[(*offset)++] = (uint8_t)value;
    record[(*offset)++] = (uint8_t)(value >> 8U);
}

static uint16_t decode_u16(const uint8_t *record, size_t *offset)
{
    uint16_t value = record[*offset];
    value |= (uint16_t)record[*offset + 1U] << 8U;
    *offset += 2U;
    return value;
}

static void encode_crc(uint8_t *record, uint32_t crc)
{
    record[APP_AMBIENCE_CRC_OFFSET] = (uint8_t)crc;
    record[APP_AMBIENCE_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8U);
    record[APP_AMBIENCE_CRC_OFFSET + 2U] = (uint8_t)(crc >> 16U);
    record[APP_AMBIENCE_CRC_OFFSET + 3U] = (uint8_t)(crc >> 24U);
}

static uint32_t decode_crc(const uint8_t *record)
{
    return (uint32_t)record[APP_AMBIENCE_CRC_OFFSET] |
           ((uint32_t)record[APP_AMBIENCE_CRC_OFFSET + 1U] << 8U) |
           ((uint32_t)record[APP_AMBIENCE_CRC_OFFSET + 2U] << 16U) |
           ((uint32_t)record[APP_AMBIENCE_CRC_OFFSET + 3U] << 24U);
}

esp_err_t app_ambience_store_load(app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                  bool *configured)
{
    if (curves == NULL || configured == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t record[APP_AMBIENCE_RECORD_SIZE];
    esp_err_t err = app_storage_read(APP_AMBIENCE_RECORD_ADDRESS, record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }
    if (memcmp(record, s_ambience_magic, sizeof(s_ambience_magic)) != 0 ||
        record[4] != APP_AMBIENCE_RECORD_VERSION || record[5] != APP_AMBIENCE_CURVE_COUNT ||
        record[6] > 1U) {
        return ESP_ERR_NOT_FOUND;
    }
    if (decode_crc(record) != calculate_crc32(record, APP_AMBIENCE_CRC_OFFSET)) {
        return ESP_ERR_INVALID_CRC;
    }

    app_ambience_curve_t decoded[APP_AMBIENCE_CURVE_COUNT] = {0};
    size_t offset = APP_AMBIENCE_HEADER_SIZE;
    for (uint8_t i = 0; i < APP_AMBIENCE_CURVE_COUNT; i++) {
        decoded[i].age_days = decode_u16(record, &offset);
        decoded[i].air_temperature_x10 = (int16_t)decode_u16(record, &offset);
        decoded[i].air_humidity_percent = record[offset++];
        decoded[i].soil_temperature_x10 = (int16_t)decode_u16(record, &offset);
        decoded[i].soil_humidity_percent = record[offset++];
        decoded[i].sprinkler_count = record[offset++];
        decoded[i].irrigator_count = record[offset++];
        decoded[i].exhaust_fan_count = record[offset++];
    }

    if (!curves_are_valid(decoded)) {
        return ESP_ERR_INVALID_CRC;
    }
    memcpy(curves, decoded, sizeof(decoded));
    *configured = record[6] != 0U;
    return ESP_OK;
}

esp_err_t app_ambience_store_save(const app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                  bool configured)
{
    if (!curves_are_valid(curves)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t record[APP_AMBIENCE_RECORD_SIZE] = {0};
    memcpy(record, s_ambience_magic, sizeof(s_ambience_magic));
    record[4] = APP_AMBIENCE_RECORD_VERSION;
    record[5] = APP_AMBIENCE_CURVE_COUNT;
    record[6] = configured ? 1U : 0U;

    size_t offset = APP_AMBIENCE_HEADER_SIZE;
    for (uint8_t i = 0; i < APP_AMBIENCE_CURVE_COUNT; i++) {
        encode_u16(record, &offset, curves[i].age_days);
        encode_u16(record, &offset, (uint16_t)curves[i].air_temperature_x10);
        record[offset++] = curves[i].air_humidity_percent;
        encode_u16(record, &offset, (uint16_t)curves[i].soil_temperature_x10);
        record[offset++] = curves[i].soil_humidity_percent;
        record[offset++] = curves[i].sprinkler_count;
        record[offset++] = curves[i].irrigator_count;
        record[offset++] = curves[i].exhaust_fan_count;
    }
    encode_crc(record, calculate_crc32(record, APP_AMBIENCE_CRC_OFFSET));

    esp_err_t err = app_storage_erase_sector(APP_AMBIENCE_RECORD_ADDRESS);
    if (err != ESP_OK) {
        return err;
    }
    err = app_storage_write(APP_AMBIENCE_RECORD_ADDRESS, record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t verify[APP_AMBIENCE_RECORD_SIZE];
    err = app_storage_read(APP_AMBIENCE_RECORD_ADDRESS, verify, sizeof(verify));
    if (err != ESP_OK) {
        return err;
    }
    return memcmp(record, verify, sizeof(record)) == 0 ? ESP_OK : ESP_ERR_INVALID_CRC;
}

void app_ambience_store_task(void *argument)
{
    (void)argument;
    app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT];
    bool configured;

    for (;;) {
        if (app_model_take_ambience_write_request(curves, &configured)) {
            esp_err_t err = app_ambience_store_save(curves, configured);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_AMBIENCE_STORE, "Falha ao salvar curvas: %s", esp_err_to_name(err));
                app_model_request_ambience_write();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}