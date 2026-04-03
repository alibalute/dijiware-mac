
#include "settings.h"
#include "spiffs.h"



static const char *TAG = "settings";

// // Function to initialize SPIFFS
// void init_spiffs() {
//     esp_vfs_spiffs_conf_t conf = {
//         .base_path = "/spiffs",
//         .partition_label = NULL,
//         .max_files = 5,
//         .format_if_mount_failed = true
//     };

//     esp_err_t ret = esp_vfs_spiffs_register(&conf);
//         if (ret != ESP_OK) {
//             if (ret == ESP_FAIL) {
//                 ESP_LOGE(TAG, "Failed to mount or format filesystem");
//             } else if (ret == ESP_ERR_NOT_FOUND) {
//                 ESP_LOGE(TAG, "Failed to find SPIFFS partition");
//             } else {
//                 ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
//             }
//             return;
//         }
//     ESP_LOGI(TAG, "SPIFFS initialized");
// }

// Function to save settings as a JSON file
// Returns true on success, false on failure.
bool save_settings_to_flash(const char *filename, cJSON *settings) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return false;
    }

    char *json_str = cJSON_Print(settings);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        fclose(file);
        return false;
    }

    fprintf(file, "%s", json_str);

    fflush(file);
    fclose(file);

    ESP_LOGI(TAG, "Settings saved to %s", filename);
    ESP_LOGI(TAG, "Contents:\n%s", json_str);
    cJSON_free(json_str);
    return true;
}

// Function to load settings from a JSON file
cJSON *load_settings_from_flash(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "Settings file not found, creating default");
        /* Create default settings so the file exists and load succeeds */
        cJSON *defaults = cJSON_CreateObject();
        if (defaults != NULL) {
            cJSON_AddNumberToObject(defaults, "instrument", 24);
            cJSON_AddNumberToObject(defaults, "chords", 0);
            cJSON_AddNumberToObject(defaults, "tuning", 0);
            cJSON_AddNumberToObject(defaults, "tapping", 0);
            cJSON_AddNumberToObject(defaults, "transpose", 0);
            cJSON_AddNumberToObject(defaults, "vibrato", 0);
            cJSON_AddNumberToObject(defaults, "leftHand", 0);
            cJSON_AddNumberToObject(defaults, "quarterTones", 0);
            cJSON_AddNumberToObject(defaults, "staccato", 0);
            cJSON_AddNumberToObject(defaults, "sustain", 1);
            cJSON_AddNumberToObject(defaults, "resonate", 0);
            cJSON_AddNumberToObject(defaults, "sympatheticVolume", 50);
            cJSON_AddNumberToObject(defaults, "percussion", 0);
            cJSON_AddNumberToObject(defaults, "tapWithoutStrum", 0);
            cJSON_AddNumberToObject(defaults, "pitchSystem", 0);
            cJSON_AddNumberToObject(defaults, "constantVelocity", 0);
            cJSON_AddNumberToObject(defaults, "effects", 0);
            cJSON_AddNumberToObject(defaults, "pitchChange", 0);
            cJSON_AddNumberToObject(defaults, "midiChannel", 0);
            cJSON_AddNumberToObject(defaults, "string1", 1);
            cJSON_AddNumberToObject(defaults, "string2", 1);
            cJSON_AddNumberToObject(defaults, "string3", 1);
            cJSON_AddNumberToObject(defaults, "string4", 1);
            cJSON_AddNumberToObject(defaults, "metronomeBpm", 60);
            cJSON_AddNumberToObject(defaults, "metronomeBeats", 4);
            cJSON_AddNumberToObject(defaults, "metronomeVol", 64);
            save_settings_to_flash(filename, defaults);
            cJSON_Delete(defaults);
        }
        file = fopen(filename, "r");
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to open file for reading (path=%s)", filename);
            return NULL;
        }
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *json_str = (char *)malloc(file_size + 1);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Memory allocation error");
        fclose(file);
        return NULL;
    }

    fread(json_str, 1, file_size, file);
    json_str[file_size] = '\0';

    fclose(file);

    cJSON *settings = cJSON_Parse(json_str);
    free(json_str);

    if (settings == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
    }

    ESP_LOGI(TAG, "Settings loaded from %s", filename);

    return settings;
}

int get_numerical_setting(cJSON *settings, const char *key) {
    cJSON *setting_item = cJSON_GetObjectItemCaseSensitive(settings, key);
    if (setting_item != NULL && cJSON_IsNumber(setting_item)) {
        /* Use valuedouble so values parsed from JSON (often only set valuedouble) are read correctly */
        return (int)setting_item->valuedouble;
    } else {
        return 0; // or any default value you prefer
    }
}

// void app_main(void) {
//     // Initialize SPIFFS
//     init_spiffs();

//     // Create sample JSON settings
//     cJSON *settings = cJSON_CreateObject();
//     cJSON_AddStringToObject(settings, "SSID", "your_wifi_ssid");
//     cJSON_AddStringToObject(settings, "Password", "your_wifi_password");
//     cJSON_AddNumberToObject(settings, "DeviceID", 12345);

//     // Save settings to flash
//     save_settings_to_flash("/spiffs/settings.json", settings);

//     // Load settings from flash
//     cJSON *loaded_settings = load_settings_from_flash("/spiffs/settings.json");
//     if (loaded_settings != NULL) {
//         // Print loaded settings
//         char *loaded_str = cJSON_Print(loaded_settings);
//         ESP_LOGI(TAG, "Loaded Settings:\n%s", loaded_str);
//         cJSON_free(loaded_str);

//         // Free loaded settings cJSON object
//         cJSON_Delete(loaded_settings);
//     }

//     // Free cJSON object
//     cJSON_Delete(settings);

//     vTaskDelay(portMAX_DELAY);
// }
