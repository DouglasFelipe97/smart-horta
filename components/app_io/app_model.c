#include "app_model.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Estrutura interna protegida por mutex.
// Estrutura de dados que serão utilizadas para a comunicação entre as tasks.
typedef struct {
    // uint8_t menu_index;
    // app_edit_field_t edit_field;
    // app_datetime_t datetime;
    app_screen_t screen;
    int adc_raw;
    // bool buzzer_enabled;
    // uint32_t led_period_ms;
} app_model_data_t;

static app_model_data_t s_model;            //Criando uma estrutura nova
static SemaphoreHandle_t s_model_mutex;     //Criando semaforo para usar o mutex

static void lock_model(void){       //com o semaforo/mutex bloquio a estrutura de dados utilizados para a comunicação entre as tasks, protegendo os dados enquanto uma tarefa usa
    // Bloqueia ate o mutex ficar disponivel.
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
}

static void unlock_model(void){
    // Libera mutex para outra task acessar o estado.
    xSemaphoreGive(s_model_mutex);
}

void app_model_init(void)
{
    // Limpa toda a estrutura com zero.
    memset(&s_model, 0, sizeof(s_model));

    s_model.screen = APP_SCREEN_HOME;
    s_model.adc_raw = 0;

    // Cria mutex apos preparar estado inicial.
    s_model_mutex = xSemaphoreCreateMutex();
}

void app_model_set_adc_raw(int adc_raw){
    lock_model();
    s_model.adc_raw = adc_raw;
    unlock_model();
}

int app_model_get_adc_raw(void){
    lock_model();
    int value = s_model.adc_raw;
    unlock_model();
    return value;
}

void app_model_get_snapshot(app_state_snapshot_t *snapshot){
    // Snapshot evita que a task de display leia campos "quebrados"
    // no meio de uma atualizacao concorrente.
    if (snapshot == NULL) {
        return;
    }

    lock_model();
    snapshot->screen = s_model.screen;
    snapshot->adc_raw = s_model.adc_raw;
    unlock_model();
}