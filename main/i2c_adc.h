/**
 * @file i2c_adc.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#ifndef __I2C_ADC_H_
#define __I2C_ADC_H_

#include <stdbool.h>
#include <stdint.h>
#include "pins.h"
#include "driver/i2c.h"
#include "esp_err.h"

enum {
  AD7994_CONVERSION_RESULT_REGISTER,
  AD7994_ALERT_STATUS_REGISTER,
  AD7994_CONFIGURATION_REGISTER,
  AD7994_CYCLE_TIMER_REGISTER,
  AD7994_DATA_LOW_1_REGISTER,
  AD7994_DATA_HIGH_1_REGISTER,
  AD7994_HYSTERESIS_1_REGISTER,
  AD7994_DATA_LOW_2_REGISTER,
  AD7994_DATA_HIGH_2_REGISTER,
  AD7994_HYSTERESIS_2_REGISTER,
  AD7994_DATA_LOW_3_REGISTER,
  AD7994_DATA_HIGH_3_REGISTER,
  AD7994_HYSTERESIS_3_REGISTER,
  AD7994_DATA_LOW_4_REGISTER,
  AD7994_DATA_HIGH_4_REGISTER,
  AD7994_HYSTERESIS_4_REGISTER,
};

extern void init_i2c_adc(void);
extern esp_err_t init_i2c_chip(uint8_t addr);
extern uint32_t read_sample(int v_channel);

#endif // __I2C_ADC_H_