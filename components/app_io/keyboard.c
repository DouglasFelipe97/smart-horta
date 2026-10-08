#include "keyboard.h"

#include "driver/adc.h"
#include "app_pin_config.h"

// -----------------------------------------------------------------------------
// Teclado analogico por divisor resistivo:
// - Cada botao gera uma tensao diferente no pino ADC.
// - Essa tensao vira um valor bruto (raw) entre 0 e 4095 (12 bits).
// - Com limiares, convertemos faixa de ADC em tecla logica.
// -----------------------------------------------------------------------------

// Limiares de exemplo para teclado resistivo (ajuste com base no seu hardware real).
#define KEY_THR_ENTER_MAX   2197
#define KEY_THR_ENTER_MIN   2167
#define KEY_THR_UP_MAX      524
#define KEY_THR_UP_MIN      494
#define KEY_THR_DOWN_MAX    3730
#define KEY_THR_DOWN_MIN    3700
#define KEY_THR_BACK_MAX    1139
#define KEY_THR_BACK_MIN    1109
#define KEY_THR_LEFT_MAX    207 
#define KEY_THR_LEFT_MIN    177
#define KEY_THR_MENU_MAX    45
#define KEY_THR_MENU_MIN    15

// Numero de amostras estaveis para confirmar mudanca.
#define KEY_DEBOUNCE_COUNT  3

typedef struct {
    // Ultima leitura instantanea (ainda sem estabilidade garantida).
    app_key_t last_raw_key;

    // Ultima tecla que ja foi considerada estavel.
    // Serve para emitir evento unico por pressionamento.
    app_key_t stable_key;

    // Quantas leituras seguidas repetiram a mesma tecla.
    int stable_count;
} keyboard_ctx_t;

static keyboard_ctx_t s_kb;

static app_key_t map_adc_to_key(int raw)
{
    // Ordem importa: cada if representa um limite superior de faixa.
    if (raw <= KEY_THR_ENTER_MAX && raw >= KEY_THR_ENTER_MIN) {
        return APP_KEY_ENTER;
    }
    else if (raw <= KEY_THR_UP_MAX && raw >= KEY_THR_UP_MIN) {
        return APP_KEY_UP;
    }
    else if (raw <= KEY_THR_DOWN_MAX && raw >= KEY_THR_DOWN_MIN) {
        return APP_KEY_DOWN;
    }
    else if (raw <= KEY_THR_BACK_MAX && raw >= KEY_THR_BACK_MIN) {
        return APP_KEY_BACK;
    }
    else if (raw <= KEY_THR_LEFT_MAX && raw >= KEY_THR_LEFT_MIN) {
        return APP_KEY_LEFT;
    }
    else if (raw <= KEY_THR_MENU_MAX && raw >= KEY_THR_MENU_MIN) {
        return APP_KEY_MENU;
    }

    return APP_KEY_NONE;
}

void keyboard_adc_init(void)
{
    // Estado inicial sem tecla pressionada.
    s_kb.last_raw_key = APP_KEY_NONE;
    s_kb.stable_key = APP_KEY_NONE;
    s_kb.stable_count = 0;
}

uint32_t keyboard_adc(void){
    int raw = adc1_get_raw(APP_ADC_KEYBOARD_CHANNEL);
    return raw;
}

app_key_t keyboard_adc_poll_event(int *raw_value)
{
    // 1) Faz uma leitura no canal ADC do teclado.
    int raw = adc1_get_raw(APP_ADC_KEYBOARD_CHANNEL);

    // 2) Opcionalmente devolve esse valor para depuracao/mapeamento.
    if (raw_value != NULL) {
        *raw_value = raw;
    }

    // 3) Converte valor analogico em tecla simbolica.
    app_key_t current = map_adc_to_key(raw);

    // 4) Debounce por estabilidade em N leituras consecutivas.
    if (current == s_kb.last_raw_key) {
        if (s_kb.stable_count < KEY_DEBOUNCE_COUNT) {
            s_kb.stable_count++;
        }
    } else {
        // Leitura mudou: reinicia contador de estabilidade.
        s_kb.last_raw_key = current;
        s_kb.stable_count = 0;
    }

    // 5) Evento unico: dispara apenas quando muda para uma tecla estavel.
    //    Exemplo: segurou ENTER por muito tempo -> sai apenas um APP_KEY_ENTER.
    if (s_kb.stable_count >= KEY_DEBOUNCE_COUNT && current != s_kb.stable_key) {
        s_kb.stable_key = current;
        if (current != APP_KEY_NONE) {
            return current;
        }
    }

    return APP_KEY_NONE;
}