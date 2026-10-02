#ifndef APP_PIN_CONFIG_H
#define APP_PIN_CONFIG_H

#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "esp_err.h"

// ------------------------------
// Mapa de pinos da aplicacao
// ------------------------------
// Dica: se mudar qualquer fio no hardware, ajuste primeiro este arquivo.

//INPUTS
#define APP_IN_1_PIN            GPIO_NUM_34
#define APP_IN_2_PIN            GPIO_NUM_35

//OUTPUTS
#define APP_OUT_1_PIN            GPIO_NUM_32
#define APP_OUT_2_PIN            GPIO_NUM_33

// LEDs
#define APP_LED_1_PIN            GPIO_NUM_15
#define APP_LED_2_PIN            GPIO_NUM_2

// Buzzer
#define APP_BUZZER_PIN           GPIO_NUM_13

// ------------------------------
// Configuração de LCD pins
// ------------------------------
// Display ST7920 (SPI por software via biblioteca u8g2)
#define APP_LCD_PIN_CS           GPIO_NUM_5
#define APP_LCD_PIN_DATA         GPIO_NUM_23
#define APP_LCD_PIN_CLK          GPIO_NUM_18
#define APP_LCD_PIN_RST          GPIO_NUM_22

// ------------------------------
// Configuração de ADC
// ------------------------------
// Teclado analogico ligado no GPIO36 (ADC1_CHANNEL_0)
// ADC_WIDTH_BIT_12 => faixa 0..4095.
// ADC_ATTEN_DB_11  => amplia faixa de tensao de entrada util.
#define APP_ADC_WIDTH            ADC_WIDTH_BIT_12
#define APP_ADC_ATTEN            ADC_ATTEN_DB_11
#define APP_ADC_KEYBOARD_CHANNEL ADC1_CHANNEL_0

// ------------------------------
// Configuracao de PWM (LEDC)
// ------------------------------
// APP_PWM_MAX_DUTY depende da resolucao de duty.
// Com 10 bits: 2^10 - 1 = 1023.
#define APP_PWM_MODE             LEDC_LOW_SPEED_MODE
#define APP_PWM_TIMER            LEDC_TIMER_0
#define APP_PWM_CHANNEL_BUZZER   LEDC_CHANNEL_0
#define APP_PWM_DUTY_RES         LEDC_TIMER_10_BIT
#define APP_PWM_FREQUENCY_HZ     2000
#define APP_PWM_MAX_DUTY         ((1U << 10) - 1U)

// Inicializa GPIOs de saida para LEDs.
void app_configure_gpio_outputs(void);
void app_configure_gpio_inputs(void);

void app_configure_gpio_leds(void);
void app_configure_gpio_buzzer(void);

// Inicializa GPIOs dedicados ao display ST7920.
void app_configure_lcd_pins(void);

// Inicializa ADC1 com canal configurado para leitura do teclado analogico.
void app_configure_adc(void);

void app_config_pins(void);

// Inicializa PWM (LEDC) para controle do buzzer.
/*esp_err_t app_configure_pwm(void);*/

// Ajusta duty-cycle do buzzer no intervalo [0, APP_PWM_MAX_DUTY].
/*esp_err_t app_set_buzzer_duty(uint32_t duty);*/

#endif