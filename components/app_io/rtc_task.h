#ifndef RTC_TASK_H
#define RTC_TASK_H

// Le DS3231 periodicamente (1 segundo) e atualiza o modelo global.
void rtc_read_task(void *pvParameters);

// Escreve no DS3231 somente quando houver pedido do menu de data/hora.
void rtc_write_task(void *pvParameters);

#endif
