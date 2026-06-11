
#include "settings.h"
#include "spiffs.h"
#include "etar.h"

static const char *TAG = "settings";

static const char *LEGACY_SETTINGS_PATH = "/spiffs/settings.json";

const char *settings_json_path(void)
{
#if defined(INST_UKULELE)
    return "/spiffs/settings_ukulele.json";
#else
    return "/spiffs/settings_setar.json";
#endif
}

const char *factory_json_path(void)
{
#if defined(INST_UKULELE)
    return "/spiffs/factory_ukulele.json";
#else
    return "/spiffs/factory_setar.json";
#endif
}

static cJSON *create_default_settings(void)
{
    cJSON *defaults = cJSON_CreateObject();
    if (defaults == NULL) {
        return NULL;
    }

#if defined(INST_UKULELE)
    const int default_tuning = 14;   /* baseTable row: A4 E4 C4 G4 */
    const int default_quarter_tones = 0;
#else
    const int default_tuning = 0;    /* baseTable row: C4 G3 C4 C3 */
    const int default_quarter_tones = 1;
#endif

    cJSON_AddNumberToObject(defaults, "instrument", 107);
    cJSON_AddNumberToObject(defaults, "chords", 0);
    cJSON_AddNumberToObject(defaults, "tuning", default_tuning);
    cJSON_AddNumberToObject(defaults, "tapping", 0);
    cJSON_AddNumberToObject(defaults, "hammerOnVelocity", 50);
    cJSON_AddNumberToObject(defaults, "hammerPostStrumGuardTicks", 5);
    cJSON_AddNumberToObject(defaults, "strumVelOutMin", 10);
    cJSON_AddNumberToObject(defaults, "strumVelOutMax", 127);
    cJSON_AddNumberToObject(defaults, "transpose", 0);
    cJSON_AddNumberToObject(defaults, "vibrato", 0);
    cJSON_AddNumberToObject(defaults, "leftHand", 0);
    cJSON_AddNumberToObject(defaults, "quarterTones", default_quarter_tones);
    cJSON_AddNumberToObject(defaults, "staccato", 0);
    cJSON_AddNumberToObject(defaults, "sustain", 1);
    cJSON_AddNumberToObject(defaults, "resonate", 0);
    cJSON_AddNumberToObject(defaults, "sympatheticVolume", 50);
    cJSON_AddNumberToObject(defaults, "percussion", 0);
    cJSON_AddNumberToObject(defaults, "tapWithoutStrum", 0);
    cJSON_AddNumberToObject(defaults, "pitchSystem", 0);
    cJSON_AddNumberToObject(defaults, "constantVelocity", 1);
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
    return defaults;
}

static cJSON *parse_settings_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return NULL;
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
        ESP_LOGE(TAG, "Failed to parse JSON (%s)", filename);
    } else {
        ESP_LOGI(TAG, "Settings loaded from %s", filename);
    }

    return settings;
}

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

cJSON *load_settings_from_flash(const char *filename) {
    cJSON *settings = parse_settings_file(filename);
    if (settings != NULL) {
        return settings;
    }

    ESP_LOGW(TAG, "Settings file not found: %s", filename);
    return NULL;
}

cJSON *load_user_settings_from_flash(void)
{
    const char *path = settings_json_path();
    cJSON *settings = parse_settings_file(path);
    if (settings != NULL) {
        return settings;
    }

    settings = parse_settings_file(LEGACY_SETTINGS_PATH);
    if (settings != NULL) {
        ESP_LOGI(TAG, "Using legacy %s", LEGACY_SETTINGS_PATH);
        return settings;
    }

    ESP_LOGW(TAG, "Creating default settings at %s", path);
    settings = create_default_settings();
    if (settings != NULL) {
        save_settings_to_flash(path, settings);
    }
    return settings;
}

int get_numerical_setting(cJSON *settings, const char *key) {
    cJSON *setting_item = cJSON_GetObjectItemCaseSensitive(settings, key);
    if (setting_item != NULL && cJSON_IsNumber(setting_item)) {
        return (int)setting_item->valuedouble;
    } else {
        return 0;
    }
}
