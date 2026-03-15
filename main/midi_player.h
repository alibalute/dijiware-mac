
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


typedef struct {
    uint8_t status;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    uint32_t time;
} MidiEvent;

void play_midi_file(void); //plays the midi file loaded into char * midiFile

uint32_t read_variable_length(FILE *file);

void send_midi_event( MidiEvent *event);
void midi_sequencer_task(void *pvParameters) ;
int compare_events( void *a,  void *b);
uint32_t readVLQ(FILE *file);
extern char * midiFile;
extern bool midiPause;
extern bool midiStop;
extern void inputToUART(uint8_t, uint8_t, uint8_t);
extern uint32_t millis(void);


