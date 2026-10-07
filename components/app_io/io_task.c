#include "io_task.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "app_pin_config.h"
#include "app_model.h"
// #include "app_menu_functions"
#include "keyboard.h"

static const char *TAG_IO = "IO_TASK";

void io_task(void *pvParameters){
    bool led_state = false;
    bool buzzer_state = false;

    // Marcadores de tempo (em ms) para tarefas periodicas.
    uint32_t last_led_toggle_ms = 0;
    uint32_t last_buzzer_toggle_ms = 0;
    uint32_t last_log_ms = 0;
    uint32_t last_second_ms = 0;

    while (1) {
        // Tempo atual em milissegundos baseado no tick do FreeRTOS.
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        /*Toggle led*/
        if (now_ms - last_led_toggle_ms >= /*led_period_ms*/ 250) {     /*Se o periodo desde a ultima mdança de estado for maior que o valor de periodo do led */
            last_led_toggle_ms = now_ms;
            led_state = !led_state;
            // LED1 e LED2 ficam em fases opostas.
            gpio_set_level(APP_LED_1_PIN, led_state ? 1 : 0);
            gpio_set_level(APP_LED_2_PIN, led_state ? 0 : 1);
        }
        /*Button buzzer*/
        // if(keyboard_adc_poll_event(NULL))     /*Se uma tecla for pressionada //keyboard_adc_poll_event(NULL) GERA CONCORRENCIA*/
        //     /*FAZER FUNÇÃO*/
        //     if(/*app_buzzer_function() &&*/ now_ms - last_buzzer_toggle_ms >= 100){     /*Se o buzzer teclas estiver hab e se o periodo desde a ultima mudança de estado for maior que o valor de periodo do buzzer */
        //         last_buzzer_toggle_ms = now_ms;
        //         buzzer_state = !buzzer_state;
        //         gpio_set_level(APP_BUZZER_PIN, buzzer_state ? 1 : 0);
        //     }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

void adc_task(void *pvParameters){
    keyboard_adc_init();
    while (1) {
        //    - Leitura do teclado analogico.
        //    - Guarda o valor bruto para visualizacao no display (mapeamento).
        //    - Dispara evento de tecla para a maquina de estados.
        int adc_raw = 0;
        app_key_t key = keyboard_adc_poll_event(&adc_raw);
        // Envia valor bruto para mapeamento
        app_model_set_adc_raw(adc_raw);
        // Entrega a tecla para a maquina de estados do menu.
        app_model_process_key(key);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}