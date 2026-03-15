#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "midi.h"

#define TAG "Midi"

///////////////////////////////////////////////////////////////////////////////////////////////////
// This callback will be called whenever a new MIDI message has been received
////////////////////////////////////////////////////////////////////////////////////////////////////
void uartmidi_receive_message_callback(uint8_t uartmidi_port, uint8_t midi_status, uint8_t *remaining_message, size_t len, size_t continued_sysex_pos)
{
  // enable to print out debug messages
  // ESP_LOGI(TAG, "receive_message CALLBACK uartmidi_port=%d, midi_status=0x%02x, len=%d, continued_sysex_pos=%d, remaining_message:", uartmidi_port, midi_status, len, continued_sysex_pos);
  // esp_log_buffer_hex(TAG, remaining_message, len);

  /* Do not loopback received MIDI to port 0 (synth). TX/RX crosstalk during the boot burst
   * can make us "receive" our own bytes; loopback would re-send them and the synth can
   * interpret them as note-on (e.g. 0xB0 misread as 0x90), causing 2 extra notes before the intro. */
  (void)uartmidi_port;
  (void)midi_status;
  (void)remaining_message;
  (void)len;
  (void)continued_sysex_pos;
#if 0
  // loopback received message (disabled for synth port to avoid spurious notes at boot)
  {
    if( midi_status == 0xf0 && continued_sysex_pos > 0 ) {
      uartmidi_send_message(0, remaining_message, len); // just forward
    } else {
      size_t loopback_packet_len = 1 + len;
      uint8_t *loopback_packet = (uint8_t *)malloc(loopback_packet_len * sizeof(uint8_t));
      if( loopback_packet == NULL ) {
      } else {
        loopback_packet[0] = midi_status;
        memcpy(&loopback_packet[1], remaining_message, len);
        uartmidi_send_message(uartmidi_port, loopback_packet, loopback_packet_len);
        free(loopback_packet);
      }
    }
  }
#endif
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// This task polls for new MIDI messages
////////////////////////////////////////////////////////////////////////////////////////////////////
static void task_uart_midi(void *pvParameters)
{
  uartmidi_init(uartmidi_receive_message_callback);

  // we use uartmidi_port 0 with standard baudrate in this demo
  // this UART is connected to TX pin 17, RX pin 16
  // see also components/uartmidi/uartmidi.c
#if 1
  uartmidi_enable_port(0, 31250); // USB port = -1
#else
  uartmidi_enable_port(1, 115200); // for "Hairless MIDI", connected to Tx Pin 1, RX Pin 3 via USB UART bridge
#endif

  while (1) {

    uartmidi_tick();
    vTaskDelay(pdMS_TO_TICKS(100));

  }
}

void start_uart_midi_task()
{
  /* Priority 7: below audio tasks (8) so they run first; only needs to drain queue every 100ms */
  xTaskCreatePinnedToCore(task_uart_midi, "task_uart_midi", 4096, NULL, 7, NULL , 1);

#if 0
  // disable this for less debug messages (avoid packet loss on loopbacks)
  // Note that they will also influence in case MIDI messages are sent via Pin1/3 (uartmidi_port 1 & 2)
  esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
}

