/**
 * CORS + Private Network Access for browser-based OTA from the app (Flutter web / WebView).
 * Without these headers, fetch() to http://192.168.4.1 fails from another origin.
 */
#ifndef HTTP_OTA_CORS_H
#define HTTP_OTA_CORS_H

#include "esp_http_server.h"

static inline void httpd_resp_set_cors(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  /* Match typical browser preflight (multipart POST, fetch probes). */
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                     "Content-Type, Accept, Authorization, X-Requested-With");
  /* Chrome PNA: localhost / public page -> 192.168.x.x */
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
}

static inline esp_err_t httpd_resp_send_cors_options(httpd_req_t *req) {
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_set_cors(req);
  httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
  /* ESP-IDF: zero-length body, not NULL */
  return httpd_resp_send(req, "", 0);
}

#endif /* HTTP_OTA_CORS_H */
