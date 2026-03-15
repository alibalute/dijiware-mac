/**
 * @file ota_debug.c
 * @brief Persist reset reason and OTA progress to NVS so we can read them
 *        after a reset (e.g. via GET /api/debug) without serial.
 */

#include "ota_debug.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ota_dbg";
#define NVS_NAMESPACE "sys"
#define KEY_RESET_REASON "reset_reason"
#define KEY_OTA_RUN     "ota_run"
#define KEY_OTA_BYTES   "ota_bytes"
#define KEY_OTA_PHASE   "ota_phase"

void ota_debug_save_reset_reason(uint8_t reason) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open failed for reset_reason");
    return;
  }
  esp_err_t err = nvs_set_u8(h, KEY_RESET_REASON, reason);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs save reset_reason failed: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGD(TAG, "Saved reset_reason=%u to NVS", (unsigned)reason);
}

void ota_debug_save_ota_state(uint8_t running, uint32_t bytes_written) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open failed for ota_state");
    return;
  }
  esp_err_t err = nvs_set_u8(h, KEY_OTA_RUN, running);
  if (err == ESP_OK) {
    err = nvs_set_u32(h, KEY_OTA_BYTES, bytes_written);
  }
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs save ota_state failed: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGD(TAG, "Saved ota_run=%u ota_bytes=%" PRIu32, (unsigned)running, bytes_written);
}

void ota_debug_save_ota_phase(uint8_t phase) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
    return;
  }
  esp_err_t err = nvs_set_u8(h, KEY_OTA_PHASE, phase);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  (void)err;
}

void ota_debug_get_json(char *buf, size_t buf_size) {
  nvs_handle_t h;
  uint8_t reset_reason = 0;
  uint8_t ota_run = 0;
  uint32_t ota_bytes = 0;
  uint8_t ota_phase = 0;

  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
    snprintf(buf, buf_size, "{\"error\":\"nvs_open\"}");
    return;
  }
  if (nvs_get_u8(h, KEY_RESET_REASON, &reset_reason) != ESP_OK) {
    reset_reason = 0;
  }
  if (nvs_get_u8(h, KEY_OTA_RUN, &ota_run) != ESP_OK) {
    ota_run = 0;
  }
  if (nvs_get_u32(h, KEY_OTA_BYTES, &ota_bytes) != ESP_OK) {
    ota_bytes = 0;
  }
  if (nvs_get_u8(h, KEY_OTA_PHASE, &ota_phase) != ESP_OK) {
    ota_phase = 0;
  }
  nvs_close(h);

  const char *reason_str = "unknown";
  switch ((int)reset_reason) {
    case 1: reason_str = "poweron"; break;
    case 2: reason_str = "external"; break;
    case 3: reason_str = "software"; break;
    case 4: reason_str = "panic"; break;
    case 5: reason_str = "int_wdt"; break;
    case 6: reason_str = "task_wdt"; break;
    case 7: reason_str = "wdt"; break;
    case 8: reason_str = "deepsleep"; break;
    case 9: reason_str = "brownout"; break;
    case 10: reason_str = "sdio"; break;
    default: break;
  }

  const char *phase_str = "unknown";
  switch (ota_phase) {
    case 0: phase_str = "started"; break;
    case 1: phase_str = "ota_begin_done"; break;
    case 2: phase_str = "before_first_write"; break;
    case 3: phase_str = "writing"; break;
    case 4: phase_str = "partition_done"; break;
    case 5: phase_str = "first_chunk"; break;
    default: break;
  }

  snprintf(buf, buf_size,
           "{\"reset_reason\":%u,\"reset_reason_str\":\"%s\","
           "\"ota_was_running\":%s,\"ota_last_bytes\":%" PRIu32 ","
           "\"ota_phase\":%u,\"ota_phase_str\":\"%s\"}",
           (unsigned)reset_reason, reason_str,
           ota_run ? "true" : "false",
           ota_bytes,
           (unsigned)ota_phase, phase_str);
}
