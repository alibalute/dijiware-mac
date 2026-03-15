/**
 * @file adc.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#ifndef __ADC_H_
#define __ADC_H_

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
// #include "esp_adc_cal.h"

#define ADC_MAX_VOLTAGE_ATTEN_DB_0 950
#define ADC_MAX_VOLTAGE_ATTEN_DB_2_5 1250
#define ADC_MAX_VOLTAGE_ATTEN_DB_6 1750
#define ADC_MAX_VOLTAGE_ATTEN_DB_11 3100
// ADC Settings
#define ADC_ATTEN ADC_ATTEN_DB_11
#define ADC_MAX_VOLTAGE ADC_MAX_VOLTAGE_ATTEN_DB_11
#define ADC_WIDTH_BIT ADC_WIDTH_BIT_12 // default for ESP32-S3
#define ADC_SAMPLES 16

// ADC Calibration
#if CONFIG_IDF_TARGET_ESP32
#define ADC_EXAMPLE_CALIBRATION_SCHEME ESP_ADC_CAL_VAL_EFUSE_VREF
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_EXAMPLE_CALIBRATION_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_EXAMPLE_CALIBRATION_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32S3
#define ADC_EXAMPLE_CALIBRATION_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP_FIT
#endif

#define EXT_ADC_MAX_VALUE 1023
#define NUM_STRINGS 4

// #define STRUM_ONE_ADC_CHANNEL ADC1_CHANNEL_0
// #define STRUM_TWO_ADC_CHANNEL ADC1_CHANNEL_1
// #define STRUM_THREE_ADC_CHANNEL ADC1_CHANNEL_2
// #define STRUM_FOUR_ADC_CHANNEL ADC1_CHANNEL_3
// #define STRUM_FIVE_ADC_CHANNEL ADC1_CHANNEL_MAX
// #define STRUM_SIX_ADC_CHANNEL ADC1_CHANNEL_MAX
// #define STRUM_SEVEN_ADC_CHANNEL ADC1_CHANNEL_MAX
// #define STRUM_EIGHT_ADC_CHANNEL ADC1_CHANNEL_MAX

#define STRING_MIN 0
#define STRING_MAX EXT_ADC_MAX_VALUE
#define STRUM_MIN 100
#define STRUM_MAX ((1 << ADC_WIDTH_BIT) - STRUM_MIN - 1)
#define ACCEL_MIN 0
#define ACCEL_MAX ((1 << ADC_WIDTH_BIT) - 1)

enum {
  ADC_CHANNEL_STRING1 = 0,
  ADC_CHANNEL_STRING2,
  ADC_CHANNEL_STRING3,
  ADC_CHANNEL_STRING4,
  ADC_CHANNEL_STRUM1,
  ADC_CHANNEL_STRUM2,
  ADC_CHANNEL_STRUM3,
  ADC_CHANNEL_STRUM4,
};

// future extension (second ADC)
// enum {
//   ADC_CHANNEL_STRING5 = 0,
//   ADC_CHANNEL_STRING6,
//   ADC_CHANNEL_STRING7,
//   ADC_CHANNEL_STRING8,
//   ADC_CHANNEL_STRUM5,
//   ADC_CHANNEL_STRUM6,
//   ADC_CHANNEL_STRUM7,
//   ADC_CHANNEL_STRUM8,
// };

enum {
  STRING_ONE = 0,
  STRING_TWO,
  STRING_THREE,
  STRING_FOUR,
  // STRING_FIVE,
  // STRING_SIX,
  // STRING_SEVEN,
  // STRING_EIGHT,
  STRING_LAST,
  STRUM_ONE = STRING_LAST,
  STRUM_TWO,
  STRUM_THREE,
  STRUM_FOUR,
  // STRUM_FIVE,
  // STRUM_SIX,
  // STRUM_SEVEN,
  // STRUM_EIGHT,
  STRUM_LAST,
  ACCELEROMETER = STRUM_LAST,
};

#define STRING_CHANNELS 0x0f
#define STRUM_CHANNELS 0xf0

#define MAX_VOLUME 3054UL

#define ABS(a,b) ((a>b)?(a-b):(b-a))

extern void adc_calibration_init(void);
extern uint16_t readBattery(void);
extern uint16_t readAccelerometer(void);
extern uint16_t readAndAverageAccelerometer(bool filtered);
extern uint16_t readAndAverageStrum(int position);
extern uint16_t readAndAverageString(int position);
extern uint32_t readVolumeVoltage(void);
extern uint8_t readVolume(void);
extern uint16_t readADC(int v_channel);
extern uint16_t readAndAverageADC(int v_channel, bool filtered);

#endif // __ADC_H_