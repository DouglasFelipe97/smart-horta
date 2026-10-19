#ifndef PROBE_INSTALL_H
#define PROBE_INSTALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Protocolo RS-485 das sondas: configura UART1 e zera as FSMs de instalacao,
// desinstalacao e telemetria. A instalacao e nao bloqueante e usa periodic().
void probe_install_manager_init(void);

// Controla descoberta/atribuicao de IDs conforme a tela de instalacao.
void probe_install_manager_set_active(bool active);

// Controla a sequencia de reset redundante conforme a tela de desinstalacao.
void probe_uninstall_manager_set_active(bool active);

// Liga/desliga polling cíclico; tambem e usado durante lote ativo para automacao.
void probe_monitor_manager_set_active(bool active);

// Avanca instalacao: broadcast, atribuicao sequencial, ACK e tentativas.
void probe_install_manager_periodic(uint32_t now_ms);

// Avanca polling das sondas 1..4, com timeout e intervalo entre varreduras.
void probe_monitor_manager_periodic(uint32_t now_ms);

// Envia comandos de protocolo; assignment grava um ID entre 1 e 4.
void send_probe_discovery(void);
void send_probe_assignment(uint8_t id);

// Decodifica payload legado (6 bytes) ou completo (8 bytes) para graus Celsius
// e percentual; ponteiros de saida podem ser nulos. Retorna false se invalido.
bool process_probe_response(const uint8_t *payload,
                            size_t payload_len,
                            float *air_temperature,
                            float *air_humidity,
                            float *soil_temperature,
                            float *soil_humidity);

// Avanca a desinstalacao: IDs 1..4 repetidos em tres ciclos, sem espera bloqueante.
void run_uninstall_sequence(uint32_t now_ms);

#endif