/**
 * @file usb.h
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#ifndef __USB_H_
#define __USB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(__APPLE__)
#include "usb_host_stub.h"
#else
#include "tinyusb.h"
#endif
// #include "tusb_cdc_acm.h"

#define PORT_SPEED 115200U

extern bool isUSBConnected(void);
extern size_t usb_write_bytes(int itf, const uint8_t *buffer, size_t len);

#endif // __USB_H_