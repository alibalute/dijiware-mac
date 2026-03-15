/*
 * UART MIDI Driver
 *
 * See README.md for usage hints
 *
 * =============================================================================
 *
 * MIT License
 *
 * Copyright (c) 2020 Thorsten Klose (tk@midibox.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_log.h"
#include <sys/time.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "usbmidi.h"

#define DEBUG_LEVEL ESP_LOG_INFO

static const char *TAG = "USB-MIDI";

#if USBMIDI_ENABLE_CONSOLE
#include "esp_console.h"
#include "argtable3/argtable3.h"
#endif

// Switches UART interface between MIDI and Console
// Has to be configured via Console Command "usbmidi_jumper" - overrule this
// define to predefine a pin w/o console
#ifndef USBMIDI_STORAGE_NAMESPACE
#define USBMIDI_STORAGE_NAMESPACE "USBMIDI"
#endif

#ifndef USBMIDI2_ENABLE_JUMPER_DEFAULT_PIN
#define USBMIDI2_ENABLE_JUMPER_DEFAULT_PIN 0
#endif
static uint8_t usbmidi_enable_jumper = USBMIDI2_ENABLE_JUMPER_DEFAULT_PIN;

// FIFO Sizes
#ifndef USBMIDI_RX_FIFO_SIZE
#define USBMIDI_RX_FIFO_SIZE 256
#endif

#ifndef USBMIDI_TX_FIFO_SIZE
#define USBMIDI_TX_FIFO_SIZE 256
#endif

typedef struct {
  int dev; // if >= 0, interface active

  struct {
    uint32_t last_event_timestamp;
    uint8_t event[3];
    uint8_t running_status;
    uint8_t expected_bytes;
    uint8_t wait_bytes;
    size_t continued_sysex_pos;
  } rx;

  struct {
    uint32_t last_event_timestamp;
    uint8_t running_status; // running status optimization for outgoing streams
                            // not used yet...
  } tx;
} usbmidi_handle_t;

static usbmidi_handle_t usbmidi_handle;

static void (*usbmidi_callback_midi_message_received)(int itf,
    uint8_t midi_status, uint8_t *remaining_message, size_t len,
    size_t continued_sysex_pos) = NULL;

static usbmidi_write_bytes_t usbmidi_write_bytes;
static usbmidi_get_buffered_data_len_t usbmidi_get_buffered_data_len;
static usbmidi_read_bytes_t usbmidi_read_bytes;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal function to initialize the UART handle
////////////////////////////////////////////////////////////////////////////////////////////////////
static int32_t usbmidi_init_handle(usbmidi_handle_t *handle) {
  handle->dev = -1;

  handle->rx.last_event_timestamp = 0;
  handle->rx.running_status = 0;
  handle->rx.expected_bytes = 0;
  handle->rx.wait_bytes = 0;
  handle->rx.continued_sysex_pos = 0;

  handle->tx.last_event_timestamp = 0;
  handle->tx.running_status = 0;

  return 0; // no error
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// A dummy function to prevent debug output
////////////////////////////////////////////////////////////////////////////////////////////////////
static int usbmidi_filter_log(const char *format, __VALIST vargs) { return 0; }

////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialization
////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t usbmidi_init(void *_callback_midi_message_received,
                     usbmidi_write_bytes_t _usbmidi_write_bytes,
                     usbmidi_get_buffered_data_len_t _usbmidi_get_buffered_data_len,
                     usbmidi_read_bytes_t _usbmidi_read_bytes) {
  esp_log_level_set(TAG,
                    DEBUG_LEVEL); // can be changed with the "blemidi_debug on"
                                   // console command
  int i;
  usbmidi_init_handle(&usbmidi_handle);
  // usbmidi_enable_port(0, 31250);

  // Finally install callback
  usbmidi_callback_midi_message_received =
      //usbmidi_receive_message_callback_for_debugging;
      _callback_midi_message_received;
  usbmidi_write_bytes = _usbmidi_write_bytes;
  usbmidi_get_buffered_data_len = _usbmidi_get_buffered_data_len;
  usbmidi_read_bytes = _usbmidi_read_bytes;


  return 0; // no error
                     }

////////////////////////////////////////////////////////////////////////////////////////////////////
// UART Port Enable (mandatory!)
////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t usbmidi_enable_port(int itf, uint32_t baudrate) {
  // usbmidi_init_handle(&usbmidi_handle);

  usbmidi_handle.dev = itf;
  ESP_LOGD(TAG, "MIDI Port %d enabled", itf);
  return 0;  // no error
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// UART Port Disable
////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t usbmidi_disable_port(int itf) {
  usbmidi_handle.dev = -1;
  ESP_LOGD(TAG, "MIDI Port %d disabled", itf);
  return 0; // no error
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns != 0 if UART Port is enabled
////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t usbmidi_get_enabled(int itf) {
  return (usbmidi_handle.dev >= 0) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns the 1 mS based Timestamp
////////////////////////////////////////////////////////////////////////////////////////////////////
static uint64_t get_timestamp_ms() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000 + (tv.tv_usec / 1000)); // 1 mS per increment
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Send a MIDI message
////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t usbmidi_send_message(uint8_t *stream, size_t len) {
  usbmidi_handle_t *handle = &usbmidi_handle;

  if (handle->dev == -1) {
    return -2; // UART not enabled
  }

  //ESP_LOGI(TAG, "send_message len=%d, stream:", len);
  //esp_log_buffer_hex(TAG, stream, len);

  if (usbmidi_write_bytes) {
    usbmidi_write_bytes(handle->dev, stream, len);
    handle->tx.last_event_timestamp = get_timestamp_ms();
  }

  return 0; // no error
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Dummy callback for demo and debugging purposes
////////////////////////////////////////////////////////////////////////////////////////////////////
void usbmidi_receive_message_callback_for_debugging(
    int itf, uint8_t midi_status, uint8_t *remaining_message, size_t len,
    size_t continued_sysex_pos) {
  ESP_LOGI(TAG,
           "[TEST] receive_message CALLBACK interface=%d, midi_status=0x%02x, "
           "len=%d, continued_sysex_pos=%d, remaining_message:",
           itf, midi_status, len, continued_sysex_pos);
  esp_log_buffer_hex(TAG, remaining_message, len);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// For internal usage only: receives a MIDI stream and calls the specified
// callback function. The user will specify this callback while calling
// usbmidi_init()
////////////////////////////////////////////////////////////////////////////////////////////////////
static int32_t usbmidi_receive_stream(int itf,
                                      uint8_t *stream, size_t len) {
  //! Number if expected bytes for a common MIDI event - 1
  const uint8_t midi_expected_bytes_common[8] = {
      2, // Note On
      2, // Note Off
      2, // Poly Preasure
      2, // Controller
      1, // Program Change
      1, // Channel Preasure
      2, // Pitch Bender
      0, // System Message - must be zero, so that
         // mios32_midi_expected_bytes_system[] will be used
  };

  //! Number if expected bytes for a system MIDI event - 1
  const uint8_t midi_expected_bytes_system[16] = {
      1, // SysEx Begin (endless until SysEx End F7)
      1, // MTC Data frame
      2, // Song Position
      1, // Song Select
      0, // Reserved
      0, // Reserved
      0, // Request Tuning Calibration
      0, // SysEx End

      // Note: just only for documentation, Realtime Messages don't change the
      // running status
      0, // MIDI Clock
      0, // MIDI Tick
      0, // MIDI Start
      0, // MIDI Continue
      0, // MIDI Stop
      0, // Reserved
      0, // Active Sense
      0, // Reset
  };

  usbmidi_handle_t *handle = &usbmidi_handle;

  if (handle->dev == -1) {
    return -2; // USB not enabled
  }

  // ESP_LOGI(TAG, "receive_stream interface=%d, len=%d, stream:", itf,
  //          len);
  // esp_log_buffer_hex(TAG, stream, len);

  uint8_t message_complete = 0; // 1: common event, 2: SysEx message
  size_t pos;
  for (pos = 0; pos < len; ++pos) {
    uint8_t byte = stream[pos];

    if (byte & 0x80) {    // new MIDI status
      if (byte >= 0xf8) { // events >= 0xf8 don't change the running status and
                          // can just be forwarded
        // Realtime messages don't change the running status and can be sent
        // immediately They also don't touch the timeout counter!
        if (usbmidi_callback_midi_message_received) {
          uint16_t dummy = 0; // just in case somebody always expects a pointer
                              // to at least two bytes...
          usbmidi_callback_midi_message_received(itf, byte,
                                                 (uint8_t *)&dummy, 0, 0);
        }
      } else {
        handle->rx.event[0] = byte;
        handle->rx.event[1] = 0x00;
        handle->rx.event[2] = 0x00;
        handle->rx.running_status = byte;
        handle->rx.expected_bytes =
            midi_expected_bytes_common[(byte >> 4) & 0x7];

        if (!handle->rx
                 .expected_bytes) { // System Message, take number of bytes
                                    // from expected_bytes_system[] array
          handle->rx.expected_bytes = midi_expected_bytes_system[byte & 0xf];

          if (byte == 0xf0) {
            handle->rx.continued_sysex_pos = 0;
            // we will notify with the following bytes
          } else if (byte == 0xf7) {
            handle->rx.continued_sysex_pos += 1;
            message_complete = 1; // -> forward to caller
          } else if (!handle->rx.expected_bytes) {
            // e.g. tune request (with no additional byte)
            message_complete = 1; // -> forward to caller
          }
        }

        handle->rx.wait_bytes = handle->rx.expected_bytes;
        handle->rx.last_event_timestamp = get_timestamp_ms();
      }
    } else {
      if (handle->rx.running_status == 0xf0) {
        message_complete = 2; // SysEx message
        handle->rx.last_event_timestamp = get_timestamp_ms();
      } else { // Common MIDI message or 0xf1 >= status >= 0xf7
        if (!handle->rx.wait_bytes) {
          // received new MIDI event with running status
          handle->rx.wait_bytes = handle->rx.expected_bytes - 1;
        } else {
          --handle->rx.wait_bytes;
        }

        if (handle->rx.expected_bytes == 1) {
          handle->rx.event[1] = byte;
        } else {
          if (handle->rx.wait_bytes)
            handle->rx.event[1] = byte;
          else
            handle->rx.event[2] = byte;
        }

        if (!handle->rx.wait_bytes) {
          handle->rx.event[0] = handle->rx.running_status;
          // midix->package.evnt1 = // already stored
          // midix->package.evnt2 = // already stored
          message_complete = 1; // -> forward to caller
        }
      }
    }

    if (message_complete == 1) {
      message_complete = 0;

      if (handle->rx.running_status != 0xf0 &&
          handle->rx.running_status != 0xf7) {
        handle->rx.continued_sysex_pos = 0;
      }

      if (usbmidi_callback_midi_message_received) {
        usbmidi_callback_midi_message_received(
            itf, handle->rx.event[0], (uint8_t *)&handle->rx.event[1],
            handle->rx.expected_bytes, handle->rx.continued_sysex_pos);
      }
    } else if (message_complete ==
               2) { // special treatment for SysEx: forward as much as possible
                    // until 0xf7 or any other status byte
      message_complete = 0;

      int bytes_forwarded = 0;
      for (; pos < len && stream[pos] < 0x80; ++pos, ++bytes_forwarded)
        ;

      if (usbmidi_callback_midi_message_received) {
        usbmidi_callback_midi_message_received(
            itf, handle->rx.event[0],
            (uint8_t *)&stream[pos - bytes_forwarded], bytes_forwarded,
            handle->rx.continued_sysex_pos);
      }

      handle->rx.continued_sysex_pos += bytes_forwarded;

      if (pos < len &&
          stream[pos] >=
              0x80) { // special case: 0xf7 or another status byte has been
                      // received: we want the parser to process it!
        --pos;
      }
    }
  }

  return 0; // no error
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// This function should be called each mS to service incoming MIDI messages
////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t usbmidi_tick(void) {
  uint32_t now = get_timestamp_ms();
  uint8_t buffer[USBMIDI_RX_FIFO_SIZE];

  usbmidi_handle_t *handle = &usbmidi_handle;
  if (handle->dev >= 0 && usbmidi_get_buffered_data_len && usbmidi_read_bytes) {
    size_t len = 0;
    usbmidi_get_buffered_data_len(handle->dev, (size_t *)&len);
    if (len > 0) {
      if (len >= USBMIDI_RX_FIFO_SIZE)
        len = USBMIDI_RX_FIFO_SIZE;

      len = usbmidi_read_bytes(
          handle->dev, buffer, len);

      usbmidi_receive_stream(handle->dev, buffer, len);
    }
    
    // timeout handling
    if (handle->rx.wait_bytes &&
        (int32_t)(now - (handle->rx.last_event_timestamp +
                         USBMIDI_RX_TIMEOUT_MS)) > 0) {
      ESP_LOGE(TAG, "Timeout detected on interface=%d", handle->dev);
      // TODO: we need a timeout callback?

      // at least we should clear the status
      handle->rx.running_status = 0;
      handle->rx.wait_bytes = 0;
    }
  }

  ESP_LOGD(TAG, "OK");
  return 0; // no error
}

#if USBMIDI_ENABLE_CONSOLE
////////////////////////////////////////////////////////////////////////////////////////////////////
// Optional Console Commands
////////////////////////////////////////////////////////////////////////////////////////////////////

static struct {
  struct arg_str *on_off;
  struct arg_end *end;
} usbmidi_debug_args;

static int cmd_usbmidi_debug(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&usbmidi_debug_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, usbmidi_debug_args.end, argv[0]);
    return 1;
  }

  if (strcasecmp(usbmidi_debug_args.on_off->sval[0], "on") == 0) {
    printf("Enable debug messages\n");
    esp_log_level_set(TAG, ESP_LOG_INFO);
  } else {
    printf("Disable debug messages - they can be re-enauartd with "
           "'usbmidi_debug on'\n");
    esp_log_level_set(TAG, ESP_LOG_WARN);
  }

  return 0; // no error
}

static struct {
  struct arg_int *pin;
  struct arg_end *end;
} usbmidi_jumper_args;

static int cmd_usbmidi_jumper(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&usbmidi_jumper_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, usbmidi_jumper_args.end, argv[0]);
    return 1;
  }

#if USBMIDI_NUM_PORTS >= 3
  usbmidi_enable_jumper = usbmidi_jumper_args.pin->ival[0];

  if (usbmidi_enable_jumper > 0) {
    printf("UART%d will be enabled via jumper at pin %d with next power-on "
           "reset.\n",
           USBMIDI_PORT2_DEV, usbmidi_enable_jumper);
  } else {
    printf("UART%d permanently disabled with next power-on reset (no pin "
           "assigned to jumper).\n",
           USBMIDI_PORT2_DEV);
  }

  {
    nvs_handle nvs_handle;
    esp_err_t err;

    err = nvs_open(USBMIDI_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "UART MIDI Configuration can't be stored!");
      return -1;
    }

    err = nvs_set_i32(nvs_handle, "enable_jumper", usbmidi_enable_jumper);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to store enable_jumper!");
      return -1;
    }
  }
#else
  printf("UART0 not available in this application.\n");
#endif

  return 0; // no error
}

void usbmidi_register_console_commands(void) {
  {
    usbmidi_debug_args.on_off =
        arg_str1(NULL, NULL, "<on/off>", "Enables/Disables debug messages");
    usbmidi_debug_args.end = arg_end(20);

    const esp_console_cmd_t usbmidi_debug_cmd = {
        .command = "usbmidi_debug",
        .help = "Enables/Disables UART MIDI Debug Messages",
        .hint = NULL,
        .func = &cmd_usbmidi_debug,
        .argtable = &usbmidi_debug_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&usbmidi_debug_cmd));
  }

  {
    usbmidi_jumper_args.pin =
        arg_int1(NULL, "pin", "<pin>",
                 "GPIO Input connected to a Jumper to enable/disable UART0");
    usbmidi_jumper_args.end = arg_end(20);

    const esp_console_cmd_t usbmidi_jumper_cmd = {
        .command = "usbmidi_jumper",
        .help = "Enables/Disables UART0 via Jumper",
        .hint = NULL,
        .func = &cmd_usbmidi_jumper,
        .argtable = &usbmidi_jumper_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&usbmidi_jumper_cmd));
  }
}

#endif
