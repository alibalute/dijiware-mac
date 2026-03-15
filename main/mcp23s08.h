/**
 * @file mcp23s08.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#ifndef __MCP23S08_H_
#define __MCP23S08_H_

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "pins.h"

extern void mcp23s08_init(uint8_t hostId);
extern void mcp23s08_set_level(int channel, bool level);
extern bool mcp23s08_get_level(int channel);

#define MCP23S08_SPI_MASTER_FREQ SPI_MASTER_FREQ_8M

#define CMD_READ 0x41
#define CMD_WRITE 0x40

#define IODIR 0x00
#define IPOL 0x01
#define GPINTEN 0x02
#define DEFVAL 0x03
#define INTCON 0x04
#define IOCON 0x05
#define GPPU 0x06
#define INTF 0x07
#define INTCAP 0x08
#define GPIO 0x09
#define OLAT 0x0a

#endif // __MCP23S08_H_