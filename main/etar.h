#ifndef __ETAR_H_
#define __ETAR_H_

#include <stdbool.h>
#include <stdint.h>

/**
    Defines
 **/
#define uart_en
//#define spi_en

//#define resonate
//#define binarySearch
//#define membrane

// The type of instrument that the flash will be loaded onto
// eTar
#define INST_SETAR
//#define INST_TANBOUR  //disables strings 2 and 3
//#define INST_UKULELE

#define STRUM_DERIVATIVE
/* Option: STRUM_DEFLECTION = deflection threshold (note-on/off by position). Dynamic center + hysteresis in etar.c make it viable. */
//#define STRUM_DEFLECTION

#define PCB_V2_2
//#define PCB_V2_3

/** FreeRTOS priority for eTar (ADC/SPI hot path). Raised above WiFi/buttons (6) to reduce BLE-related scheduling delays; stay below Bluedroid (~19). */
#define ETAR_TASK_PRIORITY 12

#define HALF_CIRCUIT_MEMBRANE
//#define FULL_CIRCUIT_MEMBRANE
#define AUTO_STRUM_CALIBRATION
#define AUTO_STRING_CALIBRATION // fret sensor calibration
// #define CHYME_WHEN_IDLE

////////// Definitions for PRE-SAMPLING/STRUM DETECTION ///////////////////
#define NUM_ADC_SAMPLES 3
#define NUM_PRESAMPLES 3  // Maximum of 3
#define NUM_POSTSAMPLES 3 // Maximum of 3
#define DEBOUNCE_TIME 5
#define CHORD_NUM_OF_NOTES 0

// String -> AN# arrangement for SMT PICf
// S1 = AN4 = readADC(4)
// S2 = AN7 = readADC(7)
// S3 = AN6 = readADC(6)
// S4 = AN5 = readADC(5)
// #define STRING_ONE_ADC_PORT 4
// #define STRING_TWO_ADC_PORT 7
// #define STRING_THREE_ADC_PORT 6
// #define STRING_FOUR_ADC_PORT 5

// The approximation equations are taken from:
// One-Sided Diferentiators
// Pavel Holoborodko
// http://www.holoborodko.com/pavel/ under numerical methods

///////// Pitch Bends and Note Defines //////////
#define NOTE_SEMITONE 0x2000
#define NOTE_QUARTERTONE 0x2800

#define MIDI_CHANNEL_1 0x0
#define MIDI_CHANNEL_2 0x1
#define MIDI_CHANNEL_3 0x2
#define MIDI_CHANNEL_4 0x3
#define MIDI_CHANNEL_5 0x4
#define MIDI_CHANNEL_6 0x5
#define MIDI_CHANNEL_7 0x6
#define MIDI_CHANNEL_8 0x7
// Channel 9 cannot be used as it is used for the percussion option
#define MIDI_CHANNEL_9 0x8
#define MIDI_CHANNEL_10 0x9
#define MIDI_CHANNEL_11 0xA
#define MIDI_CHANNEL_12 0xB
#define MIDI_CHANNEL_13 0xC
#define MIDI_CHANNEL_14 0xD
#define MIDI_CHANNEL_15 0xE
#define MIDI_CHANNEL_16 0xF

#define CHANNEL_PERCUSSION 9

// Connection Type
#define TARGET_ETAR 0
#define TARGET_WINDOWS 1
#define TARGET_WINDOWS_ETAR 2
#define TARGET_ANDROID 3
#define TARGET_OSX 4
#define TARGET_IOS_GB 5
#define TARGET_IOS_BS16I 6

/// USB Connection Type Tones Adjustments
#define ETAR_SEMITONE_CENTER 0x4000
#define ETAR_QUARTERTONE_OFFSET 0x1200

#define WIN_SEMITONE_CENTER 0x4000
#define WIN_QUARTERTONE_OFFSET 0x1000 // 1000

#define WIN_ETARAPP_SEMITONE_CENTER 0x4000
#define WIN_ETARAPP_QUARTERTONE_OFFSET 0x1000 // 1000

#define ANDROID_SEMITONE_CENTER 0x4000
#define ANDROID_QUARTERTONE_OFFSET 0x1000 // 1000

#define OSX_SEMITONE_CENTER 0x4000
#define OSX_QUARTERTONE_OFFSET 0x1000

#define IOS_GB_SEMITONE_CENTER 0x4000
#define IOS_GB_QUARTERTONE_OFFSET 0x1200

#define IOS_BS16I_SEMITONE_CENTER 0x4000
#define IOS_BS16I_QUARTERTONE_OFFSET 0x800 // 600

/**
*	Structs & Typedefs      *
 **/
typedef uint8_t uint8_t;

typedef struct _STRUM {
  bool inStrum;                   // 1
  bool resonation;                // 1
  uint32_t strumDirection;        // 4
  uint32_t adcFret;               // 4
  uint32_t adcPrevFret;           // 2
  uint32_t strumPeakValue;          // 4
  float adcFretAverage;           // 8
  uint8_t note;                   // 1
  uint8_t noteIn24;               // 1
  uint8_t prevNote;               // 1
  uint8_t prev24Note;             // 1
  uint8_t strumVelocity;          // 1
  uint8_t prevVelocity;           // 1
  uint16_t pitchBend;             // 2
  uint16_t prevPitchBend;         // 2
  uint8_t fret;                   // 1
  uint8_t prevFret;               // 1
  volatile uint16_t timingCounter; // supports requiredTimeStepsBetweenStrums > 255

  ///// Sampling Fields /////
  uint32_t currentSampleN;
   uint32_t strumAverage;

  // Pre Sampling Fields
  uint32_t preSampleN1; // 4  adc value n-1
  uint32_t preSampleN2; // 4  adc value n-2
  uint32_t preSampleN3; // 4  adc value n-3

  float averagedPreSample;
  // no counter needed since this is always sliding

  // Post Sampling Fields
  uint32_t postSampleN1;
  uint32_t postSampleN2;
  uint32_t postSampleN3;
  float averagedPostSample;
  uint32_t atPostSample;

  // Detection variable for plucked instruments
  int strumDerivative;
  int absStrumDerivative;
  // Detection variable for sustained instruments
  int averagedStrumDeflection;

  bool strumNoteIsPlayed; // to check if a strum note is played , regardless of
                          // instrum if true or not. needed when tapping is
                          // eanbled
  bool enoughTimePassedSinceLastStrum;

} STRUM;

typedef struct _TAP // adding even one more field causes "code cannot fit in the
                    // section"
{
  bool strumOccured;
  bool resonation;
  // uint32_t	upDownStrum;   //commented this to allow "fret" be added to the
  // structure
  uint32_t adcFret;
  float previousFretSample;
  float adcFretAverage;
  float strummedFretSample;
  uint8_t note;
  uint8_t noteIn24;
  uint8_t prevNote;
  uint16_t prevPitchBend;
  uint8_t velocity;
  uint16_t pitchBend;
  uint32_t timingCounter;
  uint8_t debounceCount;
  uint8_t fret;
  uint8_t prevFret;
  
  bool tapNoteIsPlayed; // to check if the tap note is played, regardless of tap
                        // instrum if true or not.
  bool tapPressed;
} TAP;

typedef union {
  uint32_t Val;
  struct {
    uint8_t code : 4;
    uint8_t cableNum : 4;
    uint8_t status;
    union {
      struct {
        uint8_t pitch;
        uint8_t velocity;
      };
      struct {
        uint8_t pbLSB;
        uint8_t pbMSB;
      };
    };
  };
} MIDI_DATA;

typedef union _ratio {
  float f;
  uint8_t b[4];
} myRatio;

/**
*	Prototypes              *
 **/
// Sleep Function
void checkSleep(void);
extern void etarTask(void *pvParameters);
extern void timer_check(void);

void changeSetting(TAP *tap, uint8_t atString);
void resetTap(TAP * tap , uint8_t atString);

/**
*	Extern Variables        *
 **/
#ifdef membrane
extern float fretRatio[41];
extern bool autoStrumCalibration;
extern bool autoStringCalibration;
extern bool fretCalibMode;

extern float lastCalibVal, minStep;
extern int calibFretNum;
#endif

// Strumming Vars
extern uint32_t strumUpDown[5];
extern uint8_t workingBaseTable[4];
extern float potMidValue[4];
extern float fretBarMaxValue[4];
extern MIDI_DATA midi1;
extern uint8_t strumSensitivityFactor;

extern uint8_t derivativeThreshold;
extern int derivativeReturnThreshold;
extern uint8_t strumStartThreshold;
extern uint8_t strumReturnThreshold;
extern uint16_t requiredTimeStepsBetweenStrums;
extern uint8_t tappingDebounceNum;
extern short PBCenter;
extern short PBCenterOffset;
extern short PBQuarterToneOffset;
extern int16_t fineTuningPB;
extern uint16_t strumSampleInterval;
extern uint8_t constVelocityValue;
extern uint8_t connectionTarget;
extern uint8_t semitoneChannel;
extern uint8_t quartertoneChannel;
extern uint8_t synthInstrument;
extern uint8_t curVol;
extern uint8_t legatoDelay;

// Joystick
extern float joystickMidValue;
extern float joystickDeflection;
extern uint8_t vibratoValue;
extern uint8_t batteryAlarmFactor;

// Setting Flags
extern bool leftHandEnabled;
extern bool effectsEnabled;
extern bool tappingEnabled;
extern bool sustainEnabled;
extern bool resonateEnabled;
extern bool quarterNotesEnabled;
extern bool vibratoEnabled;
extern bool chordEnabled;
extern bool rpnFlag;
extern uint8_t rpnValue;
extern bool noteOnReleaseEnabled;
extern bool staccatoEnable;
extern bool constantVelocityEnable;
extern bool channelChange;
extern bool stringEnabledArray[4];

// Calibration Flags
extern bool initialCalib;

// Connection Flags
extern bool usbCableConnected;

// Misc Flags
extern bool vibratoPause;

// Misc Variables
extern int16_t transposeValue; // value of transpose
extern uint8_t row;        // default tuning is row 0
extern int chordType;

// Setting Variables
extern uint8_t strumCalibTimeout; // Timeout for Auto Strum Calibration
extern uint8_t
    strumStartThreshold; // How far a strum must go before we start reading it
extern uint8_t strumReturnThreshold; // How far the strum must return before we
                                     // finish reading it
extern uint8_t fretNoiseMargin;      // tapping adc tolerance
extern uint8_t
    tappingDebounceNum; // number of samples to take for tap debouncing
extern uint8_t vibratoSampleTime; // How often a vibrato sample is transferred
extern uint8_t tappingVolume;     // this is the midi velcoity of tapping
extern uint16_t requiredTimeStepsBetweenStrums; // time between strums (ms) to prevent
                                                // oscillations causing strums; supports > 255

extern uint8_t deviceNumber;
extern uint8_t deviceConnectionType;
extern bool reconfigUSBFlag;

// Timers
extern uint8_t vibratoSampleCounter;

// percussion instrument
extern bool percussionInstrument;

extern bool tapWithoutStrumEnabled;
extern bool hammerOnEnabled;

// index the pitch adjustment array in midi.c. shows what microtnal system is
// selected
extern uint8_t pitchSystem;

extern int16_t pitchChangeValue;

// extern uint32_t battVoltage;
extern uint8_t numSamples;

extern bool pluckedInstrument;
extern bool acousticSetarFretsEnabled;

extern int deviceState;

#endif