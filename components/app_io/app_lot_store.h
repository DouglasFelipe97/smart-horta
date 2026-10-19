#ifndef APP_LOT_STORE_H
#define APP_LOT_STORE_H

#include "app_model.h"
#include "esp_err.h"

esp_err_t app_lot_store_load(app_datetime_t *start_datetime);
esp_err_t app_lot_store_save(const app_datetime_t *start_datetime);
esp_err_t app_lot_store_clear(void);

#endif