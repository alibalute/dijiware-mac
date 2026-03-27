/**
 * Stub for macOS/host parsing only. usb.h pulls in tinyusb.h which uses
 * section attributes invalid for Mach-O. ESP32 builds never include this.
 */
#ifndef USB_HOST_STUB_H
#define USB_HOST_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PORT_SPEED 115200U

/* ---- Minimal TinyUSB/ESP-IDF symbols for macOS clangd parsing only ---- */
typedef struct {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
} tusb_desc_device_t;

typedef struct {
  const tusb_desc_device_t *device_descriptor;
  const char **string_descriptor;
  const uint8_t *configuration_descriptor;
  bool external_phy;
  bool self_powered;
  int vbus_monitor_io;
} tinyusb_config_t;

#ifndef TUSB_DESC_DEVICE
#define TUSB_DESC_DEVICE 1
#endif
#ifndef TUSB_CLASS_UNSPECIFIED
#define TUSB_CLASS_UNSPECIFIED 0
#endif
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC 0
#endif
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC 0
#endif
#ifndef CFG_TUD_HID
#define CFG_TUD_HID 0
#endif
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI 1
#endif
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR 0
#endif

#ifndef ESP_OK
#define ESP_OK 0
#endif

#ifndef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x) ((void)(x))
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

typedef int esp_log_level_t;
#ifndef ESP_LOG_INFO
#define ESP_LOG_INFO 3
#endif
#ifndef ESP_LOG_DEBUG
#define ESP_LOG_DEBUG 4
#endif
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#endif
#ifndef ESP_LOGD
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#endif
#ifndef ESP_LOGE
#define ESP_LOGE(tag, fmt, ...) ((void)0)
#endif
#ifndef ESP_LOG_BUFFER_HEX_LEVEL
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, len, lvl) ((void)0)
#endif

static inline int tinyusb_driver_install(const tinyusb_config_t *cfg) {
  (void)cfg;
  return ESP_OK;
}
static inline size_t tud_midi_stream_write(int itf, const uint8_t *buffer, size_t len) {
  (void)itf; (void)buffer; return len;
}
static inline size_t tud_midi_available(void) { return 0; }
static inline size_t tud_midi_stream_read(uint8_t *buffer, size_t len) {
  (void)buffer; (void)len; return 0;
}
static inline void vTaskDelay(int ticks) { (void)ticks; }
static inline int xTaskCreate(void (*task)(void *), const char *name, uint32_t stack,
                              void *arg, int priority, void *handle) {
  (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return 0;
}
static inline void esp_log_level_set(const char *tag, esp_log_level_t level) {
  (void)tag; (void)level;
}

extern bool isUSBConnected(void);
extern size_t usb_write_bytes(int itf, const uint8_t *buffer, size_t len);

#endif
