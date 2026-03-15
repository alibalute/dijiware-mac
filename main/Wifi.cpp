/**
 * @file Wifi.cpp
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 WaterlooTI
 *
 */

#include "Wifi.h"
// #include <string>
#include <cstring>
#include "esp_log.h"
#include "WebServer.h"

/* FreeRTOS event group to signal when we are connected*/

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_READY BIT0

static const char *TAG = "Wifi";

static EventGroupHandle_t wifi_event_group;

// bridge C/C++

bool wifiInit() { return wifi.init(); }
bool wifiStart() { return wifi.start(); }
bool wifiStop() { return wifi.stop(); }

WebServer webServer(false);
static bool startServer = false;

/**
 * @brief Construct a new Wifi:: Wifi object
 *
 */
Wifi::Wifi() {}

/**
 * @brief Destroy the Wifi:: Wifi object
 *
 */
Wifi::~Wifi() {}

bool Wifi::init() {
  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_event_group = xEventGroupCreate();

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &instance().event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &instance().event_handler, NULL,
      &instance_got_ip));

  wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
  return (ESP_OK == esp_wifi_init(&wifiConfig));
}

/**
 * @brief start wifi
 *
 * @return true
 * @return false
 */
bool Wifi::start() {
  wifi_config_t wifiAPConfig = {.ap = {
                                    .ssid_len = 0,
                                    .channel = 1,
                                    .authmode = WIFI_AUTH_WPA_WPA2_PSK,
                                    .max_connection = 4,
                                }};
  memset((void *)wifiAPConfig.ap.password, 0, sizeof(wifiAPConfig.ap.password));
  memset((void *)wifiAPConfig.ap.ssid, 0, sizeof(wifiAPConfig.ap.ssid));
  strcpy((char *)wifiAPConfig.ap.password, _apPasswd);
  strcpy((char *)wifiAPConfig.ap.ssid, _apSSID);

  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_config(WIFI_IF_AP, &wifiAPConfig);

  if (ESP_OK != esp_wifi_start()) {
    return false;
  }
  ESP_LOGI(TAG, "wifi started.");

  EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_READY, pdFALSE,
                                         pdFALSE, portMAX_DELAY);

  if (bits & WIFI_READY) {
    webServer.start();
  }
  return true;
}

void Wifi::event_handler(void *arg, esp_event_base_t event_base,
                         int32_t event_id, void *event_data) {
  ESP_LOGI(TAG, "Event base = %s, id = %ld", event_base, event_id);
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
    ESP_LOGD(TAG, "AP Started. Connecting...");
    xEventGroupSetBits(wifi_event_group, WIFI_READY);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
    ESP_LOGI(TAG, "AP stopped");
    xEventGroupClearBits(wifi_event_group, WIFI_READY);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
    ip_event_ap_staipassigned_t *event =
        (ip_event_ap_staipassigned_t *)event_data;
    ESP_LOGI(TAG, "assigned ip " IPSTR " to mac " MACSTR, IP2STR(&event->ip),
             MAC2STR(event->mac));
  }
}

bool Wifi::stop() {
  if (webServer.isRunning()) {
    webServer.stop();
  }
  return (ESP_OK == esp_wifi_stop());
}

WifiStatus Wifi::status() { return instance()._status; }

Wifi *Wifi::_instance = nullptr;

Wifi wifi;