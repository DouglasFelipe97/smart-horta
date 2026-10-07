#include "app_pin_config.h"

void app_configure_gpio_outputs(void){
    /*Configuraqndo saidas de uso geral*/
    gpio_config_t out_config = {
        .pin_bit_mask = (1ULL << APP_OUT_1_PIN) | (1ULL << APP_OUT_2_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_config);
}

void app_configure_gpio_inputs(void){
    /*Configuraqndo entradas de uso geral*/
    gpio_config_t in_config = {
        .pin_bit_mask = (1ULL << APP_IN_1_PIN) | (1ULL << APP_IN_2_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_config);
}

void app_configure_gpio_leds(void){
    /*Configurando leds de uso geral*/
    gpio_config_t leds_config = {
        .pin_bit_mask = (1ULL << APP_LED_1_PIN) | (1ULL << APP_LED_2_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&leds_config);
}

void app_configure_gpio_buzzer(void){
    /*Configurando buzzer de uso geral*/
    gpio_config_t buzzer_config = {
        .pin_bit_mask = 1ULL << APP_BUZZER_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&buzzer_config);
}

void app_configure_lcd_pins(void){
    /*Configurando pinos usados pelo display em modo saida*/
    gpio_config_t lcd_conf = {
        .pin_bit_mask = (1ULL << APP_LCD_PIN_CS) |
                        (1ULL << APP_LCD_PIN_DATA) |
                        (1ULL << APP_LCD_PIN_CLK) |
                        (1ULL << APP_LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&lcd_conf);

    // Estado inicial seguro para o display ST7920.
    gpio_set_level(APP_LCD_PIN_CS, 0);
    gpio_set_level(APP_LCD_PIN_CLK, 0);
    gpio_set_level(APP_LCD_PIN_DATA, 0);
    gpio_set_level(APP_LCD_PIN_RST, 1);
}

void app_configure_adc(void){
    // Configuracao basica para o ADC1 usado no teclado analogico.
    // Essas duas chamadas definem como o ESP32 ira amostrar o pino.
    adc1_config_width(APP_ADC_WIDTH);
    adc1_config_channel_atten(APP_ADC_KEYBOARD_CHANNEL, APP_ADC_ATTEN);
}

esp_err_t app_configure_rtc_i2c(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = APP_RTC_I2C_SDA_PIN,
        .scl_io_num = APP_RTC_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = APP_RTC_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(APP_RTC_I2C_PORT, &conf);
    if (err != ESP_OK) {
        return err;
    }

    // Se o driver ja estiver instalado, reinstalamos para manter idempotente.
    i2c_driver_delete(APP_RTC_I2C_PORT);
    return i2c_driver_install(APP_RTC_I2C_PORT, conf.mode, 0, 0, 0);
}

void app_config_pins(void){
    app_configure_gpio_outputs();
    app_configure_gpio_inputs();
    app_configure_gpio_leds();
    app_configure_gpio_buzzer();
    app_configure_adc();
}