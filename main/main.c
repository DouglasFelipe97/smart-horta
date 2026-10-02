#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "app_model.h"
#include "io_task.h"
#include "display_task.h"
#include "app_pin_config.h"

// Mesma prioridade para as duas tasks, conforme solicitado.
#define APP_ADC_TASK_PRIORITY     6
#define APP_TASK_PRIORITY     5

// Stack de cada task. O display com u8g2 precisa mais espaco.
#define DISPLAY_TASK_STACK    4096
#define IO_TASK_STACK         3072
#define ADC_TASK_STACK        2048

static const char *TAG_MAIN = "APP_MAIN";

void app_main(void)
{
    // ---------------------------------------------------------------------
    // app_main e o "main" do ESP-IDF.
    // Sequencia tipica:
    //   1) Inicializar estado de software.
    //   2) Inicializar hardware (GPIO, ADC, PWM, etc).
    //   3) Criar tasks FreeRTOS.
    // Depois disso, o trabalho real fica com as tasks.
    // ---------------------------------------------------------------------
    ESP_LOGI(TAG_MAIN, "Inicializando hardware...");

    // Inicializa o estado global da aplicacao (menu, data/hora e configuracoes).
    // Essa estrutura sera lida/escrita por mais de uma task,
    // por isso ela usa mutex internamente.
    app_model_init();

    // Configura GPIOs.
    // Os pinos reais estao em app_pin_config.h.
    app_config_pins();

    // // Configura PWM para o buzzer.
    // // Sem PWM o buzzer nao teria controle de intensidade (duty).
    // esp_err_t pwm_err = app_configure_pwm();
    // if (pwm_err != ESP_OK) {
    //     ESP_LOGE(TAG_MAIN, "Falha ao configurar PWM do buzzer: %s", esp_err_to_name(pwm_err));
    // }

    ESP_LOGI(TAG_MAIN, "Criando tasks FreeRTOS...");
    // Task de I/O: teclado ADC.
    BaseType_t keyboard_created = xTaskCreate(
        adc_task,
        "adc_task",
        ADC_TASK_STACK,
        NULL,
        APP_ADC_TASK_PRIORITY,
        NULL
    );
    if (keyboard_created != pdPASS) {
        ESP_LOGE(TAG_MAIN, "Falha ao criar keyboard_task");
    }
    // Task de I/O: LEDS e buzzer.
    BaseType_t io_created = xTaskCreate(
        io_task,
        "io_task",
        IO_TASK_STACK,
        NULL,
        APP_TASK_PRIORITY,
        NULL
    );
    if (io_created != pdPASS) {
        ESP_LOGE(TAG_MAIN, "Falha ao criar io_task");
    }
    // Task do display: responsavel apenas por desenhar telas.
    BaseType_t display_created = xTaskCreate(
        display_task,
        "display_task",
        DISPLAY_TASK_STACK,
        NULL,
        APP_TASK_PRIORITY,
        NULL
    );
    if (display_created != pdPASS) {
        ESP_LOGE(TAG_MAIN, "Falha ao criar display_task");
    }
    
    ESP_LOGI(
        TAG_MAIN,
        "Sistema iniciado com duas tasks na mesma prioridade (%d).",
        APP_TASK_PRIORITY
    );

    // Importante para iniciante:
    // app_main termina aqui, mas o firmware continua vivo porque
    // as tasks criadas entram em loop infinito.
}