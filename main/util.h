/* 
 * File:   util.h
 * Author: BaluteInc
 *
 * Created on July 17, 2013, 3:06 PM
 */

#ifndef UTIL_H_GUARD
#define	UTIL_H_GUARD

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

extern bool instrumentSelected;
extern char * midiFile;
void setMidiFile(const char* newString);
extern bool midiPause;
extern bool midiStop ;

//Calibrate Functions
void strumCalibrate(void);
void stringCalibrate(int i , bool recalibrate);
void joystickCalibrate(void);

//Message Handler Functions
void handleMessage(int8_t, int8_t);
void handleUsbMessage(uint8_t, uint8_t);
void handleMidiMessage(uint8_t midi_status, uint8_t *remaining_message, size_t len, size_t continued_sysex_pos);

/** Load settings from SPIFFS and apply them. Call at boot to restore saved preferences. */
void load_settings_at_boot(void);

// Flash Memory Functions
uint8_t readFromFlash(uint32_t);
void memcpyram2flash(uint32_t, float[]);
void writeSettingsToFlash(uint32_t, uint8_t[]);

//Misc Functions
void delay(void);
void delayFor(int);
int absoluteDiff(int, int);
float fAbsoluteDiff(float, float);
//void usbReconfig(void);

/* Boot no-sound debug: reset before burst, then read fail/retry counts after intro */
void boot_uart_stats_reset(void);
void boot_uart_stats_log(const char *tag);
uint32_t boot_uart_get_fail_count(void);
uint32_t boot_uart_get_retry_count(void);

//UART Functions
#ifdef uart_en
    void start_uart(void);
    void stop_uart(void);
    void inputToUART(uint8_t, uint8_t, uint8_t);
    void inputToUART2(uint8_t, uint8_t);  /* 2-byte messages (e.g. Program Change) */
    void uartNoteOn(STRUM *, uint8_t ,  uint8_t);
    void uartNoteOff(STRUM *, uint8_t);
    void tapUartNoteOn(TAP *, uint8_t, uint8_t);
    void tapUartNoteOff(TAP *, uint8_t);
#endif


//metronome functions
extern void start_metronome_timer(void);
extern void change_metronome_speed(uint8_t val);
extern void stop_metronome_timer(void);

extern void change_metronome_volume(int value);
extern void change_metronome_numBeats(int value);


#endif	/* UTIL_H */

