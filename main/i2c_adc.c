/**
 * @file i2c_adc.cpp
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#include "i2c_adc.h"
#include "adc.h"

/**
 * @brief init I2C bus as master and install driver
 *
 */
void init_i2c_adc(void) {
  i2c_config_t config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = SDA,
      .scl_io_num = SCL,
      .sda_pullup_en = false,
      .scl_pullup_en = false,
      .master.clk_speed = 400000UL,
      .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,
  };
  i2c_param_config(0, &config);
  i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, ESP_INTR_FLAG_LOWMED);

  init_i2c_chip(STRING_ADC_ADDR);
  init_i2c_chip(STRUM_ADC_ADDR);
}

/**
 * @brief initialize adc chips
 *
 * @param addr
 * @return esp_err_t
 */
esp_err_t init_i2c_chip(uint8_t addr) {
  // nothing to do for now
  return ESP_OK;
}

/**
 * @brief read a channel (mapped to PIC schematics)
 *
 * @param v_channel
 * @return uint32_t
 */
uint32_t read_sample(int v_channel) {
  uint8_t addr = 0, channel = 0;
  if (v_channel < STRING_ONE || v_channel > STRING_EIGHT) {
    return 0;
  }

  addr = STRUM_ADC_ADDR;
  channel = v_channel - STRING_ONE; // channel# same as string#

  // read strings from external ADC
  uint8_t tx_data[2], rx_data[2];
  uint8_t tx_len = 0, rx_len;
  tx_data[tx_len++] = (addr << 1) | I2C_MASTER_WRITE;
  tx_data[tx_len++] = (1 << (channel + 4)); // select channel to sample
  rx_len = 2;
  uint32_t sample = 0;

  if (ESP_OK == i2c_master_write_read_device(I2C_PORT, addr, tx_data, tx_len,
                                             rx_data, rx_len,
                                             pdMS_TO_TICKS(1))) {
    sample = (((uint16_t)rx_data[0]) << 8) + rx_data[1];
  }
  return sample;
}