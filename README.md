# Smart Horta

Firmware de uma estacao embarcada para monitoramento de horta, desenvolvido como TCC.
O projeto roda em ESP32 e integra interface local em display ST7920, teclado analogico,
RTC DS3231 e gerenciamento de sondas com comunicacao serial.

## Visao Geral

O sistema foi projetado para operar em campo com navegacao local, foco em leitura rapida
de status e fluxo de instalacao/manutencao de sondas.

Cada sonda instalada fornece:
- Temperatura do ar
- Umidade do ar
- Temperatura do solo
- Umidade do solo

As sondas sao tratadas como dispositivos identificaveis (ID), permitindo crescimento futuro
para novos tipos de sonda no mesmo ecossistema.

## Tipos de Sonda

O firmware ja trabalha com identificacao de tipo por sonda (`probe_type`) no snapshot.

Tipos atuais no codigo:
- `APP_PROBE_TYPE_UNKNOWN (0)`: tipo nao identificado.
- `APP_PROBE_TYPE_AIR_SOIL_TH (1)`: sonda de temperatura/umidade de ar e solo.

Cada slot de sonda tambem carrega:
- `probe_id`: identificador logico no barramento (1..4 na versao atual).
- `probe_type`: perfil funcional da sonda para suportar expansao futura.

## Funcionalidades

- Interface grafica no display ST7920 (128x64) via u8g2.
- Teclado analogico com debounce e maquina de estados de navegacao.
- Tela HOME com:
	- contagem de alarmes ativos
	- data/hora
	- medias das leituras de ar e solo das sondas instaladas
- Menu Relatorios com historico persistente de alarmes de temperatura e umidade do ar e do solo.
- Menu Ajustes > Opcoes > Buzzer para habilitar/desabilitar o beep de navegacao.
- Atalho por tecla esquerda na HOME para tela direta de sondas instaladas.
- Fluxo de instalacao de sondas com atribuicao de IDs.
- Fluxo de desinstalacao redundante (multiplos ciclos para robustez).
- Polling ciclico de telemetria por sonda.
- Sincronizacao de data/hora com RTC DS3231.
- Buzzer configuravel no menu de ajustes.
- Timeout de inatividade para retorno automatico a HOME.

## Aplicacao Pratica

Aplicavel em hortas e ambientes de cultivo que exigem acompanhamento local continuo,
permitindo:
- verificacao rapida de condicoes medias do ambiente
- deteccao visual de condicoes fora de faixa por alarmes
- manutencao simplificada de sondas diretamente no equipamento

## Estrutura do Projeto

- main/
	- inicializacao principal da aplicacao
- components/app_io/
	- modelo global de estado da UI
	- tasks de IO, teclado, display e RTC
	- protocolo e FSMs de instalacao/desinstalacao/monitoramento das sondas
- components/u8g2/
	- biblioteca grafica do display
- docs/
	- documentacao tecnica e notas de release

## Documentacao

- Guia tecnico das sondas: docs/GUIA_TECNICO_SONDAS.md
- Guia tecnico do sistema, arquitetura, RTOS, controle, persistencia e protocolo: docs/GUIA_TECNICO_SISTEMA.md
- Release notes de sondas: docs/RELEASE_NOTES_SONDAS.md
- Mapeamento de pinos: docs/MAPEAMENTO_PINOS.md

## Build e Flash (ESP-IDF)

Pre-requisitos:
- ESP-IDF instalado e exportado no ambiente
- Toolchain do ESP32 configurada

Fluxo basico:

```bash
idf.py set-target esp32
idf.py build
idf.py -p <PORTA_SERIAL> flash monitor
```

## Estado Atual

Projeto em evolucao ativa. O foco atual esta em consolidacao de telemetria,
UX de campo e padronizacao para suportar novos tipos de sonda.

## Fluxo de Funcionamento

### Inicializacao

`main/app_main` segue esta ordem:

1. Cria o modelo global e seus mutexes (`app_model_init`).
2. Configura GPIOs locais, ADC do teclado e I2C compartilhado por DS3231/PCF8574.
3. Inicializa o PCF8574; P0..P6 iniciam inativos em LOW (driver ativo HIGH) e P7 fica HIGH sem uso.
4. Inicializa a AT25SF321B e a task serializadora de flash.
5. Carrega lote, curvas de Ambiencia e historico de alarmes, validando seus formatos/CRCs.
6. Cria tasks de teclado, I/O, display, RTC e persistencia de Ambiencia.

Falha ao carregar um registro invalida somente aquele dado: o lote fica inativo ou
as curvas usam os defaults locais. Falha de flash e registrada; sem flash inicializada
a task de persistencia nao e iniciada.

### Tasks

| Task | Periodo/comportamento | Responsabilidade |
|---|---|---|
| `adc_task` | 20 ms | Le ADC, aplica debounce e envia tecla ao modelo. |
| `io_task` | 50 ms | LEDs, buzzer, polling RS-485, controle de Ambiencia, alarmes e escrita do lote. |
| `display_task` | 90 ms | Copia snapshot e desenha HOME/menus no ST7920. |
| `rtc_read_task` | 1 s | Le DS3231 e atualiza relogio/idade do lote. |
| `rtc_write_task` | 100 ms | Consome pedido de escrita do editor de data/hora. |
| `ambience_store_task` | verifica a cada 250 ms | Persiste curvas apos debounce; em falha agenda nova tentativa. |
| `storage_task` | bloqueada na fila | Executa uma operacao SPI por vez e sinaliza o chamador. |

O modelo e o ponto de sincronizacao entre tasks. Escritas passam por setters com
mutex; display/controlador leem um `app_state_snapshot_t` consistente.

### Teclado e navegacao

O ADC e convertido em faixas de tecla em `keyboard.c`. O contador de estabilidade
precisa atingir `KEY_DEBOUNCE_COUNT` antes de confirmar uma mudanca; pressionar e
segurar nao repete o evento. `adc_task` envia o evento a `app_model_process_key`,
que e a maquina de estados da interface.

- HOME + MENU abre o menu principal; UP/DOWN move o cursor e ENTER abre a opcao.
- HOME + LEFT abre a visualizacao direta das sondas.
- Em Ajustes, ENTER abre o submenu; BACK retorna um nivel ou cancela o fluxo.
- O beep de teclas fica em Ajustes > Opcoes > Buzzer; BACK retorna Buzzer -> Opcoes -> Ajustes.
- Em Ambiencia, UP/DOWN seleciona curva/campo, ENTER avanca e BACK retorna.
- Alterar uma curva e sair da lista com BACK confirma/habilita a automacao.
- Sem interacao por 30 s, a interface volta para HOME sem apagar configuracoes.

## Lote e Curvas de Ambiencia

O lote tem tres estados: inativo, agendado (inicio no futuro) e ativo. A idade em
dias e calculada com o DS3231; um lote agendado nao executa controle de saidas.
Finalizar o lote apaga somente o registro do lote, nao as curvas.

Cada uma das cinco curvas contem idade, temperatura/umidade alvo do ar,
temperatura/umidade alvo do solo e quantidade de aspersores, irrigadores e
exaustores. Faixas do editor: idade 0..999 dias; temperaturas -10..60 C em passos
de 0,5 C; umidades 0..100%; contagens 0..2. Idades devem ser estritamente
crescentes. Sem registro salvo, as idades iniciais sao 0, 7, 14, 21 e 28 dias;
metas e contagens comecam em zero, e Ambiencia fica OFF. Nao sao defaults agronomicos.

Entre duas idades, as metas de temperatura/umidade do ar e umidade do solo sao
interpoladas linearmente. Temperatura do solo continua armazenada/editavel, mas
por enquanto e apenas informativa. Contagens de saida nao sao interpoladas: vale
a quantidade do ponto mais recente cuja idade foi atingida; depois do ultimo
ponto, mantem-se o ultimo valor.

## Controle e Alarmes

O controlador `app_ambience_control_update` so pode comandar saídas quando todas
estas condicoes forem verdadeiras:

- O lote esta `APP_LOT_STATUS_ACTIVE` (nao apenas agendado).
- Ambiencia foi editada e confirmada; o indicador da tela mostra ON.
- O monitor de sondas esta ativo e ha leituras validas de sondas instaladas.

O polling atualiza as sondas em background continuamente, exceto durante
instalacao/desinstalacao, que tambem usam a UART RS-485. O controle ambiental
continua restrito as condicoes acima; apenas a coleta de leituras fica ativa
fora da tela Sondas.

Qualquer sonda instalada com leitura valida pode disparar uma condicao. Os limites
sao inclusivos e relativos a meta interpolada:

| Condicao | Disparo | Saidas apos 30 s |
|---|---|---|
| Temperatura do ar alta | `T_ar >= meta + 5 C` | Exaustores configurados |
| Umidade do ar baixa | `U_ar <= meta - 5%` | Aspersores configurados |
| Umidade do ar alta | `U_ar >= meta + 5%` | Exaustores configurados |
| Umidade do solo baixa | `U_solo <= meta - 5%` | Irrigadores configurados |

O alarme visual/sonoro inicia no disparo. Cada condicao tem seu proprio timer;
depois de 30 s ativa a primeira saida configurada. Para cada unidade adicional
de violacao (+1 C ou +1 ponto percentual na direcao da violacao), ativa a proxima
saida, limitado pela contagem da curva. Quando leituras voltam 1 C/1% para dentro
do limite, a condicao e limpa e suas saidas desligam. Se a contagem configurada
for zero, o alarme ainda ocorre, mas nenhum atuador daquele grupo e ligado.

Contagens usam os primeiros N canais de cada grupo: aspersores P0/P1, irrigadores
P2/P3, exaustores P4/P5. P6 e auxiliar sem funcao automatica nesta versao; P7 nao
e utilizado. A saida e active-HIGH conforme `APP_PCF8574_ACTIVE_HIGH`. O PCF8574
nao deve alimentar cargas diretamente: use modulo rele/transistor apropriado.

A HOME tambem mantem alarmes gerais por faixas fixas: temperatura do ar e do solo
de 5..35 C, umidade do ar de 20..90% e umidade do solo de 15..95%. A contagem
mostrada combina essas faixas de monitoramento com o alarme dinamico da Ambiencia;
as faixas fixas nao comandam o PCF8574. Temperatura do solo continua fora do
controle ambiental, mas agora tambem gera registro de alarme.

Durante lote ativo, cada sonda instalada com leitura valida pode gerar um evento
quando uma das quatro grandezas cruza seu limite fixo. As transicoes do controlador
de Ambiencia relativas as metas tambem sao registradas e identificadas como tal.
Cada condicao e salva uma vez enquanto permanecer ativa; depois de normalizar, uma
nova ocorrencia pode ser registrada. O relatorio mostra o tipo do alarme, a sonda,
data/hora e idade do lote. UP/DOWN percorre do evento mais recente ao mais antigo;
BACK retorna ao menu. O setor comporta ate 240 eventos, mantidos mesmo apos
reinicializacao. Ao confirmar o inicio/agendamento de um novo lote, somente o
historico de alarmes e apagado; finalizar o lote nao apaga os registros.

## Protocolo das Sondas

UART1 a 9600 baud usa RS-485 half-duplex (TX GPIO17, RX GPIO16, RE/DE GPIO4).
O frame possui delimitador `0x55`, versao, tamanho, registrador, ID, byte reservado,
CRC16 Modbus e delimitador final. A FSM de instalacao transmite descoberta no ID
broadcast `0xFF`, atribui IDs 1..4 e tenta novamente a atribuicao ate cinco vezes.
Desinstalacao grava o ID virgem em tres ciclos pelos IDs 1..4.

O monitor consulta IDs 1..4, espera ate 100 ms por resposta e repete scans a cada
1,2 s. O parser aceita payload legado de 6 bytes (temperatura do solo espelha a
do ar) e payload atual de 8 bytes (temperatura de solo dedicada). Temperaturas e
umidades sao inteiros escalados por 10 no fio e convertidos para `float` na API.

## Persistencia e Flash

A AT25SF321B fornece 4 MiB, paginas de 256 bytes e setores de 4 KiB. O driver de
baixo nivel (`at25sf321b.c`) divide leituras/escritas por tamanho de transferencia
/ pagina e aguarda BUSY. `app_storage.c` serializa pedidos por fila; as chamadas
de fachada sao sincrona para seu chamador.

| Registro | Setor/endereco | Conteudo |
|---|---|---|
| Alarmes | Antepenultimo setor, `0x3FD000` | SHAL, versao 1, ate 240 eventos de 17 bytes, CRC32 por cabecalho e evento. |
| Curvas | Penultimo setor, `0x3FE000` | SHAM, versao 2, flag ON/OFF, 5 curvas, CRC32. |
| Lote | Ultimo setor, `0x3FF000` | SHLT, versao 1, flag de lote, data/hora, CRC32. |

Cada save de configuracao apaga somente seu setor, grava e relê para verificar.
O historico de alarmes e append-only para evitar apagar o setor em cada evento;
apenas o inicio de novo lote limpa `0x3FD000`. Curvas sao gravadas 1,5 s apos a
ultima edicao; erro gera nova tentativa. Ao finalizar lote, apenas `0x3FF000` e
apagado. O restante da flash permanece disponivel.

## Catalogo de `components/app_io`

Todos os arquivos de `app_io` e suas responsabilidades:

| Arquivo | Responsabilidade / API |
|---|---|
| `CMakeLists.txt` | Registra fontes do componente e dependencias ESP-IDF (`driver`, `freertos`, `u8g2`). |
| `app_model.h` | Enums, structs publicas, snapshot e contratos do modelo. |
| `app_model.c` | Mutex/snapshot, navegacao, edicao de lote/data/Ambiencia, eventos e helpers de calendario. |
| `display_task.h` | Contrato da task de renderizacao. |
| `display_task.c` | HOME, menus, curvas, sondas, callback u8g2/GPIO e task de refresh. |
| `io_task.h` | Contratos das tasks de I/O e ADC. |
| `io_task.c` | Orquestracao de LEDs/buzzer, sondas, controlador ambiental e pedido de persistencia do lote. |
| `keyboard.h` | API de inicializacao e polling do teclado. |
| `keyboard.c` | Faixas ADC, debounce de 3 amostras e evento unico por tecla estavel. |
| `rtc_task.h` | Contratos das tasks de leitura/escrita do RTC. |
| `rtc_task.c` | Conversao BCD, DS3231 e tasks periodica/sob demanda. |
| `probe_install.h` | API publica para instalacao, desinstalacao, monitor e parser. |
| `probe_install.c` | CRC/framing UART, half-duplex, FSMs de instalacao/desinstalacao e polling. |
| `app_pin_config.h` | Mapa de GPIO, ADC, I2C, SPI e polaridade do PCF8574. |
| `app_pin_config.c` | Configuracao dos GPIOs locais, ADC e barramento I2C. |
| `app_pcf8574.h` | Enum de saidas P0..P6 e API de escrita eletrica. |
| `app_pcf8574.c` | Mutex e escrita atomica do byte do PCF8574 no I2C compartilhado. |
| `app_ambience_control.h` | API de um ciclo de avaliacao do controlador. |
| `app_ambience_control.c` | Interpolacao, limites, timers, histerese, escalonamento e acionamento das saidas. |
| `app_ambience_store.h` | API para carregar/gravar curvas e worker de persistencia. |
| `app_ambience_store.c` | Formato SHAM, validacao, CRC32, setor de curvas e debounce/retry. |
| `app_alarm_monitor.h` / `.c` | Limites fixos e deteccao de novas ocorrencias por sonda durante lote ativo. |
| `app_alarm_store.h` / `.c` | Formato SHAL, CRC32, historico append-only e cache de eventos em RAM. |
| `app_lot_store.h` | API de carga, gravacao e limpeza do lote. |
| `app_lot_store.c` | Formato SHLT, validacao de calendario, CRC32 e setor do lote. |
| `app_storage.h` | API sincrona da fila serializadora de flash. |
| `app_storage.c` | Queue/task de armazenamento e sincronizacao de pedidos. |
| `at25sf321b.h` | Capacidade, pagina/setor e API do chip AT25SF321B. |
| `at25sf321b.c` | Comandos SPI de baixo nivel, pagina, status BUSY, leitura e erase. |

## Referencia de Funcoes

Funcoes publicas e helpers principais por arquivo-fonte:

- `app_model.c`: `app_model_init`, `app_model_set_adc_raw`, `app_model_get_adc_raw`,
  `app_model_set_datetime`, `app_model_set_lot_state`,
  `app_model_take_lot_write_request`, `app_model_take_rtc_write_request`,
  `app_model_get_snapshot`, `app_model_process_key`,
  `app_model_set_key_press_flag`, `app_model_take_key_press_flag`,
  `app_model_is_key_beep_enabled`, `app_model_is_probe_install_screen_active`,
  `app_model_is_probe_uninstall_screen_active`, `app_model_is_probe_list_screen_active`,
  `app_model_set_probe_install_status`, `app_model_set_probe_uninstall_status`,
  `app_model_set_probe_snapshot`, `app_model_reset_probe_install_data`,
  `app_model_exit_probe_uninstall_to_menu`, `app_model_set_ambience_curves`,
  `app_model_take_ambience_write_request`, `app_model_request_ambience_write`,
	`app_model_set_ambience_alarm_active`, `app_model_check_inactivity_timeout`.
	Helpers: `lock_model`, `unlock_model`, `reset_settings_submenus`,
	`adjust_ambience_field_unlocked`, `reset_probe_install_data_unlocked`,
	`is_leap_year`, `days_in_month`, `date_to_day_count`, `datetime_to_seconds`,
	`normalize_datetime`, `increment_field`, `decrement_field`.
- `display_task.c`: `draw_home`, `draw_main_menu`, `draw_placeholder_screen`,
	`format_probe_value`, `draw_install_progress_indicator`, `draw_probe_rows`,
	`draw_settings_menu`, `draw_about_menu`, `draw_screen`,
	`u8x8_gpio_and_delay_esp32`, `display_task`.
- `io_task.c`: `io_task` (loop de hardware e FSMs) e `adc_task` (entrada/UI).
- `keyboard.c`: `map_adc_to_key`, `keyboard_adc_init`, `keyboard_adc` (raw),
	`keyboard_adc_poll_event` (mapeamento + debounce).
- `probe_install.c`: `uart_write_probe_bytes`, `crc16_modbus`,
	`build_probe_frame`, `build_probe_uninstall_frame`, `select_probe_target_address`,
	`parse_probe_frame`, `uart_read_probe_frame`, `send_probe_discovery`,
	`send_probe_assignment`, `send_probe_uninstall`, `send_probe_read_request`,
	`process_probe_response`, `probe_install_manager_init`, os tres
	`probe_monitor_manager_set_active`, `probe_install_manager_set_active`,
	`probe_uninstall_manager_set_active`, `probe_install_manager_periodic`,
	`probe_monitor_manager_periodic` e `run_uninstall_sequence`.
- `rtc_task.c`: `to_bcd`, `from_bcd`, `weekday_from_date`,
	`ds3231_read_datetime`, `ds3231_write_datetime`, `rtc_read_task`, `rtc_write_task`.
- `app_pin_config.c`: `app_configure_gpio_inputs`, `app_configure_gpio_leds`,
	`app_configure_gpio_buzzer`, `app_configure_lcd_pins`, `app_configure_adc`,
	`app_configure_rtc_i2c`, `app_config_pins`.
- `app_pcf8574.c`: `app_pcf8574_init`, `app_pcf8574_write_output`.
- `app_ambience_control.c`: `curve_for_age`, `interpolate_value`, `targets_for_age`,
	`count_for_deviation`, `set_group_outputs`, `apply_outputs`,
	`app_ambience_control_update`.
- `app_ambience_store.c`: `calculate_crc32`, `curves_are_valid`, `encode_u16`,
  `decode_u16`, `encode_crc`, `decode_crc`, `app_ambience_store_load`,
  `app_ambience_store_save`, `app_ambience_store_task`.
- `app_lot_store.c`: `is_leap_year`, `days_in_month`, `datetime_is_valid`,
  `calculate_crc32`, `encode_crc`, `decode_crc`, `app_lot_store_load`,
  `app_lot_store_save`, `app_lot_store_clear`.
- `app_storage.c`: `storage_task`, `submit_request`, `app_storage_init`,
  `app_storage_read`, `app_storage_write`, `app_storage_erase_sector`.
- `at25sf321b.c`: `transmit_bytes`, `write_enable`, `read_status`, `wait_until_ready`,
  `encode_address`, `address_range_is_valid`, `at25sf321b_init`,
  `at25sf321b_deinit`, `at25sf321b_read`, `at25sf321b_write`,
  `at25sf321b_erase_sector`.

Os headers pareados declaram as APIs e structs correspondentes. Comentarios nos
`.c` explicam contratos, unidades, invariantes e motivos dos blocos de controle;
linhas triviais ficam sem narracao duplicada.
