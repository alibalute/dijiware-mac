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

#include <stdio.h>

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

#define BUFFER_SIZE 2048
/* Write to flash in small chunks so each esp_ota_write is short; avoids interrupt WDT reset. */
#define OTA_WRITE_CHUNK 128
#define DEBUG_LEVEL ESP_LOG_INFO

/* Set to 1 to restart device after successful OTA; 0 to leave running (e.g. to see serial logs). */
#ifndef OTA_AUTO_RESTART
#define OTA_AUTO_RESTART 0
#endif

static const char *TAG = "FWupd";
static bool otaStarted = false, otaFailed = false;
static bool binaryFound = false;
static uint8_t buffer[BUFFER_SIZE];

TaskHandle_t g_hOta;
static int s_ota_bytes_for_debug = 0;

static void __attribute__((noreturn)) task_fatal_error(void) {
  otaFailed = true;
  ota_debug_save_ota_state(0, (uint32_t)s_ota_bytes_for_debug);
  ESP_LOGE(TAG, "Exiting task due to fatal error...");
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  if (self != NULL) {
    esp_task_wdt_delete(self);
  }
  (void)vTaskDelete(NULL);

  while (1) {
    ;
  }
}

void otaTask(void *pvParameter) {
  esp_err_t err;
  httpd_req *req = (httpd_req *)pvParameter;
  size_t recv_size = MIN(req->content_len, BUFFER_SIZE);
  TaskHandle_t self = xTaskGetCurrentTaskHandle();

  /* Subscribe to task WDT so this task is watched; we will feed it in the loop.
   * Otherwise only idle is watched and we starve it during long OTA -> device reset. */
  if (self != NULL && esp_task_wdt_add(self) == ESP_OK) {
    ESP_LOGD(TAG, "OTA task subscribed to WDT");
  }

  /* Idle for 1s so WiFi/HTTP and power settle before we touch NVS or partition. */
  vTaskDelay(pdMS_TO_TICKS(1000));

  ota_debug_save_ota_state(1, 0);
  ota_debug_save_ota_phase(0);

  /* Brief delay before partition/flash ops. */
  vTaskDelay(pdMS_TO_TICKS(300));

  ESP_LOGD(TAG, "OTA Task starting");

  /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t *update_partition = NULL;

  ESP_LOGI(TAG, "Starting OTA");

  const esp_partition_t *configured = esp_ota_get_boot_partition();
  vTaskDelay(pdMS_TO_TICKS(80));
  const esp_partition_t *running = esp_ota_get_running_partition();
  vTaskDelay(pdMS_TO_TICKS(80));

  if (configured != running) {
    ESP_LOGW(TAG,
             "Configured OTA boot partition at offset 0x%08lx, but running from "
             "offset 0x%08lx",
             configured->address, running->address);
    ESP_LOGW(TAG,
             "(This can happen if either the OTA boot data or preferred boot "
             "image become corrupted somehow.)");
  }
  ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08lx)",
           running->type, running->subtype, running->address);

  update_partition = esp_ota_get_next_update_partition(NULL);
  vTaskDelay(pdMS_TO_TICKS(80));
  assert(update_partition != NULL);
  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
           update_partition->subtype, update_partition->address);

  ota_debug_save_ota_phase(4);  /* partition_done: we got past partition ops */

  int binary_file_length = 0;
  initParser();
  size_t total = 0;
  bool image_header_was_checked = false;
  while (1) {
    if (self != NULL) {
      esp_task_wdt_reset();
    }
    int data_read = httpd_req_recv(req, (char *)buffer, recv_size);
    ESP_LOG_BUFFER_HEXDUMP(TAG, buffer, data_read, ESP_LOG_DEBUG);
    if (data_read < 0 && data_read != HTTPD_SOCK_ERR_TIMEOUT) {
      ESP_LOGE(TAG, "Error: HTTP receive error");
      otaFailed = true;
      task_fatal_error();
    } else if (data_read == 0) {
      break;  // we're done
    } else if (data_read == HTTPD_SOCK_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(10));
    } else if (data_read > 0) {
      total += data_read;
      uint8_t *firmwareData = NULL;
      size_t size = 0;
      if (parseData(buffer, data_read, &firmwareData, &size) && size > 0) {
        memcpy(buffer, firmwareData, size);
        data_read = size;
      }
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
              task_fatal_error();
            }
          }

          if (memcmp(new_app_info.version, running_app_info.version,
                     sizeof(new_app_info.version)) == 0) {
            ESP_LOGW(TAG,
                     "Current running version is the same as a new. We will "
                     "not continue the update.");
            task_fatal_error();
          }

          image_header_was_checked = true;

          vTaskDelay(pdMS_TO_TICKS(1000));  /* settle before esp_ota_begin (heavy flash) */
          err = esp_ota_begin(update_partition, 0, &update_handle);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            esp_ota_end(update_handle);
            task_fatal_error();
          }
          ESP_LOGI(TAG, "esp_ota_begin succeeded");
          vTaskDelay(pdMS_TO_TICKS(500));
          ota_debug_save_ota_phase(1);
        } else {
          ESP_LOGE(TAG, "received package is not fit len");
          esp_ota_end(update_handle);
          task_fatal_error();
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
            task_fatal_error();
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
  ota_debug_save_ota_state(0, (uint32_t)binary_file_length);
  otaStarted = true;
  ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

  err = esp_ota_end(update_handle);
  if (err != ESP_OK) {
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
      ESP_LOGE(TAG, "Image validation failed, image is corrupted");
    } else {
      ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
    }
    task_fatal_error();
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!",
             esp_err_to_name(err));
    task_fatal_error();
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
#if OTA_AUTO_RESTART
  ESP_LOGI(TAG, "Prepare to restart system!");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
  return;
#else
  ESP_LOGI(TAG, "OTA success. Device left running (OTA_AUTO_RESTART=0).");
  vTaskDelete(NULL);
  return;
#endif
}

bool parseData(uint8_t *buffer, int ret, uint8_t **firmwareData, size_t *size) {
  if (binaryFound) {
    *firmwareData = buffer;
    *size = (size_t)ret;
    return true;
  }

  char *content = (char *)buffer;
  if (strstr(content,
             "Content-Disposition: form-data; name=\"firmware\"; filename=")) {
    char *firmwareStart = strstr(content, "\r\n\r\n");
    if (firmwareStart) {
      ESP_LOGD(TAG, "Found firmware start");
      *firmwareData = (uint8_t *)firmwareStart + 4;
      *size = (size_t)ret - (*firmwareData - buffer);
      binaryFound = true;
    }
  }

  return binaryFound;
}

void initParser() { binaryFound = false; }

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

      ESP_LOGD(TAG, "Free heap size = %lu (internal = %lu)",
               esp_get_free_heap_size(), esp_get_free_internal_heap_size());
      otaStarted = false;
      otaFailed = false;
      /* Delay before spawning OTA task so connection and system settle (avoids immediate reset when curl connects). */
      vTaskDelay(pdMS_TO_TICKS(1500));
      if (xTaskCreatePinnedToCore(otaTask, "OTA", 8 * 1024, (void *)req, 20,
                                  &g_hOta, 1) == pdTRUE) {
        // if (g_hOta) {
        // esp_task_wdt_add(g_hOta);
        /* Send a simple response */
        while (!otaStarted && !otaFailed) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }
      }
      if (otaStarted && !otaFailed) {
        const char resp[] = "OK";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
      }
      const char resp[] = "FAIL";
      httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    } break;
    default:
      httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
      return ESP_FAIL;
  }
}
