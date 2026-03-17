/**
 * @file FirmwareUpdater.cpp
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2021-04-08
 *
 * @copyright Copyright (c) 2021 WaterlooTI
 *
 */

#include "FirmwareUpdater.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if !defined(__APPLE__)
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_app_format.h"
#include "esp_task_wdt.h"
#include "ota_debug.h"
#endif

#if !defined(__APPLE__)

#define BUFFER_SIZE 2048
#define HEADER_ACCUM_MAX 2048
/* Write to flash in small chunks so each esp_ota_write is short; avoids interrupt WDT reset. */
#define OTA_WRITE_CHUNK 128
#define DEBUG_LEVEL ESP_LOG_INFO
#define OTA_RESTART_DELAY_MS 8000

static const char *TAG = "FWupd";
static bool otaStarted = false, otaFailed = false;
static bool binaryFound = false;
static uint8_t buffer[BUFFER_SIZE];
/* Accumulate first chunk(s) so multipart header split across recv() is found (browser uploads). */
static uint8_t header_accum[HEADER_ACCUM_MAX];
static size_t header_accum_len = 0;
/* When we find firmware start in accumulation but consumed only part of recv, rest goes here. */
static uint8_t overflow_buf[BUFFER_SIZE];
static size_t overflow_len = 0;
TaskHandle_t g_hOta;
static int s_ota_bytes_for_debug = 0;

#define OTA_FAIL_REASON_MAX 120
static char s_ota_fail_reason[OTA_FAIL_REASON_MAX];

/* Forward declaration: defined below after runOtaUpload. */
static int findFirmwareStartInBuffer(const char *buf, size_t len);

static void set_ota_fail_reason(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_ota_fail_reason, sizeof(s_ota_fail_reason), fmt, ap);
  va_end(ap);
}

/* On error: set state and return so handler can send fail response. */
static void ota_fail_return(void) {
  otaFailed = true;
  ota_debug_save_ota_state(0, (uint32_t)s_ota_bytes_for_debug);
  ESP_LOGE(TAG, "OTA failed, returning to handler");
  return;
}

/**
 * Run OTA in the handler task so request body read and response send use the same context.
 * Returns when done; on failure sets otaFailed and returns; on success sends message and esp_restart().
 */
static void runOtaUpload(httpd_req_t *req) {
  esp_err_t err;
  /* Never pass 0 to recv; use BUFFER_SIZE so we always read in chunks (browser may omit Content-Length). */
  size_t recv_size = (req->content_len > 0 && req->content_len <= BUFFER_SIZE)
      ? (size_t)req->content_len : BUFFER_SIZE;
  if (recv_size == 0) {
    recv_size = BUFFER_SIZE;
  }
  TaskHandle_t self = xTaskGetCurrentTaskHandle();

  /* No NVS, no partition, no delay – go straight to reading (reduces chance of early crash/restart). */
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t *update_partition = NULL;

  int binary_file_length = 0;
  initParser();
  size_t total = 0;
  bool image_header_was_checked = false;
  while (1) {
    int data_read = httpd_req_recv(req, (char *)buffer, recv_size);
    ESP_LOG_BUFFER_HEXDUMP(TAG, buffer, data_read, ESP_LOG_DEBUG);
    if (data_read < 0 && data_read != HTTPD_SOCK_ERR_TIMEOUT) {
      ESP_LOGE(TAG, "Error: HTTP receive error");
      set_ota_fail_reason("Network or connection error during upload.");
      otaFailed = true;
      ota_fail_return();
      return;
    } else if (data_read == 0) {
      break;  // we're done
    } else if (data_read == HTTPD_SOCK_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(10));
    } else if (data_read > 0) {
      /* Do not send any response until OTA is fully done (same as curl: read all, then respond once). */
      /* Prepend any overflow from previous iteration (body bytes we had to defer). */
      if (overflow_len > 0) {
        size_t total = overflow_len + (size_t)data_read;
        if (total <= BUFFER_SIZE) {
          memmove(buffer + overflow_len, buffer, (size_t)data_read);
          memcpy(buffer, overflow_buf, overflow_len);
          data_read = (int)total;
          overflow_len = 0;
        } else {
          memcpy(header_accum, overflow_buf, overflow_len);
          memcpy(header_accum + overflow_len, buffer, (size_t)data_read);
          memcpy(buffer, header_accum, BUFFER_SIZE);
          data_read = BUFFER_SIZE;
          overflow_len = total - BUFFER_SIZE;
          memcpy(overflow_buf, header_accum + BUFFER_SIZE, overflow_len);
        }
      }
      total += data_read;
      uint8_t *firmwareData = NULL;
      size_t size = 0;
      if (!binaryFound && header_accum_len + (size_t)data_read <= HEADER_ACCUM_MAX) {
        /* Accumulate so multipart header split across recv() is found (browser uploads). */
        memcpy(header_accum + header_accum_len, buffer, (size_t)data_read);
        header_accum_len += (size_t)data_read;
        int off = findFirmwareStartInBuffer((const char *)header_accum, header_accum_len);
        if (off >= 0) {
          size_t body_len = header_accum_len - (size_t)off;
          memcpy(buffer, header_accum + off, body_len);
          data_read = (int)body_len;
          binaryFound = true;
          ESP_LOGI(TAG, "Found firmware in accumulated header (%u bytes)", (unsigned)body_len);
        } else {
          data_read = 0;  /* skip this chunk, wait for more to find header */
        }
      } else if (!binaryFound && header_accum_len + (size_t)data_read > HEADER_ACCUM_MAX) {
        /* Fill accumulation to limit and search once. */
        size_t to_add = HEADER_ACCUM_MAX - header_accum_len;
        memcpy(header_accum + header_accum_len, buffer, to_add);
        header_accum_len = HEADER_ACCUM_MAX;
        int off = findFirmwareStartInBuffer((const char *)header_accum, header_accum_len);
        if (off >= 0) {
          size_t body_len = header_accum_len - (size_t)off;
          int orig_recv = data_read;
          if (to_add < (size_t)orig_recv) {
            overflow_len = (size_t)orig_recv - to_add;
            memcpy(overflow_buf, buffer + to_add, overflow_len);
          }
          memcpy(buffer, header_accum + off, body_len);
          data_read = (int)body_len;
          binaryFound = true;
          ESP_LOGI(TAG, "Found firmware in accumulated header (%u bytes)", (unsigned)body_len);
        } else {
          ESP_LOGE(TAG, "Multipart header not found in first %u bytes", (unsigned)HEADER_ACCUM_MAX);
          set_ota_fail_reason("Invalid upload format. Use the firmware page and select a .bin file.");
          otaFailed = true;
          ota_fail_return();
        return;
        }
      } else if (parseData(buffer, data_read, &firmwareData, &size) && size > 0) {
        memcpy(buffer, firmwareData, size);
        data_read = (int)size;
      }
      if (data_read <= 0) continue;
      ESP_LOGD(TAG, "Received %u bytes (total = %u)", data_read, total);
      if (image_header_was_checked == false) {
        esp_app_desc_t new_app_info;
        if (data_read > sizeof(esp_image_header_t) +
                            sizeof(esp_image_segment_header_t) +
                            sizeof(esp_app_desc_t)) {
          ota_debug_save_ota_phase(5);  /* first_chunk: got body, about to do version check + esp_ota_begin */
          // check current version with downloading
          memcpy(&new_app_info,
                 &buffer[sizeof(esp_image_header_t) +
                         sizeof(esp_image_segment_header_t)],
                 sizeof(esp_app_desc_t));
          ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

          ota_debug_save_ota_state(1, 0);
          ota_debug_save_ota_phase(0);

          /* Only touch partition now that we have firmware data (avoids early restart). */
          const esp_partition_t *configured = esp_ota_get_boot_partition();
          vTaskDelay(pdMS_TO_TICKS(80));
          const esp_partition_t *running = esp_ota_get_running_partition();
          vTaskDelay(pdMS_TO_TICKS(80));
          if (configured != running) {
            ESP_LOGW(TAG, "Configured boot partition differs from running");
          }
          update_partition = esp_ota_get_next_update_partition(NULL);
          vTaskDelay(pdMS_TO_TICKS(80));
          if (update_partition == NULL) {
            ESP_LOGE(TAG, "No OTA update partition found");
            set_ota_fail_reason("No OTA partition found. Device configuration error.");
            ota_fail_return();
            return;
          }
          ESP_LOGI(TAG, "Writing to partition at offset 0x%lx", (unsigned long)update_partition->address);
          ota_debug_save_ota_phase(4);

          if (self != NULL && esp_task_wdt_add(self) == ESP_OK) {
            ESP_LOGD(TAG, "OTA: subscribed to WDT");
          }

          esp_app_desc_t running_app_info;
          if (esp_ota_get_partition_description(running, &running_app_info) ==
              ESP_OK) {
            ESP_LOGI(TAG, "Running firmware version: %s",
                     running_app_info.version);
          }
          vTaskDelay(pdMS_TO_TICKS(150));  /* between flash reads */

          const esp_partition_t *last_invalid_app =
              esp_ota_get_last_invalid_partition();
          esp_app_desc_t invalid_app_info;
          if (esp_ota_get_partition_description(last_invalid_app,
                                                &invalid_app_info) == ESP_OK) {
            ESP_LOGI(TAG, "Last invalid firmware version: %s",
                     invalid_app_info.version);
          }
          vTaskDelay(pdMS_TO_TICKS(150));  /* between flash reads */

          // check current version with last invalid partition
          if (last_invalid_app != NULL) {
            if (memcmp(invalid_app_info.version, new_app_info.version,
                       sizeof(new_app_info.version)) == 0) {
              ESP_LOGW(TAG, "New version is the same as invalid version.");
              ESP_LOGW(TAG,
                       "Previously, there was an attempt to launch the "
                       "firmware with %s version, but it failed.",
                       invalid_app_info.version);
              ESP_LOGW(
                  TAG,
                  "The firmware has been rolled back to the previous version.");
              set_ota_fail_reason("This firmware version was previously tried and failed to boot. Use a different firmware version.");
              ota_fail_return();
              return;
            }
          }

          if (memcmp(new_app_info.version, running_app_info.version,
                     sizeof(new_app_info.version)) == 0) {
            ESP_LOGW(TAG,
                     "Current running version is the same as a new. We will "
                     "not continue the update.");
            set_ota_fail_reason("The selected firmware is the same version already installed. No update needed.");
            ota_fail_return();
            return;
          }

          image_header_was_checked = true;

          vTaskDelay(pdMS_TO_TICKS(1000));  /* settle before esp_ota_begin (heavy flash) */
          err = esp_ota_begin(update_partition, 0, &update_handle);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            esp_ota_end(update_handle);
            set_ota_fail_reason("Could not start update (%s).", esp_err_to_name(err));
            ota_fail_return();
          return;
          }
          ESP_LOGI(TAG, "esp_ota_begin succeeded");
          vTaskDelay(pdMS_TO_TICKS(500));
          ota_debug_save_ota_phase(1);
        } else {
          ESP_LOGE(TAG, "received package is not fit len");
          esp_ota_end(update_handle);
          set_ota_fail_reason("Firmware file too small or invalid. Select a valid .bin file.");
          ota_fail_return();
          return;
        }
      }
      if (image_header_was_checked && data_read > 0) {
        if (binary_file_length == 0) {
          vTaskDelay(pdMS_TO_TICKS(2000));
          ota_debug_save_ota_phase(2);
        }
        if (binary_file_length == 4096 || binary_file_length == 8192) {
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
        for (size_t off = 0; off < (size_t)data_read; off += OTA_WRITE_CHUNK) {
          size_t n = (off + OTA_WRITE_CHUNK <= (size_t)data_read)
              ? OTA_WRITE_CHUNK
              : ((size_t)data_read - off);
          /* Pause before writes that cross 2KB / 4KB (flash boundaries). */
          if (binary_file_length == 1920 && n >= 128) {
            vTaskDelay(pdMS_TO_TICKS(600));
          }
          if (binary_file_length == 3968 && n >= 128) {
            vTaskDelay(pdMS_TO_TICKS(1500));
          }
          err = esp_ota_write(update_handle, (const void *)(buffer + off), n);
          if (err != ESP_OK) {
            esp_ota_end(update_handle);
            set_ota_fail_reason("Write error during update.");
            ota_fail_return();
            return;
          }
          binary_file_length += (int)n;
          s_ota_bytes_for_debug = binary_file_length;
          if (binary_file_length > 0 &&
              (binary_file_length <= 4096 || (binary_file_length % 4096) == 0)) {
            ota_debug_save_ota_state(1, (uint32_t)binary_file_length);
            ota_debug_save_ota_phase(3);
          }
          if (self != NULL) {
            esp_task_wdt_reset();
          }
          vTaskDelay(pdMS_TO_TICKS(1));
          /* Extra pause right after completing 4KB so flash/cache can settle. */
          if (binary_file_length == 4096) {
            vTaskDelay(pdMS_TO_TICKS(1200));
          }
          if (binary_file_length < 4096) {
            vTaskDelay(pdMS_TO_TICKS(50));
          } else if (binary_file_length < 16384) {
            vTaskDelay(pdMS_TO_TICKS(30));
          }
        }
        ESP_LOGD(TAG, "Written image length %d", binary_file_length);
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  if (self != NULL) {
    esp_task_wdt_delete(self);
  }
  if (!image_header_was_checked || binary_file_length == 0) {
    ESP_LOGE(TAG, "OTA incomplete: no firmware received");
    set_ota_fail_reason("No valid firmware data received. The file may be empty or corrupted.");
    otaFailed = true;
    ota_fail_return();
  return;
  }
  ota_debug_save_ota_state(0, (uint32_t)binary_file_length);
  otaStarted = true;
  ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

  err = esp_ota_end(update_handle);
  if (err != ESP_OK) {
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
      ESP_LOGE(TAG, "Image validation failed, image is corrupted");
      set_ota_fail_reason("Firmware image is corrupted or invalid.");
    } else {
      ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
      set_ota_fail_reason("Update validation failed (%s).", esp_err_to_name(err));
    }
    ota_fail_return();
  return;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!",
             esp_err_to_name(err));
    set_ota_fail_reason("Could not set boot partition (%s).", esp_err_to_name(err));
    ota_fail_return();
  return;
  }

  nvs_handle_t h_nvs;
  err = nvs_open("config", NVS_READWRITE, &h_nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "NVS open OK.");
    err = nvs_set_u8(h_nvs, "updated", 1);
    nvs_commit(h_nvs);
    nvs_close(h_nvs);
  }

  /* Same as curl: send one response only after transfer is complete, then delay and restart. */
  static const char success_html[] =
    "<!DOCTYPE html><html><head><title>OTA Update</title></head><body>"
    "<p style=\"color:green;font-weight:bold;\">Transfer complete successfully.</p>"
    "<p>Turn on the Dijilele after it shuts down...</p></body></html>";
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, success_html, (ssize_t)sizeof(success_html) - 1);
  vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
  ESP_LOGI(TAG, "Restarting device.");
  esp_restart();
  return;
}

/* Find start of binary in multipart body: header must contain form-data, name="firmware", and filename=;
 * body starts after the first \r\n\r\n. Returns offset of first body byte, or -1. */
static int findFirmwareStartInBuffer(const char *buf, size_t len) {
  const char *body = NULL;
  for (size_t i = 0; i + 4 <= len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
      body = buf + i;
      break;
    }
  }
  if (!body) return -1;
  size_t header_len = (size_t)(body - buf);  /* length before \r\n\r\n */
  body += 4;  /* first byte of binary */
  if (header_len > 512) return -1;  /* sanity */
  char header[520];
  if (header_len >= sizeof(header)) return -1;
  memcpy(header, buf, header_len);
  header[header_len] = '\0';
  /* Require all three (browsers may send name/filename in any order) */
  if (!strstr(header, "form-data")) return -1;
  if (!strstr(header, "name=\"firmware\"") && !strstr(header, "name='firmware'")) return -1;
  if (!strstr(header, "filename=")) return -1;
  return (int)(body - buf);
}

bool parseData(uint8_t *buffer, int ret, uint8_t **firmwareData, size_t *size) {
  if (binaryFound) {
    *firmwareData = buffer;
    *size = (size_t)ret;
    return true;
  }

  int off = findFirmwareStartInBuffer((const char *)buffer, (size_t)ret);
  if (off >= 0) {
    ESP_LOGI(TAG, "Found firmware start at offset %d", off);
    *firmwareData = buffer + off;
    *size = (size_t)ret - (size_t)off;
    binaryFound = true;
  }

  return binaryFound;
}

void initParser() {
  binaryFound = false;
  header_accum_len = 0;
  overflow_len = 0;
}

//
// FirmwareUpdater Class
//

FirmwareUpdater::FirmwareUpdater() { esp_log_level_set(TAG, DEBUG_LEVEL); }

esp_err_t FirmwareUpdater::handler(httpd_req *req) {
  switch (req->method) {
    // case HTTP_GET: {
    //   cJSON* currentConfig = getJson();
    //   char* json = cJSON_Print(currentConfig);
    //   ESP_LOGD(TAG, "returning config");
    //   return httpd_resp_send(req, json, strlen(json));
    // }
    case HTTP_POST: {
      ESP_LOGD(TAG, "Receiving %u bytes", req->content_len);

      otaStarted = false;
      otaFailed = false;
      s_ota_fail_reason[0] = '\0';

      /* If no body, send message and return so browser always gets a response. */
      if (req->content_len == 0) {
        static const char no_body[] =
          "<!DOCTYPE html><html><head><title>OTA</title></head><body>"
          "<p>No file or empty upload. Please select a firmware file.</p></body></html>";
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, no_body, (ssize_t)sizeof(no_body) - 1);
        return ESP_OK;
      }

      runOtaUpload(req);

      /* If OTA failed, unregister from WDT and send one response with reason. */
      if (otaFailed) {
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (self != NULL) {
          esp_task_wdt_delete(self);
        }
        static char fail_resp[512];
        const char *reason = (s_ota_fail_reason[0] != '\0')
            ? s_ota_fail_reason
            : "Unknown error.";
        /* Fragment for client to inject into status box (no full document). */
        int n = snprintf(fail_resp, sizeof(fail_resp),
            "<p class=\"ota-fail-title\">Transfer failed.</p><p class=\"ota-fail-reason\">%s</p>",
            reason);
        if (n > 0 && (size_t)n < sizeof(fail_resp)) {
          httpd_resp_set_status(req, "200 OK");
          httpd_resp_set_type(req, "text/html");
          httpd_resp_send(req, fail_resp, (ssize_t)n);
        } else {
          static const char fail_fallback[] =
              "<p class=\"ota-fail-title\">Transfer failed.</p><p class=\"ota-fail-reason\">Unknown error.</p>";
          httpd_resp_set_status(req, "200 OK");
          httpd_resp_set_type(req, "text/html");
          httpd_resp_send(req, fail_fallback, (ssize_t)sizeof(fail_fallback) - 1);
        }
        return ESP_OK;
      }
      /* On success runOtaUpload already sent the response and restarted the device. */
      return ESP_OK;
    } break;
    default:
      httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
      return ESP_FAIL;
  }
}

#else  /* __APPLE__: stubs for macOS host parsing (Mach-O rejects ESP-IDF section attributes) */

bool parseData(uint8_t *buffer, int ret, uint8_t **firmwareData, size_t *size) {
  (void)buffer;
  (void)ret;
  (void)firmwareData;
  (void)size;
  return false;
}

void initParser(void) {}

FirmwareUpdater::FirmwareUpdater() {}

esp_err_t FirmwareUpdater::handler(httpd_req *req) {
  (void)req;
  return 0;
}

#endif  /* !__APPLE__ */
