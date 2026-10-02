#ifndef APP_MODEL_H 
#define APP_MODEL_H

#include <stdbool.h>
#include <stdint.h>

// Telas principais da aplicacao.
typedef enum {
    APP_SCREEN_HOME = 0,
    APP_SCREEN_MENU,
    APP_SCREEN_MENU_BUZZER,
    APP_SCREEN_MENU_LED_TIME,
    APP_SCREEN_MENU_DATE_TIME,
} app_screen_t;

// Teclas lidas via teclado resistivo no ADC.
typedef enum {
    APP_KEY_NONE = 0,
    APP_KEY_UP,
    APP_KEY_DOWN,
    APP_KEY_ENTER,
    APP_KEY_BACK,
} app_key_t;

// Snapshot para leitura sem expor estado interno.

typedef struct {
    app_screen_t screen;
    int adc_raw;
} app_state_snapshot_t;

void app_model_init(void);

void app_model_set_adc_raw(int adc_raw);
int app_model_get_adc_raw(void);
void app_model_get_snapshot(app_state_snapshot_t *snapshot);

#endif