#ifndef AT25SF321B_H
#define AT25SF321B_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AT25SF321B_CAPACITY_BYTES  (4U * 1024U * 1024U)
#define AT25SF321B_PAGE_SIZE       256U
#define AT25SF321B_SECTOR_SIZE     4096U

// Driver SPI de baixo nivel para AT25SF321B; enderecos sao bytes desde zero.
esp_err_t at25sf321b_init(void);
void at25sf321b_deinit(void);
// read/write aceitam intervalos dentro da capacidade; write divide por paginas.
esp_err_t at25sf321b_read(uint32_t address, void *data, size_t size);
esp_err_t at25sf321b_write(uint32_t address, const void *data, size_t size);
// erase requer endereco alinhado a setor de 4 KiB.
esp_err_t at25sf321b_erase_sector(uint32_t address);

#endif