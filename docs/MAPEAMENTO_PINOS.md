# Mapeamento de Pinos - Smart Horta

Este documento centraliza o mapeamento de GPIOs do firmware.
Fonte de verdade: components/app_io/app_pin_config.h

## Entradas Gerais

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_IN_1_PIN | 34 | Entrada | Entrada digital geral |
| APP_IN_2_PIN | 35 | Entrada | Entrada digital geral |

## Indicadores e Atuadores

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_LED_1_PIN | 15 | Saida | LED 1 |
| APP_LED_2_PIN | 2 | Saida | LED 2 |
| APP_BUZZER_PIN | 13 | Saida | Buzzer |

## Display ST7920 (SPI via software)

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_LCD_PIN_CS | 5 | Saida | Chip Select |
| APP_LCD_PIN_DATA | 23 | Saida | Dados (MOSI por software) |
| APP_LCD_PIN_CLK | 18 | Saida | Clock (SCK por software) |
| APP_LCD_PIN_RST | 32 | Saida | Reset do display |

## RTC DS3231 e PCF8574 (I2C)

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_RTC_I2C_SDA_PIN | 21 | Bidirecional | Linha SDA |
| APP_RTC_I2C_SCL_PIN | 22 | Saida clock | Linha SCL |
| APP_RTC_I2C_PORT | I2C_NUM_0 | Barramento | Porta I2C usada |
| APP_RTC_I2C_FREQ_HZ | 100000 | Configuracao | 100 kHz |
| APP_PCF8574_I2C_ADDR | 0x20 | Endereco I2C | Ajustar conforme A0-A2 do modulo |
| APP_PCF8574_ACTIVE_HIGH | 1 | Polaridade | HIGH aciona; LOW desliga |

| Canal PCF8574 | Equipamento |
|---|---|
| P0 | Aspersor 1 |
| P1 | Aspersor 2 |
| P2 | Irrigador 1 |
| P3 | Irrigador 2 |
| P4 | Exaustor 1 |
| P5 | Exaustor 2 |
| P6 | Saida auxiliar |
| P7 | Nao utilizado; mantido em nivel HIGH |

O PCF8574 usa o mesmo barramento I2C do RTC. O endereco padrao e 0x20
(ajustavel em `app_pin_config.h` conforme A0-A2, faixa 0x20 a 0x27).
As escritas da API representam nivel eletrico (HIGH/LOW). A inicializacao
mantem P0-P6 em LOW (inativos para acionamento ativo HIGH) e P7 em HIGH.
Use estagio de acionamento apropriado (rele/transistor); nao conecte cargas
diretamente aos pinos do PCF8574.

### Controle de ambiencia

As metas de temperatura e umidade sao interpoladas linearmente entre as idades
das curvas; as contagens de saidas usam o ponto mais recente ja atingido. Com
lote ativo, temperatura do ar acima da meta +5 C, umidade do ar fora da faixa
meta +/-5%, ou umidade do solo abaixo da meta -5% inicia alarme imediato. Apos
30 s, sao acionadas as saidas configuradas para a condicao: exaustores para
temperatura alta ou umidade do ar alta, aspersores para umidade do ar baixa e
irrigadores para umidade do solo baixa. Cada grau/ponto percentual adicional
aciona a proxima saida, limitado a quantidade configurada. A recuperacao usa
histerese de 1 C/1%; a temperatura do solo e apenas informativa.

## Memoria flash AT25SF321B (SPI2)

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_FLASH_PIN_MISO | 19 | Entrada | Dados da flash para o ESP32 |
| APP_FLASH_PIN_MOSI | 25 | Saida | Dados do ESP32 para a flash |
| APP_FLASH_PIN_CLK | 26 | Saida | Clock SPI, modo 0 |
| APP_FLASH_PIN_CS | 27 | Saida | Chip Select, ativo em nivel baixo |

Os GPIOs escolhidos nao aparecem em outro uso neste mapa. Confirme a fiação e as
restricoes da placa antes de conectar; os defines ficam em `app_pin_config.h`.

## Registro persistente do lote

A AT25SF321B possui 4 MiB. O ultimo setor de 4 KiB, no endereco `0x3FF000`, fica
reservado para data/hora de inicio e flag do lote. O penultimo setor, em
`0x3FE000`, armazena as cinco curvas de ambiencia com versao e CRC. Ao finalizar
o lote, somente o setor `0x3FF000` e apagado; os demais setores permanecem
disponiveis para futuras configuracoes e relatorios.
As alteracoes das curvas sao gravadas apos 1,5 s sem edicao; as idades iniciais
sao 0, 7, 14, 21 e 28 dias e devem permanecer em ordem crescente. A automacao
so e habilitada depois de editar uma curva e usar BACK na lista de curvas; o
estado ON/OFF tambem e persistido.

## Sondas RS-485 (MAX485)

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| UART TX | 17 | Saida | UART1 para o MAX485 |
| UART RX | 16 | Entrada | UART1 para o MAX485 |
| RE/DE | 4 (D4) | Saida | HIGH transmite; LOW recebe |

## Teclado Analogico (ADC)

| Sinal | Canal | GPIO | Observacao |
|---|---|---:|---|
| APP_ADC_KEYBOARD_CHANNEL | ADC1_CHANNEL_0 | 36 | Leitura do teclado resistivo |
| APP_ADC_WIDTH | ADC_WIDTH_BIT_12 | - | Resolucao 12 bits (0..4095) |
| APP_ADC_ATTEN | ADC_ATTEN_DB_11 | - | Faixa ampliada de entrada |

## PWM (LEDC)

| Sinal | Valor | Observacao |
|---|---|---|
| APP_PWM_MODE | LEDC_LOW_SPEED_MODE | Modo PWM |
| APP_PWM_TIMER | LEDC_TIMER_0 | Timer PWM |
| APP_PWM_CHANNEL_BUZZER | LEDC_CHANNEL_0 | Canal sugerido para buzzer |
| APP_PWM_DUTY_RES | LEDC_TIMER_10_BIT | Resolucao duty |
| APP_PWM_FREQUENCY_HZ | 2000 | Frequencia de 2 kHz |
| APP_PWM_MAX_DUTY | 1023 | Maximo para 10 bits |

## Procedimento de Alteracao de Pinos

1. Alterar os defines em components/app_io/app_pin_config.h.
2. Revisar eventuais restricoes eletricas da placa.
3. Validar boot, display, teclado ADC, RTC I2C e buzzer.
4. Atualizar este documento para manter rastreabilidade.
