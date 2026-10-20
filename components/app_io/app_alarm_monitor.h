#ifndef APP_ALARM_MONITOR_H
#define APP_ALARM_MONITOR_H

#include "app_model.h"

// Limites fixos usados tanto no resumo HOME quanto no historico de alarmes.
#define APP_ALARM_AIR_TEMPERATURE_MIN_C 5.0f
#define APP_ALARM_AIR_TEMPERATURE_MAX_C 35.0f
#define APP_ALARM_AIR_HUMIDITY_MIN_PCT 20.0f
#define APP_ALARM_AIR_HUMIDITY_MAX_PCT 90.0f
#define APP_ALARM_SOIL_TEMPERATURE_MIN_C 5.0f
#define APP_ALARM_SOIL_TEMPERATURE_MAX_C 35.0f
#define APP_ALARM_SOIL_HUMIDITY_MIN_PCT 15.0f
#define APP_ALARM_SOIL_HUMIDITY_MAX_PCT 95.0f

// Detecta novas entradas em faixa de alarme durante um lote ativo.
void app_alarm_monitor_update(const app_state_snapshot_t *snapshot, bool monitor_active);
// Descarta estados anteriores para que um lote novo possa registrar alarmes novamente.
void app_alarm_monitor_reset(void);

#endif