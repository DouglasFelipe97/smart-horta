# Mapeamento de Pinos - Smart Horta

Este documento centraliza o mapeamento de GPIOs do firmware.
Fonte de verdade: components/app_io/app_pin_config.h

## Entradas Gerais

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_IN_1_PIN | 34 | Entrada | Entrada digital geral |
| APP_IN_2_PIN | 35 | Entrada | Entrada digital geral |

## Saidas Gerais

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_OUT_1_PIN | 32 | Saida | Saida digital geral |
| APP_OUT_2_PIN | 33 | Saida | Saida digital geral |

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

## RTC DS3231 (I2C)

| Sinal | GPIO | Direcao | Observacao |
|---|---:|---|---|
| APP_RTC_I2C_SDA_PIN | 21 | Bidirecional | Linha SDA |
| APP_RTC_I2C_SCL_PIN | 22 | Saida clock | Linha SCL |
| APP_RTC_I2C_PORT | I2C_NUM_0 | Barramento | Porta I2C usada |
| APP_RTC_I2C_FREQ_HZ | 100000 | Configuracao | 100 kHz |

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

## Alerta de Conflito Atual

Atualmente ha sobreposicao de pino entre:
- APP_OUT_1_PIN = GPIO32
- APP_LCD_PIN_RST = GPIO32

Esse conflito pode causar comportamento indefinido quando ambos os usos estiverem ativos.

Recomendacao:
1. Definir GPIO exclusivo para APP_OUT_1_PIN ou APP_LCD_PIN_RST.
2. Atualizar components/app_io/app_pin_config.h.
3. Refazer validacao eletrica e funcional apos a mudanca.

## Procedimento de Alteracao de Pinos

1. Alterar os defines em components/app_io/app_pin_config.h.
2. Revisar eventuais restricoes eletricas da placa.
3. Validar boot, display, teclado ADC, RTC I2C e buzzer.
4. Atualizar este documento para manter rastreabilidade.
