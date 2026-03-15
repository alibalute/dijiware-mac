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

#include <stdint.h>
#include "tinyusb.h"
// #include "tusb_cdc_acm.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define PORT_SPEED 115200U

extern bool isUSBConnected(void);
extern size_t usb_write_bytes(int itf, const uint8_t *buffer, size_t len);

#endif // __USB_H_