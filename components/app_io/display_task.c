#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "u8g2.h"
#include "app_pin_config.h"
#include "app_model.h"
#include "display_task.h"

static const char *TAG_LCD = "DISPLAY_TASK";

// Limiares locais de alarme usados no resumo da HOME.
// Podem migrar para configuracao de usuario em revisao futura.
#define ALARM_AIR_TEMP_MIN_C    5.0f
#define ALARM_AIR_TEMP_MAX_C   35.0f
#define ALARM_AIR_HUM_MIN_PCT  20.0f
#define ALARM_AIR_HUM_MAX_PCT  90.0f
#define ALARM_SOIL_TEMP_MIN_C   0.0f
#define ALARM_SOIL_TEMP_MAX_C  45.0f
#define ALARM_SOIL_HUM_MIN_PCT 15.0f
#define ALARM_SOIL_HUM_MAX_PCT 95.0f

// -----------------------------------------------------------------------------
// Funcoes draw_*:
// Cada funcao desenha uma tela especifica da interface.
// Isso facilita manutencao: alterar layout de uma tela nao afeta as outras.
// -----------------------------------------------------------------------------

static void draw_home(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    char line[32];
    uint8_t samples = 0;
    uint8_t alarms = 0;
    float sum_air_t = 0.0f;
    float sum_air_u = 0.0f;
    float sum_soil_t = 0.0f;
    float sum_soil_u = 0.0f;

    // HOME considera apenas sondas instaladas com leitura valida
    // para compor medias e contagem de alarmes ativos.
    for (int i = 0; i < APP_PROBE_COUNT; i++) {
        if (!st->probes[i].installed || !st->probes[i].has_reading) {
            continue;
        }

        sum_air_t += st->probes[i].air_temperature;
        sum_air_u += st->probes[i].air_humidity;
        sum_soil_t += st->probes[i].soil_temperature;
        sum_soil_u += st->probes[i].soil_humidity;
        samples++;

        bool probe_alarm =
            (st->probes[i].air_temperature < ALARM_AIR_TEMP_MIN_C) ||
            (st->probes[i].air_temperature > ALARM_AIR_TEMP_MAX_C) ||
            (st->probes[i].air_humidity < ALARM_AIR_HUM_MIN_PCT) ||
            (st->probes[i].air_humidity > ALARM_AIR_HUM_MAX_PCT) ||
            (st->probes[i].soil_temperature < ALARM_SOIL_TEMP_MIN_C) ||
            (st->probes[i].soil_temperature > ALARM_SOIL_TEMP_MAX_C) ||
            (st->probes[i].soil_humidity < ALARM_SOIL_HUM_MIN_PCT) ||
            (st->probes[i].soil_humidity > ALARM_SOIL_HUM_MAX_PCT);

        if (probe_alarm) {
            alarms++;
        }
    }

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);

    if (alarms > 0U) {
        snprintf(line, sizeof(line), "ALARMES ATIVOS: %u", alarms);
    } else {
        snprintf(line, sizeof(line), "Alarmes ativos: 0");
    }
    u8g2_DrawStr(u8g2, 2, 10, line);

    snprintf(line, sizeof(line), "%02u/%02u/%04u %02u:%02u",
             st->datetime.day,
             st->datetime.month,
             st->datetime.year,
             st->datetime.hour,
             st->datetime.minute);
    u8g2_DrawStr(u8g2, 2, 22, line);

    if (samples > 0U) {
        snprintf(line, sizeof(line), "Ar  T:%4.1f U:%4.1f",
                 sum_air_t / (float)samples,
                 sum_air_u / (float)samples);
        u8g2_DrawStr(u8g2, 2, 36, line);

        snprintf(line, sizeof(line), "Solo T:%4.1f U:%4.1f",
                 sum_soil_t / (float)samples,
                 sum_soil_u / (float)samples);
        u8g2_DrawStr(u8g2, 2, 50, line);
    } else {
        u8g2_DrawStr(u8g2, 2, 36, "Ar  T:--.- U:--.-");
        u8g2_DrawStr(u8g2, 2, 50, "Solo T:--.- U:--.-");
    }
    if (st->lot_status == APP_LOT_STATUS_ACTIVE) {
        snprintf(line, sizeof(line), "Lote: ATIVO %lu d", (unsigned long)st->lot_age_days);
    } else if (st->lot_status == APP_LOT_STATUS_SCHEDULED) {
        snprintf(line, sizeof(line), "Lote: AGENDADO");
    } else {
        snprintf(line, sizeof(line), "Lote: INATIVO");
    }
    u8g2_DrawStr(u8g2, 2, 62, line);
}

static void draw_main_menu(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    static const char *items[] = {"Ajustes", "Relatorios", "Sobre"};
    char line[24];

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(u8g2, 2, 10, "MENU");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    for (int i = 0; i < 3; i++) {
        snprintf(line, sizeof(line), "%c%s", (st->menu_index == (uint8_t)i) ? '>' : ' ', items[i]);
        u8g2_DrawStr(u8g2, 2, 23 + (i * 12), line);
    }
}

static void draw_placeholder_screen(u8g2_t *u8g2, const char *title)
{
    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(u8g2, 2, 10, title);
    u8g2_DrawHLine(u8g2, 0, 12, 128);
    u8g2_DrawStr(u8g2, 2, 34, "Em desenvolvimento");
    u8g2_DrawStr(u8g2, 2, 50, "BACK = voltar");
}

static void format_probe_value(char *out, size_t out_len, bool has_value, float value)
{
    if (!has_value) {
        snprintf(out, out_len, "--");
        return;
    }

    snprintf(out, out_len, "%2.0f", value);
}

static void draw_install_progress_indicator(u8g2_t *u8g2)
{
    static uint8_t phase = 0;
    const uint8_t segments = 12;
    const uint8_t active_window = 3;
    const uint8_t bar_x = 2;
    const uint8_t bar_y = 60;
    const uint8_t bar_w = 124;
    const uint8_t bar_h = 3;

    // Barra ciclica: janela de segmentos "acesos" caminhando no tempo.
    u8g2_DrawFrame(u8g2, bar_x, bar_y, bar_w, bar_h);
    for (uint8_t i = 0; i < segments; i++) {
        uint8_t wrapped = (uint8_t)((i + segments - phase) % segments);
        if (wrapped < active_window) {
            uint8_t seg_x = (uint8_t)(bar_x + 1U + (i * 10U));
            u8g2_DrawBox(u8g2, seg_x, (uint8_t)(bar_y + 1U), 8, 1);
        }
    }

    phase = (uint8_t)((phase + 1U) % segments);
}

static void draw_probe_rows(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    char line[32];
    char a_t[4];
    char a_u[4];
    char s_t[4];
    char s_u[4];

    // Abreviacoes para caber com fonte padrao: A/U (ar), T/S (solo).
    // Exemplo: S1 A23 U55 T21 S40.
    for (int i = 0; i < APP_PROBE_COUNT; i++) {
        bool has_data = st->probes[i].installed && st->probes[i].has_reading;
        format_probe_value(a_t, sizeof(a_t), has_data, st->probes[i].air_temperature);
        format_probe_value(a_u, sizeof(a_u), has_data, st->probes[i].air_humidity);
        format_probe_value(s_t, sizeof(s_t), has_data, st->probes[i].soil_temperature);
        format_probe_value(s_u, sizeof(s_u), has_data, st->probes[i].soil_humidity);

        snprintf(line,
                 sizeof(line),
                 "S%u A%s U%s T%s S%s",
                 (unsigned)st->probes[i].probe_id,
                 a_t,
                 a_u,
                 s_t,
                 s_u);
        u8g2_DrawStr(u8g2, 2, 24 + (i * 12), line);
    }
}

static void draw_settings_menu(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    static const char *items[] = {"Lote", "Data", "Temperaturas", "Buzzer", "Umidade", "Instalar Sondas"};
    static const char *temp_items[] = {"Curvas", "Offset"};
    static const char *buzzer_items[] = {"Habilitar", "Desabilitar"};
    static const char *probe_items[] = {"Instalar", "Desinstalar", "Sondas"};
    char line[64];

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(u8g2, 2, 10, st->settings_lot_mode == APP_LOT_MODE_NONE ? "AJUSTES" : "LOTE");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    if (st->settings_editing_datetime) {
        const char *field_name = "Campo";
        switch (st->datetime_field_index) {
            case 0:
                field_name = "Dia";
                break;
            case 1:
                field_name = "Mes";
                break;
            case 2:
                field_name = "Ano";
                break;
            case 3:
                field_name = "Hora";
                break;
            case 4:
                field_name = "Min";
                break;
            case 5:
                field_name = "Seg";
                break;
            default:
                break;
        }
        u8g2_DrawStr(u8g2, 2, 22, ">Data");
        snprintf(line, sizeof(line), "%s", field_name);
        u8g2_DrawStr(u8g2, 2, 34, line);
        snprintf(line, sizeof(line), "%02u/%02u/%04u", st->datetime.day, st->datetime.month, st->datetime.year);
        u8g2_DrawStr(u8g2, 2, 46, line);
        snprintf(line, sizeof(line), "%02u:%02u:%02u", st->datetime.hour, st->datetime.minute, st->datetime.second);
        u8g2_DrawStr(u8g2, 2, 58, line);
    } else if (st->settings_lot_mode == APP_LOT_MODE_MENU) {
        u8g2_DrawStr(u8g2, 2, 24, ">Iniciar Lote");
        if (st->lot_started) {
            const char *status = st->lot_status == APP_LOT_STATUS_SCHEDULED ? "Agendado" : "Ja iniciado";
            snprintf(line, sizeof(line), "Atual: %s", status);
            u8g2_DrawStr(u8g2, 2, 40, line);
        }
    } else if (st->settings_lot_mode == APP_LOT_MODE_EDIT) {
        static const char *lot_fields[] = {"Dia", "Mes", "Ano", "Hora", "Min"};
        snprintf(line, sizeof(line), "Inicio: %s", lot_fields[st->datetime_field_index]);
        u8g2_DrawStr(u8g2, 2, 24, line);
        snprintf(line, sizeof(line), "%02u/%02u/%04u",
                 st->datetime.day, st->datetime.month, st->datetime.year);
        u8g2_DrawStr(u8g2, 2, 40, line);
        snprintf(line, sizeof(line), "%02u:%02u", st->datetime.hour, st->datetime.minute);
        u8g2_DrawStr(u8g2, 2, 54, line);
    } else if (st->settings_lot_mode == APP_LOT_MODE_CONFIRMED) {
        u8g2_DrawStr(u8g2, 2, 24,
                     st->lot_status == APP_LOT_STATUS_SCHEDULED ? "LOTE AGENDADO" : "LOTE INICIADO");
        snprintf(line, sizeof(line), "%02u/%02u/%04u",
                 st->lot_start_datetime.day,
                 st->lot_start_datetime.month,
                 st->lot_start_datetime.year);
        u8g2_DrawStr(u8g2, 2, 40, line);
        snprintf(line, sizeof(line), "%02u:%02u",
                 st->lot_start_datetime.hour, st->lot_start_datetime.minute);
        u8g2_DrawStr(u8g2, 2, 52, line);
        u8g2_DrawStr(u8g2, 2, 62, "OK/BACK: voltar");
    } else if (st->settings_temperature_mode == 1) {
        u8g2_DrawStr(u8g2, 2, 22, "> Temperaturas");
        for (int i = 0; i < 2; i++) {
            snprintf(line, sizeof(line), "%c%s", (st->temperature_index == (uint8_t)i) ? '>' : ' ', temp_items[i]);
            u8g2_DrawStr(u8g2, 2, 36 + (i * 12), line);
        }
    } else if (st->settings_temperature_mode == 2) {
        u8g2_DrawStr(u8g2, 2, 22, "> Temperaturas");
        u8g2_DrawStr(u8g2, 2, 34, "Curvas");
        u8g2_DrawStr(u8g2, 2, 46, "Em desenvolvimento");
        u8g2_DrawStr(u8g2, 2, 58, "BACK = voltar");
    } else if (st->settings_temperature_mode == 3) {
        u8g2_DrawStr(u8g2, 2, 22, "> Temperaturas");
        u8g2_DrawStr(u8g2, 2, 34, "Offset");
        u8g2_DrawStr(u8g2, 2, 46, "Em desenvolvimento");
        u8g2_DrawStr(u8g2, 2, 58, "BACK = voltar");
    } else if (st->settings_buzzer_mode == 1) {
        u8g2_DrawStr(u8g2, 2, 22, "> Buzzer");
        for (int i = 0; i < 2; i++) {
            snprintf(line, sizeof(line), "%c%s", (st->buzzer_index == (uint8_t)i) ? '>' : ' ', buzzer_items[i]);
            u8g2_DrawStr(u8g2, 2, 36 + (i * 12), line);
        }
        snprintf(line, sizeof(line), "Atual: %s", st->key_beep_enabled ? "ON" : "OFF");
        u8g2_DrawStr(u8g2, 2, 60, line);
    } else if (st->settings_probe_mode == 1) {
        // Submenu de gerenciamento de sondas dentro de Ajustes.
        u8g2_DrawStr(u8g2, 2, 22, "> Instalar Sondas");
        for (int i = 0; i < 3; i++) {
            snprintf(line, sizeof(line), "%c%s", (st->probe_menu_index == (uint8_t)i) ? '>' : ' ', probe_items[i]);
            u8g2_DrawStr(u8g2, 2, 36 + (i * 12), line);
        }
    } else if (st->settings_probe_mode == 2) {
        u8g2_DrawStr(u8g2, 2, 10, "INSTALAR SONDAS");
        u8g2_DrawHLine(u8g2, 0, 12, 128);

        draw_probe_rows(u8g2, st);
        if (st->probe_install_running) {
            // Durante instalacao ativa, mostra progresso visual ciclico.
            draw_install_progress_indicator(u8g2);
        } else if (st->probe_install_finished) {
            // Ao finalizar 4/4, barra some e entra status final.
            snprintf(line, sizeof(line), "Sondas instaladas: %u/4", st->probe_install_completed_count);
            u8g2_DrawStr(u8g2, 2, 60, line);
        } else {
            u8g2_DrawStr(u8g2, 2, 60, "Aguardando inicio");
        }
    } else if (st->settings_probe_mode == 3) {
        // Tela interna de desinstalacao com status fixo e progresso de ciclos.
        u8g2_DrawStr(u8g2, 2, 10, "DESINSTALAR");
        u8g2_DrawHLine(u8g2, 0, 12, 128);
        if (st->probe_uninstall_finished) {
            u8g2_DrawStr(u8g2, 2, 36, "Concluido");
        } else {
            uint8_t cycle = st->probe_uninstall_cycle;
            if (cycle < 1U) {
                cycle = 1U;
            } else if (cycle > 3U) {
                cycle = 3U;
            }
            snprintf(line, sizeof(line), "Ciclo %u/3", cycle);
            u8g2_DrawStr(u8g2, 2, 36, line);
        }
        u8g2_DrawStr(u8g2, 2, 60, "Desinstalando...");
    } else if (st->settings_probe_mode == 4) {
        u8g2_DrawStr(u8g2, 2, 10, "SONDAS");
        u8g2_DrawHLine(u8g2, 0, 12, 128);
        draw_probe_rows(u8g2, st);
    } else {
        uint8_t first_visible = st->settings_index > 3U
            ? (uint8_t)(st->settings_index - 3U)
            : 0U;
        for (uint8_t row = 0; row < 4U && first_visible + row < 6U; row++) {
            uint8_t item_index = (uint8_t)(first_visible + row);
            snprintf(line, sizeof(line), "%c%s",
                     st->settings_index == item_index ? '>' : ' ', items[item_index]);
            u8g2_DrawStr(u8g2, 2, 23 + (row * 12), line);
        }
    }
}

static void draw_about_menu(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    (void)st;

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(u8g2, 2, 10, "SOBRE");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    u8g2_DrawStr(u8g2, 2, 30, "SMART-Horta");
    u8g2_DrawStr(u8g2, 2, 46, "Versao: V0.0.1");
    u8g2_DrawStr(u8g2, 2, 60, "BACK = voltar");
}

static void draw_screen(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    // Dispatcher de tela: escolhe qual layout desenhar conforme estado atual.
    switch (st->screen) {
        case APP_SCREEN_HOME:
            draw_home(u8g2, st);
            break;
        case APP_SCREEN_HOME_PROBES:
            // Tela espelho de sondas acessada pela tecla esquerda na HOME.
            u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
            u8g2_DrawStr(u8g2, 2, 10, "SONDAS INSTALADAS");
            u8g2_DrawHLine(u8g2, 0, 12, 128);
            draw_probe_rows(u8g2, st);
            break;
        case APP_SCREEN_MENU:
            draw_main_menu(u8g2, st);
            break;
        case APP_SCREEN_MENU_SETTINGS:
            draw_settings_menu(u8g2, st);
            break;
        case APP_SCREEN_MENU_REPORTS:
            draw_placeholder_screen(u8g2, "RELATORIOS");
            break;
        case APP_SCREEN_MENU_ABOUT:
            draw_about_menu(u8g2, st);
            break;
        default:
            draw_home(u8g2, st);
            break;
    }
}

// Esta funcao e um callback exigido pelo u8g2 para abstrair GPIO e delays.
// Em resumo: o u8g2 manda uma "mensagem" (msg) e aqui traduzimos para ESP32.
uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    // Parametros nao usados diretamente neste callback.
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            // O u8g2 chama este evento durante a inicializacao.
            // Centralizamos o mapeamento de pinos no arquivo de configuracao.
            app_configure_lcd_pins();

            // Sequencia correta de reset para ST7920 (SPI em software).
            // O display normalmente precisa ser colocado em LOW antes de iniciar,
            // depois de um pequeno delay e em HIGH para operar normalmente.
            gpio_set_level(APP_LCD_PIN_CS, 0);
            gpio_set_level(APP_LCD_PIN_RST, 0);
            esp_rom_delay_us(10000);
            gpio_set_level(APP_LCD_PIN_RST, 1);
            esp_rom_delay_us(10000);
            gpio_set_level(APP_LCD_PIN_CS, 1);
            break;

        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;

        case U8X8_MSG_DELAY_10MICRO:
            esp_rom_delay_us(10);
            break;

        case U8X8_MSG_DELAY_100NANO:
            esp_rom_delay_us(1);
            break;

        case U8X8_MSG_GPIO_CS:
            // ST7920 Chip Select ativo em nivel ALTO (1 = selecionado)
            gpio_set_level(APP_LCD_PIN_CS, arg_int ? 1 : 0);
            break;

        case U8X8_MSG_GPIO_RESET:
            gpio_set_level(APP_LCD_PIN_RST, arg_int);
            break;

        case U8X8_MSG_GPIO_SPI_CLOCK:
            gpio_set_level(APP_LCD_PIN_CLK, arg_int);
            break;

        case U8X8_MSG_GPIO_SPI_DATA:
            gpio_set_level(APP_LCD_PIN_DATA, arg_int);
            break;

        default:
            return 0;
    }
    return 1;
}

void display_task(void *pvParameters)
{
    (void)pvParameters;

    // Esta task fica em loop desenhando os dados no display.
    // Nao acessa LED e buzzer para manter responsabilidades separadas.
    ESP_LOGI(TAG_LCD, "Inicializando ST7920...");

    u8g2_t u8g2;

    // Inicializacao oficial para ST7920 128x64 em modo Serial Software SPI
    // O protocolo SPI aqui e "bit-bang", feito por software via callback.
    u8g2_Setup_st7920_s_128x64_f(
        &u8g2,
        U8G2_R0,
        u8x8_byte_4wire_sw_spi,
        u8x8_gpio_and_delay_esp32
    );

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    // Snapshot local para receber copia consistente do estado global.
    app_state_snapshot_t snapshot;

    while (1) {
        // Lemos um snapshot do modelo para desenhar uma tela consistente.
        app_model_get_snapshot(&snapshot);

        u8g2_ClearBuffer(&u8g2);

        // Desenha a tela correspondente ao estado atual da FSM.
        draw_screen(&u8g2, &snapshot);

        // Moldura para facilitar visualizacao durante depuracao.
        u8g2_DrawFrame(&u8g2, 0, 0, 128, 64);

        u8g2_SendBuffer(&u8g2);

        // Refresh mais rapido para UI ficar responsiva ao teclado.
        vTaskDelay(pdMS_TO_TICKS(90));
    }
}