#ifndef APP_SETTINGS_STORE_H
#define APP_SETTINGS_STORE_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t app_settings_store_load(bool *key_beep_enabled);
esp_err_t app_settings_store_save(bool key_beep_enabled);

#endif