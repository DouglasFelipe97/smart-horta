#ifndef APP_MODEL_H 
#define APP_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define APP_PROBE_COUNT 4
#define APP_AMBIENCE_CURVE_COUNT 5

// Telas principais da aplicacao.
typedef enum {
    APP_SCREEN_HOME = 0,
    // Tela de acesso rapido por tecla esquerda na HOME.
    APP_SCREEN_HOME_PROBES,
    APP_SCREEN_MENU,
    APP_SCREEN_MENU_SETTINGS,
    APP_SCREEN_MENU_REPORTS,
    APP_SCREEN_MENU_ABOUT,
    APP_SCREEN_MENU_BUZZER,
    APP_SCREEN_MENU_LED_TIME,
    APP_SCREEN_MENU_DATE_TIME,
} app_screen_t;

typedef enum {
    APP_LOT_MODE_NONE = 0,
    APP_LOT_MODE_MENU,
    APP_LOT_MODE_EDIT,
    APP_LOT_MODE_CONFIRMED,
    APP_LOT_MODE_FINISH_CONFIRM,
    APP_LOT_MODE_FINISHED,
} app_lot_mode_t;

typedef enum {
    APP_LOT_STATUS_INACTIVE = 0,
    APP_LOT_STATUS_SCHEDULED,
    APP_LOT_STATUS_ACTIVE,
} app_lot_status_t;

typedef enum {
    APP_AMBIENCE_MODE_NONE = 0,
    APP_AMBIENCE_MODE_CURVE_LIST,
    APP_AMBIENCE_MODE_EDIT_CURVE,
} app_ambience_mode_t;

typedef struct {
    uint16_t age_days;
    int16_t air_temperature_x10;
    uint8_t air_humidity_percent;
    int16_t soil_temperature_x10;
    uint8_t soil_humidity_percent;
    uint8_t sprinkler_count;
    uint8_t irrigator_count;
    uint8_t exhaust_fan_count;
} app_ambience_curve_t;

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

typedef enum {
    APP_PROBE_TYPE_UNKNOWN = 0,
    // Sonda atual do projeto: temperatura/umidade de ar + solo.
    APP_PROBE_TYPE_AIR_SOIL_TH = 1,
} app_probe_type_t;

// Data/hora usada no app e no modulo RTC DS3231.
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} app_datetime_t;

// Snapshot para leitura sem expor estado interno.

// Snapshot de uma sonda para renderizacao da UI.
// installed  : representa se a sonda e considerada presente/ativa no barramento.
// has_reading: indica se os campos T/U/S carregam leitura valida neste ciclo.
//              Quando falso, a UI exibe placeholder (__._) para os tres valores.
typedef struct {
    // ID logico da sonda no barramento (1..4 nesta versao).
    uint8_t probe_id;
    // Tipo funcional da sonda para suportar novos modelos no futuro.
    app_probe_type_t probe_type;
    bool installed;
    bool has_reading;
    // Leituras separadas por meio (ar e solo).
    float air_temperature;
    float air_humidity;
    float soil_temperature;
    float soil_humidity;
} app_probe_snapshot_t;

// Snapshot global da aplicacao lido pelas tasks de renderizacao/IO.
// A secao "probes" concentra o estado visual de instalacao, desinstalacao
// e monitoramento ciclico de telemetria da tela "Sondas".
typedef struct {
    app_screen_t screen;
    int adc_raw;
    uint8_t menu_index;
    uint8_t settings_index;
    app_lot_mode_t settings_lot_mode;
    bool settings_editing_datetime;
    app_ambience_mode_t ambience_mode;
    uint8_t ambience_curve_index;
    uint8_t ambience_field_index;
    app_ambience_curve_t ambience_curves[APP_AMBIENCE_CURVE_COUNT];
    bool ambience_configured;
    bool ambience_alarm_active;
    uint8_t settings_buzzer_mode;
    uint8_t buzzer_index;
    uint8_t settings_probe_mode;
    uint8_t probe_menu_index;
    bool probe_install_running;
    bool probe_install_finished;
    uint8_t probe_install_completed_count;
    bool probe_uninstall_running;
    bool probe_uninstall_finished;
    uint8_t probe_uninstall_cycle;
    app_probe_snapshot_t probes[APP_PROBE_COUNT];
    bool key_beep_enabled;
    uint8_t datetime_field_index;
    app_datetime_t datetime;
    bool lot_started;
    app_datetime_t lot_start_datetime;
    app_lot_status_t lot_status;
    uint32_t lot_age_days;
} app_state_snapshot_t;

void app_model_init(void);

void app_model_set_adc_raw(int adc_raw);
int app_model_get_adc_raw(void);
void app_model_get_snapshot(app_state_snapshot_t *snapshot);
void app_model_process_key(app_key_t key);

// Atualiza cache local da data/hora lida no RTC.
void app_model_set_datetime(const app_datetime_t *dt);

// Restaura o estado persistido do lote sem gerar novo pedido de gravacao.
void app_model_set_lot_state(bool started, const app_datetime_t *start_datetime);

// Consome o pedido para persistir um lote confirmado.
bool app_model_take_lot_write_request(app_datetime_t *start_datetime, bool *started);

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

// Informa se a tela atual e Ajustes->Instalar Sondas->Instalar.
// Usado para ligar/desligar automaticamente a maquina de estados de instalacao.
bool app_model_is_probe_install_screen_active(void);

// Informa se a tela atual e Ajustes->Instalar Sondas->Desinstalar.
// Usado para ligar/desligar automaticamente a rotina de desinstalacao.
bool app_model_is_probe_uninstall_screen_active(void);

// Informa se a tela atual e Ajustes->Instalar Sondas->Sondas.
// Usado para ligar/desligar automaticamente o polling ciclico de telemetria.
bool app_model_is_probe_list_screen_active(void);

// Atualiza estado geral da rotina de instalacao das sondas.
// completed_count representa quantas sondas (0..4) ja receberam ID definitivo.
void app_model_set_probe_install_status(bool running, bool finished, uint8_t completed_count);

// Atualiza estado geral da rotina de desinstalacao das sondas.
// cycle representa o progresso do ciclo redundante (1..3).
void app_model_set_probe_uninstall_status(bool running, bool finished, uint8_t cycle);

// Atualiza telemetria/estado de uma sonda (slot 0..3 para S1..S4).
// probe_id/probe_type permitem rastrear o dispositivo fisico e seu perfil.
void app_model_set_probe_snapshot(uint8_t probe_slot,
                                  uint8_t probe_id,
                                  app_probe_type_t probe_type,
                                  bool installed,
                                  bool has_reading,
                                  float air_temperature,
                                  float air_humidity,
                                  float soil_temperature,
                                  float soil_humidity);

void app_model_set_ambience_curves(const app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                   bool configured);
bool app_model_take_ambience_write_request(app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                          bool *configured);
void app_model_request_ambience_write(void);
void app_model_set_ambience_alarm_active(bool active);

// Reseta placeholders e status da rotina de instalacao/desinstalacao para inicio limpo.
// Esta funcao e chamada na entrada dos fluxos para evitar reuso de dados antigos.
void app_model_reset_probe_install_data(void);

// Retorna automaticamente da tela de desinstalacao para o submenu "Instalar Sondas".
void app_model_exit_probe_uninstall_to_menu(void);

#endif