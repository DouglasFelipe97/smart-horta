#include "app_settings_store.h"

#include <string.h>
#include "app_storage.h"
#include "at25sf321b.h"

#define APP_SETTINGS_RECORD_ADDRESS (AT25SF321B_CAPACITY_BYTES - (4U * AT25SF321B_SECTOR_SIZE))
#define APP_SETTINGS_RECORD_VERSION 1U
#define APP_SETTINGS_CRC_OFFSET     6U
#define APP_SETTINGS_RECORD_SIZE    10U

static const uint8_t s_settings_magic[4] = {'S', 'H', 'C', 'F'};

static uint32_t calculate_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0; index < size; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

esp_err_t app_settings_store_load(bool *key_beep_enabled)
{
    if (key_beep_enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t record[APP_SETTINGS_RECORD_SIZE];
    esp_err_t err = app_storage_read(APP_SETTINGS_RECORD_ADDRESS, record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }
    if (memcmp(record, s_settings_magic, sizeof(s_settings_magic)) != 0 ||
        record[4] != APP_SETTINGS_RECORD_VERSION) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t stored_crc = 0;
    for (uint8_t byte = 0; byte < 4U; byte++) {
        stored_crc |= (uint32_t)record[APP_SETTINGS_CRC_OFFSET + byte] << (byte * 8U);
    }
    if (record[5] > 1U || stored_crc != calculate_crc32(record, APP_SETTINGS_CRC_OFFSET)) {
        return ESP_ERR_INVALID_CRC;
    }

    *key_beep_enabled = record[5] != 0U;
    return ESP_OK;
}

esp_err_t app_settings_store_save(bool key_beep_enabled)
{
    uint8_t record[APP_SETTINGS_RECORD_SIZE] = {0};
    memcpy(record, s_settings_magic, sizeof(s_settings_magic));
    record[4] = APP_SETTINGS_RECORD_VERSION;
    record[5] = key_beep_enabled ? 1U : 0U;
    uint32_t crc = calculate_crc32(record, APP_SETTINGS_CRC_OFFSET);
    for (uint8_t byte = 0; byte < 4U; byte++) {
        record[APP_SETTINGS_CRC_OFFSET + byte] = (uint8_t)(crc >> (byte * 8U));
    }

    uint8_t verify[APP_SETTINGS_RECORD_SIZE];
    esp_err_t err = app_storage_read(APP_SETTINGS_RECORD_ADDRESS, verify, sizeof(verify));
    if (err != ESP_OK || memcmp(record, verify, sizeof(record)) == 0) {
        return err;
    }
    err = app_storage_erase_sector(APP_SETTINGS_RECORD_ADDRESS);
    if (err != ESP_OK) {
        return err;
    }
    err = app_storage_write(APP_SETTINGS_RECORD_ADDRESS, record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }
    err = app_storage_read(APP_SETTINGS_RECORD_ADDRESS, verify, sizeof(verify));
    if (err != ESP_OK) {
        return err;
    }
    return memcmp(record, verify, sizeof(record)) == 0 ? ESP_OK : ESP_ERR_INVALID_CRC;
}