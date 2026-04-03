/**
 * @file midi_recorder.h
 * @brief Record outbound MIDI (note on/off, pitch bend) to SPIFFS for playback via midi_player.
 */

#ifndef MIDI_RECORDER_H
#define MIDI_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/** Written on stop; play (0x52,2) uses this path after a successful recording. */
#define MIDI_REC_FILENAME "/spiffs/recording.mid"

void midi_recorder_init(void);
bool midi_recorder_is_active(void);
/** Clears buffer, arms capture (call after stopping playback if needed). */
void midi_recorder_start(void);
/** If recording: write SMF to MIDI_REC_FILENAME and setMidiFile(); always clears active flag. */
void midi_recorder_stop_and_finalize(void);
/** Note on/off and pitch bend: from inputToUART when uart_en; else from midiTx. */
void midi_recorder_capture(uint8_t status, uint8_t data1, uint8_t data2);

#endif
