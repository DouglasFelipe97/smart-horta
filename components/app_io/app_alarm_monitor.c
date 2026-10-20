#include "app_alarm_monitor.h"

#include <stdbool.h>
#include <string.h>
#include "app_alarm_store.h"
#include "esp_log.h"

static const char *TAG_ALARM_MONITOR = "ALARM_MONITOR";

// Guarda a transicao normal->alarme separadamente por sonda e tipo.
static bool s_alarm_active[APP_PROBE_COUNT][APP_ALARM_FIXED_TYPE_COUNT];

// Compara uma leitura com os limites fixos correspondentes ao tipo de alarme.
static bool reading_is_out_of_range(const app_probe_snapshot_t *reading,
                                    app_alarm_type_t type)
{
    switch (type) {
        case APP_ALARM_AIR_TEMPERATURE_LOW:
            return reading->air_temperature < APP_ALARM_AIR_TEMPERATURE_MIN_C;
        case APP_ALARM_AIR_TEMPERATURE_HIGH:
            return reading->air_temperature > APP_ALARM_AIR_TEMPERATURE_MAX_C;
        case APP_ALARM_AIR_HUMIDITY_LOW:
            return reading->air_humidity < APP_ALARM_AIR_HUMIDITY_MIN_PCT;
        case APP_ALARM_AIR_HUMIDITY_HIGH:
            return reading->air_humidity > APP_ALARM_AIR_HUMIDITY_MAX_PCT;
        case APP_ALARM_SOIL_TEMPERATURE_LOW:
            return reading->soil_temperature < APP_ALARM_SOIL_TEMPERATURE_MIN_C;
        case APP_ALARM_SOIL_TEMPERATURE_HIGH:
            return reading->soil_temperature > APP_ALARM_SOIL_TEMPERATURE_MAX_C;
        case APP_ALARM_SOIL_HUMIDITY_LOW:
            return reading->soil_humidity < APP_ALARM_SOIL_HUMIDITY_MIN_PCT;
        case APP_ALARM_SOIL_HUMIDITY_HIGH:
            return reading->soil_humidity > APP_ALARM_SOIL_HUMIDITY_MAX_PCT;
        default:
            return false;
    }
}

// Zera a memoria de transicoes, sem modificar eventos que ja estao na flash.
void app_alarm_monitor_reset(void)
{
    memset(s_alarm_active, 0, sizeof(s_alarm_active));
}

// Registra somente a primeira leitura fora da faixa ate a condicao normalizar.
void app_alarm_monitor_update(const app_state_snapshot_t *snapshot, bool monitor_active)
{
    if (snapshot == NULL) {
        return;
    }

    // Alarmes de relatorio pertencem a um lote e exigem telemetria atual.
    if (snapshot->lot_status != APP_LOT_STATUS_ACTIVE || !monitor_active) {
        app_alarm_monitor_reset();
        return;
    }

    for (uint8_t probe = 0; probe < APP_PROBE_COUNT; probe++) {
        const app_probe_snapshot_t *reading = &snapshot->probes[probe];
        if (!reading->installed || !reading->has_reading) {
            continue;
        }

        for (uint8_t alarm = 0; alarm < APP_ALARM_FIXED_TYPE_COUNT; alarm++) {
            bool active = reading_is_out_of_range(reading, (app_alarm_type_t)alarm);
            if (active && !s_alarm_active[probe][alarm]) {
                app_alarm_event_t event = {
                    .type = (app_alarm_type_t)alarm,
                    .probe_id = reading->probe_id,
                    .datetime = snapshot->datetime,
                    .lot_age_days = snapshot->lot_age_days,
                };
                esp_err_t err = app_alarm_store_append(&event);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG_ALARM_MONITOR, "Falha ao registrar alarme: %s",
                             esp_err_to_name(err));
                }
            }
            s_alarm_active[probe][alarm] = active;
        }
    }
}