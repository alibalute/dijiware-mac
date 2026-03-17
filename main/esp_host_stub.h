/**
 * Stub for macOS/host parsing only. Avoid pulling in esp_log.h and esp_sleep.h
 * (they use section attributes invalid for Mach-O). ESP32 builds never include this.
 */
#ifndef ESP_HOST_STUB_H
#define ESP_HOST_STUB_H

#include <stdint.h>

/* esp_log.h stubs */
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOG_DEBUG 0
#define ESP_LOG_INFO  1
#define ESP_LOG_WARN  2
#define ESP_LOG_ERROR 3
void esp_log_level_set(const char *tag, int level);

/* esp_sleep.h stubs */
#define ESP_SLEEP_WAKEUP_TIMER  4
#define ESP_SLEEP_WAKEUP_GPIO   2
static inline void esp_sleep_enable_timer_wakeup(uint64_t us) { (void)us; }
static inline void esp_sleep_enable_gpio_wakeup(void) { }
static inline void esp_sleep_enable_gpio_switch(int enable) { (void)enable; }
static inline int esp_sleep_get_wakeup_cause(void) { return 0; }
static inline void esp_light_sleep_start(void) { }
static inline void esp_deep_sleep_start(void) { }

#endif
