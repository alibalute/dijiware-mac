/*
 * USB MIDI Driver
 *
 * See README.md for usage hints
 *
 * =============================================================================
 *
 * MIT License
 *
 * Copyright (c) 2020 Thorsten Klose (tk@midibox.org)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * =============================================================================
 */

#ifndef _USBMIDI_H
#define _USBMIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


#ifndef USBMIDI_RX_TIMEOUT_MS
#define USBMIDI_RX_TIMEOUT_MS 2000
#endif

#ifndef USBMIDI_ENABLE_CONSOLE
#define USBMIDI_ENABLE_CONSOLE 1
#endif

typedef size_t (*usbmidi_write_bytes_t)(int itf, const uint8_t *buffer, size_t len);
typedef size_t (*usbmidi_get_buffered_data_len_t)(int itf, size_t *len);
typedef size_t (*usbmidi_read_bytes_t)(int itf, uint8_t *buffer, size_t len);

/**
 * @brief Initializes the USB MIDI Drvier
 *
 * @param  callback_midi_message_received References the callback function
 * which is called whenever a new MIDI message has been received. API see
 * usbmidi_receive_packet_callback_for_debugging Specify NULL if no callback
 * required in your application.
 *
 * @return < 0 on errors
 *
 */
extern int32_t usbmidi_init(void *callback_midi_message_received,
             usbmidi_write_bytes_t _usbmidi_write_bytes,
             usbmidi_get_buffered_data_len_t _usbmidi_get_buffered_data_len,
             usbmidi_read_bytes_t _usbmidi_read_bytes);

/**
 * @brief Enables a USB for sending/receiving MIDI messages.
 *
 * @param  itf    USB interface
 * @param  baudrate       should be 31250 for standard MIDI baudrate, or for example 115200 for "Hairless MIDI" over USB bridge
 *
 * @return < 0 on errors
 *
 */
extern int32_t usbmidi_enable_port(int itf, uint32_t baudrate);

/**
 * @brief Disables USB midi
 *
 * @param  itf    USB interface
 * @return < 0 on errors
 *
 */
extern int32_t usbmidi_disable_port(int itf);

/**
 * @brief Returns MIDI Port Status
 *
 * @param  itf    USB interface
 * @return != 0 if USB Port is enabled
 *
 */
//
////////////////////////////////////////////////////////////////////////////////////////////////////
extern int32_t usbmidi_get_enabled(int itf);

/**
 * @brief Sends a MIDI message over USB
 *
 * @param  itf    USB interface
 * @param  stream        output stream
 * @param  len           output stream length
 *
 * @return < 0 on errors
 *
 */
extern int32_t usbmidi_send_message(uint8_t *stream, size_t len);

/**
 * @brief A dummy callback which demonstrates the usage.
 *        It will just print out incoming MIDI messages on the terminal.
 *        You might want to implement your own for doing something more useful!
 * 
 * @param  midi_status  the MIDI status byte (first byte of a MIDI message)
 * @param  remaining_message the remaining bytes
 * @param  len size of the remaining message
 * @param  continued_sysex_pos on ongoing SysEx stream
 *
 * @return < 0 on errors
 */
extern void usbmidi_receive_message_callback_for_debugging(int itf, uint8_t midi_status, uint8_t *remaining_message, size_t len, size_t continued_sysex_pos);

/**
 * @brief This function should be called each mS to service incoming MIDI messages
 *
 * @return < 0 on errors
 */  
extern int32_t usbmidi_tick(void);

#if USBMIDI_ENABLE_CONSOLE
/**
 * @brief Register Console Commands
 *
 * @return < 0 on errors
 */
extern void usbmidi_register_console_commands(void);
#endif
#ifdef __cplusplus
}
#endif

#endif /* _USBMIDI_H */
