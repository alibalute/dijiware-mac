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