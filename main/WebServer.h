/**
 * @file WebServer.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2021-02-11
 *
 * @copyright (c) 2021, WaterlooTI
 *
 */

#ifndef __SERVER_H
#define __SERVER_H

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "FileHandler.h"
#include "FirmwareUpdater.h"

#define MAX_URIS 20

typedef enum {
  FILE_HANDLER,
  FIRMWARE_HANDLER,
  STORAGE_HANDLER,
  DEBUG_HANDLER,
} HandlerType;

typedef struct {
  const char* name;
  HandlerType handler;
  int numMethods;
  httpd_method_t methods[5];
} Api;

class WebServer {
 public:
  WebServer(bool useFAT = false);
  void start(void);
  void stop(void);
  bool addAliases(void);
  bool addApis(const char* path);
  bool addUris(const char* elements[], int count, const char* path);
  void addUriHandler(httpd_uri_t* uri);
  bool isRunning(void);

 private:
  httpd_handle_t server;
  httpd_config_t webConfig = HTTPD_DEFAULT_CONFIG();
  httpd_uri_t* uris[MAX_URIS];
  char paths[MAX_URIS][40];
  int uriCount = 0;
  bool bRunning = false;
  FileHandler fileHandler;
  FirmwareUpdater firmwareUpdater;
};

#endif