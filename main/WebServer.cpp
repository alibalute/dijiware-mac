/**
 * @file WebServer.cpp
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2021-02-11
 *
 * @copyright (c) 2021, WaterlooTI
 *
 */

#include "WebServer.h"
#include "ota_debug.h"
#include <string.h>

// extern Config config;

static const char* TAG = "http";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t logo_png_start[] asm("_binary_logo_png_start");
extern const uint8_t logo_png_end[] asm("_binary_logo_png_end");

MemFile memFiles[] = {
    {.name = "index.html", .start = index_html_start, .end = index_html_end},
    {.name = "logo.png", .start = logo_png_start, .end = logo_png_end},
};

Alias aliases[] = {
    {
        .uri = "/",
        .page = "/index.html",
        .method = HTTP_GET,
    },
};
int numAliases = sizeof(aliases) / sizeof(Alias);

static esp_err_t debugHandler(httpd_req_t* req) {
  char buf[320];
  ota_debug_get_json(buf, sizeof(buf));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, strlen(buf));
}

Api apis[] = {
    {
        .name = "update",
        .handler = FIRMWARE_HANDLER,
        .numMethods = 1,
        .methods = {HTTP_POST},
    },
    {
        .name = "debug",
        .handler = DEBUG_HANDLER,
        .numMethods = 1,
        .methods = {HTTP_GET},
    },
};
int numApis = sizeof(apis) / sizeof(Api);

static const char* images[] = {
    "logo.png",
};

const char* HTML_PATH = "/";
const char* IMAGE_PATH = "/images";
const char* API_PATH = "/api";

static const int numImages = sizeof(images) / sizeof(char*);

static FileHandler* pFileHandler = NULL;
static FirmwareUpdater* pFirmwareUpdater = NULL;

static esp_err_t fileHandlerCaller(httpd_req_t* req) {
  return pFileHandler->handler(req);
}
static esp_err_t firmwareUpdaterCaller(httpd_req_t* req) {
  return pFirmwareUpdater->handler(req);
}

WebServer::WebServer(bool useFAT)
    : fileHandler(FileHandler(useFAT)) {
  pFileHandler = &fileHandler;
  pFirmwareUpdater = &firmwareUpdater;
}

bool WebServer::addUris(const char* elements[], int count, const char* path) {
  for (int i = 0; i < count && uriCount < MAX_URIS; i++) {
    uris[uriCount] = (httpd_uri_t*)malloc(sizeof(httpd_uri_t));
    if (!uris[uriCount]) {
      ESP_LOGE(TAG, "Memory Low Error");
      return false;
    }
    sprintf(paths[uriCount], "%s/%s", path, elements[i]);
    uris[uriCount]->uri = (const char*)&paths[uriCount];
    uris[uriCount]->method = HTTP_GET;
    uris[uriCount]->handler = fileHandlerCaller;
    uris[uriCount]->user_ctx = (void*)path;
    ESP_LOGD(TAG, "Adding URI %s", paths[uriCount]);
    addUriHandler(uris[uriCount]);
  }
  return true;
}

bool WebServer::addAliases(void) {
  for (int i = 0; i < numAliases && uriCount < MAX_URIS; i++) {
    uris[uriCount] = (httpd_uri_t*)malloc(sizeof(httpd_uri_t));
    if (!uris[uriCount]) {
      ESP_LOGE(TAG, "Memory Low Error");
      return false;
    }
    strcpy(paths[uriCount], aliases[i].uri);
    uris[uriCount]->uri = (const char*)&paths[uriCount];
    uris[uriCount]->method = aliases[i].method;
    uris[uriCount]->handler = fileHandlerCaller;
    uris[uriCount]->user_ctx = NULL;
    ESP_LOGD(TAG, "Adding URI %s", paths[uriCount]);
    addUriHandler(uris[uriCount]);
  }
  return true;
}

bool WebServer::addApis(const char* path) {
  for (int i = 0; i < numApis && uriCount < MAX_URIS; i++) {
    for (int j = 0; j < apis[i].numMethods && uriCount < MAX_URIS; j++) {
      uris[uriCount] = (httpd_uri_t*)malloc(sizeof(httpd_uri_t));
      if (!uris[uriCount]) {
        ESP_LOGE(TAG, "Memory Low Error");
        return false;
      }
      sprintf(paths[uriCount], "%s/%s", path, apis[i].name);
      uris[uriCount]->uri = (const char*)&paths[uriCount];
      uris[uriCount]->uri = (const char*)&paths[uriCount];
      uris[uriCount]->method = apis[i].methods[j];
      switch (apis[i].handler) {
        case FIRMWARE_HANDLER:
          uris[uriCount]->handler = firmwareUpdaterCaller;
          break;
        case DEBUG_HANDLER:
          uris[uriCount]->handler = debugHandler;
          break;
        case FILE_HANDLER:
          [[fallthrough]];
        default:
          uris[uriCount]->handler = fileHandlerCaller;
          break;
      };
      uris[uriCount]->user_ctx = (void*)path;
      ESP_LOGD(TAG, "Adding URI %s (method %d)", paths[uriCount],
               apis[i].methods[j]);
      addUriHandler(uris[uriCount]);
    }
  }
  return true;
}

void WebServer::start(void) {
  webConfig.lru_purge_enable = true;
  webConfig.max_uri_handlers = MAX_URIS;
  ESP_LOGI(TAG, "Starting server on port: '%d'", webConfig.server_port);
  if (httpd_start(&server, &webConfig) == ESP_OK) {
    uriCount = 0;
    ESP_LOGI(TAG, "Registering URI handlers");
    addAliases();
    addApis(API_PATH);
    addUris(images, numImages, IMAGE_PATH);
    bRunning = true;
    ESP_LOGI(TAG, "WebServer started.");
    return;
  }

  ESP_LOGI(TAG, "Error starting server!");
  return;
}

void WebServer::stop(void) {
  httpd_stop(server);
  server = NULL;
  bRunning = false;
}

void WebServer::addUriHandler(httpd_uri_t* _uri) {
  uris[uriCount++] = _uri;
  httpd_register_uri_handler(server, _uri);
}

bool WebServer::isRunning(void) { return bRunning; }
