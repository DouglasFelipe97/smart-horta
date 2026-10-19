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

typedef enum {
    APP_STORAGE_OP_READ,
    APP_STORAGE_OP_WRITE,
    APP_STORAGE_OP_ERASE,
} app_storage_operation_t;

typedef struct {
    app_storage_operation_t operation;
    uint32_t address;
    void *buffer;
    size_t size;
    esp_err_t result;
    SemaphoreHandle_t completed;
} app_storage_request_t;

static QueueHandle_t s_request_queue;
static bool s_initialized;

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

esp_err_t app_storage_erase_sector(uint32_t address)
{
    app_storage_request_t request = {
        .operation = APP_STORAGE_OP_ERASE,
        .address = address,
    };
    return submit_request(&request);
}