#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define APP_PROBE_COUNT 4
#define APP_PROBE_TYPE_AIR_SOIL_TH 1
#define PROBE_UART_RE_DE_PIN 4
#define UART_NUM_1 1
#define GPIO_NUM_17 17
#define GPIO_NUM_16 16
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_ERROR_CHECK(value) assert((value) == ESP_OK)
#define portMAX_DELAY UINT32_MAX
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1

typedef int esp_err_t;
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;

static uint8_t input[512];
static size_t input_pos, input_len;
static unsigned snapshots;
static bool last_valid;
static float last_temperature;

static int gpio_set_level(int pin, int level) { (void)pin; (void)level; return 0; }
static int gpio_config(const gpio_config_t *cfg) { (void)cfg; return 0; }
static int uart_write_bytes(int port, const char *data, size_t len)
{ (void)port; (void)data; return (int)len; }
static int uart_wait_tx_done(int port, uint32_t ticks) { (void)port; (void)ticks; return 0; }
static int uart_read_bytes(int port, void *data, size_t len, uint32_t ticks)
{
    (void)port; (void)ticks;
    size_t available = input_len - input_pos;
    if (len > available) len = available;
    memcpy(data, input + input_pos, len);
    input_pos += len;
    return (int)len;
}
static int uart_driver_install(int port, int rx, int tx, int queue, void *handle, int flags)
{ (void)port; (void)rx; (void)tx; (void)queue; (void)handle; (void)flags; return 0; }
static int uart_param_config(int port, const uart_config_t *cfg)
{ (void)port; (void)cfg; return 0; }
static int uart_set_pin(int port, int tx, int rx, int rts, int cts)
{ (void)port; (void)tx; (void)rx; (void)rts; (void)cts; return 0; }
static void app_model_set_probe_snapshot(uint8_t slot, uint8_t id, int type,
                                        bool installed, bool valid, float air_t,
                                        float air_h, float soil_t, float soil_h)
{
    (void)slot; (void)id; (void)type; (void)installed;
    (void)air_h; (void)soil_t; (void)soil_h;
    snapshots++;
    last_valid = valid;
    last_temperature = air_t;
}
static void app_model_reset_probe_install_data(void) {}
static void app_model_set_probe_install_status(bool active, bool done, uint8_t count)
{ (void)active; (void)done; (void)count; }
static void app_model_set_probe_uninstall_status(bool active, bool done, uint8_t count)
{ (void)active; (void)done; (void)count; }
static void app_model_exit_probe_uninstall_to_menu(void) {}

static size_t build_probe_frame(uint8_t, uint8_t, uint8_t, uint8_t *, size_t);
static uint16_t crc16_modbus(const uint8_t *, size_t);
static bool uart_read_probe_frame(uint8_t *, size_t, size_t *);
void probe_install_manager_init(void);
void probe_install_manager_set_active(bool);
void probe_install_manager_periodic(uint32_t);
void probe_monitor_manager_set_active(bool);
void probe_monitor_manager_periodic(uint32_t);

static void feed(const uint8_t *data, size_t len)
{
    assert(input_pos == input_len);
    memcpy(input, data, len);
    input_pos = 0;
    input_len = len;
}

int main(void)
{
    uint8_t ack[9], received[256];
    size_t received_len = 0;
    probe_install_manager_init();
    assert(build_probe_frame(0x10, 1, 0, ack, sizeof(ack)) == sizeof(ack));
    for (size_t split = 1; split < sizeof(ack); split++) {
        feed(ack, split);
        assert(!uart_read_probe_frame(received, sizeof(received), &received_len));
        feed(ack + split, sizeof(ack) - split);
        assert(uart_read_probe_frame(received, sizeof(received), &received_len));
        assert(received_len == sizeof(ack));
        assert(memcmp(ack, received, sizeof(ack)) == 0);
    }
    uint8_t joined[18];
    memcpy(joined, ack, 9);
    memcpy(joined + 9, ack, 9);
    feed(joined, sizeof(joined));
    assert(uart_read_probe_frame(received, sizeof(received), &received_len));
    assert(uart_read_probe_frame(received, sizeof(received), &received_len));
    joined[6] ^= 1;
    feed(joined, sizeof(joined));
    assert(uart_read_probe_frame(received, sizeof(received), &received_len));
    assert(memcmp(ack, received, sizeof(ack)) == 0);

    probe_install_manager_set_active(true);
    probe_install_manager_periodic(500);
    build_probe_frame(0x11, 0xFF, 0, ack, sizeof(ack));
    feed(ack, sizeof(ack));
    probe_install_manager_periodic(520);
    build_probe_frame(0x10, 1, 0, ack, sizeof(ack));
    feed(ack, 4);
    probe_install_manager_periodic(530);
    feed(ack + 4, 5);
    probe_install_manager_periodic(540);
    assert(snapshots == 1);

    uint8_t telemetry[] = {0x55, 1, 9, 0x20, 1, 0, 0xEA, 0, 0x2B, 2, 0xC8, 1, 0, 0, 0x55};
    build_probe_frame(0x11, 0xFF, 0, ack, sizeof(ack));
    feed(ack, sizeof(ack));
    probe_install_manager_periodic(560);
    telemetry[3] = 0x10;
    telemetry[4] = 2;
    uint16_t crc = crc16_modbus(telemetry + 1, 11);
    telemetry[12] = (uint8_t)crc;
    telemetry[13] = (uint8_t)(crc >> 8);
    feed(telemetry, 7);
    probe_install_manager_periodic(570);
    feed(telemetry + 7, sizeof(telemetry) - 7);
    probe_install_manager_periodic(580);
    assert(snapshots == 2 && last_valid);
    assert(last_temperature > 23.39f && last_temperature < 23.41f);
    probe_install_manager_set_active(false);
    telemetry[3] = 0x20;
    telemetry[4] = 1;
    crc = crc16_modbus(telemetry + 1, 11);
    telemetry[12] = (uint8_t)crc;
    telemetry[13] = (uint8_t)(crc >> 8);
    probe_monitor_manager_set_active(true);
    probe_monitor_manager_periodic(600);
    for (size_t index = 0; index < sizeof(telemetry); index++) {
        feed(telemetry + index, 1);
        probe_monitor_manager_periodic(610 + (uint32_t)index);
    }
    assert(last_valid);
    assert(last_temperature > 23.39f && last_temperature < 23.41f);
    puts("PASS: fragmented ACK/telemetry, joined frames, CRC recovery, installation and polling");
    return 0;
}