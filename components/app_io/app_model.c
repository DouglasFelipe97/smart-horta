#include "app_model.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Estrutura interna protegida por mutex para comunicacao entre tasks.
typedef struct {
    uint8_t menu_index;
    uint8_t settings_index;
    bool settings_editing_datetime;
    uint8_t settings_temperature_mode;
    uint8_t temperature_index;
    uint8_t settings_buzzer_mode;
    uint8_t buzzer_index;
    bool key_beep_enabled;
    bool key_press_pending;
    uint8_t datetime_field_index;
    app_screen_t screen;
    int adc_raw;
    app_datetime_t datetime;
    app_datetime_t edit_datetime;
    bool rtc_write_pending;
    uint32_t last_user_interaction_ms;
} app_model_data_t;

static app_model_data_t s_model;            //Criando uma estrutura nova
static SemaphoreHandle_t s_model_mutex;     //Criando semaforo para usar o mutex

#define APP_MAIN_MENU_ITEMS      3
#define APP_SETTINGS_MENU_ITEMS  5
#define APP_TEMPERATURE_MENU_ITEMS 2
#define APP_BUZZER_MENU_ITEMS    2
#define APP_DATE_FIELDS          6

#define APP_TEMPERATURE_MODE_NONE    0
#define APP_TEMPERATURE_MODE_MENU    1
#define APP_TEMPERATURE_MODE_CURVES  2
#define APP_TEMPERATURE_MODE_OFFSET  3

#define APP_BUZZER_MODE_NONE     0
#define APP_BUZZER_MODE_MENU     1

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
{
    s_model.settings_editing_datetime = false;
    s_model.settings_temperature_mode = APP_TEMPERATURE_MODE_NONE;
    s_model.settings_buzzer_mode = APP_BUZZER_MODE_NONE;
}

static bool is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
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

static void normalize_datetime(app_datetime_t *dt)
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
{
    // Limpa toda a estrutura com zero.
    memset(&s_model, 0, sizeof(s_model));

    s_model.screen = APP_SCREEN_HOME;
    s_model.adc_raw = 0;
    s_model.menu_index = 0;
    s_model.settings_index = 0;
    s_model.settings_editing_datetime = false;
    s_model.settings_temperature_mode = APP_TEMPERATURE_MODE_NONE;
    s_model.temperature_index = 0;
    s_model.settings_buzzer_mode = APP_BUZZER_MODE_NONE;
    s_model.buzzer_index = 0;
    s_model.key_beep_enabled = true;
    s_model.key_press_pending = false;
    s_model.datetime_field_index = 0;

    // Valor inicial seguro ate a primeira leitura do DS3231.
    s_model.datetime.year = 2026;
    s_model.datetime.month = 1;
    s_model.datetime.day = 1;
    s_model.datetime.hour = 0;
    s_model.datetime.minute = 0;
    s_model.datetime.second = 0;
    s_model.edit_datetime = s_model.datetime;
    s_model.last_user_interaction_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    s_model_mutex = xSemaphoreCreateMutex();
}

void app_model_set_adc_raw(int adc_raw)
{
    lock_model();
    s_model.adc_raw = adc_raw;
    unlock_model();
}

int app_model_get_adc_raw(void)
{
    lock_model();
    int value = s_model.adc_raw;
    unlock_model();
    return value;
}

void app_model_set_datetime(const app_datetime_t *dt)
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

bool app_model_take_rtc_write_request(app_datetime_t *dt)
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
{
    if (snapshot == NULL) {
        return;
    }

    lock_model();
    snapshot->screen = s_model.screen;
    snapshot->adc_raw = s_model.adc_raw;
    snapshot->menu_index = s_model.menu_index;
    snapshot->settings_index = s_model.settings_index;
    snapshot->settings_editing_datetime = s_model.settings_editing_datetime;
    snapshot->settings_temperature_mode = s_model.settings_temperature_mode;
    snapshot->temperature_index = s_model.temperature_index;
    snapshot->settings_buzzer_mode = s_model.settings_buzzer_mode;
    snapshot->buzzer_index = s_model.buzzer_index;
    snapshot->key_beep_enabled = s_model.key_beep_enabled;
    snapshot->datetime_field_index = s_model.datetime_field_index;

    if (s_model.settings_editing_datetime) {
        snapshot->datetime = s_model.edit_datetime;
    } else {
        snapshot->datetime = s_model.datetime;
    }
    unlock_model();
}

void app_model_process_key(app_key_t key)
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
            } else if (s_model.settings_temperature_mode == APP_TEMPERATURE_MODE_MENU) {
                if (key == APP_KEY_UP) {
                    s_model.temperature_index = (s_model.temperature_index + APP_TEMPERATURE_MENU_ITEMS - 1U) % APP_TEMPERATURE_MENU_ITEMS;
                } else if (key == APP_KEY_DOWN) {
                    s_model.temperature_index = (s_model.temperature_index + 1U) % APP_TEMPERATURE_MENU_ITEMS;
                } else if (key == APP_KEY_ENTER) {
                    s_model.settings_temperature_mode = (s_model.temperature_index == 0U)
                        ? APP_TEMPERATURE_MODE_CURVES
                        : APP_TEMPERATURE_MODE_OFFSET;
                } else if (key == APP_KEY_BACK) {
                    s_model.settings_temperature_mode = APP_TEMPERATURE_MODE_NONE;
                }
            } else if (s_model.settings_temperature_mode == APP_TEMPERATURE_MODE_CURVES ||
                       s_model.settings_temperature_mode == APP_TEMPERATURE_MODE_OFFSET) {
                if (key == APP_KEY_BACK) {
                    s_model.settings_temperature_mode = APP_TEMPERATURE_MODE_MENU;
                }
            } else if (s_model.settings_buzzer_mode == APP_BUZZER_MODE_MENU) {
                if (key == APP_KEY_UP) {
                    s_model.buzzer_index = (s_model.buzzer_index + APP_BUZZER_MENU_ITEMS - 1U) % APP_BUZZER_MENU_ITEMS;
                } else if (key == APP_KEY_DOWN) {
                    s_model.buzzer_index = (s_model.buzzer_index + 1U) % APP_BUZZER_MENU_ITEMS;
                } else if (key == APP_KEY_ENTER) {
                    s_model.key_beep_enabled = (s_model.buzzer_index == 0U);
                } else if (key == APP_KEY_BACK) {
                    s_model.settings_buzzer_mode = APP_BUZZER_MODE_NONE;
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
                    // Somente item "Data" entra em modo de edicao.
                    if (s_model.settings_index == 0U) {
                        s_model.settings_editing_datetime = true;
                        s_model.datetime_field_index = 0;
                        s_model.edit_datetime = s_model.datetime;
                    } else if (s_model.settings_index == 1U) {
                        s_model.settings_temperature_mode = APP_TEMPERATURE_MODE_MENU;
                        s_model.temperature_index = 0;
                    } else if (s_model.settings_index == 2U) {
                        s_model.settings_buzzer_mode = APP_BUZZER_MODE_MENU;
                        s_model.buzzer_index = s_model.key_beep_enabled ? 0U : 1U;
                    }
                }
            }
            break;

        case APP_SCREEN_MENU_REPORTS:
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
{
    lock_model();
    s_model.key_press_pending = true;
    unlock_model();
}

bool app_model_take_key_press_flag(void)
{
    bool has_key_press = false;

    lock_model();
    if (s_model.key_press_pending) {
        has_key_press = true;
        s_model.key_press_pending = false;
    }
    unlock_model();

    return has_key_press;
}

bool app_model_is_key_beep_enabled(void)
{
    bool enabled;

    lock_model();
    enabled = s_model.key_beep_enabled;
    unlock_model();

    return enabled;
}

void app_model_check_inactivity_timeout(uint32_t now_ms)
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
