#ifndef APP_AMBIENCE_STORE_H
#define APP_AMBIENCE_STORE_H

#include "app_model.h"
#include "esp_err.h"

esp_err_t app_ambience_store_load(app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
								  bool *configured);
esp_err_t app_ambience_store_save(const app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
								  bool configured);
void app_ambience_store_task(void *argument);

#endif