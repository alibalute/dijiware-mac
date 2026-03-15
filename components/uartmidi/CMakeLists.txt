set(COMPONENT_SRCS "uartmidi.c")
set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_REQUIRES nvs_flash driver console)
register_component()
