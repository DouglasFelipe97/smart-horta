#ifndef APP_PCF8574_H
#define APP_PCF8574_H

#include <stdbool.h>
#include "esp_err.h"

// Ordem fixa das portas P0..P6; P7 permanece sem funcao neste firmware.
typedef enum {
    APP_OUTPUT_SPRINKLER_1 = 0,
    APP_OUTPUT_SPRINKLER_2,
    APP_OUTPUT_IRRIGATOR_1,
    APP_OUTPUT_IRRIGATOR_2,
    APP_OUTPUT_EXHAUST_FAN_1,
    APP_OUTPUT_EXHAUST_FAN_2,
    APP_OUTPUT_AUXILIARY,
    APP_OUTPUT_COUNT
} app_pcf8574_output_t;

// Inicializa o endereco configurado com todas as cargas inativas.
esp_err_t app_pcf8574_init(void);
// Escreve nivel eletrico bruto (high=true significa nivel HIGH, nao "ligado").
esp_err_t app_pcf8574_write_output(app_pcf8574_output_t output, bool high);

#endif