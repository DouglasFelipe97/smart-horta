#ifndef APP_ALARM_STORE_H
#define APP_ALARM_STORE_H

#include <stdint.h>
#include "app_model.h"
#include "esp_err.h"

// O setor reservado suporta 240 eventos de 17 bytes apos o cabecalho.
#define APP_ALARM_HISTORY_MAX_RECORDS 240U

// Tipos de alarme fixo monitorados para cada sonda instalada.
typedef enum {
    APP_ALARM_AIR_TEMPERATURE_LOW = 0,
    APP_ALARM_AIR_TEMPERATURE_HIGH,
    APP_ALARM_AIR_HUMIDITY_LOW,
    APP_ALARM_AIR_HUMIDITY_HIGH,
    APP_ALARM_SOIL_TEMPERATURE_LOW,
    APP_ALARM_SOIL_TEMPERATURE_HIGH,
    APP_ALARM_SOIL_HUMIDITY_LOW,
    APP_ALARM_SOIL_HUMIDITY_HIGH,
    APP_ALARM_FIXED_TYPE_COUNT,
    APP_ALARM_AMBIENCE_AIR_HOT = APP_ALARM_FIXED_TYPE_COUNT,
    APP_ALARM_AMBIENCE_AIR_DRY,
    APP_ALARM_AMBIENCE_AIR_HUMID,
    APP_ALARM_AMBIENCE_SOIL_DRY,
    APP_ALARM_TYPE_COUNT,
} app_alarm_type_t;

// Evento persistido com data/hora, idade do lote e identificacao da sonda.
typedef struct {
    app_alarm_type_t type;
    uint8_t probe_id;
    app_datetime_t datetime;
    uint32_t lot_age_days;
} app_alarm_event_t;

// Carrega e valida o historico ou prepara um setor vazio na primeira inicializacao.
esp_err_t app_alarm_store_init(void);
// Retorna quantos eventos foram carregados ou gravados durante esta sessao.
uint16_t app_alarm_store_count(void);
// Copia um evento pela ordem cronologica, com indice zero para o mais antigo.
esp_err_t app_alarm_store_get(uint16_t chronological_index, app_alarm_event_t *event);
// Grava um evento sem apagar os anteriores; retorna ESP_ERR_NO_MEM quando cheio.
esp_err_t app_alarm_store_append(const app_alarm_event_t *event);
// Apaga o setor e o cache de eventos para iniciar um novo lote.
esp_err_t app_alarm_store_clear(void);

#endif