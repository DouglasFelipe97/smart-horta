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
