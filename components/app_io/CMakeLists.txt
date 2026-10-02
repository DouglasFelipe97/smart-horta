
idf_component_register(
    SRCS "app_pin_config.c" "io_task.c" "keyboard.c" "app_model.c" "display_task.c"             #Arquivos a ser APONTADOS pelo CMAKE para a compilação
    INCLUDE_DIRS "."                                #Diretório que se encontra (""."" na raiz da pasta)
    REQUIRES driver esp_timer freertos u8g2         #Arquivos internos que devem ser compilados.
)