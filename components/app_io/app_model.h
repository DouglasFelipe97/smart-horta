#ifndef APP_MODEL_H 
#define APP_MODEL_H

#include <stdbool.h>
#include <stdint.h>

// Contrato do estado compartilhado entre tasks: entradas sao atualizadas por
// setters protegidos por mutex e a UI/controladores consomem snapshots coerentes.
#define APP_PROBE_COUNT 4
#define APP_AMBIENCE_CURVE_COUNT 5

// Tela atualmente exibida; os submodos de Ajustes sao mantidos separadamente.
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

// Passos internos do fluxo de lote dentro do menu Ajustes.
typedef enum {
    APP_LOT_MODE_NONE = 0,
    APP_LOT_MODE_MENU,
    APP_LOT_MODE_EDIT,
    APP_LOT_MODE_CONFIRMED,
    APP_LOT_MODE_FINISH_CONFIRM,
    APP_LOT_MODE_FINISHED,
} app_lot_mode_t;

// Estado temporal do lote: um lote agendado ainda nao habilita a automacao.
typedef enum {
    APP_LOT_STATUS_INACTIVE = 0,
    APP_LOT_STATUS_SCHEDULED,
    APP_LOT_STATUS_ACTIVE,
} app_lot_status_t;

// Navegacao do editor das cinco curvas de idade.
typedef enum {
    APP_AMBIENCE_MODE_NONE = 0,
    APP_AMBIENCE_MODE_CURVE_LIST,
    APP_AMBIENCE_MODE_EDIT_CURVE,
} app_ambience_mode_t;

// Submenu Opcoes dentro de Ajustes.
typedef enum {
    APP_OPTIONS_MODE_NONE = 0,
    APP_OPTIONS_MODE_MENU,
} app_options_mode_t;

// Um ponto da curva, com metas em unidades inteiras escaladas e contagens
// de canais (0..2 por tipo de equipamento).
typedef struct {
    uint16_t age_days;                 // Idade do lote em dias; pontos devem crescer em ordem.
    int16_t air_temperature_x10;       // Temperatura do ar em decimos de grau Celsius.
    uint8_t air_humidity_percent;      // Umidade alvo do ar em percentual inteiro.
    int16_t soil_temperature_x10;      // Temperatura alvo do solo; atualmente informativa.
    uint8_t soil_humidity_percent;     // Umidade alvo do solo em percentual inteiro.
    uint8_t sprinkler_count;           // Aspersores a usar (0..2; P0/P1).
    uint8_t irrigator_count;           // Irrigadores a usar (0..2; P2/P3).
    uint8_t exhaust_fan_count;         // Exaustores a usar (0..2; P4/P5).
} app_ambience_curve_t;

// Eventos logicos emitidos pelo teclado ADC apos debounce.
// Perfil funcional reportado por cada dispositivo de sonda.
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

// Data/hora civil usada pelo modelo e convertida para BCD no DS3231.
typedef struct {
    uint16_t year;       // Ano civil (2000..2099).
    uint8_t month;       // Mes (1..12).
    uint8_t day;         // Dia valido para mes/ano.
    uint8_t weekday;     // Dia da semana do DS3231 (1..7).
    uint8_t hour;        // Hora 24 h (0..23).
    uint8_t minute;      // Minuto (0..59).
    uint8_t second;      // Segundo (0..59).
} app_datetime_t;

// Snapshot de telemetria: installed informa resposta/presenca; has_reading
// informa se os quatro valores foram decodificados neste ciclo.
typedef struct {
    // ID logico da sonda no barramento (1..4 nesta versao).
    uint8_t probe_id;                // ID de barramento da sonda (1..4).
    // Tipo funcional da sonda para suportar novos modelos no futuro.
    app_probe_type_t probe_type;     // Perfil de telemetria decodificado.
    bool installed;                  // Respondeu ao polling mais recente.
    bool has_reading;                // Leituras abaixo sao validas neste snapshot.
    // Leituras separadas por meio (ar e solo).
    float air_temperature;            // Celsius.
    float air_humidity;               // Percentual relativo.
    float soil_temperature;           // Celsius; apenas informativa no controle atual.
    float soil_humidity;              // Percentual relativo.
} app_probe_snapshot_t;

// Copia consistente do estado do modelo para renderizacao e controle, sem
// expor o mutex nem permitir que consumidores alterem o estado interno.
typedef struct {
    app_screen_t screen;                                       // Tela raiz corrente.
    int adc_raw;                                                // Ultima amostra ADC do teclado.
    uint8_t menu_index;                                        // Cursor do menu principal.
    uint8_t settings_index;                                    // Cursor de Ajustes.
    uint16_t report_count;                                     // Quantidade de alarmes persistidos.
    uint16_t report_index;                                     // Posicao relativa ao mais recente.
    app_lot_mode_t settings_lot_mode;                           // Subfluxo de lote.
    bool settings_editing_datetime;                            // Editor do RTC aberto.
    app_options_mode_t settings_options_mode;                   // Submenu Opcoes aberto.
    app_ambience_mode_t ambience_mode;                          // Lista/editor de curvas.
    uint8_t ambience_curve_index;                              // Curva selecionada (0..4).
    uint8_t ambience_field_index;                              // Campo selecionado (0..7).
    app_ambience_curve_t ambience_curves[APP_AMBIENCE_CURVE_COUNT]; // Metas/saidas por idade.
    bool ambience_configured;                                   // Perfil confirmado e automacao habilitada.
    bool ambience_alarm_active;                                 // Algum limite ambiental esta violado.
    uint8_t settings_buzzer_mode;                               // Submenu do beep de teclas.
    uint8_t buzzer_index;                                       // Cursor do submenu Buzzer.
    uint8_t settings_probe_mode;                                // FSM/tela do gestor de sondas.
    uint8_t probe_menu_index;                                   // Cursor Instalar/Desinstalar/Sondas.
    bool probe_install_running;                                 // Descoberta/atribuicao em progresso.
    bool probe_install_finished;                                // Instalacao terminou.
    uint8_t probe_install_completed_count;                      // IDs atribuidos (0..4).
    bool probe_uninstall_running;                               // Ciclos de reset em progresso.
    bool probe_uninstall_finished;                              // Desinstalacao terminou.
    uint8_t probe_uninstall_cycle;                              // Ciclo atual (0..3).
    app_probe_snapshot_t probes[APP_PROBE_COUNT];                // Estado/telemetria por slot.
    bool key_beep_enabled;                                      // Beep de navegacao habilitado.
    uint8_t datetime_field_index;                               // Campo de data/hora selecionado.
    app_datetime_t datetime;                                    // Relogio corrente mostrado na UI.
    bool lot_started;                                           // Existe inicio de lote persistido.
    app_datetime_t lot_start_datetime;                          // Data/hora do inicio/agendamento.
    app_lot_status_t lot_status;                                // Inativo, agendado ou ativo.
    uint32_t lot_age_days;                                      // Dias decorridos desde inicio efetivo.
} app_state_snapshot_t;

// Inicializa o mutex, os menus, as metas default de idade e placeholders.
void app_model_init(void);

// Atualiza/consulta o valor bruto de ADC mostrado durante diagnostico.
void app_model_set_adc_raw(int adc_raw);
int app_model_get_adc_raw(void);

// Copia todos os dados publicos sob mutex; destino nulo e ignorado.
void app_model_get_snapshot(app_state_snapshot_t *snapshot);

// Aplica uma tecla logica a maquina de estados da interface.
void app_model_process_key(app_key_t key);

// Atualiza cache do RTC e normaliza cada campo para o intervalo suportado.
void app_model_set_datetime(const app_datetime_t *dt);

// Restaura lote sem gerar pedido de gravacao (usado durante o boot).
void app_model_set_lot_state(bool started, const app_datetime_t *start_datetime);

// Consome uma unica solicitacao de persistencia do lote e devolve seu estado.
bool app_model_take_lot_write_request(app_datetime_t *start_datetime, bool *started);

// Consome o pedido de escrita no RTC produzido pelo editor de data/hora.
bool app_model_take_rtc_write_request(app_datetime_t *dt);

// Publica/consome evento de tecla estavel para beep, no maximo uma vez.
void app_model_set_key_press_flag(void);
bool app_model_take_key_press_flag(void);

// Consulta se o beep de navegacao esta habilitado no menu.
bool app_model_is_key_beep_enabled(void);

// Retorna a HOME apos 30 s sem tecla; nao altera persistencia de configuracoes.
void app_model_check_inactivity_timeout(uint32_t now_ms);

// Consultas de tela que controlam a ativacao dos tres managers de sondas.
bool app_model_is_probe_install_screen_active(void);
bool app_model_is_probe_uninstall_screen_active(void);
bool app_model_is_probe_list_screen_active(void);

// Atualiza progresso da FSM de instalacao (completed_count limitado a 4).
void app_model_set_probe_install_status(bool running, bool finished, uint8_t completed_count);

// Atualiza progresso da FSM de desinstalacao (cycle limitado a 3).
void app_model_set_probe_uninstall_status(bool running, bool finished, uint8_t cycle);

// Atualiza um slot 0..3 com identidade, presenca, validade e leituras em graus/%.
void app_model_set_probe_snapshot(uint8_t probe_slot,
                                  uint8_t probe_id,
                                  app_probe_type_t probe_type,
                                  bool installed,
                                  bool has_reading,
                                  float air_temperature,
                                  float air_humidity,
                                  float soil_temperature,
                                  float soil_humidity);

// Restaura curvas e estado ON/OFF sem agendar nova gravacao.
void app_model_set_ambience_curves(const app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                   bool configured);
// Entrega snapshot pendente apos debounce de 1,5 s; limpa o pedido consumido.
bool app_model_take_ambience_write_request(app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                          bool *configured);
// Reagenda uma gravacao depois de falha de armazenamento.
void app_model_request_ambience_write(void);
// Publica estado de alarme para display e buzzer.
void app_model_set_ambience_alarm_active(bool active);
// Atualiza contagem e limita a selecao atual do historico de relatorios.
void app_model_set_report_count(uint16_t count);

// Zera status e telemetria das sondas para iniciar um fluxo sem dados antigos.
void app_model_reset_probe_install_data(void);

// Fecha o fluxo de desinstalacao apos a pausa visual de conclusao.
void app_model_exit_probe_uninstall_to_menu(void);

#endif