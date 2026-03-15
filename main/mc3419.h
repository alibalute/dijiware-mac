/**
 * @file mc3419.h
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#ifndef __MC3419_H_
#define __MC3419_H_

#include "driver/spi_master.h"

#define MC3419_SPI_MASTER_FREQ SPI_MASTER_FREQ_10M //(APB_CLK_FREQ/64)

#define NO_OPERATION 0x00
#define SINGLE_REGISTER_READ 0x10
#define SINGLE_REGISTER_WRITE 0x08
#define SET_BIT 0x18
#define CLEAR_BIT 0x20

#define NUM_ADC_CHANNELS 8

enum {
  DEV_STAT = 0x05,
  INTR_CTRL,
  MODE,
  SR,
  MOTION_CTRL,
  FIFO_STAT,
  FIFO_RD_P,
  FIFO_WR_P,
  XOUT_EX_L,
  XOUT_EX_H,
  YOUT_EX_L,
  YOUT_EX_H,
  ZOUT_EX_L,
  ZOUT_EX_H,
  STATUS,
  INTR_STAT,
  RANGE = 0x20,
  XOFFL,
  XOFFH,
  YOFFL,
  YOFFH,
  ZOFFL,
  ZOFFH,
  XGAIN,
  YGAIN,
  ZGAIN,
  FIFO_CTRL = 0x2D,
  FIFO_TH,
  FIFO_INTR,
  FIFO_CTRL2_SR2,
  COMM_CTRL,
  GPIO_CTRL = 0x33,
  TF_THRESH_LSB = 0x40,
  TF_THRESH_MSB,
  TF_DB,
  AM_THRESH_LSB,
  AM_THRESH_MSB,
  AM_DB,
  SHK_THRESH_LSB,
  SHK_THRESH_MSB,
  PK_P2P_DUR_THRESH_LSH,
  PK_P2P_DUR_THRESH_MSH,
  TIMER_CTRL,
  RD_CNT,
};

extern void mc3419_init(uint8_t hostId);
extern uint16_t get_acceleration(void);

// extern void sampleChannels(uint8_t channels);
#endif // __MC3419_H_