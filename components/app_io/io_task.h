#ifndef IO_TASK_H
#define IO_TASK_H

// Task periodica para LEDs, buzzer, sondas, controle ambiental e lote.
void io_task(void *pvParameters);

// Task de leitura/debounce do teclado e despacho de eventos ao modelo.
void adc_task(void *pvParameters);

#endif