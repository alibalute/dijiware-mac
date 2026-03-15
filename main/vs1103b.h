/**
 * @file vs1103b.h
 * @author Phil Hilger (phil@peergum.com)
 * @brief 
 * @version 0.1
 * @date 2022-07-15
 * 
 * Copyright (c) 2022 Waterloo Tech Inc.
 * 
 */

#ifndef __VS1103B_H_
#define __VS1103B_H_

#include "esp_err.h"
#include "driver/spi_master.h"

#define MIN(a,b) ((a<b)?a:b)

#define VS1103B_SPI_MASTER_FREQ 500000

#define WRITE_INSTRUCTION 0x02
#define READ_INSTRUCTION 0x03

#define SCI_MODE 0x00
#define SCI_STATUS 0x01
#define SCI_BASS 0x02
#define SCI_CLOCKF 0x03 // control internal clock (n x XTALI)
#define SCI_DECODE_TIME 0x04 // decoding MIDI duration
#define SCI_AUDATA 0x05 // do not use on vs1103b
#define SCI_WRAM 0x06 // upload app program
#define SCI_WRAMADDR 0x07 // program address
#define SCI_IN0 0x08 // write SCI stream
#define SCI_IN1 0x09 // length of SCI stream
#define SCI_AIADDR 0x0a // program start address
#define SCI_VOL 0x0b // volume control
#define SCI_MIXERVOL 0x0c // gains1/2/3
#define SCI_ADPCMRECCTL 0x0d // gain4
#define SCI_AICTRL2 0x0e // program
#define SCI_AICTRL3 0x0f // program

#define SM_DIFF 0x0001
#define SM_RECORD_PATH 0x0002
#define SM_RESET 0x0004
#define SM_OUTOFMIDI 0x0008
#define SM_PDOWN 0x0010
#define SM_TESTS 0x0020
#define SM_ICONF 0x00c0
#define SM_DACT 0x0100
#define SM_SDIORD 0x0200
#define SM_SDISHARE 0x0400
#define SM_SDINEW 0x0800
#define SM_EARSPEAKER 0x3000
#define SM_LINE_IN 0x4000
#define SM_ADPCM 0x8000

#define SM_DEFAULT 0x0000

#define XTALI 0
#define XTALIx1_5 1
#define XTALIx2_0 2
#define XTALIx2_5 3
#define XTALIx3_0 4
#define XTALIx3_5 5
#define XTALIx4_0 6
#define XTALIx4_5 7

#define SARC_DREQ512 0x0080
#define SARC_OUTOFADPCM 0x0040
#define SARC_MANUALGAIN 0x0020
#define SARC_GAIN4 0x001f

extern void vs1103b_init(uint8_t hostId);
extern void resetAudioChip();
extern void vs1103b_resetAudio(void);
extern void setClockF(uint16_t multiplier, uint32_t freq);
extern void setBassAndTrebleBoost(uint16_t bassBoost, uint16_t bassCut,
                               uint16_t trebleBoost, uint16_t trebleCut);
extern esp_err_t setVolume(uint8_t volume, bool usingHeadphone);
extern esp_err_t vs1103b_read_status(uint16_t *out_status);
extern void setGain(uint16_t gain1, uint16_t gain2, uint16_t gain3,
                    uint16_t gain4, bool enabled);
#endif // __VS1103B_H_