/**
 * @file midi_player.c
 * @author Ali
 * @brief
 * @version 0.1
 * @date 2024-1-3
 *
 * Copyright (c) 2023 elemental ID
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "sdkconfig.h"
#if CONFIG_SPIRAM
#include "esp_heap_caps.h"
#endif
#include "midi_player.h"
#include "spiffs.h"

static const char* TAG = "midi_player";

/** Set on any parse_midi_track() failure; printed with "MIDI parse error in track N". */
static char s_midi_parse_err[128];

static int parse_trk_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_midi_parse_err, sizeof(s_midi_parse_err), fmt, ap);
    va_end(ap);
    return -1;
}

extern int bpm; // bpm is declared and initialized in metronome.c
extern int16_t transposeValue; // value of transpose
extern uint8_t semitoneChannel;
extern uint8_t quartertoneChannel;

MidiEvent *events = NULL;
size_t eventsCount = 0;

bool midi_strum_step_mode = false;
static size_t midi_step_event_index = 0;
static SemaphoreHandle_t s_midi_events_mutex;

static void midi_stream_state_free(void);

static void midi_take(void)
{
    if (s_midi_events_mutex == NULL) {
        s_midi_events_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_midi_events_mutex, portMAX_DELAY);
}

static void midi_give(void)
{
    xSemaphoreGive(s_midi_events_mutex);
}

static void midi_events_free(void)
{
    midi_stream_state_free();
    if (events != NULL) {
        free(events);
        events = NULL;
    }
    eventsCount = 0;
}

//in this code, parse_midi_file() opens the midi file and parses it into a MidiEvent vector, then starts the sequencer task to send these events to the synthesizer
//parse_midi_file() is called from util.c

uint16_t format;
uint16_t tracks;
uint16_t division;

double ticksPerQuarterNote = 480;  // Ticks per quarter note
double microsecondsPerQuarterNote ;  // Microseconds per quarter note (tempo) , this should be read from the midi file. it can be done later!! The code is there but doesnt work!

 // event time in ms
uint32_t event_time;


uint32_t convertDeltaTimeToTime(double deltaTime, double ticksPerQuarterNote, double microsecondsPerQuarterNote) {

    return (uint32_t)((deltaTime / ticksPerQuarterNote) * (microsecondsPerQuarterNote / 1000.0)); //in miliseconds
}

uint32_t swap_endianness(uint32_t value) {
    return ((value >> 24) & 0x000000FF) |
           ((value >> 8) & 0x0000FF00) |
           ((value << 8) & 0x00FF0000) |
           ((value << 24) & 0xFF000000);
}

/** Same tick/channel: program change, CC (e.g. sustain), pitch bend, note on, note off. */
static int smf_event_sort_priority(uint8_t status_hi)
{
    switch (status_hi) {
        case 0xC0:
            return 0;
        case 0xB0:
            return 1;
        case 0xE0:
            return 2;
        case 0x90:
            return 3;
        case 0x80:
            return 4;
        default:
            return 5;
    }
}

int compare_events(const void *a, const void *b) {
    const MidiEvent *ea = (const MidiEvent *)a;
    const MidiEvent *eb = (const MidiEvent *)b;
    if (ea->time < eb->time) {
        return -1;
    }
    if (ea->time > eb->time) {
        return 1;
    }
    if (ea->channel < eb->channel) {
        return -1;
    }
    if (ea->channel > eb->channel) {
        return 1;
    }
    int pa = smf_event_sort_priority(ea->status);
    int pb = smf_event_sort_priority(eb->status);
    if (pa != pb) {
        return (pa < pb) ? -1 : 1;
    }
    if (ea->note < eb->note) {
        return -1;
    }
    if (ea->note > eb->note) {
        return 1;
    }
    if (ea->velocity != eb->velocity) {
        return (ea->velocity < eb->velocity) ? -1 : 1;
    }
    return (int)ea->status - (int)eb->status;
}

/** Per-track voice event before tick→ms merge (multi-track SMF). Packed to fit large scores on internal RAM. */
typedef struct __attribute__((packed)) {
    uint32_t tick;
    uint32_t seq;
    uint8_t status; /* full status byte (e.g. 0x90 | ch) */
    uint8_t d1;
    uint8_t d2;
} MidiRawEvent;

#if CONFIG_SPIRAM
static bool s_midi_raw_spiram;
#endif

static void *midi_raw_realloc(void *ptr, size_t new_size)
{
#if CONFIG_SPIRAM
    if (ptr == NULL) {
        void *p = heap_caps_malloc(new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) {
            s_midi_raw_spiram = true;
            return p;
        }
        s_midi_raw_spiram = false;
        return malloc(new_size);
    }
    if (s_midi_raw_spiram) {
        void *p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) {
            return p;
        }
        void *p2 = malloc(new_size);
        if (!p2) {
            return NULL;
        }
        size_t old_sz = heap_caps_get_allocated_size(ptr);
        if (old_sz > new_size) {
            old_sz = new_size;
        }
        memcpy(p2, ptr, old_sz);
        heap_caps_free(ptr);
        s_midi_raw_spiram = false;
        return p2;
    }
#endif
    return realloc(ptr, new_size);
}

static MidiEvent *midi_events_alloc(size_t n)
{
    if (n == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    MidiEvent *p = heap_caps_calloc(n, sizeof(MidiEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
        return p;
    }
#endif
    return calloc(n, sizeof(MidiEvent));
}

typedef struct {
    uint32_t tick;
    uint32_t seq;
    uint32_t us_per_qn;
} MidiTempoPoint;

static bool s_midi_stream_mode;
static MidiTempoPoint *s_stream_tp;
static size_t s_stream_tp_n;
static uint32_t s_stream_us0;
static long s_stream_track_start;
static long s_stream_track_end;

static void midi_stream_state_free(void)
{
    if (s_stream_tp != NULL) {
        free(s_stream_tp);
        s_stream_tp = NULL;
    }
    s_stream_tp_n = 0;
    s_midi_stream_mode = false;
}

static int cmp_raw_event(const void *a, const void *b)
{
    const MidiRawEvent *x = (const MidiRawEvent *)a;
    const MidiRawEvent *y = (const MidiRawEvent *)b;
    if (x->tick != y->tick) {
        return (x->tick < y->tick) ? -1 : 1;
    }
    if (x->seq != y->seq) {
        return (x->seq < y->seq) ? -1 : 1;
    }
    return 0;
}

static int cmp_tempo_point(const void *a, const void *b)
{
    const MidiTempoPoint *x = (const MidiTempoPoint *)a;
    const MidiTempoPoint *y = (const MidiTempoPoint *)b;
    if (x->tick != y->tick) {
        return (x->tick < y->tick) ? -1 : 1;
    }
    if (x->seq != y->seq) {
        return (x->seq < y->seq) ? -1 : 1;
    }
    return 0;
}

/** Wall-clock ms at absolute tick `target`, given sorted tempo map (us per quarter). */
static uint32_t ms_at_tick(uint32_t target, const MidiTempoPoint *tp, size_t tp_n,
                           double tpqn, uint32_t us0)
{
    if (target == 0) {
        return 0;
    }
    double ms = 0;
    uint32_t pos = 0;
    uint32_t us = us0;
    size_t ti = 0;

    while (pos < target) {
        while (ti < tp_n && tp[ti].tick == pos) {
            us = tp[ti].us_per_qn;
            ti++;
        }
        uint32_t next = target;
        if (ti < tp_n && tp[ti].tick < next) {
            next = tp[ti].tick;
        }
        double dt = (double)(next - pos);
        ms += (dt / tpqn) * ((double)us / 1000.0);
        pos = next;
    }
    return (uint32_t)(ms + 0.5);
}

/**
 * Parse one MTrk chunk: append tempo and note/pitch events using absolute tick time
 * (all tracks share the same tick timeline in Format 1).
 */
static int parse_midi_track(
    FILE *file,
    long track_end,
    MidiRawEvent **raw_out,
    size_t *raw_count,
    size_t *raw_cap,
    MidiTempoPoint **tp_out,
    size_t *tp_count,
    size_t *tp_cap,
    uint32_t *global_seq)
{
    long track_start = ftell(file);
    uint32_t seq_at_track = *global_seq;
    size_t base_count = *raw_count;
    size_t voice_n = 0;

    /* Two passes: lyric/meta-heavy files over-estimate voice events from byte size
     * (~8851 slots) and exhaust internal RAM. Pass 0 counts exact voice events;
     * one realloc(base_count + count); pass 1 fills without duplicate tempo rows. */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t abs_tick = 0;
        uint8_t running_status = 0;
        bool have_running_status = false;
        size_t voice_in_track = 0;
        size_t emit_idx = base_count;

        if (pass > 0) {
            if (fseek(file, track_start, SEEK_SET) != 0) {
                return parse_trk_fail("fseek track");
            }
            *global_seq = seq_at_track;
        }

        while (ftell(file) < track_end) {
        uint32_t delta_time = read_variable_length(file);
        abs_tick += delta_time;

        uint8_t first_byte;
        if (fread(&first_byte, 1, 1, file) != 1) {
            return parse_trk_fail("EOF after delta (tick %lu, need more track data)",
                                  (unsigned long)abs_tick);
        }

        if (first_byte == 0xFF) {
            have_running_status = false;
            uint8_t metaType;
            if (fread(&metaType, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after meta FF (tick %lu)", (unsigned long)abs_tick);
            }
            uint32_t meta_len = readVLQ(file);

            if (metaType == 0x51 && meta_len == 3) {
                uint32_t tempo = 0;
                for (uint32_t i = 0; i < meta_len; i++) {
                    uint8_t byte;
                    if (fread(&byte, 1, 1, file) != 1) {
                        return parse_trk_fail("EOF in tempo meta (tick %lu)", (unsigned long)abs_tick);
                    }
                    tempo = (tempo << 8) | byte;
                }
                if (pass == 0) {
                    if (*tp_count >= *tp_cap) {
                        size_t ncap = *tp_cap ? (*tp_cap * 2) : 16;
                        MidiTempoPoint *n = realloc(*tp_out, ncap * sizeof(MidiTempoPoint));
                        if (n == NULL) {
                            return parse_trk_fail("OOM tempo map");
                        }
                        *tp_out = n;
                        *tp_cap = ncap;
                    }
                    (*tp_out)[*tp_count].tick = abs_tick;
                    (*tp_out)[*tp_count].seq = (*global_seq)++;
                    (*tp_out)[*tp_count].us_per_qn = tempo;
                    (*tp_count)++;
                    ESP_LOGD(TAG, "Tempo @tick %lu: %lu us/qn", (unsigned long)abs_tick, (unsigned long)tempo);
                } else {
                    (*global_seq)++;
                }
            } else {
                for (uint32_t i = 0; i < meta_len; i++) {
                    uint8_t ign;
                    if (fread(&ign, 1, 1, file) != 1) {
                        return parse_trk_fail("EOF in meta type 0x%02x len %lu (tick %lu)",
                                              (unsigned)metaType, (unsigned long)meta_len,
                                              (unsigned long)abs_tick);
                    }
                }
            }
            continue;
        }

        /* SysEx / escape (clears running status in SMF streams). */
        if (first_byte == 0xF0 || first_byte == 0xF7) {
            have_running_status = false;
            uint32_t syxlen = readVLQ(file);
            for (uint32_t i = 0; i < syxlen; i++) {
                uint8_t ign;
                if (fread(&ign, 1, 1, file) != 1) {
                    return parse_trk_fail("EOF inside SysEx (declared len %lu, tick %lu)",
                                          (unsigned long)syxlen, (unsigned long)abs_tick);
                }
            }
            continue;
        }

        /* SMF tracks should only have meta, SysEx, and channel messages. Some DAWs
         * insert wire-format system common / real-time bytes (F1–FE); skip them or
         * they hit status_byte>0xEF and fail with "parse error in track 0". */
        if (first_byte == 0xF1) {
            have_running_status = false;
            uint8_t ign;
            if (fread(&ign, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after F1 (tick %lu)", (unsigned long)abs_tick);
            }
            continue;
        }
        if (first_byte == 0xF2) {
            have_running_status = false;
            uint8_t b[2];
            if (fread(b, 1, 2, file) != 2) {
                return parse_trk_fail("EOF after F2 (tick %lu)", (unsigned long)abs_tick);
            }
            continue;
        }
        if (first_byte == 0xF3) {
            have_running_status = false;
            uint8_t ign;
            if (fread(&ign, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after F3 (tick %lu)", (unsigned long)abs_tick);
            }
            continue;
        }
        if (first_byte == 0xF4 || first_byte == 0xF5 || first_byte == 0xF6) {
            have_running_status = false;
            continue;
        }
        if (first_byte >= 0xF8 && first_byte <= 0xFE) {
            have_running_status = false;
            continue;
        }

        uint8_t status_byte;
        uint8_t param1 = 0;
        uint8_t param2 = 0;
        bool using_running_status = (first_byte & 0x80) == 0;

        if (!using_running_status) {
            status_byte = first_byte;
            running_status = status_byte;
            have_running_status = true;
        } else {
            if (!have_running_status || (running_status & 0x80) == 0) {
                return parse_trk_fail("data 0x%02x without running status at tick %lu",
                                      (unsigned)first_byte, (unsigned long)abs_tick);
            }
            status_byte = running_status;
            param1 = first_byte;
        }

        if (status_byte < 0x80 || status_byte > 0xEF) {
            return parse_trk_fail("bad status 0x%02x at tick %lu (running=%d)",
                                  (unsigned)status_byte, (unsigned long)abs_tick,
                                  using_running_status ? 1 : 0);
        }

        uint8_t event_type = status_byte & 0xF0;

        if (event_type == 0xD0) {
            if (!using_running_status) {
                if (fread(&param1, 1, 1, file) != 1) {
                    return parse_trk_fail("EOF after pressure status 0x%02x tick %lu",
                                          (unsigned)status_byte, (unsigned long)abs_tick);
                }
            }
            continue;
        }

        if (event_type == 0xC0) {
            if (!using_running_status) {
                if (fread(&param1, 1, 1, file) != 1) {
                    return parse_trk_fail("EOF after program change 0x%02x tick %lu",
                                          (unsigned)status_byte, (unsigned long)abs_tick);
                }
            }
            if (pass == 0) {
                voice_in_track++;
                (*global_seq)++;
            } else {
                MidiRawEvent *re = &(*raw_out)[emit_idx++];
                re->tick = abs_tick;
                re->seq = (*global_seq)++;
                re->status = status_byte;
                re->d1 = param1;
                re->d2 = 0;
            }
            continue;
        }

        if (!using_running_status) {
            if (fread(&param1, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after status 0x%02x tick %lu",
                                      (unsigned)status_byte, (unsigned long)abs_tick);
            }
        }
        if (fread(&param2, 1, 1, file) != 1) {
            return parse_trk_fail("EOF reading 2nd data byte (status 0x%02x tick %lu)",
                                  (unsigned)status_byte, (unsigned long)abs_tick);
        }

        if (event_type == 0xB0) {
            if (param1 == 0x40) {
                /* Sustain pedal (damper); GM controller 64. */
                if (pass == 0) {
                    voice_in_track++;
                    (*global_seq)++;
                } else {
                    MidiRawEvent *re = &(*raw_out)[emit_idx++];
                    re->tick = abs_tick;
                    re->seq = (*global_seq)++;
                    re->status = status_byte;
                    re->d1 = param1;
                    re->d2 = param2;
                }
            }
            continue;
        }

        if (event_type == 0x80 || event_type == 0x90 || event_type == 0xE0) {
            if (pass == 0) {
                voice_in_track++;
                (*global_seq)++;
            } else {
                MidiRawEvent *re = &(*raw_out)[emit_idx++];
                re->tick = abs_tick;
                re->seq = (*global_seq)++;
                re->status = status_byte;
                re->d1 = param1;
                re->d2 = param2;
            }
        }
        } /* end while */

        if (pass == 0) {
            voice_n = voice_in_track;
            if (voice_n > 0) {
                size_t need = base_count + voice_n;
                MidiRawEvent *n = midi_raw_realloc(*raw_out, need * sizeof(MidiRawEvent));
                if (n == NULL) {
                    return parse_trk_fail("OOM event list (%u timed events)", (unsigned)voice_n);
                }
                *raw_out = n;
                *raw_cap = need;
            }
        }
    } /* end for pass */

    *raw_count = base_count + voice_n;
    return 0;
}

/**
 * Single-pass scan of one MTrk: collect Set Tempo (0xFF 0x51) only. Keeps file in sync with parse_midi_track().
 */
static int collect_tempo_from_track(FILE *file, long track_end, MidiTempoPoint **tp_out, size_t *tp_count, size_t *tp_cap)
{
    uint32_t abs_tick = 0;
    uint8_t running_status = 0;
    bool have_running_status = false;
    uint32_t tempo_seq = 0;

    while (ftell(file) < track_end) {
        uint32_t delta_time = read_variable_length(file);
        abs_tick += delta_time;

        uint8_t first_byte;
        if (fread(&first_byte, 1, 1, file) != 1) {
            return parse_trk_fail("EOF after delta (tick %lu, need more track data)",
                                  (unsigned long)abs_tick);
        }

        if (first_byte == 0xFF) {
            have_running_status = false;
            uint8_t metaType;
            if (fread(&metaType, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after meta FF (tick %lu)", (unsigned long)abs_tick);
            }
            uint32_t meta_len = readVLQ(file);

            if (metaType == 0x51 && meta_len == 3) {
                uint32_t tempo = 0;
                for (uint32_t i = 0; i < meta_len; i++) {
                    uint8_t byte;
                    if (fread(&byte, 1, 1, file) != 1) {
                        return parse_trk_fail("EOF in tempo meta (tick %lu)", (unsigned long)abs_tick);
                    }
                    tempo = (tempo << 8) | byte;
                }
                if (*tp_count >= *tp_cap) {
                    size_t ncap = *tp_cap ? (*tp_cap * 2) : 16;
                    MidiTempoPoint *n = realloc(*tp_out, ncap * sizeof(MidiTempoPoint));
                    if (n == NULL) {
                        return parse_trk_fail("OOM tempo map");
                    }
                    *tp_out = n;
                    *tp_cap = ncap;
                }
                (*tp_out)[*tp_count].tick = abs_tick;
                (*tp_out)[*tp_count].seq = tempo_seq++;
                (*tp_out)[*tp_count].us_per_qn = tempo;
                (*tp_count)++;
                ESP_LOGD(TAG, "Tempo @tick %lu: %lu us/qn", (unsigned long)abs_tick, (unsigned long)tempo);
            } else {
                for (uint32_t i = 0; i < meta_len; i++) {
                    uint8_t ign;
                    if (fread(&ign, 1, 1, file) != 1) {
                        return parse_trk_fail("EOF in meta type 0x%02x len %lu (tick %lu)",
                                              (unsigned)metaType, (unsigned long)meta_len,
                                              (unsigned long)abs_tick);
                    }
                }
            }
            continue;
        }

        if (first_byte == 0xF0 || first_byte == 0xF7) {
            have_running_status = false;
            uint32_t syxlen = readVLQ(file);
            for (uint32_t i = 0; i < syxlen; i++) {
                uint8_t ign;
                if (fread(&ign, 1, 1, file) != 1) {
                    return parse_trk_fail("EOF inside SysEx (declared len %lu, tick %lu)",
                                          (unsigned long)syxlen, (unsigned long)abs_tick);
                }
            }
            continue;
        }

        if (first_byte == 0xF1) {
            have_running_status = false;
            uint8_t ign;
            if (fread(&ign, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after F1 (tick %lu)", (unsigned long)abs_tick);
            }
            continue;
        }
        if (first_byte == 0xF2) {
            have_running_status = false;
            uint8_t b[2];
            if (fread(b, 1, 2, file) != 2) {
                return parse_trk_fail("EOF after F2 (tick %lu)", (unsigned long)abs_tick);
            }
            continue;
        }
        if (first_byte == 0xF3) {
            have_running_status = false;
            uint8_t ign;
            if (fread(&ign, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after F3 (tick %lu)", (unsigned long)abs_tick);
            }
            continue;
        }
        if (first_byte == 0xF4 || first_byte == 0xF5 || first_byte == 0xF6) {
            have_running_status = false;
            continue;
        }
        if (first_byte >= 0xF8 && first_byte <= 0xFE) {
            have_running_status = false;
            continue;
        }

        uint8_t status_byte;
        uint8_t param1 = 0;
        uint8_t param2 = 0;
        bool using_running_status = (first_byte & 0x80) == 0;

        if (!using_running_status) {
            status_byte = first_byte;
            running_status = status_byte;
            have_running_status = true;
        } else {
            if (!have_running_status || (running_status & 0x80) == 0) {
                return parse_trk_fail("data 0x%02x without running status at tick %lu",
                                      (unsigned)first_byte, (unsigned long)abs_tick);
            }
            status_byte = running_status;
            param1 = first_byte;
        }

        if (status_byte < 0x80 || status_byte > 0xEF) {
            return parse_trk_fail("bad status 0x%02x at tick %lu (running=%d)",
                                  (unsigned)status_byte, (unsigned long)abs_tick,
                                  using_running_status ? 1 : 0);
        }

        uint8_t event_type = status_byte & 0xF0;

        if (event_type == 0xC0 || event_type == 0xD0) {
            if (!using_running_status) {
                if (fread(&param1, 1, 1, file) != 1) {
                    return parse_trk_fail("EOF after PC/pressure status 0x%02x tick %lu",
                                          (unsigned)status_byte, (unsigned long)abs_tick);
                }
            }
            continue;
        }

        if (!using_running_status) {
            if (fread(&param1, 1, 1, file) != 1) {
                return parse_trk_fail("EOF after status 0x%02x tick %lu",
                                      (unsigned)status_byte, (unsigned long)abs_tick);
            }
        }
        if (fread(&param2, 1, 1, file) != 1) {
            return parse_trk_fail("EOF reading 2nd data byte (status 0x%02x tick %lu)",
                                  (unsigned)status_byte, (unsigned long)abs_tick);
        }

        (void)event_type;
    }
    return 0;
}

/**
 * Stream one MTrk from current file position: program change, sustain (CC 64), notes, pitch bend with tempo map.
 */
static void stream_track_play(FILE *file, long track_end, const MidiTempoPoint *tp, size_t tp_n,
                              double tpqn, uint32_t us0)
{
    uint32_t abs_tick = 0;
    uint8_t running_status = 0;
    bool have_running_status = false;
    uint32_t current_time;
    uint32_t start_time = esp_log_timestamp();

    while (ftell(file) < track_end) {
        uint32_t delta_time = read_variable_length(file);
        abs_tick += delta_time;

        uint8_t first_byte;
        if (fread(&first_byte, 1, 1, file) != 1) {
            break;
        }

        if (first_byte == 0xFF) {
            have_running_status = false;
            uint8_t metaType;
            if (fread(&metaType, 1, 1, file) != 1) {
                break;
            }
            uint32_t meta_len = readVLQ(file);
            for (uint32_t i = 0; i < meta_len; i++) {
                uint8_t ign;
                if (fread(&ign, 1, 1, file) != 1) {
                    return;
                }
            }
            continue;
        }

        if (first_byte == 0xF0 || first_byte == 0xF7) {
            have_running_status = false;
            uint32_t syxlen = readVLQ(file);
            for (uint32_t i = 0; i < syxlen; i++) {
                uint8_t ign;
                if (fread(&ign, 1, 1, file) != 1) {
                    return;
                }
            }
            continue;
        }

        if (first_byte == 0xF1) {
            have_running_status = false;
            uint8_t ign;
            if (fread(&ign, 1, 1, file) != 1) {
                return;
            }
            continue;
        }
        if (first_byte == 0xF2) {
            have_running_status = false;
            uint8_t b[2];
            if (fread(b, 1, 2, file) != 2) {
                return;
            }
            continue;
        }
        if (first_byte == 0xF3) {
            have_running_status = false;
            uint8_t ign;
            if (fread(&ign, 1, 1, file) != 1) {
                return;
            }
            continue;
        }
        if (first_byte == 0xF4 || first_byte == 0xF5 || first_byte == 0xF6) {
            have_running_status = false;
            continue;
        }
        if (first_byte >= 0xF8 && first_byte <= 0xFE) {
            have_running_status = false;
            continue;
        }

        uint8_t status_byte;
        uint8_t param1 = 0;
        uint8_t param2 = 0;
        bool using_running_status = (first_byte & 0x80) == 0;

        if (!using_running_status) {
            status_byte = first_byte;
            running_status = status_byte;
            have_running_status = true;
        } else {
            if (!have_running_status || (running_status & 0x80) == 0) {
                break;
            }
            status_byte = running_status;
            param1 = first_byte;
        }

        if (status_byte < 0x80 || status_byte > 0xEF) {
            break;
        }

        uint8_t event_type = status_byte & 0xF0;

        if (event_type == 0xD0) {
            if (!using_running_status) {
                if (fread(&param1, 1, 1, file) != 1) {
                    return;
                }
            }
            continue;
        }

        if (event_type == 0xC0) {
            if (!using_running_status) {
                if (fread(&param1, 1, 1, file) != 1) {
                    return;
                }
            }
            if (midiStop) {
                return;
            }
            uint8_t hi = (uint8_t)(status_byte & 0xF0);
            MidiEvent ev;
            ev.status = hi;
            ev.channel = (uint8_t)(status_byte & 0x0F);
            ev.note = param1;
            ev.velocity = 0;
            ev.time = ms_at_tick(abs_tick, tp, tp_n, tpqn, us0);

            do {
                current_time = esp_log_timestamp();
                vTaskDelay(1 / portTICK_PERIOD_MS);
                while (midiPause && !midiStop) {
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    start_time = esp_log_timestamp();
                }
            } while (!midiStop && current_time < start_time + ev.time);

            if (midiStop) {
                return;
            }
            send_midi_event(&ev);
            continue;
        }

        if (!using_running_status) {
            if (fread(&param1, 1, 1, file) != 1) {
                return;
            }
        }
        if (fread(&param2, 1, 1, file) != 1) {
            return;
        }

        if (event_type == 0xB0 && param1 == 0x40) {
            if (midiStop) {
                return;
            }
            MidiEvent ev;
            ev.status = 0xB0;
            ev.channel = (uint8_t)(status_byte & 0x0F);
            ev.note = param1;
            ev.velocity = param2;
            ev.time = ms_at_tick(abs_tick, tp, tp_n, tpqn, us0);

            do {
                current_time = esp_log_timestamp();
                vTaskDelay(1 / portTICK_PERIOD_MS);
                while (midiPause && !midiStop) {
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    start_time = esp_log_timestamp();
                }
            } while (!midiStop && current_time < start_time + ev.time);

            if (midiStop) {
                return;
            }
            send_midi_event(&ev);
            continue;
        }

        if (event_type == 0xB0) {
            continue;
        }

        if (event_type == 0x80 || event_type == 0x90 || event_type == 0xE0) {
            if (midiStop) {
                return;
            }
            uint8_t hi = (uint8_t)(status_byte & 0xF0);
            MidiEvent ev;
            ev.status = hi;
            ev.channel = (uint8_t)(status_byte & 0x0F);
            ev.note = param1;
            ev.velocity = param2;
            ev.time = ms_at_tick(abs_tick, tp, tp_n, tpqn, us0);

            do {
                current_time = esp_log_timestamp();
                vTaskDelay(1 / portTICK_PERIOD_MS);
                while (midiPause && !midiStop) {
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    start_time = esp_log_timestamp();
                }
            } while (!midiStop && current_time < start_time + ev.time);

            if (midiStop) {
                return;
            }
            send_midi_event(&ev);
        }
    }
}

static void midi_stream_sequencer_task(void *pvParameters)
{
    (void)pvParameters;
    const MidiTempoPoint *tp = s_stream_tp;
    size_t tp_n = s_stream_tp_n;
    uint32_t us0 = s_stream_us0;
    long tstart = s_stream_track_start;
    long tend = s_stream_track_end;
    double tpqn = ticksPerQuarterNote;

    if (midiFile == NULL) {
        vTaskDelete(NULL);
        return;
    }

    FILE *file = fopen(midiFile, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "stream play: fopen failed %s", midiFile);
        vTaskDelete(NULL);
        return;
    }
    if (fseek(file, tstart, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "stream play: fseek failed");
        fclose(file);
        vTaskDelete(NULL);
        return;
    }

    stream_track_play(file, tend, tp, tp_n, tpqn, us0);
    fclose(file);
    vTaskDelete(NULL);
}

static int midi_parse_internal(bool force_full_events)
{
    MidiRawEvent *raw = NULL;
    MidiTempoPoint *tempos = NULL;
    FILE *file = NULL;

    midi_take();
    midi_events_free();
#if CONFIG_SPIRAM
    s_midi_raw_spiram = false;
#endif

    file = fopen(midiFile, "rb");

    if (file == NULL) {
        ESP_LOGW(TAG, "Failed to open MIDI file: %s", midiFile ? midiFile : "(null)");
        midi_give();
        return -1;
    }

    uint8_t sig[4];
    if (fread(sig, 1, 4, file) != 4 || sig[0] != 'M' || sig[1] != 'T' || sig[2] != 'h' || sig[3] != 'd') {
        ESP_LOGW(TAG, "Invalid MIDI: missing MThd");
        goto parse_fail;
    }

    uint32_t header_length;
    if (fread(&header_length, 4, 1, file) != 1) {
        goto parse_fail;
    }
    header_length = swap_endianness(header_length);

    uint8_t hd[6];
    if (header_length < 6 || fread(hd, 1, 6, file) != 6) {
        goto parse_fail;
    }
    format = (uint16_t)((hd[0] << 8) | hd[1]);
    tracks = (uint16_t)((hd[2] << 8) | hd[3]);
    division = (uint16_t)((hd[4] << 8) | hd[5]);
    if (header_length > 6) {
        fseek(file, (long)(header_length - 6), SEEK_CUR);
    }

    if ((division & 0x8000) == 0 && division != 0) {
        ticksPerQuarterNote = (double)division;
    } else {
        ticksPerQuarterNote = 480.0;
    }

    bool use_stream = !force_full_events && !midi_strum_step_mode && format == 0 && tracks == 1;

    if (use_stream) {
        size_t tempo_count = 0;
        size_t tempo_cap = 0;
        if (fread(sig, 1, 4, file) != 4 || sig[0] != 'M' || sig[1] != 'T' || sig[2] != 'r' || sig[3] != 'k') {
            ESP_LOGW(TAG, "Invalid MIDI: missing MTrk (stream)");
            goto parse_fail;
        }
        uint32_t track_length;
        if (fread(&track_length, 4, 1, file) != 1) {
            goto parse_fail;
        }
        track_length = swap_endianness(track_length);

        long track_start = ftell(file);
        long track_end = track_start + (long)track_length;
        s_midi_parse_err[0] = '\0';
        if (collect_tempo_from_track(file, track_end, &tempos, &tempo_count, &tempo_cap) != 0) {
            ESP_LOGW(TAG, "MIDI tempo scan (stream): %s",
                     s_midi_parse_err[0] != '\0' ? s_midi_parse_err : "unknown");
            goto parse_fail;
        }
        fclose(file);
        file = NULL;

        if (tempo_count > 0) {
            qsort(tempos, tempo_count, sizeof(MidiTempoPoint), cmp_tempo_point);
        }
        uint32_t us0 = (uint32_t)(1000000 * 60 / bpm);
        if (tempo_count > 0 && tempos[0].tick == 0) {
            us0 = tempos[0].us_per_qn;
        }
        microsecondsPerQuarterNote = (double)us0;

        s_stream_tp = tempos;
        tempos = NULL;
        s_stream_tp_n = tempo_count;
        s_stream_us0 = us0;
        s_stream_track_start = track_start;
        s_stream_track_end = track_end;
        s_midi_stream_mode = true;

        eventsCount = 0;
        events = NULL;

        midi_give();
        ESP_LOGI(TAG, "MIDI stream mode: %u tempo points, fmt %u tracks %u from %s",
                 (unsigned)tempo_count, (unsigned)format, (unsigned)tracks,
                 midiFile ? midiFile : "?");
        return 0;
    }

    fclose(file);
    file = NULL;
    file = fopen(midiFile, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Failed to reopen MIDI file: %s", midiFile ? midiFile : "(null)");
        midi_give();
        return -1;
    }

    if (fread(sig, 1, 4, file) != 4 || sig[0] != 'M' || sig[1] != 'T' || sig[2] != 'h' || sig[3] != 'd') {
        ESP_LOGW(TAG, "Invalid MIDI: missing MThd");
        goto parse_fail;
    }
    if (fread(&header_length, 4, 1, file) != 1) {
        goto parse_fail;
    }
    header_length = swap_endianness(header_length);

    if (header_length < 6 || fread(hd, 1, 6, file) != 6) {
        goto parse_fail;
    }
    format = (uint16_t)((hd[0] << 8) | hd[1]);
    tracks = (uint16_t)((hd[2] << 8) | hd[3]);
    division = (uint16_t)((hd[4] << 8) | hd[5]);
    if (header_length > 6) {
        fseek(file, (long)(header_length - 6), SEEK_CUR);
    }

    if ((division & 0x8000) == 0 && division != 0) {
        ticksPerQuarterNote = (double)division;
    } else {
        ticksPerQuarterNote = 480.0;
    }

    size_t raw_count = 0;
    size_t raw_cap = 0;
    size_t tempo_count = 0;
    size_t tempo_cap = 0;
    uint32_t seq_counter = 0;

    for (uint16_t tr = 0; tr < tracks; tr++) {
        if (fread(sig, 1, 4, file) != 4 || sig[0] != 'M' || sig[1] != 'T' || sig[2] != 'r' || sig[3] != 'k') {
            ESP_LOGW(TAG, "Invalid MIDI: missing MTrk (track %u)", (unsigned)tr);
            goto parse_fail;
        }

        uint32_t track_length;
        if (fread(&track_length, 4, 1, file) != 1) {
            goto parse_fail;
        }
        track_length = swap_endianness(track_length);

        long track_end = ftell(file) + (long)track_length;
        s_midi_parse_err[0] = '\0';
        if (parse_midi_track(file, track_end, &raw, &raw_count, &raw_cap,
                              &tempos, &tempo_count, &tempo_cap, &seq_counter) != 0) {
            ESP_LOGW(TAG, "MIDI parse error in track %u: %s", (unsigned)tr,
                     s_midi_parse_err[0] != '\0' ? s_midi_parse_err : "unknown");
            goto parse_fail;
        }
        long pos = ftell(file);
        if (pos != track_end) {
            /* Some encoders leave padding; align to chunk end. */
            if (pos < track_end) {
                fseek(file, track_end, SEEK_SET);
            } else {
                ESP_LOGW(TAG, "MIDI track %u over-read", (unsigned)tr);
                goto parse_fail;
            }
        }
    }

    fclose(file);
    file = NULL;

    if (tempo_count > 0) {
        qsort(tempos, tempo_count, sizeof(MidiTempoPoint), cmp_tempo_point);
    }
    if (raw_count > 0) {
        qsort(raw, raw_count, sizeof(MidiRawEvent), cmp_raw_event);
    }

    uint32_t us0 = (uint32_t)(1000000 * 60 / bpm);
    if (tempo_count > 0 && tempos[0].tick == 0) {
        us0 = tempos[0].us_per_qn;
    }
    microsecondsPerQuarterNote = (double)us0;

    events = midi_events_alloc(raw_count);
    if (raw_count > 0 && events == NULL) {
        goto parse_fail_nomidi;
    }
    eventsCount = raw_count;

    for (size_t i = 0; i < raw_count; i++) {
        uint8_t hi = (uint8_t)(raw[i].status & 0xF0);
        events[i].status = hi;
        events[i].channel = (uint8_t)(raw[i].status & 0x0F);
        events[i].note = raw[i].d1;
        events[i].velocity = raw[i].d2;
        events[i].time = ms_at_tick(raw[i].tick, tempos, tempo_count, ticksPerQuarterNote, us0);
    }

    free(raw);
    free(tempos);
    raw = NULL;
    tempos = NULL;

    /* Same absolute ms can occur for multiple notes; stable order by channel then note. */
    if (eventsCount > 0 && events != NULL) {
        qsort(events, eventsCount, sizeof(MidiEvent), compare_events);
    }

    s_midi_stream_mode = false;

    midi_give();
    ESP_LOGI(TAG, "MIDI parsed %u events, %u tracks, format %u from %s",
             (unsigned)eventsCount, (unsigned)tracks, (unsigned)format,
             midiFile ? midiFile : "?");
    return 0;

parse_fail:
    if (file) {
        fclose(file);
    }
parse_fail_nomidi:
    free(raw);
    free(tempos);
    midi_events_free();
    midi_give();
    return -1;
}

int midi_parse_current_file(void)
{
    return midi_parse_internal(false);
}

int midi_parse_current_file_events(void)
{
    return midi_parse_internal(true);
}

void midi_strum_step_set_enabled(bool enable)
{
    if (enable) {
        midiStop = true;
        vTaskDelay(pdMS_TO_TICKS(50));
        if (midi_parse_current_file_events() != 0) {
            ESP_LOGW(TAG, "MIDI strum-step: parse failed, mode not enabled");
            return;
        }
        midi_take();
        midi_step_event_index = 0;
        midi_strum_step_mode = true;
        midi_give();
        ESP_LOGI(TAG, "MIDI strum-step: %u events from %s", (unsigned)eventsCount, midiFile ? midiFile : "?");
    } else {
        midi_take();
        midi_strum_step_mode = false;
        midi_give();
    }
}

void midi_strum_step_reset_index(void)
{
    midi_take();
    midi_step_event_index = 0;
    midi_give();
}

bool midi_strum_step_try_note(void)
{
    if (!midi_strum_step_mode) {
        return false;
    }

    midi_take();
    if (eventsCount == 0) {
        midi_give();
        return false;
    }

    for (size_t i = midi_step_event_index; i < eventsCount; i++) {
        if (events[i].status == 0x90 && events[i].velocity > 0) {
            MidiEvent ev = events[i];
            midi_step_event_index = i + 1;
            midi_give();
            send_midi_event(&ev);
            return true;
        }
    }

    midi_step_event_index = 0;
    for (size_t i = 0; i < eventsCount; i++) {
        if (events[i].status == 0x90 && events[i].velocity > 0) {
            MidiEvent ev = events[i];
            midi_step_event_index = i + 1;
            midi_give();
            send_midi_event(&ev);
            return true;
        }
    }

    midi_give();
    return false;
}

void play_midi_file(void)
{
    if (midi_strum_step_mode) {
        ESP_LOGI(TAG, "play skipped: MIDI strum-step mode active");
        return;
    }
    if (midi_parse_current_file() != 0) {
        return;
    }
    if (s_midi_stream_mode) {
        xTaskCreate(&midi_stream_sequencer_task, "midi_stream", 8192, NULL, 5, NULL);
    } else {
        xTaskCreate(&midi_sequencer_task, "midi_sequencer_task", 4096, NULL, 5, NULL);
    }
}

// Function to read variable-length quantity (VLQ) from a file
uint32_t readVLQ(FILE *file) {
    uint32_t value = 0;
    uint8_t byte = 0;
    int n = 0;

    do {
        if (fread(&byte, 1, 1, file) != 1) {
            return value;
        }
        if (++n > 5) {
            ESP_LOGW(TAG, "MIDI parse: VLQ too long (corrupt file?)");
            return value;
        }
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

uint32_t read_variable_length(FILE *file) {
    uint32_t value = 0;
    uint8_t byte = 0;
    int n = 0;

    do {
        if (fread(&byte, 1, 1, file) != 1) {
            return value;
        }
        if (++n > 5) {
            ESP_LOGW(TAG, "MIDI parse: delta VLQ too long (corrupt file?)");
            return value;
        }
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

// MIDI output function (replace with actual MIDI output logic)
void send_midi_event(MidiEvent *event)
{
    uint8_t st = event->status;
    uint8_t hi = (uint8_t)(st & 0xF0);
    if (hi == 0xC0) {
        /* Program change: same convention as handleMessage 0x16 (util.c). */
        uint8_t prog = event->note;
        inputToUART((uint8_t)(0xC0 + semitoneChannel), 0x00, prog);
        inputToUART((uint8_t)(0xC0 + quartertoneChannel), 0x00, prog);
        return;
    }
    if (hi == 0xB0 && event->note == 0x40) {
        /* Sustain pedal (CC 64); same as sustain handling in util.c. */
        uint8_t val = event->velocity;
        inputToUART((uint8_t)(0xB0 + semitoneChannel), 0x40, val);
        inputToUART((uint8_t)(0xB0 + quartertoneChannel), 0x40, val);
        return;
    }
    if (hi == 0x80 || hi == 0x90 || hi == 0xE0) {
        st = (uint8_t)(hi | (semitoneChannel & 0x0F));
    }
    /* Pitch bend data is two raw bytes (LSB, MSB); do not transpose it. */
    if (hi == 0xE0) {
        inputToUART(st, event->note, event->velocity);
    } else {
        inputToUART(st, event->note + transposeValue, event->velocity);
    }
}

// MIDI sequencer task
void midi_sequencer_task(void *pvParameters) {

    size_t num_events = eventsCount;

    // Sort MIDI events by timestamp (no need, as already are sorted in midi file)
    //qsort(events, num_events, sizeof(MidiEvent), compare_events);

    uint32_t current_time;
    uint32_t start_time = esp_log_timestamp() ;//this is the time we start playing notes


    for (size_t i = 0; i < num_events; ++i) {

        
        if(midiStop)
            break; //if midi stop is enabled, do not bother to continue sending midi events 
        
        // Wait until it's time to send the MIDI event. Keep checking current time until it passes the start-time + event-time
        do {
            current_time = esp_log_timestamp();
            vTaskDelay(1 / portTICK_PERIOD_MS);
            while(midiPause && !midiStop){ //wait until pause is removed, but if midi stop is enabled while waiting , exit this wait
                vTaskDelay(1 / portTICK_PERIOD_MS);
                start_time = esp_log_timestamp(); //when we pause, we should move the start-time forward so notes wont expire
           }

        } while (current_time < start_time + events[i].time   );

        // Send the MIDI event
        send_midi_event(&events[i]);
    }

    
    vTaskDelete(NULL);
}




// void read_file_from_spiffs() {
//     FILE *file = fopen(FILE_PATH, "r");
//         ESP_LOGD(TAG, "entered read from spiffs\n");

    
//     if (file == NULL) {
//         ESP_LOGE(TAG, "Failed to open file for reading");
//         return;
//     }

//     fseek(file, 0, SEEK_END);
//     long file_size = ftell(file);
//     fseek(file, 0, SEEK_SET);

//     ESP_LOGD(TAG, "midi file size %d" , (int)file_size );

//     char *file_content = (char *)malloc(file_size + 1);

//     if (file_content == NULL) {
//         ESP_LOGE(TAG, "Memory allocation error");
//         fclose(file);
//         return;
//     }

    
//     fread(file_content, 1, file_size, file);
//     file_content[file_size] = '\0';

//     fclose(file);

//     // Process the file content as needed
//     ESP_LOGD(TAG, "File Content:\n%s", file_content);

//     free(file_content);
// }

// void read_embedded_file() {
//     // Calculate the size of the embedded file
//     size_t file_size = jmayram_mid_end - jmaryam_mid_start;


//     // Access the file's data
//     printf("Embedded File Content:\n");
//     for (size_t i = 0; i < file_size; i++) {
//         putchar(jmaryam_mid_start[i]);
//     }
//     printf("\n");
// }