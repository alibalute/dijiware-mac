#ifndef OTA_DEBUG_H
#define OTA_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Save current reset reason to NVS (call early in app_main). */
void ota_debug_save_reset_reason(uint8_t reason);

/** Save OTA state: running=1 when OTA starts, 0 when it ends; bytes_written = total so far. */
void ota_debug_save_ota_state(uint8_t running, uint32_t bytes_written);

/** Save OTA phase so we see how far we got before reset: 0=started, 1=ota_begin_done, 2=before_first_write, 3=writing. */
void ota_debug_save_ota_phase(uint8_t phase);

/** Write JSON debug info into buf (for GET /api/debug). */
void ota_debug_get_json(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
