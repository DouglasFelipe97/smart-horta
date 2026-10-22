
idf_component_register(
    SRCS "app_pin_config.c" "app_pcf8574.c" "app_ambience_store.c" "app_ambience_control.c" "app_alarm_store.c" "app_alarm_monitor.c" "io_task.c" "keyboard.c" "app_model.c" "display_task.c" "rtc_task.c" "probe_install.c" "at25sf321b.c" "app_storage.c" "app_lot_store.c" "app_settings_store.c" #Arquivos a ser APONTADOS pelo CMAKE para a compilação
    INCLUDE_DIRS "."                                #Diretório que se encontra (""."" na raiz da pasta)
    REQUIRES driver esp_timer freertos u8g2         #Arquivos internos que devem ser compilados.
)