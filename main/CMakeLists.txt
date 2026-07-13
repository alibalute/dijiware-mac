idf_component_register(SRCS main.c
  ota_debug.c
  midi.c
  ble.c
  usb.c
  etar.c
  util.c
  pic-midi.c
  adc.c
  spi.c

  #i2c_adc.c
  tla2518.c
  mc3419.c
  vs1103b.c
  mcp23s08.c
  gpio.c
  led.c
  Wifi.cpp
  WebServer.cpp
  FileHandler.cpp
  FirmwareUpdater.cpp
  interfaces.c
  spiffs.c
  #i2c_gui.c
  metronome.c
  cJSON.c
  settings.c
  midi_player.c
  midi_recorder.c
  
  REQUIRES uartmidi blemidi usb tinyusb esp_tinyusb usbmidi esp_adc esp_http_server bootloader_support app_update esp_hw_support spiffs driver mbedtls
  EMBED_TXTFILES "index.html" "images/logo.png"
  
  INCLUDE_DIRS "." )

if(DEFINED ENV{DIJILELE_PRODUCT_ID} AND NOT "$ENV{DIJILELE_PRODUCT_ID}" STREQUAL "")
  if("$ENV{DIJILELE_PRODUCT_ID}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DIJILELE_PRODUCT_ID=1 INST_DIJILELE_M)
  elseif("$ENV{DIJILELE_PRODUCT_ID}" STREQUAL "2")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DIJILELE_PRODUCT_ID=2 INST_DIJILELE_S)
  else()
    message(FATAL_ERROR "DIJILELE_PRODUCT_ID must be 1 (Dijilele M) or 2 (Dijilele S), got: $ENV{DIJILELE_PRODUCT_ID}")
  endif()
endif()
  
  spiffs_create_partition_image(storage ../spiffs_img FLASH_IN_PROJECT)


  

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
