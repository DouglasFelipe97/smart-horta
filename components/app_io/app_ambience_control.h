#ifndef APP_AMBIENCE_CONTROL_H
#define APP_AMBIENCE_CONTROL_H

#include <stdint.h>
#include "app_model.h"

void app_ambience_control_update(const app_state_snapshot_t *snapshot,
								uint32_t now_ms,
								bool sensor_monitor_active);

#endif