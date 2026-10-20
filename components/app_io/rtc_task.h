#ifndef RTC_TASK_H
#define RTC_TASK_H

// Task leitora: consulta o DS3231 a cada segundo e atualiza o modelo.
void rtc_read_task(void *pvParameters);

// Task escritora: consome pedidos do editor; nao escreve se nada mudou.
void rtc_write_task(void *pvParameters);

#endif
