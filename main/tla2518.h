/**
 * @file tla2518.h
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#ifndef __TLA2518_H_
#define __TLA2518_H_

#include "tla2528.h"
#include "driver/spi_master.h"

#define TLA2518_SPI_MASTER_FREQ 100000UL //SPI_MASTER_FREQ_10M //(APB_CLK_FREQ/100)

#define NO_OPERATION 0x00
#define SINGLE_REGISTER_READ 0x10
#define SINGLE_REGISTER_WRITE 0x08
#define SET_BIT 0x18
#define CLEAR_BIT 0x20

#define NUM_ADC_CHANNELS 8
extern void tla2518_init(uint8_t hostId);
extern void sampleChannels(uint8_t channelMask);
extern void sampleChannel(uint8_t channel);
extern uint16_t getSample(int channel);
#endif // __TLA2518_H_