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

// -----------------------------------------------------------------------------
// Funcoes draw_*:
// Cada funcao desenha uma tela especifica da interface.
// Isso facilita manutencao: alterar layout de uma tela nao afeta as outras.
// -----------------------------------------------------------------------------

static void draw_home(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    char line[32];
    static const char *days_of_week[] = {"none", "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};   

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);

    snprintf(line, sizeof(line), "%s %02u/%02u/%04u", days_of_week[st->datetime.weekday], st->datetime.day, st->datetime.month, st->datetime.year);
    u8g2_DrawStr(u8g2, 3, 10, line);

    snprintf(line, sizeof(line), "%02u:%02u", st->datetime.hour, st->datetime.minute);
    u8g2_DrawStr(u8g2, 92, 10, line);

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
        snprintf(out, out_len, "--.-");
        return;
    }

    snprintf(out, out_len, "%4.1f", value);
}

static void draw_settings_menu(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    static const char *items[] = {"Data", "Temperaturas", "Buzzer", "Umidade", "Instalar Sondas"};
    static const char *temp_items[] = {"Curvas", "Offset"};
    static const char *buzzer_items[] = {"Habilitar", "Desabilitar"};
    static const char *probe_items[] = {"Instalar", "Desinstalar", "Sondas"};
    char line[64];

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(u8g2, 2, 10, "AJUSTES");
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
        // Tela interna de instalacao com telemetria formatada por sonda.
        char t_str[8];
        char u_str[8];
        char s_str[8];

        u8g2_DrawStr(u8g2, 2, 10, "INSTALAR");
        u8g2_DrawHLine(u8g2, 0, 12, 128);

        // Fonte menor para caber T/U/S das 4 sondas em linhas separadas.
        u8g2_SetFont(u8g2, u8g2_font_4x6_tr);
        for (int i = 0; i < APP_PROBE_COUNT; i++) {
            format_probe_value(t_str, sizeof(t_str), st->probes[i].has_reading, st->probes[i].temperature);
            format_probe_value(u_str, sizeof(u_str), st->probes[i].has_reading, st->probes[i].air_humidity);
            format_probe_value(s_str, sizeof(s_str), st->probes[i].has_reading, st->probes[i].soil_humidity);
            snprintf(line, sizeof(line), "S%d:T:%s U:%s S:%s", i + 1, t_str, u_str, s_str);
            u8g2_DrawStr(u8g2, 2, 22 + (i * 10), line);
        }

        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        if (st->probe_install_running) {
            u8g2_DrawStr(u8g2, 2, 60, "Instalando");
        } else if (st->probe_install_finished) {
            u8g2_DrawStr(u8g2, 2, 60, "Instalacao OK");
        } else {
            u8g2_DrawStr(u8g2, 2, 60, "Aguardando");
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
        // Tela "Sondas": leitura ciclica em tempo real por sensor.
        char t_str[8];
        char u_str[8];
        char s_str[8];

        u8g2_DrawStr(u8g2, 2, 10, "SONDAS");
        u8g2_DrawHLine(u8g2, 0, 12, 128);

        u8g2_SetFont(u8g2, u8g2_font_4x6_tr);
        for (int i = 0; i < APP_PROBE_COUNT; i++) {
            if (st->probes[i].installed && st->probes[i].has_reading) {
                snprintf(t_str, sizeof(t_str), "%4.1f", st->probes[i].temperature);
                snprintf(u_str, sizeof(u_str), "%4.1f", st->probes[i].air_humidity);
                snprintf(s_str, sizeof(s_str), "%4.1f", st->probes[i].soil_humidity);
            } else {
                snprintf(t_str, sizeof(t_str), "__._");
                snprintf(u_str, sizeof(u_str), "__._");
                snprintf(s_str, sizeof(s_str), "__._");
            }

            snprintf(line, sizeof(line), "Sensor %d: T:%s U:%s S:%s", i + 1, t_str, u_str, s_str);
            u8g2_DrawStr(u8g2, 2, 22 + (i * 10), line);
        }

        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    } else {
        for (int i = 0; i < 5; i++) {
            if(st->settings_index < 4){
                snprintf(line, sizeof(line), "%c%s", (st->settings_index == (uint8_t)i) ? '>' : ' ', items[i]);
                u8g2_DrawStr(u8g2, 2, 23 + (i * 12), line);
            }
            else if(st->settings_index >= 4 && i >= 4){
                snprintf(line, sizeof(line), "%c%s", (st->settings_index == (uint8_t)i) ? '>' : ' ', items[i]);
                u8g2_DrawStr(u8g2, 2, 23 + ((i-4) * 12), line);
            }
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