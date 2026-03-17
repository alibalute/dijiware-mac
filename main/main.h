/**
 * @file Main.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief 
 * @version 0.1
 * @date 2022-05-17
 * 
 * (c) Copyright 2022, Waterloo Tech Inc.
 * 
 */

#ifndef __MAIN_H_
#define __MAIN_H_

#if defined(__APPLE__) || defined(IDF_HOST_PARSING)
/* Minimal stub for macOS/host parsing; avoid including any ESP-IDF headers
 * that use section attributes invalid for Mach-O. */
#include "etar.h"
typedef int gpio_num_t;
#else
#include "pins.h"
#include "adc.h"
#include "gpio.h"
#include "i2c_adc.h"
#include "spi.h"
#include "etar.h"
#endif

#ifndef ABS
#define ABS(a,b) ((a>b)?(a-b):(b-a))
#endif

extern void init_usb_serial(void);
extern void start_uart_midi_task(void);
extern void start_ble_midi_task(gpio_num_t ledBt);
extern void start_usb_midi_task(void);
extern void start_metronome_task(void);
extern void start_metronome_timer(void);
extern void change_metronome_speed(uint8_t bpm);

extern void inputToUART(uint8_t, uint8_t, uint8_t);

extern uint32_t millis(void);
extern void stop_timer(void);
extern void start_timer(void);
extern void muteOnPowerChange(void);
extern void mute(bool shouldmute);
void app_main(void);
void init_pins(void);
extern void reset(void);

//global variable
extern uint8_t batteryLevel;
extern bool isBLEConnected(void);

#endif  // __MAIN_H_