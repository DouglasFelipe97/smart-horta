#include "probe_install.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_model.h"

#define PROBE_UART_PORT            UART_NUM_1
#define PROBE_UART_BAUDRATE        9600
#define PROBE_UART_TX_PIN          GPIO_NUM_17
#define PROBE_UART_RX_PIN          GPIO_NUM_16
#define PROBE_UART_RX_BUFFER_SIZE  256

#define PROBE_FRAME_DELIMITER      0x55
#define PROBE_SW_VERSION           0x01
#define PROBE_INSTALL_REGISTER     0x10
#define PROBE_PAYLOAD_LENGTH       3
#define PROBE_FRAME_TX_SIZE        9

#define PROBE_DISCOVERY_ID         0xFF
#define PROBE_MAX_ID               4

#define PROBE_DISCOVERY_PERIOD_MS  500U
#define PROBE_ASSIGN_RETRY_MS      700U
#define PROBE_ASSIGN_MAX_RETRY     5U

#define PROBE_UNINSTALL_CYCLES     3U
#define PROBE_UNINSTALL_GAP_MS     80U
#define PROBE_UNINSTALL_RETURN_MS  500U

static const char *TAG = "PROBE_INSTALL";

typedef struct __attribute__((packed)) {
    uint8_t start;
    uint8_t version;
    uint8_t length;
    uint8_t reg;
    uint8_t id;
    uint8_t reserved;
    uint16_t crc;
    uint8_t end;
} probe_install_frame_t;

typedef enum {
    PROBE_STAGE_IDLE = 0,
    PROBE_STAGE_DISCOVERY,
    PROBE_STAGE_WAIT_ASSIGN_ACK,
    PROBE_STAGE_DONE,
} probe_stage_t;

typedef struct {
    bool active;
    probe_stage_t stage;
    uint8_t current_id;
    uint8_t completed_count;
    uint8_t assign_retry_count;
    uint32_t last_tx_ms;
} probe_install_state_t;

typedef struct {
    bool active;
    bool finished;
    uint8_t cycle_index;
    uint8_t target_id;
    uint32_t last_tx_ms;
    uint32_t finished_ms;
} probe_uninstall_state_t;

static probe_install_state_t s_probe_state;
static probe_uninstall_state_t s_uninstall_state;
static bool s_uart_initialized = false;

static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

static size_t build_probe_frame(uint8_t id, uint8_t *out_buf, size_t out_buf_len)
{
    if (out_buf == NULL || out_buf_len < PROBE_FRAME_TX_SIZE) {
        return 0;
    }

    probe_install_frame_t frame;
    frame.start = PROBE_FRAME_DELIMITER;
    frame.version = PROBE_SW_VERSION;
    frame.length = PROBE_PAYLOAD_LENGTH;
    frame.reg = PROBE_INSTALL_REGISTER;
    frame.id = id;
    frame.reserved = 0x00;

    // CRC calculado sobre versao + tamanho + payload (exclui delimitadores).
    uint8_t crc_input[] = {
        frame.version,
        frame.length,
        frame.reg,
        frame.id,
        frame.reserved,
    };
    frame.crc = crc16_modbus(crc_input, sizeof(crc_input));
    frame.end = PROBE_FRAME_DELIMITER;

    out_buf[0] = frame.start;
    out_buf[1] = frame.version;
    out_buf[2] = frame.length;
    out_buf[3] = frame.reg;
    out_buf[4] = frame.id;
    out_buf[5] = frame.reserved;
    out_buf[6] = (uint8_t)(frame.crc & 0x00FFU);
    out_buf[7] = (uint8_t)((frame.crc >> 8U) & 0x00FFU);
    out_buf[8] = frame.end;

    return PROBE_FRAME_TX_SIZE;
}

static size_t build_probe_uninstall_frame(uint8_t *out_buf, size_t out_buf_len)
{
    // Reutiliza framing padrao: campo de ID gravado recebe 0xFF (estado virgem).
    return build_probe_frame(PROBE_DISCOVERY_ID, out_buf, out_buf_len);
}

static void select_probe_target_address(uint8_t target_id)
{
    // Hook para selecao de endereco fisico/logico fora do payload, se necessario.
    // Nesta versao nao ha camada adicional de enderecamento alem do framing.
    (void)target_id;
}

static bool parse_probe_frame(const uint8_t *frame,
                              size_t frame_len,
                              uint8_t *out_id,
                              const uint8_t **out_payload,
                              size_t *out_payload_len)
{
    if (frame == NULL || frame_len < PROBE_FRAME_TX_SIZE) {
        return false;
    }

    if (frame[0] != PROBE_FRAME_DELIMITER || frame[1] != PROBE_SW_VERSION) {
        return false;
    }

    uint8_t payload_len = frame[2];
    size_t total_len = (size_t)payload_len + 6U;
    if (total_len != frame_len || frame[frame_len - 1U] != PROBE_FRAME_DELIMITER) {
        return false;
    }

    if (payload_len < PROBE_PAYLOAD_LENGTH) {
        return false;
    }

    size_t crc_pos = frame_len - 3U;
    uint16_t crc_rx = (uint16_t)frame[crc_pos] | (uint16_t)(frame[crc_pos + 1U] << 8U);
    uint16_t crc_calc = crc16_modbus(&frame[1], crc_pos - 1U);
    if (crc_rx != crc_calc) {
        return false;
    }

    // Payload minimo: reg, id, reservado.
    if (frame[3] != PROBE_INSTALL_REGISTER) {
        return false;
    }

    if (out_id != NULL) {
        *out_id = frame[4];
    }

    if (out_payload != NULL) {
        *out_payload = &frame[6];
    }

    if (out_payload_len != NULL) {
        *out_payload_len = (size_t)(payload_len - PROBE_PAYLOAD_LENGTH);
    }

    return true;
}

static bool uart_read_probe_frame(uint8_t *buf, size_t buf_size, size_t *out_len)
{
    if (buf == NULL || out_len == NULL || buf_size < PROBE_FRAME_TX_SIZE) {
        return false;
    }

    int read_len = uart_read_bytes(PROBE_UART_PORT, buf, buf_size, 0);
    if (read_len < PROBE_FRAME_TX_SIZE) {
        return false;
    }

    for (int i = 0; i <= (read_len - PROBE_FRAME_TX_SIZE); i++) {
        if (buf[i] != PROBE_FRAME_DELIMITER) {
            continue;
        }

        uint8_t payload_len = buf[i + 2];
        size_t candidate_len = (size_t)payload_len + 6U;
        if ((i + (int)candidate_len) > read_len || candidate_len > buf_size) {
            continue;
        }

        if (buf[i + (int)candidate_len - 1] != PROBE_FRAME_DELIMITER) {
            continue;
        }

        memmove(buf, &buf[i], candidate_len);
        *out_len = candidate_len;
        return true;
    }

    return false;
}

void send_probe_discovery(void)
{
    uint8_t tx_frame[PROBE_FRAME_TX_SIZE] = {0};
    size_t tx_len = build_probe_frame(PROBE_DISCOVERY_ID, tx_frame, sizeof(tx_frame));
    if (tx_len == 0) {
        return;
    }

    uart_write_bytes(PROBE_UART_PORT, (const char *)tx_frame, tx_len);
}

void send_probe_assignment(uint8_t id)
{
    uint8_t tx_frame[PROBE_FRAME_TX_SIZE] = {0};
    size_t tx_len = build_probe_frame(id, tx_frame, sizeof(tx_frame));
    if (tx_len == 0) {
        return;
    }

    uart_write_bytes(PROBE_UART_PORT, (const char *)tx_frame, tx_len);
}

static void send_probe_uninstall(uint8_t target_id)
{
    uint8_t tx_frame[PROBE_FRAME_TX_SIZE] = {0};
    size_t tx_len = build_probe_uninstall_frame(tx_frame, sizeof(tx_frame));
    if (tx_len == 0) {
        return;
    }

    // Direciona transmissao para o alvo corrente (S1..S4) antes de gravar 0xFF.
    select_probe_target_address(target_id);
    uart_write_bytes(PROBE_UART_PORT, (const char *)tx_frame, tx_len);
}

bool process_probe_response(const uint8_t *payload,
                            size_t payload_len,
                            float *temperature,
                            float *air_humidity,
                            float *soil_humidity)
{
    // Hook/stub: protocolo de telemetria pode variar conforme firmware da sonda.
    // Layout esperado nesta implementacao: temp_x10(int16), umid_ar_x10(uint16), umid_solo_x10(uint16).
    if (payload == NULL || payload_len < 6U) {
        return false;
    }

    int16_t t_x10 = (int16_t)((uint16_t)payload[0] | (uint16_t)(payload[1] << 8U));
    uint16_t u_x10 = (uint16_t)payload[2] | (uint16_t)(payload[3] << 8U);
    uint16_t s_x10 = (uint16_t)payload[4] | (uint16_t)(payload[5] << 8U);

    if (temperature != NULL) {
        *temperature = (float)t_x10 / 10.0f;
    }

    if (air_humidity != NULL) {
        *air_humidity = (float)u_x10 / 10.0f;
    }

    if (soil_humidity != NULL) {
        *soil_humidity = (float)s_x10 / 10.0f;
    }

    return true;
}

void probe_install_manager_init(void)
{
    memset(&s_probe_state, 0, sizeof(s_probe_state));
    memset(&s_uninstall_state, 0, sizeof(s_uninstall_state));

    uart_config_t cfg = {
        .baud_rate = PROBE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(PROBE_UART_PORT, PROBE_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PROBE_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PROBE_UART_PORT, PROBE_UART_TX_PIN, PROBE_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_uart_initialized = true;
}

void probe_install_manager_set_active(bool active)
{
    if (!s_uart_initialized) {
        return;
    }

    if (active && !s_probe_state.active) {
        // Entrada na tela Instalar: inicia processo automatico do ID 1.
        s_probe_state.active = true;
        s_probe_state.stage = PROBE_STAGE_DISCOVERY;
        s_probe_state.current_id = 1;
        s_probe_state.completed_count = 0;
        s_probe_state.assign_retry_count = 0;
        s_probe_state.last_tx_ms = 0;

        app_model_reset_probe_install_data();
        app_model_set_probe_install_status(true, false, 0);
    } else if (!active && s_probe_state.active) {
        s_probe_state.active = false;
        s_probe_state.stage = PROBE_STAGE_IDLE;
        app_model_set_probe_install_status(false, false, s_probe_state.completed_count);
    }
}

void probe_uninstall_manager_set_active(bool active)
{
    if (!s_uart_initialized) {
        return;
    }

    if (active && !s_uninstall_state.active) {
        s_uninstall_state.active = true;
        s_uninstall_state.finished = false;
        s_uninstall_state.cycle_index = 0;
        s_uninstall_state.target_id = 1;
        s_uninstall_state.last_tx_ms = 0;
        s_uninstall_state.finished_ms = 0;
        app_model_set_probe_uninstall_status(true, false, 1U);
    } else if (!active && s_uninstall_state.active) {
        s_uninstall_state.active = false;
        s_uninstall_state.finished = false;
        app_model_set_probe_uninstall_status(false, false, s_uninstall_state.cycle_index);
    }
}

void probe_install_manager_periodic(uint32_t now_ms)
{
    if (!s_uart_initialized || !s_probe_state.active) {
        return;
    }

    uint8_t rx_buf[PROBE_UART_RX_BUFFER_SIZE] = {0};
    size_t frame_len = 0;
    uint8_t rx_id = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    bool has_valid_response = false;
    if (uart_read_probe_frame(rx_buf, sizeof(rx_buf), &frame_len)) {
        has_valid_response = parse_probe_frame(rx_buf, frame_len, &rx_id, &payload, &payload_len);
    }

    if (s_probe_state.stage == PROBE_STAGE_DISCOVERY) {
        if ((now_ms - s_probe_state.last_tx_ms) >= PROBE_DISCOVERY_PERIOD_MS) {
            send_probe_discovery();
            s_probe_state.last_tx_ms = now_ms;
        }

        if (has_valid_response) {
            // Encontrou sonda no barramento: envia atribuicao para ID atual.
            send_probe_assignment(s_probe_state.current_id);
            s_probe_state.last_tx_ms = now_ms;
            s_probe_state.assign_retry_count = 0;
            s_probe_state.stage = PROBE_STAGE_WAIT_ASSIGN_ACK;
        }

        return;
    }

    if (s_probe_state.stage == PROBE_STAGE_WAIT_ASSIGN_ACK) {
        if (has_valid_response) {
            if (rx_id != s_probe_state.current_id && rx_id != PROBE_DISCOVERY_ID) {
                return;
            }

            float t = 0.0f;
            float u = 0.0f;
            float s = 0.0f;
            bool has_telemetry = process_probe_response(payload, payload_len, &t, &u, &s);

            uint8_t slot = (uint8_t)(s_probe_state.current_id - 1U);
            app_model_set_probe_snapshot(slot, true, has_telemetry, t, u, s);

            s_probe_state.completed_count++;
            app_model_set_probe_install_status(true, false, s_probe_state.completed_count);

            s_probe_state.current_id++;
            if (s_probe_state.current_id > PROBE_MAX_ID) {
                s_probe_state.stage = PROBE_STAGE_DONE;
                app_model_set_probe_install_status(false, true, s_probe_state.completed_count);
                ESP_LOGI(TAG, "Instalacao de sondas concluida (4/4)");
            } else {
                s_probe_state.stage = PROBE_STAGE_DISCOVERY;
            }
            return;
        }

        if ((now_ms - s_probe_state.last_tx_ms) >= PROBE_ASSIGN_RETRY_MS) {
            if (s_probe_state.assign_retry_count < PROBE_ASSIGN_MAX_RETRY) {
                send_probe_assignment(s_probe_state.current_id);
                s_probe_state.assign_retry_count++;
                s_probe_state.last_tx_ms = now_ms;
            } else {
                // Evita travamento em barramento silencioso: volta para descoberta.
                s_probe_state.assign_retry_count = 0;
                s_probe_state.stage = PROBE_STAGE_DISCOVERY;
                s_probe_state.last_tx_ms = now_ms;
            }
        }

        return;
    }

    if (s_probe_state.stage == PROBE_STAGE_DONE) {
        // Mantem estado de termino enquanto usuario estiver na tela.
        return;
    }
}

void run_uninstall_sequence(uint32_t now_ms)
{
    if (!s_uart_initialized || !s_uninstall_state.active) {
        return;
    }

    if (!s_uninstall_state.finished) {
        if ((now_ms - s_uninstall_state.last_tx_ms) < PROBE_UNINSTALL_GAP_MS) {
            return;
        }

        // Envia frame de reset para estado virgem (0xFF) por alvo S1..S4.
        send_probe_uninstall(s_uninstall_state.target_id);
        s_uninstall_state.last_tx_ms = now_ms;

        if (s_uninstall_state.target_id < PROBE_MAX_ID) {
            s_uninstall_state.target_id++;
            return;
        }

        // Fechou uma varredura completa dos quatro IDs.
        s_uninstall_state.target_id = 1;
        s_uninstall_state.cycle_index++;

        if (s_uninstall_state.cycle_index >= PROBE_UNINSTALL_CYCLES) {
            s_uninstall_state.finished = true;
            s_uninstall_state.finished_ms = now_ms;
            app_model_set_probe_uninstall_status(false, true, PROBE_UNINSTALL_CYCLES);
            return;
        }

        app_model_set_probe_uninstall_status(true, false, (uint8_t)(s_uninstall_state.cycle_index + 1U));
        return;
    }

    // Delay curto para feedback visual antes de voltar automaticamente.
    if ((now_ms - s_uninstall_state.finished_ms) >= PROBE_UNINSTALL_RETURN_MS) {
        app_model_exit_probe_uninstall_to_menu();
        s_uninstall_state.active = false;
        s_uninstall_state.finished = false;
    }
}
