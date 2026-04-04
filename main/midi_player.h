
/**
 * @file midi_player.h
 * @author Ali
 * @brief
 * @version 0.1
 * @date 2024-1-3
 *
 * Copyright (c) 2023 elemental ID
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


typedef struct {
    uint8_t status;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    uint32_t time;
} MidiEvent;

void play_midi_file(void); //plays the midi file loaded into char * midiFile
/** Load midiFile into events[] (used by play and strum-step mode). Returns 0 on success. */
int midi_parse_current_file(void);

uint32_t read_variable_length(FILE *file);

void send_midi_event( MidiEvent *event);
void midi_sequencer_task(void *pvParameters) ;
int compare_events(const void *a, const void *b);
uint32_t readVLQ(FILE *file);
extern char * midiFile;
extern bool midiPause;
extern bool midiStop;
extern void inputToUART(uint8_t, uint8_t, uint8_t);
extern uint32_t millis(void);

/** Strum-step mode: each strum plays next note-on from current midiFile (handleMessage 0x57; 0x56 is sympathetic volume). */
extern bool midi_strum_step_mode;

void midi_strum_step_set_enabled(bool enable);
void midi_strum_step_reset_index(void);
/** Advance to next note-on in parsed file and send it. Returns true if a note was sent. */
bool midi_strum_step_try_note(void);


