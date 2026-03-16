/**
 * @file Wifi.h
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 WaterlooTI
 *
 */

#ifndef __WIFI_H_
#define __WIFI_H_

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "common.h"
#include "esp_mac.h"

#define WIFI_MAXIMUM_RETRY 5

#ifdef __cplusplus
extern "C" {
#endif

bool wifiInit();
bool wifiStart();
bool wifiStop();

extern bool wifiOn;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

typedef enum {
  INIT,
  DISCONNECTED,
  CONNECTED,
} WifiStatus;

class Wifi {
 public:
  Wifi();
  ~Wifi();

  static Wifi &instance() {
    if (!_instance) {
      _instance = new Wifi();
    }
    return *_instance;
  }

  bool init();
  bool start(void);
  bool stop(void);

  WifiStatus status(void);

  static void event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data);


 private:
  static Wifi *_instance;

  WifiStatus _status = INIT;
  char _apSSID[32] = DIJILELE_AP_SSID; // ESP AP
  char _apPasswd[64] = DIJILELE_AP_PASSWD;
  // wifi_auth_mode_t _authMode = WIFI_AUTH_WPA2_WPA3_PSK;
  wifi_auth_mode_t _apAuthMode = WIFI_AUTH_WPA2_PSK;
  int _retry_num = 0;
};

extern Wifi wifi;

#endif // _cplusplus
#endif  // __WIFI_H_