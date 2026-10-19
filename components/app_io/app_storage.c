#include "app_storage.h"

#include <stdbool.h>
#include "at25sf321b.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define APP_STORAGE_QUEUE_LENGTH       8
#define APP_STORAGE_TASK_STACK         4096
#define APP_STORAGE_TASK_PRIORITY      4
#define APP_STORAGE_QUEUE_WAIT_MS      1000

// Operacoes aceitas pela fila unica que serializa o barramento SPI.
typedef enum {
    APP_STORAGE_OP_READ,
    APP_STORAGE_OP_WRITE,
    APP_STORAGE_OP_ERASE,
} app_storage_operation_t;

// Pedido vive na pilha do chamador ate o semaforo completed ser sinalizado.
typedef struct {
    app_storage_operation_t operation; // Leitura, escrita ou erase.
    uint32_t address;                  // Endereco em bytes na AT25.
    void *buffer;                      // Buffer emprestado ate completar a operacao.
    size_t size;                       // Bytes para read/write; ignorado em erase.
    esp_err_t result;                  // Resultado produzido pelo worker.
    SemaphoreHandle_t completed;       // Sinaliza retorno ao chamador sincrono.
} app_storage_request_t;

// Fila/workers sao unicos para impedir intercalacao de comandos SPI.
static QueueHandle_t s_request_queue;
static bool s_initialized;

// Worker unico: executa cada pedido na flash e devolve o resultado ao chamador.
static void storage_task(void *argument)
{
    (void)argument;
    app_storage_request_t *request = NULL;

    for (;;) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (request->operation) {
            case APP_STORAGE_OP_READ:
                request->result = at25sf321b_read(request->address, request->buffer, request->size);
                break;
            case APP_STORAGE_OP_WRITE:
                request->result = at25sf321b_write(request->address, request->buffer, request->size);
                break;
            case APP_STORAGE_OP_ERASE:
                request->result = at25sf321b_erase_sector(request->address);
                break;
            default:
                request->result = ESP_ERR_INVALID_ARG;
                break;
        }

        xSemaphoreGive(request->completed);
    }
}

// Enfileira um pedido e bloqueia apenas o chamador ate a operacao terminar.
static esp_err_t submit_request(app_storage_request_t *request)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    request->completed = xSemaphoreCreateBinary();
    if (request->completed == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xQueueSend(s_request_queue, &request, pdMS_TO_TICKS(APP_STORAGE_QUEUE_WAIT_MS)) != pdTRUE) {
        vSemaphoreDelete(request->completed);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(request->completed, portMAX_DELAY);
    vSemaphoreDelete(request->completed);
    return request->result;
}

// Inicializa driver AT25, fila e worker; deve ocorrer antes de read/write/erase.
esp_err_t app_storage_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_request_queue = xQueueCreate(APP_STORAGE_QUEUE_LENGTH, sizeof(app_storage_request_t *));
    if (s_request_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = at25sf321b_init();
    if (err != ESP_OK) {
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return err;
    }

    if (xTaskCreate(storage_task, "storage_task", APP_STORAGE_TASK_STACK,
                    NULL, APP_STORAGE_TASK_PRIORITY, NULL) != pdPASS) {
        at25sf321b_deinit();
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
}

// Fachada sincrona de leitura com validacao de buffer e tamanho.
esp_err_t app_storage_read(uint32_t address, void *data, size_t size)
{
    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    app_storage_request_t request = {
        .operation = APP_STORAGE_OP_READ,
        .address = address,
        .buffer = data,
        .size = size,
    };
    return submit_request(&request);
}

// Fachada sincrona de escrita; buffer deve permanecer valido ate retornar.
esp_err_t app_storage_write(uint32_t address, const void *data, size_t size)
{
    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    app_storage_request_t request = {
        .operation = APP_STORAGE_OP_WRITE,
        .address = address,
        .buffer = (void *)data,
        .size = size,
    };
    return submit_request(&request);
}

// Fachada sincrona para apagar um setor alinhado de 4 KiB.
esp_err_t app_storage_erase_sector(uint32_t address)
{
    app_storage_request_t request = {
        .operation = APP_STORAGE_OP_ERASE,
        .address = address,
    };
    return submit_request(&request);
}