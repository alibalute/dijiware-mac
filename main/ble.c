/**
 * @file ble.c
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief 
 * @version 0.1
 * @date 2022-05-27
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "ble.h"
// #include "driver/gpio.h"
#include "interfaces.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define TAG "BLE"

static gpio_num_t btLed = -1;

extern void handleMidiMessage(uint8_t midi_status, uint8_t *remaining_message, size_t len, size_t continued_sysex_pos);
extern char appVersion[];

static bool bleConnected = false;

/* Private BLE status codes for app telemetry (carried over BLE-MIDI characteristic):
 * 0x57 -> firmware major, 0x58 -> minor, 0x59 -> patch. */
static void sendFirmwareVersion(void) {
  unsigned major = 0, minor = 0, patch = 0;
  if (sscanf(appVersion, "%u.%u.%u", &major, &minor, &patch) != 3) {
    ESP_LOGW(TAG, "Could not parse appVersion '%s'", appVersion);
    return;
  }
  if (major > 127) major = 127;
  if (minor > 127) minor = 127;
  if (patch > 127) patch = 127;

  uint8_t msg[3];
  msg[2] = 0;

  msg[0] = 0x57; msg[1] = (uint8_t)major; blemidi_send_message(0, msg, sizeof(msg));
  msg[0] = 0x58; msg[1] = (uint8_t)minor; blemidi_send_message(0, msg, sizeof(msg));
  msg[0] = 0x59; msg[1] = (uint8_t)patch; blemidi_send_message(0, msg, sizeof(msg));
  ESP_LOGI(TAG, "Sent FW version %u.%u.%u over BLE", major, minor, patch);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// This task is periodically called to send a MIDI message
////////////////////////////////////////////////////////////////////////////////////////////////////
void task_ble_midi(void *pvParameters) {
  bool prevConnected = false;
  TickType_t connectTick = 0;
  bool versionSentForSession = false;
  TickType_t lastVersionTxTick = 0;

  while( 1 ) {
    // vTaskDelayUntil(&xLastExecutionTime, pdMS_TO_TICKS(500));

    blemidi_tick(); // for timestamp and output buffer handling

    if (bleConnected && !prevConnected) {
      connectTick = xTaskGetTickCount();
      lastVersionTxTick = connectTick;
      versionSentForSession = false;
    } else if (!bleConnected) {
      versionSentForSession = false;
    }
    prevConnected = bleConnected;

    /* Give app a moment to register notification handler after connect. */
    if (bleConnected && !versionSentForSession &&
        (xTaskGetTickCount() - connectTick) >= pdMS_TO_TICKS(1200)) {
      sendFirmwareVersion();
      lastVersionTxTick = xTaskGetTickCount();
      versionSentForSession = true;
    }

    /* Keep publishing version occasionally so apps that subscribe later still receive it. */
    if (bleConnected &&
        (xTaskGetTickCount() - lastVersionTxTick) >= pdMS_TO_TICKS(5000)) {
      sendFirmwareVersion();
      lastVersionTxTick = xTaskGetTickCount();
    }

    vTaskDelay(pdMS_TO_TICKS(10)); //100 makes it laggy, 1 increases chance of freezing
  
#if 0
    unsigned ctr = 0;
    ctr += 1;
    ESP_LOGI(TAG, "Sending MIDI Note #%u", ctr);

    {
      // TODO: more comfortable packet creation via special APIs
      uint8_t message[3] = { 0x90, 0x3c, 0x7f };
      blemidi_send_message(0, message, sizeof(message));
    }
    
    vTaskDelayUntil(&xLastExecutionTime, 500 / portTICK_RATE_MS);

    blemidi_tick(); // for timestamp and output buffer handling

    {
      // TODO: more comfortable packet creation via special APIs
      uint8_t message[3] = { 0x90, 0x3c, 0x00 };
      blemidi_send_message(0, message, sizeof(message));
    }
#endif
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// This callback is called whenever a new MIDI message is received
////////////////////////////////////////////////////////////////////////////////////////////////////
void callback_midi_message_received(uint8_t blemidi_port, uint16_t timestamp, uint8_t midi_status, uint8_t *remaining_message, size_t len, size_t continued_sysex_pos)
{
  ESP_LOGI(TAG, "CALLBACK blemidi_port=%d, timestamp=%d, midi_status=0x%02x, len=%d, continued_sysex_pos=%d, remaining_message:", blemidi_port, timestamp, midi_status, len, continued_sysex_pos);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, remaining_message, len, ESP_LOG_DEBUG);

  /* handleMidiMessage routes channel messages to UART/synth and, for status 0xfe only, calls handleMessage(code, data).
   * Do not interpret raw payload bytes here: SysEx (0xf0/0xf7) and other statuses must not be treated as (code,data)
   * or they trigger e.g. strumCalibrate (0x02) from app packet data and cause freezes. */
  handleMidiMessage(midi_status, remaining_message, len, continued_sysex_pos);

  return;
  ////!!!! the following chunk is never reached and should be removed but leave it for the time being
  // loopback received message
  {
    // TODO: more comfortable packet creation via special APIs

    // Note: by intention we create new packets for each incoming message
    // this shows that running status is maintained, and that SysEx streams work as well
    
    if( midi_status == 0xf0 && continued_sysex_pos > 0 ) {
      blemidi_send_message(0, remaining_message, len); // just forward
    } else {
      size_t loopback_message_len = 1 + len; // includes MIDI status and remaining bytes
      uint8_t *loopback_message = (uint8_t *)malloc(loopback_message_len * sizeof(uint8_t));
      if( loopback_message == NULL ) {
        // no memory...
      } else {
        loopback_message[0] = midi_status;
        memcpy(&loopback_message[1], remaining_message, len);

        blemidi_send_message(0, loopback_message, loopback_message_len);

        free(loopback_message);
      }
    }
  }
}

void connectionCB(bool connected) {
  ESP_LOGI(TAG, "BLE %sconnected", connected ? "" : "dis");
  setLed(btLed, connected);
  bleConnected = connected;
}

void start_ble_midi_task(gpio_num_t _btLed)
{
  btLed = _btLed;
  // install BLE MIDI service
  int status = blemidi_init(connectionCB, callback_midi_message_received);
  if( status < 0 ) {
    ESP_LOGE(TAG, "BLE MIDI Driver returned status=%d", status);
  } else {
    ESP_LOGI(TAG, "BLE MIDI Driver initialized successfully");
    xTaskCreate(task_ble_midi, "task_ble_midi", 4096, NULL, 1 , NULL); //increased priority from 8 to 1
  }

#if 0
  // disable this for less debug messages (avoid packet loss on loopbacks)
  esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
}

bool isBLEConnected(void) { return bleConnected; }