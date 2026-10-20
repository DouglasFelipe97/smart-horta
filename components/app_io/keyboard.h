#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "app_model.h"

// Teclado em divisor resistivo: init limpa debounce; poll devolve uma tecla
// somente na transicao para estado estavel e pode retornar a amostra ADC bruta.
void keyboard_adc_init(void);

// Le ADC e retorna tecla reconhecida em borda (evento unico por pressionamento).
// Também devolve o valor bruto lido para debug e mapeamento de telas.
app_key_t keyboard_adc_poll_event(int *raw_value);

#endif