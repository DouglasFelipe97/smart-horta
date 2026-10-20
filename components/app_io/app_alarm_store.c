#include "app_alarm_store.h"

#include <stdbool.h>
#include <string.h>
#include "app_storage.h"
#include "at25sf321b.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define APP_ALARM_STORE_ADDRESS (AT25SF321B_CAPACITY_BYTES - (3U * AT25SF321B_SECTOR_SIZE))
#define APP_ALARM_HEADER_SIZE 16U
#define APP_ALARM_HEADER_CRC_OFFSET 12U
#define APP_ALARM_RECORD_SIZE 17U
#define APP_ALARM_RECORD_CRC_OFFSET 13U
#define APP_ALARM_STORE_VERSION 1U

static const uint8_t s_alarm_magic[4] = {'S', 'H', 'A', 'L'};
static app_alarm_event_t s_events[APP_ALARM_HISTORY_MAX_RECORDS];
static uint16_t s_event_count;
static SemaphoreHandle_t s_alarm_store_mutex;
static bool s_store_ready;
static bool s_append_blocked;

// Calcula CRC32 para detectar gravacoes incompletas ou dados corrompidos.
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

// Codifica um CRC32 little-endian no deslocamento indicado pelo formato.
static void encode_crc(uint8_t *record, size_t offset, uint32_t crc)
{
    record[offset] = (uint8_t)crc;
    record[offset + 1U] = (uint8_t)(crc >> 8U);
    record[offset + 2U] = (uint8_t)(crc >> 16U);
    record[offset + 3U] = (uint8_t)(crc >> 24U);
}

// Decodifica um CRC32 little-endian para validar os bytes persistidos.
static uint32_t decode_crc(const uint8_t *record, size_t offset)
{
    return (uint32_t)record[offset] |
           ((uint32_t)record[offset + 1U] << 8U) |
           ((uint32_t)record[offset + 2U] << 16U) |
           ((uint32_t)record[offset + 3U] << 24U);
}

// Monta o cabecalho fixo do setor de historico, independente do padding C.
static void encode_header(uint8_t header[APP_ALARM_HEADER_SIZE])
{
    memset(header, 0xFF, APP_ALARM_HEADER_SIZE);
    memcpy(header, s_alarm_magic, sizeof(s_alarm_magic));
    header[4] = APP_ALARM_STORE_VERSION;
    header[5] = APP_ALARM_RECORD_SIZE;
    encode_crc(header, APP_ALARM_HEADER_CRC_OFFSET,
               calculate_crc32(header, APP_ALARM_HEADER_CRC_OFFSET));
}

// Confirma assinatura, versao, tamanho de registro e integridade do cabecalho.
static bool header_is_valid(const uint8_t header[APP_ALARM_HEADER_SIZE])
{
    return memcmp(header, s_alarm_magic, sizeof(s_alarm_magic)) == 0 &&
           header[4] == APP_ALARM_STORE_VERSION &&
           header[5] == APP_ALARM_RECORD_SIZE &&
           decode_crc(header, APP_ALARM_HEADER_CRC_OFFSET) ==
               calculate_crc32(header, APP_ALARM_HEADER_CRC_OFFSET);
}

// Serializa os campos de evento em 13 bytes anteriores ao CRC.
static void encode_event(uint8_t record[APP_ALARM_RECORD_SIZE],
                         const app_alarm_event_t *event)
{
    size_t offset = 0;
    record[offset++] = (uint8_t)event->type;
    record[offset++] = event->probe_id;
    record[offset++] = (uint8_t)event->datetime.year;
    record[offset++] = (uint8_t)(event->datetime.year >> 8U);
    record[offset++] = event->datetime.month;
    record[offset++] = event->datetime.day;
    record[offset++] = event->datetime.hour;
    record[offset++] = event->datetime.minute;
    record[offset++] = event->datetime.second;
    record[offset++] = (uint8_t)event->lot_age_days;
    record[offset++] = (uint8_t)(event->lot_age_days >> 8U);
    record[offset++] = (uint8_t)(event->lot_age_days >> 16U);
    record[offset++] = (uint8_t)(event->lot_age_days >> 24U);
    encode_crc(record, APP_ALARM_RECORD_CRC_OFFSET,
               calculate_crc32(record, APP_ALARM_RECORD_CRC_OFFSET));
}

// Decodifica e valida um evento antes de publica-lo no cache em RAM.
static bool decode_event(const uint8_t record[APP_ALARM_RECORD_SIZE],
                         app_alarm_event_t *event)
{
    if (record[0] >= APP_ALARM_TYPE_COUNT || record[1] < 1U || record[1] > APP_PROBE_COUNT ||
        decode_crc(record, APP_ALARM_RECORD_CRC_OFFSET) !=
            calculate_crc32(record, APP_ALARM_RECORD_CRC_OFFSET)) {
        return false;
    }

    event->type = (app_alarm_type_t)record[0];
    event->probe_id = record[1];
    event->datetime.year = (uint16_t)record[2] | ((uint16_t)record[3] << 8U);
    event->datetime.month = record[4];
    event->datetime.day = record[5];
    event->datetime.hour = record[6];
    event->datetime.minute = record[7];
    event->datetime.second = record[8];
    event->datetime.weekday = 0;
    event->lot_age_days = (uint32_t)record[9] |
                          ((uint32_t)record[10] << 8U) |
                          ((uint32_t)record[11] << 16U) |
                          ((uint32_t)record[12] << 24U);
    return event->datetime.year >= 2000U && event->datetime.year <= 2099U &&
           event->datetime.month >= 1U && event->datetime.month <= 12U &&
           event->datetime.day >= 1U && event->datetime.day <= 31U &&
           event->datetime.hour <= 23U && event->datetime.minute <= 59U &&
           event->datetime.second <= 59U;
}

// Publica a quantidade ao modelo para a navegacao do menu de relatorios.
static void publish_event_count(uint16_t count)
{
    app_model_set_report_count(count);
}

// Carrega registros sequenciais e para no primeiro espaco vazio ou invalido.
static esp_err_t load_records(void)
{
    s_event_count = 0;
    s_append_blocked = false;
    for (uint16_t index = 0; index < APP_ALARM_HISTORY_MAX_RECORDS; index++) {
        uint8_t record[APP_ALARM_RECORD_SIZE];
        uint32_t address = APP_ALARM_STORE_ADDRESS + APP_ALARM_HEADER_SIZE +
                           ((uint32_t)index * APP_ALARM_RECORD_SIZE);
        esp_err_t err = app_storage_read(address, record, sizeof(record));
        if (err != ESP_OK) {
            return err;
        }
        if (record[0] == 0xFFU) {
            break;
        }
        if (!decode_event(record, &s_events[index])) {
            // Nao reutiliza um slot parcialmente gravado sem apagar o setor.
            s_append_blocked = true;
            break;
        }
        s_event_count++;
    }
    publish_event_count(s_event_count);
    return ESP_OK;
}

// Prepara o cache e valida o formato do setor antes de iniciar o monitoramento.
esp_err_t app_alarm_store_init(void)
{
    if (s_alarm_store_mutex == NULL) {
        s_alarm_store_mutex = xSemaphoreCreateMutex();
        if (s_alarm_store_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_alarm_store_mutex, portMAX_DELAY);
    s_store_ready = false;
    uint8_t header[APP_ALARM_HEADER_SIZE];
    esp_err_t err = app_storage_read(APP_ALARM_STORE_ADDRESS, header, sizeof(header));
    if (err == ESP_OK && !header_is_valid(header)) {
        err = app_storage_erase_sector(APP_ALARM_STORE_ADDRESS);
        if (err == ESP_OK) {
            encode_header(header);
            err = app_storage_write(APP_ALARM_STORE_ADDRESS, header, sizeof(header));
        }
    }
    if (err == ESP_OK) {
        err = load_records();
    }
    s_store_ready = err == ESP_OK;
    xSemaphoreGive(s_alarm_store_mutex);
    return err;
}

// Retorna contagem consistente com o cache protegido pelo mutex do armazenamento.
uint16_t app_alarm_store_count(void)
{
    if (s_alarm_store_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_alarm_store_mutex, portMAX_DELAY);
    uint16_t count = s_event_count;
    xSemaphoreGive(s_alarm_store_mutex);
    return count;
}

// Copia um evento do cache para que a interface nao acesse diretamente a flash.
esp_err_t app_alarm_store_get(uint16_t chronological_index, app_alarm_event_t *event)
{
    if (event == NULL || s_alarm_store_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_alarm_store_mutex, portMAX_DELAY);
    if (chronological_index >= s_event_count) {
        xSemaphoreGive(s_alarm_store_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *event = s_events[chronological_index];
    xSemaphoreGive(s_alarm_store_mutex);
    return ESP_OK;
}

// Acrescenta um registro no primeiro slot vazio sem apagar o historico anterior.
esp_err_t app_alarm_store_append(const app_alarm_event_t *event)
{
    if (event == NULL || event->type >= APP_ALARM_TYPE_COUNT ||
        event->probe_id < 1U || event->probe_id > APP_PROBE_COUNT ||
        s_alarm_store_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_alarm_store_mutex, portMAX_DELAY);
    if (!s_store_ready) {
        xSemaphoreGive(s_alarm_store_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_append_blocked) {
        xSemaphoreGive(s_alarm_store_mutex);
        return ESP_ERR_INVALID_CRC;
    }
    if (s_event_count >= APP_ALARM_HISTORY_MAX_RECORDS) {
        xSemaphoreGive(s_alarm_store_mutex);
        return ESP_ERR_NO_MEM;
    }

    uint8_t record[APP_ALARM_RECORD_SIZE];
    encode_event(record, event);
    uint32_t address = APP_ALARM_STORE_ADDRESS + APP_ALARM_HEADER_SIZE +
                       ((uint32_t)s_event_count * APP_ALARM_RECORD_SIZE);
    esp_err_t err = app_storage_write(address, record, sizeof(record));
    if (err == ESP_OK) {
        uint8_t verify[APP_ALARM_RECORD_SIZE];
        err = app_storage_read(address, verify, sizeof(verify));
        if (err == ESP_OK && memcmp(record, verify, sizeof(record)) != 0) {
            err = ESP_ERR_INVALID_CRC;
        }
    }
    if (err == ESP_OK) {
        s_events[s_event_count++] = *event;
        publish_event_count(s_event_count);
    } else {
        // Uma falha pode deixar bits programados; bloqueia novas tentativas no mesmo slot.
        s_append_blocked = true;
    }
    xSemaphoreGive(s_alarm_store_mutex);
    return err;
}

// Limpa somente o setor de historico e disponibiliza novamente os 240 slots.
esp_err_t app_alarm_store_clear(void)
{
    if (s_alarm_store_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_alarm_store_mutex, portMAX_DELAY);
    s_store_ready = false;
    esp_err_t err = app_storage_erase_sector(APP_ALARM_STORE_ADDRESS);
    if (err == ESP_OK) {
        s_store_ready = false;
        s_append_blocked = false;
        s_event_count = 0;
        publish_event_count(0);
        uint8_t header[APP_ALARM_HEADER_SIZE];
        encode_header(header);
        err = app_storage_write(APP_ALARM_STORE_ADDRESS, header, sizeof(header));
        s_store_ready = err == ESP_OK;
    }
    xSemaphoreGive(s_alarm_store_mutex);
    return err;
}