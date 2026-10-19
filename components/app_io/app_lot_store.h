#ifndef APP_LOT_STORE_H
#define APP_LOT_STORE_H

#include "app_model.h"
#include "esp_err.h"

// Registro versionado e protegido por CRC no ultimo setor da AT25SF321B.
// load retorna ESP_ERR_NOT_FOUND se nao houver lote persistido.
esp_err_t app_lot_store_load(app_datetime_t *start_datetime);
// Save apaga/escreve/verifica o setor; clear remove apenas o registro do lote.
esp_err_t app_lot_store_save(const app_datetime_t *start_datetime);
esp_err_t app_lot_store_clear(void);

#endif