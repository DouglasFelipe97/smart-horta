# Release Notes - Secao de Sondas

Data: 2026-10-13
Escopo: Ajustes -> Instalar Sondas -> (Instalar, Desinstalar, Sondas)

## Visao Geral
Esta entrega introduz um subsistema completo para gerenciamento de sondas no firmware:
- Instalacao sequencial de IDs (S1..S4) com descoberta por broadcast.
- Desinstalacao redundante (3 ciclos) com retorno automatico de tela.
- Monitoramento ciclico em tempo real na tela "Sondas" com timeout por sensor.
- Integracao com a UI sem bloqueio de navegacao (FSMs acionadas por tick no loop de IO).

## Itens Entregues

### 1) Estrutura de Dados no Modelo (app_model)
- Inclusao de `APP_PROBE_COUNT` (=4).
- Inclusao de `app_probe_snapshot_t` com:
  - `installed`
  - `has_reading`
  - `temperature`
  - `air_humidity`
  - `soil_humidity`
- Inclusao no snapshot global de status operacionais:
  - `probe_install_running`
  - `probe_install_finished`
  - `probe_install_completed_count`
  - `probe_uninstall_running`
  - `probe_uninstall_finished`
  - `probe_uninstall_cycle`

### 2) Navegacao e Estados da UI
- Fluxo de menu implementado e estabilizado:
  - Ajustes -> Instalar Sondas -> Instalar
  - Ajustes -> Instalar Sondas -> Desinstalar
  - Ajustes -> Instalar Sondas -> Sondas
- BACK consistente entre niveis.
- Entrada em "Instalar" e "Desinstalar" reseta estado de execucao para evitar dados antigos na UI.

### 3) Camada UART/Protocolo (modulo dedicado)
- Novo modulo `probe_install.c/.h` para isolar protocolo e FSMs.
- Frame padrao de 9 bytes com delimitadores 0x55 e CRC-16 Modbus.
- Funcoes de envio modularizadas:
  - `send_probe_discovery()`
  - `send_probe_assignment(uint8_t id)`
- Parser central com verificacao de:
  - delimitadores
  - tamanho
  - CRC
  - extracao de `reg`, `id` e payload

### 4) Instalacao de Sondas (FSM)
- Disparo automatico ao entrar na tela "Instalar".
- Etapas:
  1. Descoberta por broadcast ciclico.
  2. Atribuicao do ID sequencial atual.
  3. Retry de atribuicao em caso de silencio.
  4. Finaliza ao concluir os 4 IDs.
- Atualiza snapshot da UI com status e telemetria quando disponivel.

### 5) Desinstalacao de Sondas (FSM)
- Disparo automatico ao entrar na tela "Desinstalar".
- Sequencia:
  - Varre IDs 1..4 por ciclo.
  - Repete 3 ciclos para redundancia.
  - Gap de 80 ms entre transmisses (nao bloqueante).
- Ao concluir:
  - marca status de finalizacao
  - aguarda 500 ms de feedback visual
  - retorna automaticamente ao submenu "Instalar Sondas"

### 6) Tela Sondas (monitoramento em tempo real)
- Polling ciclico de IDs 1..4 com timeout por sensor (100 ms).
- Requisicao por sensor + parse de resposta valida.
- Em timeout/erro, sensor marcado como inativo para a UI.
- Intervalo entre varreduras completas: 1200 ms.
- UI exibe:
  - valor real quando resposta valida
  - mascara `__._` quando ausente/sem resposta

### 7) Integracao no Scheduler (task de IO)
- Managers acionados no loop de `io_task` (~50 ms por iteracao):
  - instalacao
  - desinstalacao
  - monitoramento de leitura
- Cada manager e ativado/desativado pela tela ativa, liberando barramento quando fora do contexto.

## Resultado de UX
- Navegacao fluida durante todas as rotinas.
- Sem congelamento da UI por loops bloqueantes.
- Feedback visual claro para:
  - instalacao em progresso
  - desinstalacao por ciclos
  - sensores ativos/inativos na tela Sondas

## Observacoes
- O hook de endereco alvo da desinstalacao (`select_probe_target_address`) permanece preparado para hardware/topologia que exija selecao externa.
- O registrador de leitura foi definido como `0x20`; ajustar caso o firmware da sonda use outro mapa.
- Avisos de includePath no editor sao de ambiente ESP-IDF e nao alteram a logica implementada.
