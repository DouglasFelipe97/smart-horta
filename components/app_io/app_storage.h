#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t app_storage_init(void);

// Operacoes sincronas para o chamador; a task de armazenamento executa o acesso SPI.
esp_err_t app_storage_read(uint32_t address, void *data, size_t size);
esp_err_t app_storage_write(uint32_t address, const void *data, size_t size);
esp_err_t app_storage_erase_sector(uint32_t address);

#endif