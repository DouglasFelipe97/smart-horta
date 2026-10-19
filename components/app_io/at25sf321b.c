#include "at25sf321b.h"

#include <string.h>
#include "driver/spi_master.h"
#include "app_pin_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AT25_CMD_READ             0x03
#define AT25_CMD_PAGE_PROGRAM     0x02
#define AT25_CMD_READ_STATUS      0x05
#define AT25_CMD_WRITE_ENABLE     0x06
#define AT25_CMD_SECTOR_ERASE     0x20
#define AT25_CMD_READ_ID          0x9F
#define AT25_STATUS_BUSY          0x01
#define AT25_STATUS_TIMEOUT_MS    10000U
#define AT25_MAX_READ_CHUNK       256U

// Dispositivo SPI compartilhado; o acesso externo e serializado por app_storage.
static spi_device_handle_t s_flash_device;

// Transmite um comando SPI completo usando o dispositivo ja inicializado.
static esp_err_t transmit_bytes(const uint8_t *tx_data, size_t size)
{
    spi_transaction_t transaction = {
        .length = size * 8U,
        .tx_buffer = tx_data,
    };
    return spi_device_transmit(s_flash_device, &transaction);
}

// Libera a flash para a proxima operacao que modifica memoria.
static esp_err_t write_enable(void)
{
    const uint8_t command = AT25_CMD_WRITE_ENABLE;
    return transmit_bytes(&command, sizeof(command));
}

// Le o registrador de estado e devolve o byte de status ao chamador.
static esp_err_t read_status(uint8_t *status)
{
    uint8_t tx_data[] = {AT25_CMD_READ_STATUS, 0xFF};
    uint8_t rx_data[sizeof(tx_data)] = {0};
    spi_transaction_t transaction = {
        .length = sizeof(tx_data) * 8U,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    esp_err_t err = spi_device_transmit(s_flash_device, &transaction);
    if (err == ESP_OK) {
        *status = rx_data[1];
    }
    return err;
}

// Aguarda BUSY limpar ou retorna timeout/erro SPI.
static esp_err_t wait_until_ready(void)
{
    TickType_t timeout_ticks = pdMS_TO_TICKS(AT25_STATUS_TIMEOUT_MS);
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks) {
        uint8_t status = 0;
        esp_err_t err = read_status(&status);
        if (err != ESP_OK) {
            return err;
        }
        if ((status & AT25_STATUS_BUSY) == 0) {
            return ESP_OK;
        }
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
}

// Codifica opcode e endereco de 24 bits no cabecalho de comando SPI.
static void encode_address(uint8_t *command, uint8_t opcode, uint32_t address)
{
    command[0] = opcode;
    command[1] = (uint8_t)(address >> 16U);
    command[2] = (uint8_t)(address >> 8U);
    command[3] = (uint8_t)address;
}

// Evita overflow e acessos que ultrapassem os 4 MiB da memoria.
static bool address_range_is_valid(uint32_t address, size_t size)
{
    return size > 0 && address < AT25SF321B_CAPACITY_BYTES &&
           size <= AT25SF321B_CAPACITY_BYTES - address;
}

// Inicializa SPI2, registra o dispositivo e confirma resposta do comando READ ID.
esp_err_t at25sf321b_init(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = APP_FLASH_PIN_MOSI,
        .miso_io_num = APP_FLASH_PIN_MISO,
        .sclk_io_num = APP_FLASH_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = AT25SF321B_PAGE_SIZE + 4U,
    };
    esp_err_t err = spi_bus_initialize(APP_FLASH_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = APP_FLASH_PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(APP_FLASH_SPI_HOST, &device_config, &s_flash_device);
    if (err != ESP_OK) {
        spi_bus_free(APP_FLASH_SPI_HOST);
        return err;
    }

    uint8_t tx_data[] = {AT25_CMD_READ_ID, 0xFF, 0xFF, 0xFF};
    uint8_t rx_data[sizeof(tx_data)] = {0};
    spi_transaction_t transaction = {
        .length = sizeof(tx_data) * 8U,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    err = spi_device_transmit(s_flash_device, &transaction);
    if (err == ESP_OK &&
        (rx_data[1] == 0x00 || rx_data[1] == 0xFF ||
         rx_data[2] == 0x00 || rx_data[2] == 0xFF ||
         rx_data[3] == 0x00 || rx_data[3] == 0xFF)) {
        err = ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        at25sf321b_deinit();
    }
    return err;
}

// Remove dispositivo e libera o barramento SPI alocado pelo driver.
void at25sf321b_deinit(void)
{
    if (s_flash_device != NULL) {
        spi_bus_remove_device(s_flash_device);
        s_flash_device = NULL;
    }
    spi_bus_free(APP_FLASH_SPI_HOST);
}

// Le em blocos limitados pelo buffer de transferencia do driver.
esp_err_t at25sf321b_read(uint32_t address, void *data, size_t size)
{
    if (data == NULL || !address_range_is_valid(address, size)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_data[AT25_MAX_READ_CHUNK + 4U];
    uint8_t rx_data[AT25_MAX_READ_CHUNK + 4U];
    uint8_t *output = data;
    while (size > 0) {
        size_t chunk_size = size > AT25_MAX_READ_CHUNK ? AT25_MAX_READ_CHUNK : size;
        encode_address(tx_data, AT25_CMD_READ, address);
        memset(tx_data + 4U, 0xFF, chunk_size);

        spi_transaction_t transaction = {
            .length = (chunk_size + 4U) * 8U,
            .tx_buffer = tx_data,
            .rx_buffer = rx_data,
        };
        esp_err_t err = spi_device_transmit(s_flash_device, &transaction);
        if (err != ESP_OK) {
            return err;
        }

        memcpy(output, rx_data + 4U, chunk_size);
        address += (uint32_t)chunk_size;
        output += chunk_size;
        size -= chunk_size;
    }
    return ESP_OK;
}

// Divide escrita em paginas de 256 bytes, habilitando e aguardando cada pagina.
esp_err_t at25sf321b_write(uint32_t address, const void *data, size_t size)
{
    if (data == NULL || !address_range_is_valid(address, size)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *input = data;
    uint8_t tx_data[AT25SF321B_PAGE_SIZE + 4U];
    while (size > 0) {
        size_t page_offset = address % AT25SF321B_PAGE_SIZE;
        size_t chunk_size = AT25SF321B_PAGE_SIZE - page_offset;
        if (chunk_size > size) {
            chunk_size = size;
        }

        esp_err_t err = wait_until_ready();
        if (err != ESP_OK) {
            return err;
        }
        err = write_enable();
        if (err != ESP_OK) {
            return err;
        }

        encode_address(tx_data, AT25_CMD_PAGE_PROGRAM, address);
        memcpy(tx_data + 4U, input, chunk_size);
        err = transmit_bytes(tx_data, chunk_size + 4U);
        if (err != ESP_OK) {
            return err;
        }
        err = wait_until_ready();
        if (err != ESP_OK) {
            return err;
        }

        address += (uint32_t)chunk_size;
        input += chunk_size;
        size -= chunk_size;
    }
    return ESP_OK;
}

// Apaga setor alinhado de 4 KiB apos write-enable e espera conclusao.
esp_err_t at25sf321b_erase_sector(uint32_t address)
{
    if (address >= AT25SF321B_CAPACITY_BYTES ||
        (address % AT25SF321B_SECTOR_SIZE) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wait_until_ready();
    if (err != ESP_OK) {
        return err;
    }
    err = write_enable();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t command[4];
    encode_address(command, AT25_CMD_SECTOR_ERASE, address);
    err = transmit_bytes(command, sizeof(command));
    return err == ESP_OK ? wait_until_ready() : err;
}