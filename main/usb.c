/**
 * @file usb.c
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-27
 *
 * @copyright Copyright (c) 2022 WTI
 *
 */

#include "usb.h"
#include "usbmidi.h"
#include "tinyusb.h"
// #include "tusb_cdc_acm.h"
#include "class/midi/midi_device.h"
#include <stdint.h>
#include <stdbool.h>

#define DEBUG_LEVEL ESP_LOG_INFO

static const char *TAG = "USB";

extern void handleUsbMessage(uint8_t, uint8_t);
extern void handleMidiMessage(uint8_t midi_status, uint8_t *remaining_message, size_t len, size_t continued_sysex_pos);

/* A combination of interfaces must have a unique product id, since PC will save
 * device driver after the first plug. Same VID/PID with different interface e.g
 * MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]       MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
#define USB_PID                                                      \
  (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
   _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

    // static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
    static size_t rx_size = 0;
static bool buffer_read = true;
static bool usbCableConnected = false;

void setUSBCableConnected(bool connected);  /* forward decl for tud_mount_cb / tud_umount_cb */

// void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event);
// void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event);

// tinyusb_config_cdcacm_t acm_cfg = {
//     .usb_dev = TINYUSB_USBDEV_0,
//     .cdc_port = TINYUSB_CDC_ACM_0,
//     .rx_unread_buf_sz = 64,
//     .callback_rx =
//         tinyusb_cdc_rx_callback, // the first way to register a callback
//     .callback_rx_wanted_char = NULL,
//     .callback_line_state_changed = tinyusb_cdc_line_state_changed_callback,
//     .callback_line_coding_changed = NULL};

tusb_desc_device_t midi_descriptor = {.bLength = sizeof(midi_descriptor),
                                      .bDescriptorType = TUSB_DESC_DEVICE,
                                      .bcdUSB = 0x0200,
                                      .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
                                      .bDeviceSubClass = 0x00,
                                      .bDeviceProtocol = 0x00,
                                      .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

                                      .idVendor = 0x303A,
                                      .idProduct = USB_PID,
                                      .bcdDevice = 0x010,

                                      .iManufacturer = 0x01,
                                      .iProduct = 0x02,
                                      .iSerialNumber = 0x03,

                                      .bNumConfigurations = 0x01};

// uint8_t const descriptor_config[] = {
//     // Configuration number, interface count, string index, total length, attribute, power in mA
//     TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
// };
static char *string_descriptors[] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "Balute",             // 1: Manufacturer
    "eTar 2.0",               // 2: Product
    "12345678",           // 3: Serials, should use chip ID
    "",                   // 4: CDC Interface
    "",                   // 5: MSC Interface
    "eTar MIDI",          // 6: MIDI Interface
};

tinyusb_config_t partial_init = {
    .device_descriptor = &midi_descriptor, // Uses default descriptor specified in Menuconfig
    .string_descriptor = &string_descriptors,
    .configuration_descriptor = NULL, // use default descriptor
    .external_phy = false,
};

void tud_mount_cb(void) {
  ESP_LOGI(TAG, "MOUNTED");
  setUSBCableConnected(true);  /* so vibrato and other MIDI go to USB without app sending 0xAE */
}

void tud_umount_cb(void) {
  ESP_LOGI(TAG, "UNMOUNTED");
  setUSBCableConnected(false);
}

/**
 * @brief init USB peripheral
 *
 */
void init_usb_serial()
{
  ESP_ERROR_CHECK(tinyusb_driver_install(&partial_init));
  tusb_init();
  // ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
}

bool isUSBConnected(void) { return usbCableConnected; }

void setUSBCableConnected(bool connected) { usbCableConnected = connected; }

// /**
//  * @brief USB serial RX callback
//  *
//  * @param itf
//  * @param event
//  */
// void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event) {
//   /* initialization */
//   rx_size = 0;

//   if (!buffer_read) {
//     ESP_LOGW(TAG, "Buffer wasn't read -> overwritten");
//   }
//   /* read */
//   esp_err_t ret =
//       tinyusb_cdcacm_read(itf, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
//   if (ret == ESP_OK) {
//     buf[rx_size] = '\0';
//     ESP_LOGI(TAG, "Got data (%d bytes): %s", rx_size, buf);
//   } else {
//     ESP_LOGE(TAG, "Read error");
//   }

//   buffer_read = false;
//   /* write back */
//   tinyusb_cdcacm_write_queue(itf, buf, rx_size);
//   tinyusb_cdcacm_write_flush(itf, 0);
// }

// void tud_midi_rx_cb(uint8_t itf) {
//   /* initialization */
//   rx_size = 0;

//   if (!buffer_read) {
//     ESP_LOGW(TAG, "Buffer wasn't read -> overwritten");
//   }
//   /* read */
//   esp_err_t ret =
//       tud_midi_stream_read(buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
//   if (ret == ESP_OK) {
//     buf[rx_size] = '\0';
//     ESP_LOGI(TAG, "Got data (%d bytes): %s", rx_size, buf);
//   } else {
//     ESP_LOGE(TAG, "Read error");
//   }

//   buffer_read = false;
//   /* write back */
//   tinyusb_cdcacm_write_queue(itf, buf, rx_size);
//   tinyusb_cdcacm_write_flush(itf, 0);
// }

size_t usb_write_bytes(int itf, const uint8_t *buffer, size_t len)
{
  ESP_LOGD(TAG, "USB Write");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, len, ESP_LOG_DEBUG);
  tud_midi_stream_write(itf, buffer, len);
  // size_t written = tinyusb_cdcacm_write_queue(itf, buffer, len);
  // tinyusb_cdcacm_write_flush(itf, 0);
  // ESP_LOGD(TAG, "Wrote %u bytes", written);
  return len; // written;
}

size_t usb_get_buffered_data_len(int itf, size_t *len)
{
  rx_size = tud_midi_available();
  if (rx_size)
  {
    ESP_LOGD(TAG, "Len = %u", rx_size);
  }
  return *len = rx_size;
}

size_t usb_read_bytes(int itf, uint8_t *buffer, size_t len)
{
  size_t rx_size = tud_midi_stream_read(buffer, len);
  ESP_LOGD(TAG, "USB Read");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, rx_size, ESP_LOG_DEBUG);
  return rx_size;

  // size_t size = MIN(len, rx_size);
  // ESP_LOGD(TAG, "Reading %u bytes", size);
  // memcpy((void *)buffer, (void *)buf, size);
  // if (size < rx_size) {
  //   memcpy((void *)buf, (void *)(buf + size), rx_size - size);
  // }
  // rx_size -= size;
  // if (!rx_size) {
  //   buffer_read = true;
  // }
  // return len;
}

// /**
//  * @brief USB changed callback
//  *
//  * @param itf
//  * @param event
//  */
// void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event) {
//   int dtr = event->line_state_changed_data.dtr;
//   int rts = event->line_state_changed_data.rts;
//   ESP_LOGI(TAG, "Line state changed! dtr:%d, rts:%d", dtr, rts);
//   if (dtr && rts) {
//     usbCableConnected = true;
//     usbmidi_enable_port(itf, PORT_SPEED);
//     ESP_LOGI(TAG, "USB Port %d enabled", itf);
//   } else if (!dtr) {
//     usbCableConnected = false;
//     usbmidi_disable_port(itf);
//     ESP_LOGI(TAG, "USB Port %d disabled", itf);
//   }
// }

///////////////////////////////////////////////////////////////////////////////////////////////////
// This callback will be called whenever a new MIDI message has been received
////////////////////////////////////////////////////////////////////////////////////////////////////
void usbmidi_receive_message_callback(int itf, uint8_t midi_status,
                                      uint8_t *remaining_message, size_t len,
                                      size_t continued_sysex_pos)
{
  //enable to print out debug messages
  // ESP_LOGI(TAG,
  //          "receive_message CALLBACK interface=%d, midi_status=0x%02x, "
  //          "len=%d, continued_sysex_pos=%d, remaining_message:",
  //          itf, midi_status, len, continued_sysex_pos);
  // ESP_LOG_BUFFER_HEX_LEVEL(TAG, remaining_message, len, ESP_LOG_DEBUG);

  handleMidiMessage(midi_status, remaining_message, len, continued_sysex_pos);

  // handleUsbMessage(midi_status, remaining_message[0]);
  // for (int i = 2; i < len; i += 3)
  // {
  //   handleUsbMessage(remaining_message[i], remaining_message[i + 1]);
  // }
  return;

  // usbmidi_enable_port(itf, PORT_SPEED);
  // if (len>=2) {
  //   handleUsbMessage(remaining_message[0],remaining_message[1]);
  // }
  // return;

  // loopback received message
  {
    // TODO: more comfortable packet creation via special APIs

    // Note: by intention we create new packets for each incoming message
    // this shows that running status is maintained, and that SysEx streams work
    // as well

    if (midi_status == 0xf0 && continued_sysex_pos > 0)
    {
      usbmidi_send_message(remaining_message, len); // just forward
    }
    else
    {
      size_t loopback_packet_len =
          1 + len; // includes MIDI status and remaining bytes
      uint8_t *loopback_packet =
          (uint8_t *)malloc(loopback_packet_len * sizeof(uint8_t));
      if (loopback_packet == NULL)
      {
        // no memory...
      }
      else
      {
        loopback_packet[0] = midi_status;
        memcpy(&loopback_packet[1], remaining_message, len);

        usbmidi_send_message(loopback_packet, loopback_packet_len);

        free(loopback_packet);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// This task polls for new MIDI messages
////////////////////////////////////////////////////////////////////////////////////////////////////
static void task_usb_midi(void *pvParameters)
{
  ESP_LOGD(TAG, "Starting Midi Task");
  usbmidi_init(usbmidi_receive_message_callback,
               usb_write_bytes, usb_get_buffered_data_len, usb_read_bytes);

  usbmidi_enable_port(0, 31250);

  TickType_t xLastExecutionTime = xTaskGetTickCount();
  unsigned ctr = 0;

  vTaskDelay(pdMS_TO_TICKS(10));

  while (1)
  {
    uint8_t packet[4];
    // while (tud_midi_available()) {
    //   ESP_LOGD(TAG, "Data available");
    //   tud_midi_packet_read(packet);
    // }

    usbmidi_tick();
    vTaskDelay(pdMS_TO_TICKS(10));

#if 0
    ctr += 1;
    ESP_LOGI(TAG, "Sending MIDI Note #%d", ctr);

    {
      // TODO: more comfortable packet creation via special APIs
      uint8_t message[3] = {0x90, 0x3c, 0x7f};
      usbmidi_send_message(0, message, sizeof(message));
    }

    vTaskDelayUntil(&xLastExecutionTime, 500 / portTICK_RATE_MS);

    usbmidi_tick(); // for timestamp and output buffer handling

    {
      // TODO: more comfortable packet creation via special APIs
      uint8_t message[3] = {0x90, 0x3c, 0x00};
      usbmidi_send_message(0, message, sizeof(message));
    }
#endif
  }
}

void start_usb_midi_task()
{
  esp_log_level_set(TAG, ESP_LOG_INFO);
  xTaskCreate(task_usb_midi, "task_usb_midi", 4096, NULL, 8, NULL);

#if 1
  // disable this for less debug messages (avoid packet loss on loopbacks)
  // Note that they will also influence in case MIDI messages are sent via
  // Pin1/3 (usbmidi_port 1 & 2)
#endif
}
