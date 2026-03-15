/**
 * @file mcp23s18.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#ifndef __MCP23S18_H_
#define __MCP23S18_H_

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "pins.h"

extern void mcp23s18_init(uint8_t hostId);
extern void mcp23s18_set_level(int channel, bool level);
extern bool mcp23s18_get_level(int channel);

#define MCP23S18_SPI_MASTER_FREQ SPI_MASTER_FREQ_8M

#define CMD_READ 0x40
#define CMD_WRITE 0x41

#define IODIRA 0x00
#define IODIRB 0x01
#define IPOLA 0x02
#define IPOLB 0x03
#define GPINTENA 0x04
#define GPINTENB 0x05
#define DEFVALA 0x06
#define DEFVALB 0x07
#define INTCONA 0x08
#define INTCONB 0x09
#define IOCON 0x0a
#define GPPUA 0x0c
#define GPPUB 0x0d
#define INTFA 0x0e
#define INTFB 0x0f
#define INTCAPA 0x10
#define INTCAPB 0x11
#define GPIOA 0x12
#define GPIOB 0x013
#define OLATA 0x14
#define OLATB 0x15

#endif // __MCP23S18_H_