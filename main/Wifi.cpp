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
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "WebServer.h"

/* FreeRTOS event group to signal when we are connected*/

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_READY BIT0

static const char *TAG = "Wifi";

/** Avoid double esp_netif_init / esp_wifi_init / duplicate handlers on repeat OTA WiFi sessions. */
static bool s_wifi_stack_inited = false;

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
  if (s_wifi_stack_inited) {
    return true;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
    return false;
  }
  esp_netif_create_default_wifi_ap();

  wifi_event_group = xEventGroupCreate();
  if (wifi_event_group == NULL) {
    ESP_LOGE(TAG, "xEventGroupCreate failed");
    return false;
  }

  esp_event_handler_instance_t instance_any_id{};
  esp_event_handler_instance_t instance_got_ip{};
  err = esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &instance().event_handler, NULL,
      &instance_any_id);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WIFI_EVENT handler register failed: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &instance().event_handler, NULL,
      &instance_got_ip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "IP_EVENT handler register failed: %s", esp_err_to_name(err));
    return false;
  }

  wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&wifiConfig);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
    return false;
  }
  s_wifi_stack_inited = true;
  ESP_LOGI(TAG, "WiFi stack initialized (first use)");
  return true;
}

/**
 * @brief start wifi
 *
 * @return true
 * @return false
 */
bool Wifi::start() {
  wifi_config_t wifiAPConfig = {};
  wifiAPConfig.ap.ssid_len = 0;
  wifiAPConfig.ap.channel = 1;
  wifiAPConfig.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  wifiAPConfig.ap.max_connection = 4;
  memset((void *)wifiAPConfig.ap.password, 0, sizeof(wifiAPConfig.ap.password));
  memset((void *)wifiAPConfig.ap.ssid, 0, sizeof(wifiAPConfig.ap.ssid));
  strcpy((char *)wifiAPConfig.ap.password, _apPasswd);
  strcpy((char *)wifiAPConfig.ap.ssid, _apSSID);

  /* Soft-AP HTTP UI at http://192.168.4.1/ (must match app + index.html links). */
  esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap_netif) {
    esp_netif_ip_info_t ip_info;
  memset(&ip_info, 0, sizeof(ip_info));
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_err_t e = esp_netif_dhcps_stop(ap_netif);
    if (e != ESP_OK && e != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
      ESP_LOGW(TAG, "dhcps_stop: %s", esp_err_to_name(e));
    }
    e = esp_netif_set_ip_info(ap_netif, &ip_info);
    if (e != ESP_OK) {
      ESP_LOGE(TAG, "set_ip_info: %s", esp_err_to_name(e));
    }
    e = esp_netif_dhcps_start(ap_netif);
    if (e != ESP_OK) {
      ESP_LOGE(TAG, "dhcps_start: %s", esp_err_to_name(e));
    }
  }

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