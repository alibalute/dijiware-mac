/**
 * @file midi_recorder.c
 *
 * Records live performance by capturing note on/off and pitch bend from inputToUART or midiTx.
 * On stop, writes a format-0 single-track SMF compatible with midi_player.c.
 */

#include "midi_recorder.h"
#include "esp_log.h"
#include "spiffs.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

extern uint32_t millis(void);
extern int bpm;
extern void setMidiFile(const char *path);

static const char *TAG = "midi_recorder";

#define MIDI_REC_MAX_EVENTS 1024
#define MIDI_REC_TPQN     480
#define MIDI_REC_TRK_BUF  14000

typedef struct {
    uint32_t t_ms;
    uint8_t status;
    uint8_t d1;
    uint8_t d2;
} RecEvent;

static RecEvent s_events[MIDI_REC_MAX_EVENTS];
static size_t s_count;
static bool s_active;
static uint32_t s_t0_ms;
static portMUX_TYPE s_rec_lock = portMUX_INITIALIZER_UNLOCKED;

/* Finalize workspace in .bss — must not live on etarTask stack (~10k); was ~22k and overflowed. */
static RecEvent s_finalize_events[MIDI_REC_MAX_EVENTS];
static uint8_t s_trk_buf[MIDI_REC_TRK_BUF];

static void write_vlq(uint8_t *buf, size_t *wp, size_t cap, uint32_t v)
{
    uint8_t tmp[4];
    int n = 0;
    tmp[n++] = (uint8_t)(v & 0x7f);
    v >>= 7;
    while (v && n < 4) {
        tmp[n++] = (uint8_t)((v & 0x7f) | 0x80);
        v >>= 7;
    }
    while (n > 0) {
        if (*wp >= cap) {
            return;
        }
        buf[(*wp)++] = tmp[--n];
    }
}

void midi_recorder_init(void)
{
    s_count = 0;
    s_active = false;
}

bool midi_recorder_is_active(void)
{
    return s_active;
}

void midi_recorder_start(void)
{
    portENTER_CRITICAL(&s_rec_lock);
    s_count = 0;
    s_t0_ms = millis();
    s_active = true;
    portEXIT_CRITICAL(&s_rec_lock);
    ESP_LOGI(TAG, "recording started");
}

void midi_recorder_capture(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!s_active) {
        return;
    }
    uint32_t t = millis();
    portENTER_CRITICAL(&s_rec_lock);
    if (!s_active) {
        portEXIT_CRITICAL(&s_rec_lock);
        return;
    }
    if (s_count >= MIDI_REC_MAX_EVENTS) {
        portEXIT_CRITICAL(&s_rec_lock);
        return;
    }
    s_events[s_count].t_ms = (t >= s_t0_ms) ? (t - s_t0_ms) : 0;
    s_events[s_count].status = status;
    s_events[s_count].d1 = data1;
    s_events[s_count].d2 = data2;
    s_count++;
    portEXIT_CRITICAL(&s_rec_lock);
}

void midi_recorder_stop_and_finalize(void)
{
    size_t ncopy;

    portENTER_CRITICAL(&s_rec_lock);
    s_active = false;
    ncopy = s_count;
    if (ncopy > MIDI_REC_MAX_EVENTS) {
        ncopy = MIDI_REC_MAX_EVENTS;
    }
    if (ncopy > 0) {
        memcpy(s_finalize_events, s_events, ncopy * sizeof(RecEvent));
    }
    s_count = 0;
    portEXIT_CRITICAL(&s_rec_lock);

    init_spiffs();

    uint32_t us_per_qn = 500000;
    if (bpm > 0 && bpm < 500) {
        us_per_qn = (uint32_t)(1000000UL * 60UL / (uint32_t)bpm);
    }

    uint8_t *trk = s_trk_buf;
    size_t w = 0;

    /* Delta 0: set tempo FF 51 03 */
    if (w < MIDI_REC_TRK_BUF) {
        trk[w++] = 0;
    }
    if (w < MIDI_REC_TRK_BUF) {
        trk[w++] = 0xFF;
    }
    if (w < MIDI_REC_TRK_BUF) {
        trk[w++] = 0x51;
    }
    if (w < MIDI_REC_TRK_BUF) {
        trk[w++] = 3;
    }
    if (w + 3 <= MIDI_REC_TRK_BUF) {
        trk[w++] = (uint8_t)((us_per_qn >> 16) & 0xff);
        trk[w++] = (uint8_t)((us_per_qn >> 8) & 0xff);
        trk[w++] = (uint8_t)(us_per_qn & 0xff);
    }

    uint32_t prev_ms = 0;
    for (size_t i = 0; i < ncopy; i++) {
        uint32_t dt_ms = s_finalize_events[i].t_ms;
        if (dt_ms < prev_ms) {
            dt_ms = prev_ms;
        }
        uint32_t delta_ms = dt_ms - prev_ms;
        prev_ms = dt_ms;

        double ticks = (double)delta_ms * (double)MIDI_REC_TPQN * 1000.0 / (double)us_per_qn;
        uint32_t delta_ticks = (uint32_t)(ticks + 0.5);
        if (delta_ticks > 0x0FFFFFFF) {
            delta_ticks = 0x0FFFFFFF;
        }
        write_vlq(trk, &w, MIDI_REC_TRK_BUF, delta_ticks);
        if (w + 3 > MIDI_REC_TRK_BUF) {
            ESP_LOGW(TAG, "track buffer full; truncating at event %u", (unsigned)i);
            break;
        }
        trk[w++] = s_finalize_events[i].status;
        trk[w++] = s_finalize_events[i].d1;
        trk[w++] = s_finalize_events[i].d2;
    }

    /* End of track */
    if (w + 4 <= MIDI_REC_TRK_BUF) {
        trk[w++] = 0;
        trk[w++] = 0xFF;
        trk[w++] = 0x2F;
        trk[w++] = 0x00;
    }

    uint32_t trk_len = (uint32_t)w;
    FILE *f = fopen(MIDI_REC_FILENAME, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "fopen %s failed", MIDI_REC_FILENAME);
        return;
    }

    /* MThd */
    fwrite("MThd", 1, 4, f);
    {
        uint8_t len_be[4] = {0, 0, 0, 6};
        fwrite(len_be, 1, 4, f);
    }
    {
        uint8_t hdata[6] = {
            0,
            0, /* format 0 */
            0,
            1, /* one track */
            (uint8_t)(MIDI_REC_TPQN >> 8),
            (uint8_t)(MIDI_REC_TPQN & 0xff),
        };
        fwrite(hdata, 1, 6, f);
    }

    fwrite("MTrk", 1, 4, f);
    {
        uint8_t tlb[4] = {
            (uint8_t)((trk_len >> 24) & 0xff),
            (uint8_t)((trk_len >> 16) & 0xff),
            (uint8_t)((trk_len >> 8) & 0xff),
            (uint8_t)(trk_len & 0xff),
        };
        fwrite(tlb, 1, 4, f);
    }
    fwrite(trk, 1, trk_len, f);
    fclose(f);

    setMidiFile(MIDI_REC_FILENAME);
    ESP_LOGI(TAG, "saved %u events, %u track bytes -> %s", (unsigned)ncopy, (unsigned)trk_len, MIDI_REC_FILENAME);
}
