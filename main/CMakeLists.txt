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
  
  REQUIRES uartmidi blemidi usb tinyusb esp_tinyusb usbmidi esp_adc esp_http_server bootloader_support app_update esp_hw_support spiffs driver
  EMBED_TXTFILES "index.html" "images/logo.png"
  
  INCLUDE_DIRS "." )
  
  spiffs_create_partition_image(storage ../spiffs_img FLASH_IN_PROJECT)


  

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
