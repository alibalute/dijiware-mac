#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"

void init_spiffs(void);
bool save_settings_to_flash(const char *filename, cJSON *settings);
cJSON *load_settings_from_flash(const char *filename);
int get_numerical_setting(cJSON *settings, const char *key);

/** SPIFFS path for user settings (compile-time: setar vs ukulele). */
const char *settings_json_path(void);
/** SPIFFS path for factory-reset defaults. */
const char *factory_json_path(void);
/** Load user settings; creates instrument defaults if the file is missing. */
cJSON *load_user_settings_from_flash(void);