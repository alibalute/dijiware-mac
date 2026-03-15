/**
 * @file spi.h
 * @author Phil Hilger (phil@peergum.com)
 * @brief 
 * @version 0.1
 * @date 2022-07-15
 * 
 * Copyright (c) 2022 Waterloo Tech Inc.
 * 
 */

#ifndef __SPI_H_
#define __SPI_H_

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "pins.h"

#define HOST_ID SPI2_HOST

extern void spi_init(uint8_t hostId, gpio_num_t miso, gpio_num_t mosi,
                     gpio_num_t sclk);

#endif // __SPI_H_