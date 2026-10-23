// Adquire o mutex antes de ler ou alterar qualquer campo do estado global.
// Libera a secao critica para as demais tasks.
#include "app_model.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Estado privado protegido por mutex. Campos em grupos guardam navegação,
// dados de negocio, pedidos para workers e snapshots de hardware.
typedef struct {
    // Cursor e subfluxos temporarios da interface.
    uint8_t menu_index;
    uint8_t settings_index;
    uint16_t report_count;
    uint16_t report_index;
    app_lot_mode_t settings_lot_mode;
    app_options_mode_t settings_options_mode;
    bool lot_started;
    app_datetime_t lot_start_datetime;
    app_datetime_t edit_lot_datetime;
    bool settings_editing_datetime;
    // Configuracao ambiental, debounce de persistencia e estado do alarme.
    app_ambience_mode_t ambience_mode;
    uint8_t ambience_curve_index;
    uint8_t ambience_field_index;
    app_ambience_curve_t ambience_curves[APP_AMBIENCE_CURVE_COUNT];
    bool ambience_configured;
    bool ambience_edit_session_dirty;
    bool ambience_alarm_active;
    bool ambience_write_pending;
    uint32_t ambience_last_edit_ms;
    // Preferencias e estado das rotinas de buzzer/sondas.
    uint8_t settings_buzzer_mode;
    uint8_t buzzer_index;
    uint8_t settings_probe_mode;
    uint8_t probe_menu_index;
    uint8_t home_alarm_offset;
    bool probe_install_running;
    bool probe_install_finished;
    uint8_t probe_install_completed_count;
    bool probe_uninstall_running;
    bool probe_uninstall_finished;
    uint8_t probe_uninstall_cycle;
    app_probe_snapshot_t probes[APP_PROBE_COUNT];
    // Eventos, buffers de edicao, relogio e comandos pendentes aos workers.
    bool key_beep_enabled;
    bool key_beep_write_pending;
    bool key_press_pending;
    uint8_t datetime_field_index;
    app_screen_t screen;
    int adc_raw;
    app_datetime_t datetime;
    app_datetime_t edit_datetime;
    bool lot_write_pending;
    bool lot_write_started;
    bool rtc_write_pending;
    uint32_t last_user_interaction_ms;
} app_model_data_t;

static app_model_data_t s_model;            //Criando uma estrutura nova
static SemaphoreHandle_t s_model_mutex;     //Criando semaforo para usar o mutex

#define APP_MAIN_MENU_ITEMS      3
#define APP_SETTINGS_MENU_ITEMS  5
#define APP_BUZZER_MENU_ITEMS    2
#define APP_PROBE_MENU_ITEMS     3
#define APP_AMBIENCE_CURVE_FIELDS 8
#define APP_AMBIENCE_SAVE_DEBOUNCE_MS 1500U
#define APP_DATE_FIELDS          6
#define APP_LOT_DATE_FIELDS      5

#define APP_BUZZER_MODE_NONE     0
#define APP_BUZZER_MODE_MENU     1

#define APP_PROBE_MODE_NONE      0
#define APP_PROBE_MODE_MENU      1
#define APP_PROBE_MODE_INSTALL   2
#define APP_PROBE_MODE_UNINSTALL 3
#define APP_PROBE_MODE_LIST      4

#define APP_INACTIVITY_TIMEOUT_MS 30000U

static void lock_model(void){       //com o semaforo/mutex bloquio a estrutura de dados utilizados para a comunicação entre as tasks, protegendo os dados enquanto uma tarefa usa
    // Bloqueia ate o mutex ficar disponivel.
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
}

static void unlock_model(void){
    // Libera mutex para outra task acessar o estado.
    xSemaphoreGive(s_model_mutex);
}

static void reset_settings_submenus(void)
// Cancela modos transitorios de Ajustes sem apagar valores configurados.
{
    s_model.settings_lot_mode = APP_LOT_MODE_NONE;
    s_model.settings_options_mode = APP_OPTIONS_MODE_NONE;
    s_model.settings_editing_datetime = false;
    s_model.ambience_mode = APP_AMBIENCE_MODE_NONE;
    s_model.settings_buzzer_mode = APP_BUZZER_MODE_NONE;
    s_model.settings_probe_mode = APP_PROBE_MODE_NONE;
}

static bool adjust_ambience_field_unlocked(bool increase)
// Ajusta campo selecionado, limita faixas e preserva ordem das idades.
{
    app_ambience_curve_t *curve = &s_model.ambience_curves[s_model.ambience_curve_index];
    bool changed = false;

    switch (s_model.ambience_field_index) {
        case 0:
            if (increase && curve->age_days < 999U &&
                (s_model.ambience_curve_index + 1U >= APP_AMBIENCE_CURVE_COUNT ||
                 curve->age_days + 1U < s_model.ambience_curves[s_model.ambience_curve_index + 1U].age_days)) {
                curve->age_days++;
                changed = true;
            } else if (!increase && curve->age_days >
                       (s_model.ambience_curve_index == 0U ? 0U :
                        s_model.ambience_curves[s_model.ambience_curve_index - 1U].age_days + 1U)) {
                curve->age_days--;
                changed = true;
            }
            break;
        case 1:
            if (increase && curve->air_temperature_x10 < 600) {
                curve->air_temperature_x10 += 5;
                changed = true;
            } else if (!increase && curve->air_temperature_x10 > -100) {
                curve->air_temperature_x10 -= 5;
                changed = true;
            }
            break;
        case 2:
            if (increase && curve->air_humidity_percent < 100U) {
                curve->air_humidity_percent++;
                changed = true;
            } else if (!increase && curve->air_humidity_percent > 0U) {
                curve->air_humidity_percent--;
                changed = true;
            }
            break;
        case 3:
            if (increase && curve->soil_temperature_x10 < 600) {
                curve->soil_temperature_x10 += 5;
                changed = true;
            } else if (!increase && curve->soil_temperature_x10 > -100) {
                curve->soil_temperature_x10 -= 5;
                changed = true;
            }
            break;
        case 4:
            if (increase && curve->soil_humidity_percent < 100U) {
                curve->soil_humidity_percent++;
                changed = true;
            } else if (!increase && curve->soil_humidity_percent > 0U) {
                curve->soil_humidity_percent--;
                changed = true;
            }
            break;
        case 5:
            if (increase && curve->sprinkler_count < 2U) {
                curve->sprinkler_count++;
                changed = true;
            } else if (!increase && curve->sprinkler_count > 0U) {
                curve->sprinkler_count--;
                changed = true;
            }
            break;
        case 6:
            if (increase && curve->irrigator_count < 2U) {
                curve->irrigator_count++;
                changed = true;
            } else if (!increase && curve->irrigator_count > 0U) {
                curve->irrigator_count--;
                changed = true;
            }
            break;
        case 7:
            if (increase && curve->exhaust_fan_count < 2U) {
                curve->exhaust_fan_count++;
                changed = true;
            } else if (!increase && curve->exhaust_fan_count > 0U) {
                curve->exhaust_fan_count--;
                changed = true;
            }
            break;
        default:
            break;
    }

    return changed;
}

static void reset_probe_install_data_unlocked(void)
// Zera status e telemetria; a funcao exige que o chamador segure o mutex.
{
    // Zera estado operacional das tres telas de sondas:
    // - Instalacao (status e contador)
    // - Desinstalacao (status e ciclo)
    // - Telemetria (placeholders via probes[])
    s_model.probe_install_running = false;
    s_model.probe_install_finished = false;
    s_model.probe_install_completed_count = 0;
    s_model.probe_uninstall_running = false;
    s_model.probe_uninstall_finished = false;
    s_model.probe_uninstall_cycle = 0;

    for (int i = 0; i < APP_PROBE_COUNT; i++) {
        // Mantemos ID/tipo estaveis no reset para a UI sempre saber
        // qual linha representa cada slot de sonda.
        s_model.probes[i].probe_id = (uint8_t)(i + 1U);
        s_model.probes[i].probe_type = APP_PROBE_TYPE_AIR_SOIL_TH;
        s_model.probes[i].installed = false;
        s_model.probes[i].has_reading = false;
        s_model.probes[i].air_temperature = 0.0f;
        s_model.probes[i].air_humidity = 0.0f;
        s_model.probes[i].soil_temperature = 0.0f;
        s_model.probes[i].soil_humidity = 0.0f;
    }
}

static bool is_leap_year(uint16_t year)
// Utilitarios de calendario gregoriano usados pelo editor e idade do lote.
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
// Retorna dias do mes, incluindo fevereiro de ano bissexto.
{
    static const uint8_t month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) {
        return 31;
    }

    if (month == 2 && is_leap_year(year)) {
        return 29;
    }

    return month_days[month - 1];
}

static uint32_t date_to_day_count(const app_datetime_t *dt)
// Converte data para dias desde 01/01/2000, base do calculo de idade do lote.
{
    uint32_t days = 0;
    for (uint16_t year = 2000; year < dt->year; year++) {
        days += is_leap_year(year) ? 366U : 365U;
    }
    for (uint8_t month = 1; month < dt->month; month++) {
        days += days_in_month(dt->year, month);
    }
    return days + (uint32_t)(dt->day - 1U);
}

static uint64_t datetime_to_seconds(const app_datetime_t *dt)
// Converte data/hora em segundos para diferenciar lote ativo de agendado.
{
    return (uint64_t)date_to_day_count(dt) * 86400U +
           (uint64_t)dt->hour * 3600U +
           (uint64_t)dt->minute * 60U + dt->second;
}

static void normalize_datetime(app_datetime_t *dt)
// Limita campos civis aos intervalos aceitos pelo RTC e pelo modelo.
{
    if (dt == NULL) {
        return;
    }

    if (dt->year < 2000) {
        dt->year = 2000;
    } else if (dt->year > 2099) {
        dt->year = 2099;
    }

    if (dt->month < 1) {
        dt->month = 1;
    } else if (dt->month > 12) {
        dt->month = 12;
    }

    uint8_t dim = days_in_month(dt->year, dt->month);
    if (dt->day < 1) {
        dt->day = 1;
    } else if (dt->day > dim) {
        dt->day = dim;
    }

    if (dt->hour > 23) {
        dt->hour = 23;
    }

    if (dt->minute > 59) {
        dt->minute = 59;
    }

    if (dt->second > 59) {
        dt->second = 59;
    }
}

static void increment_field(app_datetime_t *dt, uint8_t field)
// Incrementa campo selecionado com wrap e corrige dias invalidos no mes.
{
    if (dt == NULL) {
        return;
    }

    switch (field) {
        case 0: // dia
            dt->day++;
            if (dt->day > days_in_month(dt->year, dt->month)) {
                dt->day = 1;
            }
            break;
        case 1: // mes
            dt->month++;
            if (dt->month > 12) {
                dt->month = 1;
            }
            if (dt->day > days_in_month(dt->year, dt->month)) {
                dt->day = days_in_month(dt->year, dt->month);
            }
            break;
        case 2: // ano
            dt->year++;
            if (dt->year > 2099) {
                dt->year = 2000;
            }
            if (dt->day > days_in_month(dt->year, dt->month)) {
                dt->day = days_in_month(dt->year, dt->month);
            }
            break;
        case 3: // hora
            dt->hour = (uint8_t)((dt->hour + 1U) % 24U);
            break;
        case 4: // minuto
            dt->minute = (uint8_t)((dt->minute + 1U) % 60U);
            break;
        case 5: // segundo
            dt->second = (uint8_t)((dt->second + 1U) % 60U);
            break;
        default:
            break;
    }
}

static void decrement_field(app_datetime_t *dt, uint8_t field)
// Decrementa campo selecionado com wrap e corrige dias invalidos no mes.
{
    if (dt == NULL) {
        return;
    }

    switch (field) {
        case 0: // dia
            if (dt->day <= 1) {
                dt->day = days_in_month(dt->year, dt->month);
            } else {
                dt->day--;
            }
            break;
        case 1: // mes
            if (dt->month <= 1) {
                dt->month = 12;
            } else {
                dt->month--;
            }
            if (dt->day > days_in_month(dt->year, dt->month)) {
                dt->day = days_in_month(dt->year, dt->month);
            }
            break;
        case 2: // ano
            if (dt->year <= 2000) {
                dt->year = 2099;
            } else {
                dt->year--;
            }
            if (dt->day > days_in_month(dt->year, dt->month)) {
                dt->day = days_in_month(dt->year, dt->month);
            }
            break;
        case 3: // hora
            dt->hour = (dt->hour == 0) ? 23 : (uint8_t)(dt->hour - 1U);
            break;
        case 4: // minuto
            dt->minute = (dt->minute == 0) ? 59 : (uint8_t)(dt->minute - 1U);
            break;
        case 5: // segundo
            dt->second = (dt->second == 0) ? 59 : (uint8_t)(dt->second - 1U);
            break;
        default:
            break;
    }
}

void app_model_init(void)
// Inicializa mutex, menus, idades default e placeholders antes das tasks.
{
    // Limpa toda a estrutura com zero.
    memset(&s_model, 0, sizeof(s_model));

    s_model.screen = APP_SCREEN_HOME;
    s_model.adc_raw = 0;
    s_model.menu_index = 0;
    s_model.settings_index = 0;
    s_model.settings_editing_datetime = false;
    s_model.ambience_mode = APP_AMBIENCE_MODE_NONE;
    s_model.ambience_curve_index = 0;
    s_model.ambience_field_index = 0;
    for (uint8_t i = 0; i < APP_AMBIENCE_CURVE_COUNT; i++) {
        s_model.ambience_curves[i].age_days = (uint16_t)(i * 7U);
    }
    s_model.settings_buzzer_mode = APP_BUZZER_MODE_NONE;
    s_model.buzzer_index = 0;
    s_model.settings_probe_mode = APP_PROBE_MODE_NONE;
    s_model.probe_menu_index = 0;
    reset_probe_install_data_unlocked();
    s_model.key_beep_enabled = true;
    s_model.key_press_pending = false;
    s_model.datetime_field_index = 0;

    // Valor inicial seguro ate a primeira leitura do DS3231.
    s_model.datetime.year = 2026;
    s_model.datetime.month = 1;
    s_model.datetime.day = 1;
    s_model.datetime.weekday = 5;
    s_model.datetime.hour = 0;
    s_model.datetime.minute = 0;
    s_model.datetime.second = 0;
    s_model.edit_datetime = s_model.datetime;
    s_model.last_user_interaction_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    s_model_mutex = xSemaphoreCreateMutex();
}

void app_model_set_adc_raw(int adc_raw)
// Publica a ultima amostra bruta do ADC para diagnostico da interface.
{
    lock_model();
    s_model.adc_raw = adc_raw;
    unlock_model();
}

int app_model_get_adc_raw(void)
// Consulta a amostra mais recente sob mutex.
{
    lock_model();
    int value = s_model.adc_raw;
    unlock_model();
    return value;
}

void app_model_set_datetime(const app_datetime_t *dt)
// Atualiza cache do RTC e buffer de edicao quando o usuario nao o altera.
{
    if (dt == NULL) {
        return;
    }

    lock_model();
    s_model.datetime = *dt;
    normalize_datetime(&s_model.datetime);

    // Enquanto o usuario nao estiver no editor de data/hora,
    // espelhamos leitura do RTC no buffer de edicao.
    if (!s_model.settings_editing_datetime) {
        s_model.edit_datetime = s_model.datetime;
    }
    unlock_model();
}

void app_model_set_lot_state(bool started, const app_datetime_t *start_datetime)
// Restaura estado do lote sem criar pedido de escrita durante o boot.
{
    if (started && start_datetime == NULL) {
        return;
    }

    lock_model();
    s_model.lot_started = started;
    if (started) {
        s_model.lot_start_datetime = *start_datetime;
        normalize_datetime(&s_model.lot_start_datetime);
    } else {
        memset(&s_model.lot_start_datetime, 0, sizeof(s_model.lot_start_datetime));
    }
    unlock_model();
}

bool app_model_take_lot_write_request(app_datetime_t *start_datetime, bool *started)
// Consome pedido de inicio/fim de lote produzido pela navegacao.
{
    bool has_request = false;

    if (start_datetime == NULL || started == NULL) {
        return false;
    }

    lock_model();
    if (s_model.lot_write_pending) {
        *start_datetime = s_model.lot_start_datetime;
        *started = s_model.lot_write_started;
        s_model.lot_write_pending = false;
        has_request = true;
    }
    unlock_model();

    return has_request;
}

bool app_model_take_rtc_write_request(app_datetime_t *dt)
// Consome pedido de escrita no DS3231 gerado pelo editor de data/hora.
{
    bool has_request = false;

    lock_model();
    if (s_model.rtc_write_pending) {
        s_model.rtc_write_pending = false;
        if (dt != NULL) {
            *dt = s_model.datetime;
        }
        has_request = true;
    }
    unlock_model();

    return has_request;
}

void app_model_get_snapshot(app_state_snapshot_t *snapshot)
// Copia estado coerente e calcula status/idade do lote dentro da secao critica.
{
    if (snapshot == NULL) {
        return;
    }

    lock_model();
    snapshot->screen = s_model.screen;
    snapshot->adc_raw = s_model.adc_raw;
    snapshot->menu_index = s_model.menu_index;
    snapshot->settings_index = s_model.settings_index;
    snapshot->report_count = s_model.report_count;
    snapshot->report_index = s_model.report_index;
    snapshot->settings_lot_mode = s_model.settings_lot_mode;
    snapshot->settings_options_mode = s_model.settings_options_mode;
    snapshot->settings_editing_datetime = s_model.settings_editing_datetime;
    snapshot->ambience_mode = s_model.ambience_mode;
    snapshot->ambience_curve_index = s_model.ambience_curve_index;
    snapshot->ambience_field_index = s_model.ambience_field_index;
    memcpy(snapshot->ambience_curves, s_model.ambience_curves,
           sizeof(snapshot->ambience_curves));
        snapshot->ambience_configured = s_model.ambience_configured;
    snapshot->ambience_alarm_active = s_model.ambience_alarm_active;
    snapshot->settings_buzzer_mode = s_model.settings_buzzer_mode;
    snapshot->buzzer_index = s_model.buzzer_index;
    snapshot->settings_probe_mode = s_model.settings_probe_mode;
    snapshot->probe_menu_index = s_model.probe_menu_index;
    snapshot->home_alarm_offset = s_model.home_alarm_offset;
    snapshot->probe_install_running = s_model.probe_install_running;
    snapshot->probe_install_finished = s_model.probe_install_finished;
    snapshot->probe_install_completed_count = s_model.probe_install_completed_count;
    snapshot->probe_uninstall_running = s_model.probe_uninstall_running;
    snapshot->probe_uninstall_finished = s_model.probe_uninstall_finished;
    snapshot->probe_uninstall_cycle = s_model.probe_uninstall_cycle;

    for (int i = 0; i < APP_PROBE_COUNT; i++) {
        snapshot->probes[i] = s_model.probes[i];
    }
    snapshot->key_beep_enabled = s_model.key_beep_enabled;
    snapshot->datetime_field_index = s_model.datetime_field_index;
    snapshot->lot_started = s_model.lot_started;
    snapshot->lot_start_datetime = s_model.lot_start_datetime;

    snapshot->lot_status = APP_LOT_STATUS_INACTIVE;
    snapshot->lot_age_days = 0;
    if (s_model.lot_started) {
        uint64_t now_seconds = datetime_to_seconds(&s_model.datetime);
        uint64_t start_seconds = datetime_to_seconds(&s_model.lot_start_datetime);
        if (start_seconds > now_seconds) {
            snapshot->lot_status = APP_LOT_STATUS_SCHEDULED;
        } else {
            snapshot->lot_status = APP_LOT_STATUS_ACTIVE;
            uint32_t today = date_to_day_count(&s_model.datetime);
            uint32_t start_day = date_to_day_count(&s_model.lot_start_datetime);
            snapshot->lot_age_days = today - start_day;
        }
    }

    if (s_model.settings_editing_datetime) {
        snapshot->datetime = s_model.edit_datetime;
    } else if (s_model.settings_lot_mode == APP_LOT_MODE_EDIT) {
        snapshot->datetime = s_model.edit_lot_datetime;
    } else {
        snapshot->datetime = s_model.datetime;
    }
    unlock_model();
}

void app_model_process_key(app_key_t key)
// Maquina de estados central que aplica eventos de tecla a tela/submenu atual.
{
    if (key == APP_KEY_NONE) {
        return;
    }

    lock_model();
    s_model.last_user_interaction_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    switch (s_model.screen) {
        case APP_SCREEN_HOME:
            if (key == APP_KEY_MENU) {
                s_model.screen = APP_SCREEN_MENU;
                s_model.menu_index = 0;
            } else if (key == APP_KEY_LEFT) {
                // Atalho operacional: HOME -> lista resumida de sondas.
                s_model.screen = APP_SCREEN_HOME_PROBES;
            } else if (key == APP_KEY_DOWN) {
                s_model.home_alarm_offset = 0;
                s_model.screen = APP_SCREEN_HOME_PROBE_ALARMS;
            }
            break;

        case APP_SCREEN_HOME_PROBES:
            // BACK/LEFT retornam para HOME sem passar pelos menus.
            if (key == APP_KEY_BACK || key == APP_KEY_LEFT) {
                s_model.screen = APP_SCREEN_HOME;
            } else if (key == APP_KEY_MENU) {
                s_model.screen = APP_SCREEN_MENU;
                s_model.menu_index = 0;
            }
            break;

        case APP_SCREEN_HOME_PROBE_ALARMS:
            if (key == APP_KEY_BACK || key == APP_KEY_LEFT) {
                s_model.screen = APP_SCREEN_HOME;
            } else if (key == APP_KEY_MENU) {
                s_model.screen = APP_SCREEN_MENU;
                s_model.menu_index = 0;
            } else if (key == APP_KEY_DOWN) {
                s_model.home_alarm_offset++;
            } else if (key == APP_KEY_UP) {
                if (s_model.home_alarm_offset > 0U) {
                    s_model.home_alarm_offset--;
                } else {
                    s_model.screen = APP_SCREEN_HOME_PROBES;
                }
            }
            break;

        case APP_SCREEN_MENU:
            if (key == APP_KEY_UP) {
                s_model.menu_index = (s_model.menu_index + APP_MAIN_MENU_ITEMS - 1U) % APP_MAIN_MENU_ITEMS;
            } else if (key == APP_KEY_DOWN) {
                s_model.menu_index = (s_model.menu_index + 1U) % APP_MAIN_MENU_ITEMS;
            } else if (key == APP_KEY_BACK) {
                s_model.screen = APP_SCREEN_HOME;
            } else if (key == APP_KEY_ENTER) {
                if (s_model.menu_index == 0U) {
                    s_model.screen = APP_SCREEN_MENU_SETTINGS;
                    s_model.settings_index = 0;
                } else if (s_model.menu_index == 1U) {
                    s_model.screen = APP_SCREEN_MENU_REPORTS;
                    s_model.report_index = 0;
                } else {
                    s_model.screen = APP_SCREEN_MENU_ABOUT;
                }
            }
            break;

        case APP_SCREEN_MENU_SETTINGS:
            if (s_model.settings_editing_datetime) {
                if (key == APP_KEY_UP) {
                    increment_field(&s_model.edit_datetime, s_model.datetime_field_index);
                } else if (key == APP_KEY_DOWN) {
                    decrement_field(&s_model.edit_datetime, s_model.datetime_field_index);
                } else if (key == APP_KEY_ENTER) {
                    s_model.datetime_field_index = (uint8_t)((s_model.datetime_field_index + 1U) % APP_DATE_FIELDS);
                } else if (key == APP_KEY_BACK) {
                    // BACK salva no modelo e sinaliza escrita unica no RTC.
                    s_model.datetime = s_model.edit_datetime;
                    normalize_datetime(&s_model.datetime);
                    s_model.rtc_write_pending = true;
                    s_model.settings_editing_datetime = false;
                }
            } else if (s_model.settings_lot_mode != APP_LOT_MODE_NONE) {
                if (s_model.settings_lot_mode == APP_LOT_MODE_MENU) {
                    if (key == APP_KEY_ENTER) {
                        if (s_model.lot_started) {
                            s_model.settings_lot_mode = APP_LOT_MODE_FINISH_CONFIRM;
                        } else {
                            s_model.edit_lot_datetime = s_model.datetime;
                            s_model.edit_lot_datetime.second = 0;
                            s_model.datetime_field_index = 0;
                            s_model.settings_lot_mode = APP_LOT_MODE_EDIT;
                        }
                    } else if (key == APP_KEY_BACK) {
                        s_model.settings_lot_mode = APP_LOT_MODE_NONE;
                    }
                } else if (s_model.settings_lot_mode == APP_LOT_MODE_EDIT) {
                    if (key == APP_KEY_UP) {
                        increment_field(&s_model.edit_lot_datetime, s_model.datetime_field_index);
                    } else if (key == APP_KEY_DOWN) {
                        decrement_field(&s_model.edit_lot_datetime, s_model.datetime_field_index);
                    } else if (key == APP_KEY_ENTER) {
                        if (s_model.datetime_field_index + 1U < APP_LOT_DATE_FIELDS) {
                            s_model.datetime_field_index++;
                        } else {
                            normalize_datetime(&s_model.edit_lot_datetime);
                            s_model.lot_start_datetime = s_model.edit_lot_datetime;
                            s_model.lot_started = true;
                            s_model.lot_write_pending = true;
                            s_model.lot_write_started = true;
                            s_model.settings_lot_mode = APP_LOT_MODE_CONFIRMED;
                        }
                    } else if (key == APP_KEY_BACK) {
                        s_model.settings_lot_mode = APP_LOT_MODE_MENU;
                    }
                } else if (s_model.settings_lot_mode == APP_LOT_MODE_FINISH_CONFIRM) {
                    if (key == APP_KEY_ENTER) {
                        s_model.lot_started = false;
                        memset(&s_model.lot_start_datetime, 0, sizeof(s_model.lot_start_datetime));
                        s_model.lot_write_pending = true;
                        s_model.lot_write_started = false;
                        s_model.settings_lot_mode = APP_LOT_MODE_FINISHED;
                    } else if (key == APP_KEY_BACK) {
                        s_model.settings_lot_mode = APP_LOT_MODE_MENU;
                    }
                } else if (s_model.settings_lot_mode == APP_LOT_MODE_FINISHED &&
                           (key == APP_KEY_ENTER || key == APP_KEY_BACK)) {
                    s_model.settings_lot_mode = APP_LOT_MODE_MENU;
                } else if (s_model.settings_lot_mode == APP_LOT_MODE_CONFIRMED &&
                           (key == APP_KEY_ENTER || key == APP_KEY_BACK)) {
                    s_model.settings_lot_mode = APP_LOT_MODE_MENU;
                }
            } else if (s_model.ambience_mode == APP_AMBIENCE_MODE_CURVE_LIST) {
                if (key == APP_KEY_UP) {
                    s_model.ambience_curve_index = (s_model.ambience_curve_index + APP_AMBIENCE_CURVE_COUNT - 1U) % APP_AMBIENCE_CURVE_COUNT;
                } else if (key == APP_KEY_DOWN) {
                    s_model.ambience_curve_index = (s_model.ambience_curve_index + 1U) % APP_AMBIENCE_CURVE_COUNT;
                } else if (key == APP_KEY_ENTER) {
                    s_model.ambience_mode = APP_AMBIENCE_MODE_EDIT_CURVE;
                    s_model.ambience_field_index = 0;
                } else if (key == APP_KEY_BACK) {
                    if (s_model.ambience_edit_session_dirty) {
                        s_model.ambience_configured = true;
                        s_model.ambience_write_pending = true;
                        s_model.ambience_last_edit_ms = s_model.last_user_interaction_ms;
                    }
                    s_model.ambience_mode = APP_AMBIENCE_MODE_NONE;
                }
            } else if (s_model.ambience_mode == APP_AMBIENCE_MODE_EDIT_CURVE) {
                if (key == APP_KEY_UP) {
                    if (adjust_ambience_field_unlocked(true)) {
                        s_model.ambience_edit_session_dirty = true;
                        s_model.ambience_write_pending = true;
                        s_model.ambience_last_edit_ms = s_model.last_user_interaction_ms;
                    }
                } else if (key == APP_KEY_DOWN) {
                    if (adjust_ambience_field_unlocked(false)) {
                        s_model.ambience_edit_session_dirty = true;
                        s_model.ambience_write_pending = true;
                        s_model.ambience_last_edit_ms = s_model.last_user_interaction_ms;
                    }
                } else if (key == APP_KEY_ENTER) {
                    s_model.ambience_field_index = (uint8_t)((s_model.ambience_field_index + 1U) % APP_AMBIENCE_CURVE_FIELDS);
                } else if (key == APP_KEY_BACK) {
                    s_model.ambience_mode = APP_AMBIENCE_MODE_CURVE_LIST;
                }
            } else if (s_model.settings_options_mode == APP_OPTIONS_MODE_MENU) {
                if (s_model.settings_buzzer_mode == APP_BUZZER_MODE_MENU) {
                    if (key == APP_KEY_UP) {
                        s_model.buzzer_index = (s_model.buzzer_index + APP_BUZZER_MENU_ITEMS - 1U) % APP_BUZZER_MENU_ITEMS;
                    } else if (key == APP_KEY_DOWN) {
                        s_model.buzzer_index = (s_model.buzzer_index + 1U) % APP_BUZZER_MENU_ITEMS;
                    } else if (key == APP_KEY_ENTER) {
                        bool enabled = (s_model.buzzer_index == 0U);
                        if (s_model.key_beep_enabled != enabled) {
                            s_model.key_beep_enabled = enabled;
                            s_model.key_beep_write_pending = true;
                        }
                    } else if (key == APP_KEY_BACK) {
                        s_model.settings_buzzer_mode = APP_BUZZER_MODE_NONE;
                    }
                } else if (key == APP_KEY_ENTER) {
                    s_model.settings_buzzer_mode = APP_BUZZER_MODE_MENU;
                    s_model.buzzer_index = s_model.key_beep_enabled ? 0U : 1U;
                } else if (key == APP_KEY_BACK) {
                    s_model.settings_options_mode = APP_OPTIONS_MODE_NONE;
                }
            } else if (s_model.settings_probe_mode == APP_PROBE_MODE_MENU) {
                // Submenu "Instalar Sondas": navega entre Instalar, Desinstalar e Sondas.
                if (key == APP_KEY_UP) {
                    s_model.probe_menu_index = (s_model.probe_menu_index + APP_PROBE_MENU_ITEMS - 1U) % APP_PROBE_MENU_ITEMS;
                } else if (key == APP_KEY_DOWN) {
                    s_model.probe_menu_index = (s_model.probe_menu_index + 1U) % APP_PROBE_MENU_ITEMS;
                } else if (key == APP_KEY_ENTER) {
                    if (s_model.probe_menu_index == 0U) {
                        // Entrar em "Instalar" sempre parte de estado limpo.
                        reset_probe_install_data_unlocked();
                        s_model.settings_probe_mode = APP_PROBE_MODE_INSTALL;
                    } else if (s_model.probe_menu_index == 1U) {
                        // Entrar em "Desinstalar" tambem limpa status antigo.
                        reset_probe_install_data_unlocked();
                        s_model.settings_probe_mode = APP_PROBE_MODE_UNINSTALL;
                    } else {
                        // Tela "Sondas" usa ultimo ciclo de monitoramento.
                        s_model.settings_probe_mode = APP_PROBE_MODE_LIST;
                    }
                } else if (key == APP_KEY_BACK) {
                    s_model.settings_probe_mode = APP_PROBE_MODE_NONE;
                }
            } else if (s_model.settings_probe_mode == APP_PROBE_MODE_INSTALL ||
                       s_model.settings_probe_mode == APP_PROBE_MODE_UNINSTALL ||
                       s_model.settings_probe_mode == APP_PROBE_MODE_LIST) {
                // Nas telas internas, BACK retorna ao submenu "Instalar Sondas".
                if (key == APP_KEY_BACK) {
                    s_model.settings_probe_mode = APP_PROBE_MODE_MENU;
                }
            } else {
                if (key == APP_KEY_UP) {
                    s_model.settings_index = (s_model.settings_index + APP_SETTINGS_MENU_ITEMS - 1U) % APP_SETTINGS_MENU_ITEMS;
                } else if (key == APP_KEY_DOWN) {
                    s_model.settings_index = (s_model.settings_index + 1U) % APP_SETTINGS_MENU_ITEMS;
                } else if (key == APP_KEY_BACK) {
                    s_model.screen = APP_SCREEN_MENU;
                    reset_settings_submenus();
                } else if (key == APP_KEY_ENTER) {
                    if (s_model.settings_index == 0U) {
                        s_model.settings_lot_mode = APP_LOT_MODE_MENU;
                    } else if (s_model.settings_index == 1U) {
                        s_model.settings_editing_datetime = true;
                        s_model.datetime_field_index = 0;
                        s_model.edit_datetime = s_model.datetime;
                    } else if (s_model.settings_index == 2U) {
                        s_model.ambience_mode = APP_AMBIENCE_MODE_CURVE_LIST;
                        s_model.ambience_curve_index = 0;
                        s_model.ambience_edit_session_dirty = false;
                    } else if (s_model.settings_index == 3U) {
                        s_model.settings_options_mode = APP_OPTIONS_MODE_MENU;
                    } else if (s_model.settings_index == 4U) {
                        // Entra no gerenciador de sondas a partir de Ajustes.
                        s_model.settings_probe_mode = APP_PROBE_MODE_MENU;
                        s_model.probe_menu_index = 0;
                    }
                }
            }
            break;

        case APP_SCREEN_MENU_REPORTS:
            if (s_model.report_count > 0U && key == APP_KEY_UP) {
                s_model.report_index = (uint16_t)((s_model.report_index + 1U) % s_model.report_count);
            } else if (s_model.report_count > 0U && key == APP_KEY_DOWN) {
                s_model.report_index = s_model.report_index == 0U
                    ? (uint16_t)(s_model.report_count - 1U)
                    : (uint16_t)(s_model.report_index - 1U);
            } else if (key == APP_KEY_BACK) {
                s_model.screen = APP_SCREEN_MENU;
            }
            break;
        case APP_SCREEN_MENU_ABOUT:
            if (key == APP_KEY_BACK) {
                s_model.screen = APP_SCREEN_MENU;
            }
            break;

        default:
            s_model.screen = APP_SCREEN_HOME;
            break;
    }

    unlock_model();
}

void app_model_set_key_press_flag(void)
// Marca evento estavel de tecla para o io_task emitir beep no maximo uma vez.
{
    lock_model();
    s_model.key_press_pending = true;
    unlock_model();
}

bool app_model_take_key_press_flag(void)
// Consome e limpa o evento de tecla pendente.
{
    bool has_key_press = false;

    lock_model();
    if (s_model.key_press_pending) {    //se houver um evento de tecla pendente,  
        has_key_press = true;       //seta a variavel has_key_press como true
        s_model.key_press_pending = false;  //e zera a flag de evento de tecla pendente
    }
    unlock_model();

    return has_key_press;
}

bool app_model_is_key_beep_enabled(void)
// Consulta opcao de beep escolhida no submenu Buzzer.
{
    bool enabled;

    lock_model();
    enabled = s_model.key_beep_enabled;     //status da funcionalidade de beep de tecla
    unlock_model();

    return enabled;
}

void app_model_set_key_beep_enabled(bool enabled)
{
    lock_model();
    s_model.key_beep_enabled = enabled;
    s_model.buzzer_index = enabled ? 0U : 1U;
    s_model.key_beep_write_pending = false;
    unlock_model();
}

bool app_model_take_key_beep_write_request(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }

    lock_model();
    bool pending = s_model.key_beep_write_pending;
    if (pending) {
        *enabled = s_model.key_beep_enabled;
        s_model.key_beep_write_pending = false;
    }
    unlock_model();
    return pending;
}

void app_model_request_key_beep_write(void)
{
    lock_model();
    s_model.key_beep_write_pending = true;
    unlock_model();
}

bool app_model_is_probe_install_screen_active(void)
// Consultas desacoplam as FSMs RS-485 da implementacao visual do display.
{
    bool active;

    lock_model();
    // Essa consulta e usada para ativar/desativar o manager de instalacao
    // sem acoplamento com o desenho da UI.
    active = (s_model.screen == APP_SCREEN_MENU_SETTINGS) &&
             (s_model.settings_probe_mode == APP_PROBE_MODE_INSTALL);
    unlock_model();

    return active;
}

bool app_model_is_probe_uninstall_screen_active(void)
{
    bool active;

    lock_model();
    // Ativa apenas dentro da tela Desinstalar.
    active = (s_model.screen == APP_SCREEN_MENU_SETTINGS) &&
             (s_model.settings_probe_mode == APP_PROBE_MODE_UNINSTALL);
    unlock_model();

    return active;
}

bool app_model_is_probe_list_screen_active(void)
{
    bool active;

    lock_model();
    // Ativa polling somente quando a tela Sondas esta aberta.
    active = (s_model.screen == APP_SCREEN_MENU_SETTINGS) &&
             (s_model.settings_probe_mode == APP_PROBE_MODE_LIST);
    unlock_model();

    return active;
}

void app_model_set_probe_install_status(bool running, bool finished, uint8_t completed_count)
// Atualiza a FSM de instalacao; limita completed_count aos quatro slots.
{
    lock_model();
    s_model.probe_install_running = running;
    s_model.probe_install_finished = finished;
    s_model.probe_install_completed_count = (completed_count > APP_PROBE_COUNT)
        ? APP_PROBE_COUNT
        : completed_count;
    unlock_model();
}

void app_model_set_probe_uninstall_status(bool running, bool finished, uint8_t cycle)
// Atualiza a FSM de desinstalacao; limita ciclo a tres passagens.
{
    lock_model();
    s_model.probe_uninstall_running = running;
    s_model.probe_uninstall_finished = finished;
    s_model.probe_uninstall_cycle = (cycle > 3U) ? 3U : cycle;
    unlock_model();
}

void app_model_set_probe_snapshot(uint8_t probe_slot,
                                // Publica identidade e telemetria; slots fora de 0..3 sao ignorados.
                                  uint8_t probe_id,
                                  app_probe_type_t probe_type,
                                  bool installed,
                                  bool has_reading,
                                  float air_temperature,
                                  float air_humidity,
                                  float soil_temperature,
                                  float soil_humidity)
{
    if (probe_slot >= APP_PROBE_COUNT) {
        return;
    }

    lock_model();
    // slot 0..3 mapeia para Sensor 1..4 na UI.
    // O par probe_id/probe_type identifica o dispositivo e seu perfil
    // para suportar coexistencia de novos tipos de sonda no futuro.
    s_model.probes[probe_slot].probe_id = probe_id;
    s_model.probes[probe_slot].probe_type = probe_type;
    s_model.probes[probe_slot].installed = installed;
    s_model.probes[probe_slot].has_reading = has_reading;
    s_model.probes[probe_slot].air_temperature = air_temperature;
    s_model.probes[probe_slot].air_humidity = air_humidity;
    s_model.probes[probe_slot].soil_temperature = soil_temperature;
    s_model.probes[probe_slot].soil_humidity = soil_humidity;
    unlock_model();
}

void app_model_reset_probe_install_data(void)
// Wrapper sincronizado para limpar status e placeholders de sondas.
{
    lock_model();
    reset_probe_install_data_unlocked();
    unlock_model();
}

void app_model_exit_probe_uninstall_to_menu(void)
// Retorna da tela de desinstalacao apos a pausa visual de conclusao.
{
    lock_model();
    // Usado pela rotina de desinstalacao apos delay visual de conclusao.
    if (s_model.screen == APP_SCREEN_MENU_SETTINGS &&
        s_model.settings_probe_mode == APP_PROBE_MODE_UNINSTALL) {
        s_model.settings_probe_mode = APP_PROBE_MODE_MENU;
    }
    unlock_model();
}

void app_model_set_ambience_curves(const app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                // Restaura curvas e estado ON/OFF lidos da flash sem agendar nova gravacao.
                                   bool configured)
{
    if (curves == NULL) {
        return;
    }

    lock_model();
    memcpy(s_model.ambience_curves, curves, sizeof(s_model.ambience_curves));
    s_model.ambience_configured = configured;
    s_model.ambience_edit_session_dirty = false;
    s_model.ambience_write_pending = false;
    unlock_model();
}

bool app_model_take_ambience_write_request(app_ambience_curve_t curves[APP_AMBIENCE_CURVE_COUNT],
                                        // Entrega snapshot pendente apos debounce, evitando writes por tecla.
                                           bool *configured)
{
    if (curves == NULL || configured == NULL) {
        return false;
    }

    bool has_request = false;
    lock_model();
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (s_model.ambience_write_pending &&
        now_ms - s_model.ambience_last_edit_ms >= APP_AMBIENCE_SAVE_DEBOUNCE_MS) {
        memcpy(curves, s_model.ambience_curves, sizeof(s_model.ambience_curves));
        *configured = s_model.ambience_configured;
        s_model.ambience_write_pending = false;
        has_request = true;
    }
    unlock_model();
    return has_request;
}

void app_model_request_ambience_write(void)
// Reagenda persistencia apos erro de armazenamento.
{
    lock_model();
    s_model.ambience_write_pending = true;
    s_model.ambience_last_edit_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    unlock_model();
}

void app_model_set_ambience_alarm_active(bool active)
// Publica alarme para HOME, indicador global e buzzer.
{
    lock_model();
    s_model.ambience_alarm_active = active;
    unlock_model();
}

void app_model_set_report_count(uint16_t count)
// Sincroniza a quantidade de eventos com o cache mantido pelo armazenamento.
{
    lock_model();
    s_model.report_count = count;
    if (count == 0U) {
        s_model.report_index = 0;
    } else if (s_model.report_index >= count) {
        s_model.report_index = (uint16_t)(count - 1U);
    }
    unlock_model();
}

void app_model_check_inactivity_timeout(uint32_t now_ms)
// Retorna a HOME apos 30 s sem tecla, preservando lote e curvas configuradas.
{
    lock_model();

    // Se estiver fora da HOME e passar 30s sem tecla estavel, retorna para inicio.
    if (s_model.screen != APP_SCREEN_HOME &&
        (uint32_t)(now_ms - s_model.last_user_interaction_ms) >= APP_INACTIVITY_TIMEOUT_MS) {
        s_model.screen = APP_SCREEN_HOME;
        reset_settings_submenus();
    }

    unlock_model();
}
