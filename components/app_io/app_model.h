#ifndef APP_MODEL_H 
#define APP_MODEL_H

#include <stdbool.h>
#include <stdint.h>

// Telas principais da aplicacao.
typedef enum {
    APP_SCREEN_HOME = 0,
    APP_SCREEN_MENU,
    APP_SCREEN_MENU_SETTINGS,
    APP_SCREEN_MENU_REPORTS,
    APP_SCREEN_MENU_ABOUT,
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
    APP_KEY_LEFT,
    APP_KEY_MENU,
} app_key_t;

// Data/hora usada no app e no modulo RTC DS3231.
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} app_datetime_t;

// Snapshot para leitura sem expor estado interno.

typedef struct {
    app_screen_t screen;
    int adc_raw;
    uint8_t menu_index;
    uint8_t settings_index;
    bool settings_editing_datetime;
    uint8_t settings_temperature_mode;
    uint8_t temperature_index;
    uint8_t settings_buzzer_mode;
    uint8_t buzzer_index;
    bool key_beep_enabled;
    uint8_t datetime_field_index;
    app_datetime_t datetime;
} app_state_snapshot_t;

void app_model_init(void);

void app_model_set_adc_raw(int adc_raw);
int app_model_get_adc_raw(void);
void app_model_get_snapshot(app_state_snapshot_t *snapshot);
void app_model_process_key(app_key_t key);

// Atualiza cache local da data/hora lida no RTC.
void app_model_set_datetime(const app_datetime_t *dt);

// Entrega uma solicitacao pendente de escrita no RTC e limpa o flag.
bool app_model_take_rtc_write_request(app_datetime_t *dt);

// Marca evento de tecla estavel para disparo de beep de navegacao.
void app_model_set_key_press_flag(void);

// Consome o evento pendente de tecla (retorna true apenas uma vez por tecla).
bool app_model_take_key_press_flag(void);

// Consulta do estado de habilitacao do beep de teclas.
bool app_model_is_key_beep_enabled(void);

// Verifica timeout de inatividade e retorna para HOME quando necessario.
void app_model_check_inactivity_timeout(uint32_t now_ms);

#endif