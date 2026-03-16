/**
 * @file pic-midi.h
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief 
 * @version 0.1
 * @date 2022-06-01
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef MIDI_H_GUARD
#define MIDI_H_GUARD

//not of these macros is used anywhere in the code base but they throw errors so they are commented out

// #define MIDI_CIN_MISC_FUNCTION_RESERVED         0x0
// #define MIDI_CIN_CABLE_EVENTS_RESERVED          0x1
// #define MIDI_CIN_2_uint8_t_MESSAGE                 0x2
// #define MIDI_CIN_MTC                            0x2
// #define MIDI_CIN_SONG_SELECT                    0x2
// #define MIDI_CIN_3_uint8_t_MESSAGE                 0x3
// #define MIDI_CIN_SSP                            0x3
// #define MIDI_CIN_SYSEX_START                    0x4
// #define MIDI_CIN_SYSEX_CONTINUE                 0x4
// #define MIDI_CIN_1_uint8_t_MESSAGE                 0x5
// #define MIDI_CIN_SYSEX_ENDS_1                   0x5
// #define MIDI_CIN_SYSEX_ENDS_2                   0x6
// #define MIDI_CIN_SYSEX_ENDS_3                   0x7
// #define MIDI_CIN_OFF                       0x8
// #define MIDI_CIN_ON                        0x9
// #define MIDI_CIN_POLY_KEY_PRESS                 0xA
// #define MIDI_CIN_CONTROL_CHANGE                 0xB
// #define MIDI_CIN_PROGRAM_CHANGE                 0xC
// #define MIDI_CIN_CHANNEL_PREASURE               0xD
// #define MIDI_CIN_PITCH_BEND_CHANGE              0xE
// #define MIDI_CIN_SINGLE_uint8_t                    0xF

//S : sharp   , s:   sori ,    k :   koron  (  e.g. : Bk = B Koron)
#define  E2			80		// E
#define  Es2			81		// E-PB
#define  F2			82		// F
#define  Fs2			83		// F-PB
#define  FS2			84		// F#
#define  Gk2                   85		// F#-PB = Gkoron
#define  G2			86		// G 
#define  Gs2			87		// G -PB
#define  GS2			88		// G#
#define  Ak2                   89		// G#-PB
#define  A2			90		// A
#define  As2			91		// A-PB
#define  BS2			92		//  As
#define  Bk2                   93		//  As-PB
#define  B2			94		// B
#define  Bs2			95		// B-PB
#define  C3			96		// C
#define  Cs3			97		// C-PB
#define  CS3			98		// C#
#define  Dk3                   99		// C#-PB
#define  D3			100		// D
#define  Ds3			101		// D-PB
#define  DS3			102		// D#
#define  Ek3                   103		// D#-PB
#define  E3			104		// E
#define  Es3			105		// E-PB
#define  F3			106		// F
#define  Fs3			107		// F-PB
#define  FS3			108		// F#
#define  Gk3                   109		// F#-PB
#define  G3			110		// G
#define  Gs3			111		// G-PB
#define  GS3			112		// G#
#define  Ak3                   113		// G#-PB
#define  A3			114		// A
#define  As3			115		// A-PB
#define  AS3			116		//  As
#define  Bk3                   117		//  As-PB
#define  B3			118		// B
#define  Bs3			119		// B-PB
#define  C4			120		// Middle C
#define  Cs4			121		// Middle C-PB
#define  CS4			122		// C#
#define  Dk4                   123		// C#-PB
#define  D4			124		// D
#define  Ds4			125		// D-PB
#define  DS4			126		// D#
#define  Ek4                   127		// D#-PB
#define  E4			128		// E
#define  Es4			129		// E-PB
#define  F4			130		// F
#define  Fs4			131		// F-PB
#define  FS4			132		// F#
#define  Gk4                   133		// F#-PB
#define  G4		 	134		// G
#define  Gs4			135		// G -PB
#define  GS4			136		// G#
#define  Ak4                   137		// G#-PB
#define  A4			138		// A
#define  As4			139		// A-PB
#define  AS4			140		//  As
#define  Bk4                   141		//  As-PB
#define  B4			142		// B
#define  Bs4			143		// B-PB
#define  C5			144		// C
#define  Cs5			145		// C-PB
#define  CS5			146		// C#
#define  Dk5                   147		// C#-PB
#define  D5			148		// D
#define  Ds5			149		// D-PB
#define  DS5			150		// D#
#define  Ek5                   151		// D#-PB
#define  E5			152		// E
#define  Es5			153		// E-PB
#define  F5			154		// F
#define  Fs5			155		// F-PB
#define  FS5			156		// F#
#define  Gk5                   157		// F#-PB
#define  G5			158		// G 
#define  Gs5			159		// G-PB
#define  GS5			160		// G#
#define  Ak5                   161		// G#-PB

enum {
  PLAY_STATE = 1,
  UI_STATE,
  VALUE_STATE,
};

#define REST                    2
#define UP                      1
#define DOWN                    0

#define INSTRUMENT_CMD                  0
#define TUNING_CMD                      1
#define	QUARTERTONE_CMD                 2
#define TRANSPOSE_CMD                   3
#define VOLUME_CMD                      4
#define REVERB_CMD              	5
#define TAPPING_CMD                     6
#define LEFTHAND_CMD                    7
#define VIBRATO_CMD                     8
#define SUSTAIN_CMD                     9
#define CHORD_CMD                       10
#define RESONATE_CMD                    11
#define PERCUSSION_CMD                  12
#define BATTERY_ALARM_SENSITIVITY_CMD   13
#define TAP_WITHOUT_STRUM_CMD           14
#define PITCH_SYSTEM_CMD                15
#define BATTERY_MONITOR_CMD             16
#define CONSTANT_VELOCITY_CMD          17
#define STACCATO_CMD                   18


//pitch systems  , this should be matching the list of freq table on the app side.
//************make sure to copy this in util.c as well
#define EDO_24_PITCH          0
#define JAZAYERI_PITCH          1
#define EDO_51_TURKISH_PITCH     2
#define EDO_51_SHARIF_PITCH      3
#define CUSTOM_PITCH            4  //array for this item, will never exist on the PIC side. if this is selected, the pitch system will be set on 0, and pitch bends will be calculated on the app side.

typedef union {
    struct{
        uint8_t acousticGrandPiano:1;     ////
        uint8_t brightGrandPiano:1;
        uint8_t electricGrandPiano:1;
        uint8_t honkyTonkGrandPiano:1;
        uint8_t electricPiano1:1;
        uint8_t electricPiano2:1;
        uint8_t harpsichord:1;
        uint8_t clavi:1;
        uint8_t celesta:1;            //////
        uint8_t glockenspiel:1;
        uint8_t musicBox:1;
        uint8_t vibraphone:1;
        uint8_t marimba:1;
        uint8_t xylophone:1;
        uint8_t tubularBells:1;
        uint8_t dulcimer:1;
        uint8_t drawbarOrgan:1;   //////
        uint8_t percussiveOrgan:1;
        uint8_t rockOrgan:1;
        uint8_t churchOrgan:1;
        uint8_t reedOrgan:1;
        uint8_t accordion:1;
        uint8_t harmonica:1;
        uint8_t tangoAccordion:1;
        uint8_t acousticGuitarNylon:1;    /////
        uint8_t acousticGuitarSteel:1;
        uint8_t electricGuitarJazz:1;
        uint8_t electricGuitarClean:1;
        uint8_t electricGuitarMuted:1;
        uint8_t overdrivenGuitar:1;
        uint8_t distortionGuitar:1;
        uint8_t guitarHarmonics:1;
        uint8_t acousticBass:1;           ////
        uint8_t electricBassFinger:1;
        uint8_t electricBassPick:1;
        uint8_t fretlessBass:1;
        uint8_t slapBass1:1;
        uint8_t slapBass2:1;
        uint8_t synthBass1:1;
        uint8_t synthBass2:1;
        uint8_t violin:1;             ///////
        uint8_t viola:1;
        uint8_t cello:1;
        uint8_t contrabass:1;
        uint8_t tremoloStrings:1;
        uint8_t pizzicatoStrings:1;
        uint8_t orchestralHarp:1;
        uint8_t timpani:1;
        uint8_t stringEnsembles1:1;       /////////
        uint8_t stringEnsembles2:1;
        uint8_t synthString1:1;
        uint8_t synthString2:1;
        uint8_t choirAahs:1;
        uint8_t voiceOohs:1;
        uint8_t synthVoice:1;
        uint8_t orchestraHit:1;
        uint8_t trumpet:1;            //////////
        uint8_t trombone:1;
        uint8_t tuba:1;
        uint8_t mutedTrumpet:1;
        uint8_t frenchHorn:1;
        uint8_t brassSection:1;
        uint8_t synthBrass1:1;
        uint8_t synthBrass2:1;
        uint8_t sopranoSax:1;         ////////
        uint8_t altoSax:1;
        uint8_t tenorSax:1;
        uint8_t baritoneSax:1;
        uint8_t oboe:1;
        uint8_t englishHorn:1;
        uint8_t bassoon:1;
        uint8_t clarinet:1;
        uint8_t piccolo:1;    /////
        uint8_t flute:1;
        uint8_t recorder:1;
        uint8_t panFlute:1;
        uint8_t blownBottle:1;
        uint8_t shakuhachi:1;
        uint8_t whistle:1;
        uint8_t ocarina:1;
        uint8_t squareLead:1; ////////
        uint8_t sawLead:1;
        uint8_t calliopeLead:1;
        uint8_t chiffLead:1;
        uint8_t chargingLead:1;
        uint8_t voiceLead:1;
        uint8_t fifthsLead:1;
        uint8_t bassLead:1;
        uint8_t newAge:1;/////////
        uint8_t warmPad:1;
        uint8_t polySynth:1;
        uint8_t choir:1;
        uint8_t bowed:1;
        uint8_t metallic:1;
        uint8_t halo:1;
        uint8_t sweep:1;
        uint8_t rain:1;///////
        uint8_t soundTrack:1;
        uint8_t crystal:1;
        uint8_t atmosphere:1;
        uint8_t brightness:1;
        uint8_t goblins:1;
        uint8_t echoes:1;
        uint8_t sciFi:1;
        uint8_t sitar:1;////////////
        uint8_t banjo:1;
        uint8_t shamisen:1;
        uint8_t koto:1;
        uint8_t kalimba:1;
        uint8_t bagPipes:1;
        uint8_t fiddle:1;
        uint8_t shanai:1;
        uint8_t tinkleBell:1;//////////////
        uint8_t agogo:1;
        uint8_t pitchedPercussion:1;
        uint8_t woodBlock:1;
        uint8_t taikoDrum:1;
        uint8_t melodicTom:1;
        uint8_t synthDrum:1;
        uint8_t reverseCymbal:1;
        uint8_t guitarFretNoise:1;//////////
        uint8_t breathNoise:1;
        uint8_t seashore:1;
        uint8_t birdTweet:1;
        uint8_t telephoneRing:1;
        uint8_t helicopter:1;
        uint8_t applause:1;
        uint8_t gunshot:1;
        uint8_t paddingBit:1;
    }s;
    uint8_t table[16];
} sustainTable;


void midiTx(uint8_t code, uint8_t status, uint8_t pitchLSB, uint8_t velocityMSB);

#ifdef binarySearch
	int binary_search(float A[], float key, int imin, int imax);
#endif


extern const int16_t pitchAdjustment[4][24];
extern uint8_t pitchSystem;
extern uint8_t velocityMultiplier;
extern int16_t fineTuningPB;


uint8_t findVelocity(float, float, uint8_t);
uint16_t fromMid(uint32_t, uint32_t, int);
uint8_t returnBaseTable(uint8_t, uint8_t);

void noteOn (STRUM *, uint8_t, uint8_t);
void noteOff(STRUM *, uint8_t, uint8_t);
void getNote(STRUM * strum, uint8_t c);

void tapNoteOn(TAP *, uint8_t, uint8_t);
void tapNoteOff(TAP *, uint8_t, uint8_t);
void getTapNote(TAP * tap, uint8_t c);

int fretVoltageToFretNumber(float );

/** Return fret number (0 = open) for a string given its current ADC average. Used e.g. for same-fret staccato detection. */
uint8_t getFretNumberForString(uint8_t stringIndex, float adcFretAverage);


#endif