/**
 * @file FirmwareUpdater.h
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2021-04-08
 *
 * @copyright Copyright (c) 2021 WaterlooTI
 *
 */

#ifndef __FIRMWAREUPDATER_H
#define __FIRMWAREUPDATER_H

#include <stddef.h>
#include <stdint.h>

#if defined(__APPLE__)
/* Stub types for macOS host parsing; ESP-IDF section attributes are invalid for Mach-O. */
typedef struct httpd_req { char _dummy; } httpd_req;
typedef int esp_err_t;
#else
#include "esp_http_server.h"
#endif

#ifndef MIN
#define MIN(a, b) ((a < b) ? a : b)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief decode uploaded data for firmware binary
 *
 * @param content
 * @param ret
 * @param firmwareData
 * @param size
 * @return true
 * @return false
 */
bool parseData(uint8_t* buffer, int ret, uint8_t** firmwareData, size_t* size);

/**
 * @brief initialize parser state
 *
 */
void initParser(void);

#ifdef __cplusplus
}
#endif

class FirmwareUpdater {
public:
  /**
   * @brief Construct a new Firmware Updater object
   *
   */
  FirmwareUpdater();

  /**
   * @brief Handles form file upload and firmware update
   *
   * @param req
   * @return esp_err_t
   */
  esp_err_t handler(httpd_req* req);

  /**
   * @brief Handles form file upload of storage.bin (SPIFFS) to the storage partition
   *
   * @param req
   * @return esp_err_t
   */
  esp_err_t storageHandler(httpd_req* req);

private:
};

#endif