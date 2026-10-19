#ifndef APP_AMBIENCE_CONTROL_H
#define APP_AMBIENCE_CONTROL_H

#include <stdint.h>
#include "app_model.h"

// Avalia limites/histerese e atualiza alarme/PCF8574. now_ms deve ser o tick
// monotonic em ms; sensor_monitor_active false desliga saídas e limpa alarmes.
void app_ambience_control_update(const app_state_snapshot_t *snapshot,
                                 uint32_t now_ms,
                                 bool sensor_monitor_active);

#endif