#include "app_lot_store.h"

#include <string.h>
#include "app_storage.h"
#include "at25sf321b.h"

#define APP_LOT_RECORD_ADDRESS       (AT25SF321B_CAPACITY_BYTES - AT25SF321B_SECTOR_SIZE)
#define APP_LOT_RECORD_SIZE          17U
#define APP_LOT_RECORD_CRC_OFFSET    13U
#define APP_LOT_RECORD_VERSION       1U

static const uint8_t s_lot_magic[4] = {'S', 'H', 'L', 'T'};

static bool is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1U || month > 12U) {
        return 0;
    }
    if (month == 2U && is_leap_year(year)) {
        return 29;
    }
    return month_days[month - 1U];
}

static bool datetime_is_valid(const app_datetime_t *datetime)
{
    return datetime != NULL && datetime->year >= 2000U && datetime->year <= 2099U &&
           datetime->month >= 1U && datetime->month <= 12U &&
           datetime->day >= 1U && datetime->day <= days_in_month(datetime->year, datetime->month) &&
           datetime->hour <= 23U && datetime->minute <= 59U && datetime->second <= 59U;
}

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

static void encode_crc(uint8_t *record, uint32_t crc)
{
    record[APP_LOT_RECORD_CRC_OFFSET] = (uint8_t)crc;
    record[APP_LOT_RECORD_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8U);
    record[APP_LOT_RECORD_CRC_OFFSET + 2U] = (uint8_t)(crc >> 16U);
    record[APP_LOT_RECORD_CRC_OFFSET + 3U] = (uint8_t)(crc >> 24U);
}

static uint32_t decode_crc(const uint8_t *record)
{
    return (uint32_t)record[APP_LOT_RECORD_CRC_OFFSET] |
           ((uint32_t)record[APP_LOT_RECORD_CRC_OFFSET + 1U] << 8U) |
           ((uint32_t)record[APP_LOT_RECORD_CRC_OFFSET + 2U] << 16U) |
           ((uint32_t)record[APP_LOT_RECORD_CRC_OFFSET + 3U] << 24U);
}

esp_err_t app_lot_store_load(app_datetime_t *start_datetime)
{
    if (start_datetime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t record[APP_LOT_RECORD_SIZE];
    esp_err_t err = app_storage_read(APP_LOT_RECORD_ADDRESS, record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }
    if (memcmp(record, s_lot_magic, sizeof(s_lot_magic)) != 0 ||
        record[4] != APP_LOT_RECORD_VERSION || record[5] != 1U) {
        return ESP_ERR_NOT_FOUND;
    }
    if (decode_crc(record) != calculate_crc32(record, APP_LOT_RECORD_CRC_OFFSET)) {
        return ESP_ERR_INVALID_CRC;
    }

    app_datetime_t decoded = {
        .year = (uint16_t)record[6] | ((uint16_t)record[7] << 8U),
        .month = record[8],
        .day = record[9],
        .weekday = 0,
        .hour = record[10],
        .minute = record[11],
        .second = record[12],
    };
    if (!datetime_is_valid(&decoded)) {
        return ESP_ERR_INVALID_CRC;
    }

    *start_datetime = decoded;
    return ESP_OK;
}

esp_err_t app_lot_store_save(const app_datetime_t *start_datetime)
{
    if (!datetime_is_valid(start_datetime)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t record[APP_LOT_RECORD_SIZE] = {0};
    memcpy(record, s_lot_magic, sizeof(s_lot_magic));
    record[4] = APP_LOT_RECORD_VERSION;
    record[5] = 1U;
    record[6] = (uint8_t)start_datetime->year;
    record[7] = (uint8_t)(start_datetime->year >> 8U);
    record[8] = start_datetime->month;
    record[9] = start_datetime->day;
    record[10] = start_datetime->hour;
    record[11] = start_datetime->minute;
    record[12] = start_datetime->second;
    encode_crc(record, calculate_crc32(record, APP_LOT_RECORD_CRC_OFFSET));

    esp_err_t err = app_storage_erase_sector(APP_LOT_RECORD_ADDRESS);
    if (err != ESP_OK) {
        return err;
    }
    err = app_storage_write(APP_LOT_RECORD_ADDRESS, record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t verify[APP_LOT_RECORD_SIZE];
    err = app_storage_read(APP_LOT_RECORD_ADDRESS, verify, sizeof(verify));
    if (err != ESP_OK) {
        return err;
    }
    return memcmp(record, verify, sizeof(record)) == 0 ? ESP_OK : ESP_ERR_INVALID_CRC;
}