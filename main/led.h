/**
 * @file led.h
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief 
 * @version 0.1
 * @date 2022-09-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __LED_H_
#define __LED_H_

#include <stdint.h>

typedef enum {
  LED_OFF,
  LED_SOLID,
  LED_BLINK,
  LED_BLINK_FAST,
  LED_BLINK_SLOW,
  LED_BLINK_EXTRA_FAST,
} LedMode;

extern void initLeds(void);
extern void batteryLed(uint32_t voltage, LedMode mode);
extern void powerLed(LedMode mode);
extern void ledLoop(void);
extern uint8_t batteryLevel;

#endif // __LED_H_