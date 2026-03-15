/**
 * @file metronome.h
 * @author Ali
 * @brief
 * @version 0.1
 * @date 2023-12-30
 *
 * Copyright (c) 2023 elemental ID
 */
extern void inputToUART(uint8_t, uint8_t, uint8_t);
void change_metronome_speed(uint8_t bpm);
void start_metronome_timer(void);
void stop_metronome_timer(void);
void change_metronome_volume(int value);
void change_metronome_numBeats(int value);

int get_metronome_bpm(void);
int get_metronome_numBeats(void);
int get_metronome_volume(void);
