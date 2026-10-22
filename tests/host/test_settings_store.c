#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_model.h"

typedef int SemaphoreHandle_t;
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1U
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return 1; }
static void xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout)
{ assert(mutex == 1); (void)timeout; }
static void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex == 1); }
static uint32_t xTaskGetTickCount(void) { return 0; }

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_INVALID_CRC 3
#define AT25SF321B_CAPACITY_BYTES (4U * 1024U * 1024U)
#define AT25SF321B_SECTOR_SIZE 4096U

static uint8_t flash[AT25SF321B_CAPACITY_BYTES];
static unsigned erases, writes;
static esp_err_t read_error, erase_error, write_error;
static bool corrupt_write;
static const uint32_t settings_address = AT25SF321B_CAPACITY_BYTES - 4U * AT25SF321B_SECTOR_SIZE;

esp_err_t app_storage_read(uint32_t address, void *data, size_t size)
{
    if (read_error != ESP_OK) return read_error;
    assert(address == settings_address && size == 10U);
    memcpy(data, flash + address, size);
    return ESP_OK;
}
esp_err_t app_storage_write(uint32_t address, const void *data, size_t size)
{
    if (write_error != ESP_OK) return write_error;
    assert(address == settings_address && size == 10U);
    memcpy(flash + address, data, size);
    if (corrupt_write) flash[address + 5] ^= 1U;
    writes++;
    return ESP_OK;
}
esp_err_t app_storage_erase_sector(uint32_t address)
{
    if (erase_error != ESP_OK) return erase_error;
    assert(address == settings_address);
    memset(flash + address, 0xFF, AT25SF321B_SECTOR_SIZE);
    erases++;
    return ESP_OK;
}

esp_err_t app_settings_store_load(bool *enabled);
esp_err_t app_settings_store_save(bool enabled);

static void open_buzzer_menu(void)
{
    app_model_process_key(APP_KEY_MENU);
    app_model_process_key(APP_KEY_ENTER);
    app_model_process_key(APP_KEY_DOWN);
    app_model_process_key(APP_KEY_DOWN);
    app_model_process_key(APP_KEY_DOWN);
    app_model_process_key(APP_KEY_ENTER);
    app_model_process_key(APP_KEY_ENTER);
}

int main(void)
{
    memset(flash, 0xFF, sizeof(flash));
    memset(flash + settings_address + AT25SF321B_SECTOR_SIZE, 0xA5,
           3U * AT25SF321B_SECTOR_SIZE);
    bool enabled = true;
    assert(app_settings_store_load(NULL) == ESP_ERR_INVALID_ARG);
    assert(app_settings_store_load(&enabled) == ESP_ERR_NOT_FOUND && enabled);
    assert(app_settings_store_save(false) == ESP_OK);
    assert(app_settings_store_load(&enabled) == ESP_OK && !enabled);
    assert(app_settings_store_save(false) == ESP_OK && writes == 1 && erases == 1);
    assert(app_settings_store_save(true) == ESP_OK);
    enabled = false;
    assert(app_settings_store_load(&enabled) == ESP_OK && enabled);
    assert(writes == 2 && erases == 2);
    for (uint32_t address = settings_address + AT25SF321B_SECTOR_SIZE;
         address < AT25SF321B_CAPACITY_BYTES; address++) {
        assert(flash[address] == 0xA5);
    }
    flash[settings_address + 5] ^= 1U;
    assert(app_settings_store_load(&enabled) == ESP_ERR_INVALID_CRC && enabled);
    flash[settings_address + 4]++;
    assert(app_settings_store_load(&enabled) == ESP_ERR_NOT_FOUND);
    read_error = ESP_FAIL;
    assert(app_settings_store_load(&enabled) == ESP_FAIL);
    assert(app_settings_store_save(false) == ESP_FAIL);
    read_error = ESP_OK;
    erase_error = ESP_FAIL;
    assert(app_settings_store_save(false) == ESP_FAIL);
    erase_error = ESP_OK;
    write_error = ESP_FAIL;
    assert(app_settings_store_save(false) == ESP_FAIL);
    write_error = ESP_OK;
    corrupt_write = true;
    assert(app_settings_store_save(false) == ESP_ERR_INVALID_CRC);
    corrupt_write = false;
    assert(app_settings_store_save(false) == ESP_OK);
    enabled = true;
    assert(app_settings_store_load(&enabled) == ESP_OK && !enabled);
    app_model_init();
    assert(app_model_is_key_beep_enabled());
    assert(!app_model_take_key_beep_write_request(&enabled));
    assert(app_settings_store_load(&enabled) == ESP_OK && !enabled);
    app_model_set_key_beep_enabled(enabled);
    assert(!app_model_is_key_beep_enabled());
    assert(!app_model_take_key_beep_write_request(&enabled));
    open_buzzer_menu();
    app_model_process_key(APP_KEY_ENTER);
    assert(!app_model_take_key_beep_write_request(&enabled));
    app_model_process_key(APP_KEY_UP);
    assert(!app_model_take_key_beep_write_request(&enabled));
    app_model_process_key(APP_KEY_ENTER);
    assert(app_model_take_key_beep_write_request(&enabled) && enabled);
    assert(!app_model_take_key_beep_write_request(&enabled));
    assert(app_settings_store_save(enabled) == ESP_OK);
    app_model_init();
    assert(app_settings_store_load(&enabled) == ESP_OK && enabled);
    app_model_set_key_beep_enabled(enabled);
    assert(app_model_is_key_beep_enabled());
    open_buzzer_menu();
    app_model_process_key(APP_KEY_DOWN);
    app_model_process_key(APP_KEY_ENTER);
    assert(app_model_take_key_beep_write_request(&enabled) && !enabled);
    app_model_request_key_beep_write();
    assert(!app_model_take_key_beep_write_request(NULL));
    assert(app_model_take_key_beep_write_request(&enabled) && !enabled);
    assert(app_settings_store_save(enabled) == ESP_OK);
    app_model_init();
    assert(app_settings_store_load(&enabled) == ESP_OK && !enabled);
    app_model_set_key_beep_enabled(enabled);
    assert(!app_model_is_key_beep_enabled());
    assert(!app_model_take_key_beep_write_request(&enabled));
    puts("PASS: menu ON/OFF, reboot restore, pending/retry, CRC, unchanged writes, sector isolation and storage failures");
    return 0;
}