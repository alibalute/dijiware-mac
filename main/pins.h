/**
 * @file pins.h
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-31
 *
 * @copyright Copyright (c) 2022 WTI
 *
 */

#ifndef __PINS_H_
#define __PINS_H_

#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "driver/i2c.h"


// --- I/O Expander

#define N_EX1_CS GPIO_NUM_16
#define N_EX2_CS GPIO_NUM_10
#define EX_INT GPIO_NUM_46 // not currently used

// first expander: channels 0-7
#define ACC_PWR 0
#define EX_EN5V 1
#define EX_EN4V 2
#define EX_N_VS_RST 3
#define EX_N_HP_MUTE 4
#define EX_N_SPK_MUTE 5
#define EX_N_DAC_MUTE 6

// second expander: channels 8-15
#define EX_HP_DETECT 8
#define EX_N_ACOK 9
#define EX_N_BOOST 10
#define EX_AXL_INT 11
#define EX_N_CHG_DETECT 12

// --- LEDs

#define LED_PWR GPIO_NUM_4
#define LED_BT GPIO_NUM_5
#define LED_BATT_G GPIO_NUM_6
#define LED_BATT_R GPIO_NUM_7

// --- Buttons

#define PWR_BTN GPIO_NUM_21
#define CMD_BTN GPIO_NUM_14

// --- Volume

#define VOL_ADC_UNIT ADC_UNIT_1
#define VOL_ADC_CHANNEL ADC_CHANNEL_0

// --- common SPI

#define MISO GPIO_NUM_11  // FSPID
#define SCLK GPIO_NUM_12  // FSPICLK
#define MOSI GPIO_NUM_13  // FSPIQ

// --- VS chip SPI/SDI

#define VS_MISO GPIO_NUM_35 // SUBSPID
#define VS_SCLK GPIO_NUM_36 // SUBSPICLK
#define VS_MOSI GPIO_NUM_37 // SUBSPIQ
#define VS_DREQ GPIO_NUM_38 // data request

#define N_VS_CS GPIO_NUM_42
#define N_VS_XDCS GPIO_NUM_47

// --- MIC

#define MIC_SCLK GPIO_NUM_39
#define MIC_SD GPIO_NUM_40
#define MIC_WS GPIO_NUM_41

// --- MIDI

#define MIDI_TX GPIO_NUM_17  // to MIDI RX on VS1103b
#define MIDI_RX GPIO_NUM_18  // from MIDI TX on VS1103b

// #define MIDI_TX_PI GPIO_NUM_17 // to MIDI RX on PI
// #define MIDI_RX_PI GPIO_NUM_18 // from MIDI TX on PI

// --- Accessory

#define ACC_DETECT GPIO_NUM_15

#define SCL GPIO_NUM_8
#define SDA GPIO_NUM_9

// --- Strings & Strums (I2C ADCs)

// --- Strings and Strums (SPI ADC)

#define N_ADC_CS GPIO_NUM_0

enum {
  STRING_1 = 0,
  STRING_2,
  STRING_3,
  STRING_4,
};

enum {
  STRUM_1 = 0,
  STRUM_2,
  STRUM_3,
  STRUM_4,
};

// --- Accelerometer

#define N_AXL_CS GPIO_NUM_48

// --- Battery & Power

#define PWR_EN GPIO_NUM_45

#define BATT_ADC_UNIT ADC_UNIT_1
#define BATT_ADC_CHANNEL ADC_CHANNEL_1

// --- SD Card

#define N_SD_CS GPIO_NUM_3
#endif  //__PINS_H_