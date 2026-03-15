/**
 * @file interfaces.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief 
 * @version 0.1
 * @date 2022-05-17
 * 
 * (c) Copyright 2022, Waterloo Tech Inc.
 * 
 */

#ifndef __INTERFACES_H_
#define __INTERFACES_H_

#include "pins.h"
#include "gpio.h"
#include "mcp23s08.h"

// status checks

extern bool isHeadPhoneConnected(void);
extern bool isCmdButtonPressed(void);
extern bool isExternalPower(void);
extern bool isCharging(void);
extern bool isPowerBoost(void);

extern void checkInputs(void);

// audio

extern void enableDAC(void);
extern void disableDAC(void);
extern void enable5VSwitch(void);
extern void disableAudioBoost(void);
extern void enableSpeaker(void);
extern void disableSpeaker(void);
extern void enableHeadphone(void);
extern void disableHeadphone(void);

extern void resetAudioChip(void);
extern void vs1103b_resetAudio(void);

// strums

extern void enableStrumPower(void);
extern void disableStrumPower(void);

// leds

extern void setLed(gpio_num_t pin, bool level);

#endif // __INTERFACES_H_