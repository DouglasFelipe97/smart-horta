#ifndef PROBE_INSTALL_H
#define PROBE_INSTALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Inicializa UART e estruturas da rotina de instalacao de sondas.
void probe_install_manager_init(void);

// Liga/desliga a rotina nao bloqueante conforme tela ativa.
void probe_install_manager_set_active(bool active);

// Liga/desliga a rotina de desinstalacao nao bloqueante conforme tela ativa.
void probe_uninstall_manager_set_active(bool active);

// Liga/desliga polling de telemetria da tela "Sondas" conforme tela ativa.
void probe_monitor_manager_set_active(bool active);

// Tick periodico para transmissao, recepcao e timeouts.
void probe_install_manager_periodic(uint32_t now_ms);

// Tick periodico para polling de leitura das sondas (tempo real).
void probe_monitor_manager_periodic(uint32_t now_ms);

// Envia pacote de descoberta (ID broadcast 0xFF).
void send_probe_discovery(void);

// Envia pacote de atribuicao para ID 0x01..0x04.
void send_probe_assignment(uint8_t id);

// Hook/stub para decodificar payload de resposta quando protocolo estiver definido.
// Retorna true se payload contem telemetria valida para preenchimento da UI.
bool process_probe_response(const uint8_t *payload,
                            size_t payload_len,
                            float *air_temperature,
                            float *air_humidity,
                            float *soil_temperature,
                            float *soil_humidity);

// Executa a sequencia de desinstalacao em ciclos sem bloquear a task chamadora.
void run_uninstall_sequence(uint32_t now_ms);

#endif