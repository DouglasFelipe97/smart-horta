#include "io_task.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "app_pin_config.h"
#include "app_model.h"
#include "app_lot_store.h"
// #include "app_menu_functions"
#include "keyboard.h"
#include "probe_install.h"

static const char *TAG_IO = "IO_TASK";

void io_task(void *pvParameters){
    bool led_state = false;

    // Marcadores de tempo (em ms) para tarefas periodicas.
    uint32_t last_led_toggle_ms = 0;
    uint32_t last_buzzer_toggle_ms = 0;

    // Inicializa UART e estado do instalador de sondas.
    probe_install_manager_init();

    while (1) {
        // Tempo atual em milissegundos baseado no tick do FreeRTOS.
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        /*Toggle led*/
        if (now_ms - last_led_toggle_ms >= /*led_period_ms*/ 250) {     /*Se o periodo desde a ultima mdança de estado for maior que o valor de periodo do led */
            last_led_toggle_ms = now_ms;        //salva o tempo da ultima mudança de estado
            led_state = !led_state;
            // LED1 e LED2 ficam em fases opostas.
            gpio_set_level(APP_LED_1_PIN, led_state ? 1 : 0);
            gpio_set_level(APP_LED_2_PIN, led_state ? 0 : 1);
        }
        // Beep unico por tecla estavel:
        // - adc_task marca um flag quando uma tecla valida e reconhecida.
        // - aqui consumimos o flag apenas uma vez e, se habilitado no menu,
        //   geramos um pulso curto no buzzer.
        if (app_model_take_key_press_flag()) {
            if (app_model_is_key_beep_enabled() && (now_ms - last_buzzer_toggle_ms >= 100U)) {
                last_buzzer_toggle_ms = now_ms;

                gpio_set_level(APP_BUZZER_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(20));
                gpio_set_level(APP_BUZZER_PIN, 0);
            }
        }

        // Orquestracao central das rotinas de sondas:
        // 1) Cada manager e habilitado apenas quando sua tela esta ativa.
        // 2) As rotinas sao chamadas em modo tick (nao bloqueante), mantendo
        //    responsividade do teclado e refresh da UI.
        // 3) Uma rotina inativa nao transmite nem consome barramento UART.
        //
        // Como o loop roda a cada ~50ms, este e o "relogio" base das FSMs.
        // Cada FSM aplica seus proprios timeouts internos (100ms, 500ms, 1200ms etc.).
        probe_install_manager_set_active(app_model_is_probe_install_screen_active());
        probe_uninstall_manager_set_active(app_model_is_probe_uninstall_screen_active());
        probe_monitor_manager_set_active(app_model_is_probe_list_screen_active());
        probe_install_manager_periodic(now_ms);
        run_uninstall_sequence(now_ms);
        probe_monitor_manager_periodic(now_ms);

        app_datetime_t lot_start_datetime;
        bool lot_started;
        if (app_model_take_lot_write_request(&lot_start_datetime, &lot_started)) {
            esp_err_t err = lot_started
                ? app_lot_store_save(&lot_start_datetime)
                : app_lot_store_clear();
            if (err != ESP_OK) {
                ESP_LOGE(TAG_IO, "Falha ao persistir estado do lote: %s", esp_err_to_name(err));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

void adc_task(void *pvParameters){
    keyboard_adc_init();        //inicia o teclado ADC
    while (1) {
        //    - Leitura do teclado analogico.
        //    - Guarda o valor bruto para visualizacao no display (mapeamento).
        //    - Dispara evento de tecla para a maquina de estados.
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        int adc_raw = 0;
        app_key_t key = keyboard_adc_poll_event(&adc_raw);
        // Envia valor bruto para mapeamento
        app_model_set_adc_raw(adc_raw);

        if (key != APP_KEY_NONE) {
            app_model_set_key_press_flag();
        }

        // Entrega a tecla para a maquina de estados do menu.
        app_model_process_key(key);

        // Retorna para HOME se ficar sem interacao por mais de 30 segundos.
        app_model_check_inactivity_timeout(now_ms);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}