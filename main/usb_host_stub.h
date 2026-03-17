/**
 * Stub for macOS/host parsing only. usb.h pulls in tinyusb.h which uses
 * section attributes invalid for Mach-O. ESP32 builds never include this.
 */
#ifndef USB_HOST_STUB_H
#define USB_HOST_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PORT_SPEED 115200U

extern bool isUSBConnected(void);
extern size_t usb_write_bytes(int itf, const uint8_t *buffer, size_t len);

#endif
