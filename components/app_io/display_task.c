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

    u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
    u8g2_DrawStr(u8g2, 2, 10, "TELA INICIAL");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    // snprintf(line, sizeof(line), "Data: %02d/%02d/%04d", st->datetime.day, st->datetime.month, st->datetime.year);
    // u8g2_DrawStr(u8g2, 0, 26, line);

    // snprintf(line, sizeof(line), "Hora: %02d:%02d:%02d", st->datetime.hour, st->datetime.minute, st->datetime.second);
    // u8g2_DrawStr(u8g2, 0, 38, line);

    snprintf(line, sizeof(line), "AD raw: %4d", st->adc_raw);
    u8g2_DrawStr(u8g2, 2, 50, line);

    // u8g2_DrawStr(u8g2, 0, 63, "ENTER=Menu");
}

static void draw_screen(u8g2_t *u8g2, const app_state_snapshot_t *st)
{
    // Dispatcher de tela: escolhe qual layout desenhar conforme estado atual.
    switch (st->screen) {
        case APP_SCREEN_HOME:
            draw_home(u8g2, st);
            break;
        // case APP_SCREEN_MENU:
        //     draw_menu(u8g2, st);
        //     break;
        // case APP_SCREEN_MENU_BUZZER:
        //     draw_buzzer_menu(u8g2, st);
        //     break;
        // case APP_SCREEN_MENU_LED_TIME:
        //     draw_led_time_menu(u8g2, st);
        //     break;
        // case APP_SCREEN_MENU_DATE_TIME:
        //     draw_datetime_menu(u8g2, st);
        //     break;
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