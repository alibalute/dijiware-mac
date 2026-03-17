/**
 * Stub for macOS/host parsing only. Do not include real FreeRTOS headers
 * (they require FreeRTOS.h before task.h and use section attributes invalid for Mach-O).
 * ESP32 builds never include this file.
 */
#ifndef FREERTOS_HOST_STUB_H
#define FREERTOS_HOST_STUB_H

#include <stdint.h>

#define pdMS_TO_TICKS(ms) ((uint32_t)(ms))

typedef void *TaskHandle_t;

void vTaskDelay(uint32_t ticks);
void vTaskPrioritySet(void *task, uint32_t prio);
void vTaskDelete(void *task);

#endif
