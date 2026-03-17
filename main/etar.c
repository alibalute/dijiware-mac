/**
 * @file etar.c
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief
 *
 * This part is ported from original PIC main module
 *
 * @version 0.1
 * @date 2022-05-31
 *
 * @copyright Copyright (c) 2022 WTI
 *
 */

/**
 * Port history: see version control. Original PIC main module ported to ESP32.
 */

/* Use stub headers when parsing on macOS (Mach-O); ESP-IDF section attributes are invalid.
 * Cross-compiler (xtensa-esp32s3-elf-gcc) does not define __APPLE__, so ESP32 builds are unchanged. */
#if defined(__APPLE__)
#define IDF_HOST_PARSING 1
#endif

#include "etar.h"

#include "main.h"
#if defined(IDF_HOST_PARSING)
#include "freertos_host_stub.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "adc.h"
#if defined(IDF_HOST_PARSING)
#include "usb_host_stub.h"
#else
#include "usb.h"
#endif
#include "pic-midi.h"
#include "util.h"
#if defined(IDF_HOST_PARSING)
#include "esp_host_stub.h"
#else
#include "esp_sleep.h"
#include "esp_log.h"
#endif
#include "led.h"
#include "interfaces.h"
#include "common.h"
#if defined(IDF_HOST_PARSING)
/* driver/i2c.h uses section attributes invalid for Mach-O; pins.h stub provides i2c types. */
/* i2c_gui.h pulls in FreeRTOS, driver/i2c.h, esp_log.h; skip for host parsing. */
#else
#include "driver/i2c.h"
#include "i2c_gui.h"
#endif

static const char *TAG = "eTar";



// to detect power changes
extern bool externalPowerChanged(void);
extern void externalPowerChangeAck(void);
extern void poweroff(void);
extern bool shouldPoweroff;
extern bool initialVolumeSet;

/** VARIABLES ******************************************************/
#ifdef membrane
float fretRatio[41];

float lastCalibVal, minStep;
int calibFretNum;
#endif

MIDI_DATA midi1;

// Received Bluetooth Data buffer
uint8_t btBuffer[2];

// The ADC value of the strum POT in the middle at reset (drifts toward actual rest when at rest)
float potMidValue[4];
/* When deflection (currentSampleN) is below this, we consider the bar at rest and nudge potMidValue toward current reading */
#define STRUM_CENTER_REST_THRESH  60
#define STRUM_CENTER_ALPHA        0.03f

// Accelerometer Values
uint32_t accel_z;
uint32_t prevAccVal, accVal;

// joystick variables
float joystickMidValue; // for vibrato joystick
uint16_t joystickReading;
float joyValue;
static float joyValuePrev __attribute__((unused)) = 320.0f; // approximate value when instrument held upright
int joyCount;
float joystickDeflection;
uint16_t latestPitchbends[4];
uint16_t tempPitchBend;

// Fret Board Commands
uint8_t commandNumber;
uint8_t commandValue[4], lastCommandValue[4] = {0};

// battery monitoring variable
uint32_t battVoltage;
uint8_t batteryAlarmFactor =
    0; // alarm at 0% charge (means no alarm by default)

// Tap Structure
TAP tap[4] = {{false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false , false},
              {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false , false},
              {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false , false},
              {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false , false}};

// Strum Structure
STRUM strum[4] = {
    {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .strumNoteIsPlayed = false, .enoughTimePassedSinceLastStrum = false},
    {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .strumNoteIsPlayed = false, .enoughTimePassedSinceLastStrum = false},
    {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .strumNoteIsPlayed = false, .enoughTimePassedSinceLastStrum = false},
    {false, false, REST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .strumNoteIsPlayed = false, .enoughTimePassedSinceLastStrum = false}};

// Setting Variables
#ifdef membrane
bool fourthFinDown = false;
#endif

static const uint8_t introNotes[4] = {60, 64, 67, 72};
#define INTRO_NOTES_COUNT 4

/**********************************************************************
 * Common Variables
 *********************************************************************/

/** Set when boot intro notes are done; main task waits for this before load_settings_at_boot(). */
volatile bool boot_intro_done = false;

static uint32_t idleTimer = 0;
static uint32_t mainTimingCounter = 0;

// Tap window: use tick counter (updated in timer_check) to avoid calling millis()
// from ProcessIO hot path, which can cause freezes (e.g. with BLE).
// Tap is allowed only after a strum, not after another tap.
static uint32_t noteTickCounter = 0;   // free-running, ~1 tick per loop
static uint32_t lastNotePlayedTick = 0; // 0 = no note yet
static bool lastNoteWasStrum = false;   // true only after a strum; cleared when a tap is played
#define TAP_WINDOW_TICKS 500  // ~200 ms at ~1 tick/ms

// Hammer-on: fret change after strum/tap without new strum (lockout avoids double note)
#define HAMMER_ON_LOCKOUT_TICKS 50   // ~50 ms no hammer-on after strum/tap
#define HAMMER_ON_WINDOW_TICKS 400   // allow hammer-on within ~400 ms of last note
#define HAMMER_ON_DEBOUNCE 2         // consecutive same new fret to trigger
static uint32_t lastNoteOnTick[4] = {0};
static uint32_t hammerOnLockoutUntilTick[4] = {0};
static uint8_t lastFretAtNoteOn[4] = {0};
static uint8_t hammerOnDebounceCount[4] = {0};
static uint8_t hammerOnLastFret[4] = {0};
static uint8_t lastPlayedNote[4] = {0};
static uint8_t lastPlayedNoteIn24[4] = {0};
static uint16_t lastPlayedPitchBend[4] = {0};
static uint32_t lastPlayedStrumDirection[4] = {0};  /* for chord noteOff: must match note-on direction */

// Strum Flags+Vars
bool strumReleased = false;
bool strumDetected = false;
bool strumOccured = false;
bool staccatoOccured = false;
bool tapDetected = false;
// Used in the timer interupt ot create time base in main line call
volatile bool strumDetectionEnabled = false;
// time between strums to prevent oscillations causing strums
uint16_t strumSampleInterval = 2;           // 10;
uint16_t requiredTimeStepsBetweenStrums = 2; // 10;

// Calibration Flags+Vars
bool autoStrumCalibration = true;
bool autoStringCalibration = true;
bool initialCalib = true;
bool fretCalibMode = false;
// The maximum value of the fret bar with open string when the current flows
// into the whole bar
float fretBarMaxValue[4] = {0, 0, 0, 0};
uint8_t strumCalibTimeout =  4; // time in seconds for auto strum calibration
uint8_t stringCalibTimeout = 4; // time in seconds for auto string calibration
volatile unsigned long strumCalibCounter = 0;  // Auto Strum Calibration Timer
volatile unsigned long stringCalibCounter = 0; // Auto String Calibration Timer

volatile bool joystickCalibrationRequired = false;

// Connection Flags+Vars
bool inSleepMode = false;
int btBuffCount = 0;  // Bluetooth received byte counter

// Misc Flags+Vars
bool staccato = false;
bool vibratoPause = false;
bool upButtonRegistered = false;
bool downButtonRegistered = false;
bool channelChange = false;
int deviceState = PLAY_STATE; // Default Device State = PLAY_STATE
uint8_t vibratoSampleCounter = 0;
uint8_t numSamples = 16;
uint8_t fretNoiseMargin = 10; // tapping adc tolerance
// uint8_t tappingVolume = 0x1E; // this is the midi velcoity of tappingtransposeValue
uint8_t vibratoSampleTime =
    100; // how often a vibrato sample is transferred
int chordType = 0; //0:major   1:minor
bool stringEnabledArray[4] = {true, true, true, true};

/**********************************************************************
 * Instrument Specific Variables
 *********************************************************************/
#ifdef INST_UKULELE
// Setting Values
uint8_t workingBaseTable[4] = {A4, E4, C4, G4};

uint8_t tuningIndex = 0;                                // Preset Tuning Row
int16_t transposeValue = 0;
int16_t pitchChangeValue = 0;
uint8_t tempoValue = 60; //60 bpm
uint8_t metronomeNumBeats = 1; 
uint8_t metronomeVol = 64; // mid volume
uint8_t curVol = 127; // Current Volume
uint8_t vibratoValue = 1;
uint8_t synthInstrument =
    107;                          // sitar 104 koto 107 viola 41 pan flute 75 steel guitar 25
uint8_t velocityMultiplier = 1;  // Dynamic velocity sensitivity
uint8_t constVelocityValue = 127; // Constant velocity value
uint8_t pitchSystem = EDO_24_PITCH;
uint8_t semitoneChannel = MIDI_CHANNEL_1;
uint8_t quartertoneChannel = MIDI_CHANNEL_2;

short PBCenter = 0x4000;
short PBCenterOffset = 0;
short PBQuarterToneOffset = ETAR_QUARTERTONE_OFFSET;
int16_t fineTuningPB = 0;

uint8_t connectionTarget = TARGET_ETAR;

// Thresholds and timings for the strum bars sensors
uint8_t derivativeThreshold = 100 ; // 66; // by experiment, the derivative of strum readings at rest never gets bigger than 22 if multiplied by maximum sensitivit factor which is 3, becomes 66
                                  //   strum be to turn the note on or off
int derivativeReturnThreshold = -36 ; // -18; // -9;
uint8_t strumStartThreshold =
    180; // How far (deflection) a strum must go before note-on (STRUM_DEFLECTION)
uint8_t strumReturnThreshold =
    140; // How far it must return before note-off; lower than start = hysteresis so note-off doesn't need perfect centering
uint8_t tappingDebounceNum =
    1; // number of samples to take for tap debouncing (Hammer On Delay)

uint8_t strumSensitivityFactor = 2; // by experimenting factor = 2 has a resaonabl sensisivity

// Setting Flags
bool chordEnabled = false;
bool sustainEnabled = true;
bool vibratoEnabled = false;
bool staccatoEnable = false;
bool effectsEnabled = false;
bool tappingEnabled = false;
bool leftHandEnabled = false;
bool resonateEnabled = false;
bool quarterNotesEnabled = false;
bool constantVelocityEnable = true;
bool noteOnReleaseEnabled = false;
bool tapWithoutStrumEnabled = false;
bool hammerOnEnabled = false;

bool pluckedInstrument = true;
bool percussionInstrument = false;
bool acousticSetarFretsEnabled = false;
#endif

#ifdef INST_SETAR
///// Setting Values /////
uint8_t workingBaseTable[4] = {C4, G3, C4, C3}; // Note for each string , default
uint8_t tuningIndex = 0;                                // Preset Tuning Row
int16_t transposeValue = 0;
int16_t pitchChangeValue = 0;
uint8_t tempoValue = 60; //60 bpm
uint8_t metronomeNumBeats = 1; 
uint8_t metronomeVol = 64; // mid volume
uint8_t curVol = 127; // Current Volume
uint8_t vibratoValue = 1;
uint8_t synthInstrument =
    107;                          // sitar 104 koto 107 viola 41 pan flute 75 steel guitar 25
uint8_t velocityMultiplier = 1;  // Dynamic velocity sensitivity
uint8_t constVelocityValue = 127; // Constant velocity value
uint8_t pitchSystem = EDO_24_PITCH;
uint8_t semitoneChannel = MIDI_CHANNEL_1;
uint8_t quartertoneChannel = MIDI_CHANNEL_2;

short PBCenter = 0x4000;
short PBCenterOffset = 0;
short PBQuarterToneOffset = ETAR_QUARTERTONE_OFFSET;
int16_t fineTuningPB = 0;

uint8_t connectionTarget = TARGET_ETAR;

// Thresholds and timings for the strum bars sensors
uint8_t derivativeThreshold = 100;//66; // by experiment, the derivative of strum readings at rest never gets bigger than 22 if multiplied by maximum sensitivit factor which is 3, becomes 66
                                  //   strum be to turn the note on or off
int derivativeReturnThreshold = -36; // -18; // -9;
uint8_t strumStartThreshold =
    180; // How far (deflection) a strum must go before note-on (STRUM_DEFLECTION)
uint8_t strumReturnThreshold =
    140; // How far it must return before note-off; lower than start = hysteresis so note-off doesn't need perfect centering
uint8_t tappingDebounceNum =
    1; // number of samples to take for tap debouncing (Hammer On Delay)

uint8_t strumSensitivityFactor = 2; // by experimenting factor = 2 has a resaonabl sensisivity

// Setting Flags
bool chordEnabled = false;
bool sustainEnabled = true;
bool vibratoEnabled = false;
bool staccatoEnable = false;
bool effectsEnabled = false;
bool tappingEnabled = false;
bool leftHandEnabled = false;
bool resonateEnabled = false;
bool quarterNotesEnabled = true;
bool constantVelocityEnable = true;
bool noteOnReleaseEnabled = false;
bool tapWithoutStrumEnabled = false;
bool hammerOnEnabled = false;

bool pluckedInstrument = true;
bool percussionInstrument = false;
bool acousticSetarFretsEnabled = false;
#endif

/* Threshold for string ADC drift before updating fretBarMaxValue (instrument-specific) */
#if defined(INST_UKULELE)
#define STRING_DRIFT_THRESHOLD 150
#elif defined(INST_SETAR)
#define STRING_DRIFT_THRESHOLD 500
#else
#define STRING_DRIFT_THRESHOLD 500
#endif

/* Set in etarTask when (iter % FREEZE_CKPT_MOD == 0); ProcessIO logs checkpoints when true. Outside INST_* so always declared. */
static bool s_etar_log_ckpt;

/** Silences all notes (all sounds off) and sets staccatoOccured so note-ons are suppressed. */
static void silenceAllNotesFromGesture(void)
{
  inputToUART(0xB0, 0x78, 0x00);  // all sounds off (CC 120)
  midiTx(0xB, 0xB0, 0x78, 0x00);
  staccatoOccured = true;
}

void ProcessIO(void);

#ifdef spi_en
void start_spi();
uint8_t reverseBit(uint8_t);
#endif



/**
 * @brief set up flags and increase counters based on time elapsed
 *
 */
void timer_check(void)
{
  // if connected to USB or BLE don't go to sleep (light sleep with BLE connected causes freeze)
  if (isUSBConnected() || isBLEConnected())
  {
    idleTimer = millis();
  }

  if (tappingEnabled || tapWithoutStrumEnabled || hammerOnEnabled)
    noteTickCounter++;  // tap window and hammer-on window

  // main timing counter incremented every 1 ms
  mainTimingCounter++;
  strumCalibCounter++;  // Increment Strum Calib Timer
  stringCalibCounter++; // Increment String Calib Timer
  // ESP_LOGD(TAG, "Calib strums=%lu, strings=%lu", strumCalibCounter,
  //          stringCalibCounter);

  // The timers stop incrementing after 100 to prevent
  // delay glitches due to the roll over of the timingCounter
  if (strum[STRING_ONE].timingCounter < requiredTimeStepsBetweenStrums)
  {
    strum[STRING_ONE].timingCounter++;
  }
  else
  {
    strum[STRING_ONE].enoughTimePassedSinceLastStrum = true;
  }

  for (int s = 1; s < NUM_STRINGS; s++)
  {
    if (strum[s].timingCounter < requiredTimeStepsBetweenStrums)
      strum[s].timingCounter++;
    else
      strum[s].enoughTimePassedSinceLastStrum = true;
  }

  // Vibrato Sampling
  if (vibratoEnabled == true && vibratoPause == false)
  {
    vibratoSampleCounter++; // Increment Sample Timer
    // If Sample Timer has reached the set timeout value
    if (vibratoSampleCounter >= vibratoSampleTime)
    {
      vibratoSampleCounter = 0; // Reset Sample Timer
    }
  }

  // This determines how often the strum sampling loop is run in ProcessIO()
  //  strum SampleInterval should be capped to 100 ms
  if (mainTimingCounter % strumSampleInterval == 0)
  {
    strumDetectionEnabled = true;
  }



  

  // Every 3 seconds
  if (mainTimingCounter % 3000 == 0)
  {
    joystickCalibrationRequired = true;
    // Reset the mainTimingCounter in the largest delay
    mainTimingCounter = 0;
  }
}

void etarTask(void *pvParameters)
{
  TaskHandle_t task = *(TaskHandle_t *)pvParameters;
  esp_log_level_set(TAG, ESP_LOG_DEBUG); //
  int i;

  ESP_LOGD(TAG, "Task eTar started.");
  vTaskDelay(pdMS_TO_TICKS(100)); // give it a bit of time to avoid VS chip collisions (volume)

#ifdef membrane
  myRatio m;
  int a = 1;
#endif

  /**
      Calibrations
   **/
  // read strum sensors at rest, put their values as their mid value
  strumCalibrate();
  strumCalibCounter = 0;

  ESP_LOGD(TAG, "Calibrating Strings");
  for (i = 0; i < NUM_STRINGS; i++)
  {
    stringCalibrate(i, false); // initial calibration
  }
  stringCalibCounter = 0;

  // for tnaboor disable the two middle strings
#ifdef INST_TANBOUR
  stringEnabledArray[1] = false;
  stringEnabledArray[2] = false;
#endif

  // wait for initial volume
  while (!initialVolumeSet) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  ESP_LOGI(TAG, "boot phase 0: initialVolumeSet seen, delay 400ms");
  vTaskDelay(pdMS_TO_TICKS(100));  /* let synth UART be ready before first MIDI; avoids no sound at boot */

  //joystickCalibrate(); //this finds the mid value of the accelerometer, but in the current approach, the mid value is assumed to be constant

  for (i = 0; i < NUM_STRINGS; i++)
  {
    tap[i].previousFretSample = fretBarMaxValue[i];
    tap[i].adcFretAverage = fretBarMaxValue[i];
    strum[i].adcPrevFret = fretBarMaxValue[i];
    strum[i].adcFretAverage = fretBarMaxValue[i];
    latestPitchbends[i] = 0x2000; // for vibrato
    ESP_LOGD(TAG, "%d max = %f", i, fretBarMaxValue[i]);
  }

  vTaskDelay(pdMS_TO_TICKS(20)); // wait before first UART (no-sound boot fix)
  boot_uart_stats_reset();
  ESP_LOGI(TAG, "boot phase 1: MIDI burst start (vol=%u) first byte B0 07 %02X", (unsigned)curVol, (unsigned)curVol);
  /* Wake synth UART: send first message 5x with gaps so synth RX is ready (no-sound boot fix) */
  // for (int w = 0; w < 5; w++) {
  //   inputToUART(0xB0, 0x07, curVol);  // channel 0 volume
  //   vTaskDelay(pdMS_TO_TICKS(35));
  // }
  ESP_LOGI(TAG, "boot phase 2: wake-up sends done");
  //vTaskDelay(pdMS_TO_TICKS(60));  /* extra pause so synth processes wake-up before 16ch burst */
  for (i = 0; i < 16; i++) {
    inputToUART(0xB0 + i, 0x07, curVol); // Set Volume on Synth Chip
    // vTaskDelay(pdMS_TO_TICKS(50));
  }
  ESP_LOGI(TAG, "boot phase 3: first 16ch volume done");
  //vTaskDelay(pdMS_TO_TICKS(50));  /* brief pause before second pass */
  for (i = 0; i < 16; i++) {
    inputToUART(0xB0 + i, 0x07, curVol); // second pass: ensure volume reached synth (no-sound boot fix)
    // vTaskDelay(pdMS_TO_TICKS(30));
  }
  ESP_LOGI(TAG, "boot phase 4: second 16ch volume done");
  //vTaskDelay(pdMS_TO_TICKS(80));  /* let UART MIDI task drain queue (tick every 100ms) */
  for (int i = 0; i < 16; i++)
  {
    inputToUART(0xB0 + i, 0x40, 0x7F); // Set Sustain on Synth Chip
  }
  vTaskDelay(pdMS_TO_TICKS(80));
  for (int i = 0; i < 16; i++)
  {
    inputToUART(0xC0 + i, 0x00 , 107); // Program Change - 107 koto
  }
  //vTaskDelay(pdMS_TO_TICKS(80));
  //repeat sending sustain messges to make sure that all channels get the sustain message
  for (int i = 0; i < 16; i++)
  {
    inputToUART(0xB0 + i, 0x40, 0x7F); // Set Sustain on Synth Chip   
  }
  //vTaskDelay(pdMS_TO_TICKS(80));
  //repeat sending volume messages to make sure that all channels receive the volume message
  for (int i = 0; i < 16; i++)
  {
    inputToUART(0xB0 + i, 0x07, curVol); // Set Volume on Synth Chip
  }
 // vTaskDelay(pdMS_TO_TICKS(80));  /* drain before intro notes */
  ESP_LOGI(TAG, "boot phase 5: burst done (sustain+program sent)");

  // for tnaboor disable the two middle strings
#ifdef INST_TANBOUR
  stringEnabledArray[1] = false;
  stringEnabledArray[2] = false;
#endif



  ESP_LOGI(TAG, "boot phase 6: intro notes start");
  for (int i = 0; i < INTRO_NOTES_COUNT; i++) {
    inputToUART(0x90, introNotes[i], curVol);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelay(pdMS_TO_TICKS(250));
  for (int i = 0; i < INTRO_NOTES_COUNT; i++)
  {
    inputToUART(0x80, introNotes[i], 127);
  }
  ESP_LOGI(TAG, "boot phase 7: intro notes done (uart_fails=%lu uart_retries=%lu)",
           (unsigned long)boot_uart_get_fail_count(), (unsigned long)boot_uart_get_retry_count());
  boot_uart_stats_log(TAG);
  if (boot_uart_get_fail_count() == 0 && boot_uart_get_retry_count() == 0) {
    ESP_LOGI(TAG, "boot: all UART sends OK; if no sound, check synth power and UART TX wiring");
  }

  boot_intro_done = true;  /* allow main task to load settings now; avoids MIDI interleaving and spurious notes */

  /**
      ProcessIO Loop
   **/

  uint32_t now = millis();
  static uint32_t s_etar_iter = 0;
  static uint32_t s_etar_last_heartbeat_ms = 0;
#define FREEZE_CKPT_MOD 300

  while (!shouldPoweroff)
  {
    s_etar_iter++;
    bool log_ckpt = (s_etar_iter % FREEZE_CKPT_MOD == 0);
    if (log_ckpt) {
      ESP_LOGI(TAG, "freeze ckpt A iter=%lu", (unsigned long)s_etar_iter);
    }
    /* Run at lower priority so BLE/ADC/other tasks get CPU and freezes are reduced (both constant velocity and dynamics) */
    vTaskPrioritySet(task, 6);
    vTaskDelay(0);  /* yield at start of each iteration so other tasks (BLE, WiFi, etc.) get CPU and reduce freezes */
    if (millis() - s_etar_last_heartbeat_ms >= 5000U) {
      ESP_LOGI(TAG, "freeze heartbeat etar");
      s_etar_last_heartbeat_ms = millis();
    }
    if (millis() - now > 10000U)
    {
      ESP_LOGD(TAG, "etarTask running");
      now = millis();
    }
    // muteOnPowerChange();

    /* timer_check() now runs in main's timerTask (every 1ms); no need to call here */

    if (joystickCalibrationRequired)
    {
      joystickCalibrationRequired = false;
    }

    /// Regular IO processing
    checkSleep(); //Checks Idle Timer & Initiates Sleep
    s_etar_log_ckpt = log_ckpt;
    if (log_ckpt) {
      ESP_LOGI(TAG, "freeze ckpt B pre-ProcessIO iter=%lu", (unsigned long)s_etar_iter);
    }
    ProcessIO(); // Process all I/O
    if (log_ckpt) {
      ESP_LOGI(TAG, "freeze ckpt C post-ProcessIO iter=%lu", (unsigned long)s_etar_iter);
    }

    vTaskDelay(pdMS_TO_TICKS(1));  /* 10 was too much, 1 causes freezing when BLE is on */
    /* Extra 1ms so BLE/idle/ADC get more CPU and freezes are reduced (both constant velocity and dynamics) */
    vTaskDelay(pdMS_TO_TICKS(1));
    /* When BLE is connected, give the BLE stack an extra 1ms so it can drain; reduces freezes right after connect (params, MTU, GATT). */
    if (isBLEConnected()) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  ESP_LOGW(TAG, "etar task exited");
  vTaskDelete(task);
  //no need for the following loop after the task is deleted
  // while (true) {
  //   vTaskDelay(1);
  // }

} // end main


void ProcessIO(void)
{
  if (s_etar_log_ckpt) {
    ESP_LOGI(TAG, "freeze ckpt D ProcessIO entry");
  }
  /* Yield only (no block) so strum sampling can keep up with 2ms timer; main loop still does 2ms delay after ProcessIO */
  vTaskDelay(0);

  /**
      Variables
   **/

  uint8_t atString; // The counter for the main strum sample for loop

  // Temporary values for ADC sampling and averaging
  uint32_t strumSample, fretSample;

  float currentFretAverage = 0.0f,
      currTapAverage = 0.0f;        // These hold the averaged fret sample for the current
                                    // sample iteration
  static float previousFretAverage; // Stores the previous averaged fret sample
                                    // for comparison during the next call

  static int tempChordSel =
      0; // Holds the fret number for comparison before setting the chord type
  static int chordDebounce =
      0; // Stores the counter for debouncing across calls of ProcessIO()

  // to count the number of times this function is called
  static long processIoCounter = 0;

  float tempFretADC;




  /**
      Strum & Tap Detection / Note Generation
   **/
  if (strumDetectionEnabled == true) //to prevent too much strum detection
  {
    strumDetectionEnabled = false;
    processIoCounter++;
    vTaskDelay(0);  /* yield before heavy ProcessIO work to reduce freezes */

    // //this section is for staccato
    // if (pluckedInstrument && staccatoEnable)
    // {

    //   if ((staccatoOccured == false) && (strum[STRING_ONE].inStrum) &&
    //       (strum[STRING_TWO].inStrum) && (strum[STRING_THREE].inStrum) &&
    //       (strum[STRING_FOUR].inStrum))
    //   {


    //     inputToUART(0xB0, 0x78, 0x00); //all sounds off
    //     if (isUSBConnected())
    //     {
    //       midiTx(0xB, 0xB0, 0x78, 0x00); //all sounds off
    //     }

    //     for (n = 0; n < NUM_STRINGS; n++)
    //     {
    //       noteOff(&strum[n], n, 0);
    //       if (sustainEnabled)
    //       {
    //         midiTx(0xB, 0xB0 + n, 0x40, 0x00); //0xB is controller command 0x40 is hold pedal 1 , 0x00 is off , 0x7f is on 
    //         midiTx(0xB, 0xB0 + n, 0x40, 0x7F);
    //       }
    //     }

    //     staccatoOccured = true;
    //   }
    //   // Detect staccato exit

    //   if ((staccatoOccured == true) && (!strum[STRING_ONE].inStrum) &&
    //       (!strum[STRING_TWO].inStrum) && (!strum[STRING_THREE].inStrum) &&
    //       (!strum[STRING_FOUR].inStrum))
    //   {

    //     staccatoOccured = false;
    //   }
    // }//end of staccato section

    ///// Main Sampling Loop for strum detection /////
    // Check each string for a strum and generate note if strum detected
    for (atString = 0; atString < NUM_STRINGS; atString++)

    {




      ///////////find the driection of the strum 
      //read 2 samples of the strum sensor, tried more samples causes lag
      strumSample = readAndAverageStrum(atString) + readAndAverageStrum(atString);
      strumSample /= 2;
      /* yield every 2 strings to reduce context switches during fast strumming */
      if (atString == 1 || atString == 3)
        vTaskDelay(0);

      if (strumSample > (uint32_t)potMidValue[atString])
      {
        if (atString == 1 || atString == 3)
        {
          strum[atString].strumDirection = DOWN;
        }
        else if (atString == 0 || atString == 2)
        {
          strum[atString].strumDirection = UP;
        }
      }
      else if (strumSample < (uint32_t)potMidValue[atString])
      {
        if (atString == 1 || atString == 3)
        {
          strum[atString].strumDirection = UP;
        }
        else if (atString == 0 || atString == 2)
        {
          strum[atString].strumDirection = DOWN;
        }
      }
      else
      {
        strum[atString].strumDirection = REST;
      }


      // find strum samples before the strum is registered call them preSample
      // The derivative calculation uses a one-sided approach thus the adc
      // sample must be positive
      strum[atString].currentSampleN =
          (int)strumSensitivityFactor * absoluteDiff(strumSample, potMidValue[atString]);


      //ESP_LOGD(TAG, "strumSensitivityFactor = %d strum[atString].currentSampleN=  %lu" , strumSensitivityFactor , strum[atString].currentSampleN);


      //Find derivative approximation by four sample . Logging values showed that adjacent samples are almost identical, so derivative is found among 1,3 and 2,4 then added for averaging
      strum[atString].strumDerivative = ((int)strum[atString].currentSampleN - (int)strum[atString].preSampleN2 ) +
                                        ((int)strum[atString].preSampleN1 - (int)strum[atString].preSampleN3) ;
      strum[atString].strumDerivative *= strumSensitivityFactor; //amplify strum derivative to increase sensitivity of strums

      //ESP_LOGD(TAG, "current sample deflection %d %d %d %d", (int)strum[atString].currentSampleN , (int)strum[atString].preSampleN1 , (int)strum[atString].preSampleN2, (int)strum[atString].preSampleN3 );
                                        
      // Shift the pre sampling window to accomidate next sample
      strum[atString].preSampleN3 = strum[atString].preSampleN2;
      strum[atString].preSampleN2 = strum[atString].preSampleN1;
      strum[atString].preSampleN1 = strum[atString].currentSampleN;

      /* Dynamic strum center: when bar is at rest, drift potMidValue toward actual reading so deflection mode works without perfect mechanical centering */
      if (!strum[atString].inStrum && strum[atString].currentSampleN < STRUM_CENTER_REST_THRESH) {
        float s = (float)strumSample;
        potMidValue[atString] += STRUM_CENTER_ALPHA * (s - potMidValue[atString]);
      }






      



      // If there is an active strum on current string
      if (strum[atString].inStrum == true)
      {


        //find max of 4 previous samples to find the strum peak value
        {
          uint32_t a = strum[atString].currentSampleN;
          uint32_t b = strum[atString].preSampleN3;
          uint32_t c = strum[atString].preSampleN2;
          uint32_t d = strum[atString].preSampleN1;
          if (b > a) a = b;
          if (c > a) a = c;
          if (d > a) a = d;
          strum[atString].strumPeakValue = a;
        }
        //ESP_LOGD( TAG , "strum peak value %d" , (int)strum[atString].strumPeakValue ); //!!!! careful! causes freezing


        // Check if the strum bar has been released
        // for Plucked instruments, check derivative of deflection, for bowed
        // instruments check average deflection
        if (pluckedInstrument == true)
        {
#ifdef STRUM_DEFLECTION
          if (strum[atString].currentSampleN < strumReturnThreshold)
#else
          if (strum[atString].strumDerivative < derivativeReturnThreshold)
#endif
          {
            strumReleased = true;
            //ESP_LOGD( TAG , "strum release" );

          }
          else{
       
            strumReleased = false;
            //ESP_LOGD( TAG , "strum not release" );

          }
        }
        else //sustained instruments like viola
        {

          if (strum[atString].currentSampleN < strumReturnThreshold)

            strumReleased = true;
          else
          {
            strumReleased = false;

            //TODO:may be something to control the volume of the bowed instrument after the not is played

          }
        }

        ///// Active Strum Released /////
        if (strumReleased)
        {
          // ESP_LOGD(TAG, "Strum %d released @ diff %lu", atString,
          //          strum[atString].currentSampleN);

          strumCalibCounter = 0;
          stringCalibCounter = 0;

          // Play the note if the note on release option is set
          if ((pluckedInstrument) && (noteOnReleaseEnabled) &&
              (staccatoOccured == false))
          {
            if (deviceState == PLAY_STATE)
            {
              // Read current fret at release (e.g. open string) so getNote/lastFretAtNoteOn are correct for hammer-on after open
              strum[atString].adcFretAverage = 0;
              for (int i = 0; i < 3; i++) {
                strum[atString].adcFretAverage += (float)readAndAverageString(atString);
                if (i == 1) vTaskDelay(0);  /* yield mid-loop to reduce ADC contention and freezes */
              }
              strum[atString].adcFretAverage /= 3;
              vTaskDelay(0);  /* yield after ADC burst before getNote/noteOn */
              getNote(&strum[atString], atString);
              // play the note only if the string is enabled
              if (stringEnabledArray[atString] == true){
                vTaskDelay(0);  /* yield before noteOn to reduce freezes (constant velocity and dynamics) */
                noteOn(&strum[atString], atString, 1);
              }
              strum[atString].strumNoteIsPlayed = true;
              latestPitchbends[atString] = strum[atString].pitchBend;
              lastNotePlayedTick = noteTickCounter;
              lastNoteWasStrum = true;
              lastNoteOnTick[atString] = noteTickCounter;
              hammerOnLockoutUntilTick[atString] = noteTickCounter + HAMMER_ON_LOCKOUT_TICKS;
              lastFretAtNoteOn[atString] = strum[atString].fret;
              lastPlayedNote[atString] = strum[atString].note;
              lastPlayedNoteIn24[atString] = strum[atString].noteIn24;
              lastPlayedPitchBend[atString] = strum[atString].pitchBend;
              lastPlayedStrumDirection[atString] = strum[atString].strumDirection;
            }
          }

          // Trun the note off and reset any flags and values for detecting
          if (strum[atString].strumNoteIsPlayed == true)
          {

            latestPitchbends[atString] = (strum[atString].pitchBend);
            noteOff(&strum[atString], atString, 1);

            strum[atString].strumNoteIsPlayed = false;
#ifdef AUTO_STRUM_CALIBRATION
            autoStrumCalibration = true;
#endif
          }

          // Mark that the string is out of the strum
          strum[atString].inStrum = false;
          // Reset pre-sample window so next strum's derivative uses a clean baseline
          // (avoids inflated derivative and inconsistent/noisy trigger on next stroke)
          strum[atString].preSampleN1 = strum[atString].currentSampleN;
          strum[atString].preSampleN2 = strum[atString].currentSampleN;
          strum[atString].preSampleN3 = strum[atString].currentSampleN;
          vTaskDelay(0);  /* yield after strum release (ADC/noteOff) to reduce freezes */
        }//strum realeased
      }//instrum == true

      else
      { // instrum == false


        // Check if the strum bar has been excited
        // for Plucked instruments, check derivative of deflection, for bowed
        // instruments check the deflection
        if (pluckedInstrument == true)
        {
#ifdef STRUM_DEFLECTION
          if (strum[atString].currentSampleN > strumStartThreshold)

#else
          if (strum[atString].strumDerivative > (int)derivativeThreshold)

#endif
            strumDetected = true;
        }
        else
        {
          if (strum[atString].currentSampleN > strumStartThreshold)
          {
            strumDetected = true;
          }
        }

        if (strumDetected)
        {
          ESP_LOGD(TAG, "Strum %d detected @ diff %d", atString,
                   strum[atString].strumDerivative); 
          strumDetected = false; // to prevent entering this section before
                                 // another detection

          // if enough time passed since last strum
          if (strum[atString].enoughTimePassedSinceLastStrum)
          {

            // reset calibration counter to zero to stop auto-calibration while
            // strumming

            strumCalibCounter = 0;
            stringCalibCounter = 0;

            idleTimer = millis();

            strum[atString].timingCounter = 0;
            strum[atString].enoughTimePassedSinceLastStrum = false;



            // Initialize the strum data structure
            // fretSample = 0;
            strum[atString].adcFretAverage = 0;

            //read fret ADC 10 times and find the average; yield periodically to avoid freezing (ADC contention)
            for (int i = 0; i < 10 ; i++){
              strum[atString].adcFretAverage += (float)readAndAverageString(atString);
              if (i == 2 || i == 5 || i == 8) vTaskDelay(0);
            }
            strum[atString].adcFretAverage /= 10;

            // Capture the time of the begining of the strum{
            // strum[atString].timingCounter = mainTimingCounter;

            // if(constantVelocityEnable){

            // This populates relavent fields of the tap structure
            // incase a tap note is played after the strum.
            if ( tapWithoutStrumEnabled || (tappingEnabled == true && tap[atString].strumOccured == false))
            {
              tap[atString].strumOccured = true;
              tap[atString].strummedFretSample = strum[atString].adcFretAverage;
              tap[atString].previousFretSample = strum[atString].adcFretAverage;

              tap[atString].prevNote = strum[atString].note;
              tap[atString].prevPitchBend = strum[atString].pitchBend;
            }

            getNote(&strum[atString], atString);


            if (deviceState == PLAY_STATE)
            {
              if (pluckedInstrument)
              {
                if ((noteOnReleaseEnabled == false) &&
                    (staccatoOccured == false))
                {
                  if (stringEnabledArray[atString] == true){
                    vTaskDelay(0);  /* yield before noteOn to reduce freezes */
                    noteOn(&strum[atString], atString, 1);
                  }
                  strum[atString].strumNoteIsPlayed = true;
                  latestPitchbends[atString] = strum[atString].pitchBend;
                  lastNotePlayedTick = noteTickCounter;
                  lastNoteWasStrum = true;
                  lastNoteOnTick[atString] = noteTickCounter;
                  hammerOnLockoutUntilTick[atString] = noteTickCounter + HAMMER_ON_LOCKOUT_TICKS;
                  lastFretAtNoteOn[atString] = strum[atString].fret;
                  lastPlayedNote[atString] = strum[atString].note;
                  lastPlayedNoteIn24[atString] = strum[atString].noteIn24;
                  lastPlayedPitchBend[atString] = strum[atString].pitchBend;
                  lastPlayedStrumDirection[atString] = strum[atString].strumDirection;
                }
              }
              else
              {
              if (stringEnabledArray[atString] == true){
                vTaskDelay(0);  /* yield before noteOn to reduce freezes (constant velocity and dynamics) */
                noteOn(&strum[atString], atString, 1);
              }

                strum[atString].strumNoteIsPlayed = true;
                latestPitchbends[atString] = strum[atString].pitchBend;
                lastNotePlayedTick = noteTickCounter;
                lastNoteWasStrum = true;
                lastNoteOnTick[atString] = noteTickCounter;
                hammerOnLockoutUntilTick[atString] = noteTickCounter + HAMMER_ON_LOCKOUT_TICKS;
                lastFretAtNoteOn[atString] = strum[atString].fret;
                lastPlayedNote[atString] = strum[atString].note;
                lastPlayedNoteIn24[atString] = strum[atString].noteIn24;
                lastPlayedPitchBend[atString] = strum[atString].pitchBend;
                lastPlayedStrumDirection[atString] = strum[atString].strumDirection;
              }
            }


            // Mark that were are now in a strum, note is already played and it is time to check for strum release in the next run and turning the note off
            strum[atString].inStrum = true;
            vTaskDelay(0);  /* yield after strum detect (heavy ADC) to avoid freeze in fast chord playing */

          } // Strum Spacing Time Check
        }// strum detected
      }//instrum false



      /////////Tapping simple algorithm (one ADC read per string; every 2nd cycle to reduce ADC contention and freezes)
      if((tappingEnabled || tapWithoutStrumEnabled || deviceState == UI_STATE) )  //&& ((processIoCounter & 1) == 0))
      {
        currTapAverage = (float)readAndAverageString(atString);
        tap[atString].adcFretAverage = currTapAverage;

        if(tap[atString].tapPressed == false)// tap not pressed
        {
          //any fret value change?
          if(fAbsoluteDiff(currTapAverage, tap[atString].previousFretSample) >=
               500)
          {
            if(tap[atString].debounceCount == 2 )
            {
              tap[atString].debounceCount = 0;
              tap[atString].velocity = 127;

              getTapNote(&tap[atString] , atString); //get the note related to this current tap
              if(tap[atString].fret != 0 ) //when fret is released we have a note change to fret = 0, which is open string, this should be ignored
              {
                if( deviceState == UI_STATE)
                {
                  {
                    changeSetting(&tap[atString] , atString );
                    inputToUART(0x99, 61, 127);   //make a drum noise
                    vTaskDelay(pdMS_TO_TICKS(100)); //wait
                    inputToUART(0x89, 61, 127);     //turn it off
                  }
                }
                else
                { //i.e. PLAY_STATE: tap-without-strum = any tap (no window); normal tapping = tap only after strum within window
                  if (stringEnabledArray[atString] == true){
                    bool mayTap = tapWithoutStrumEnabled || (tappingEnabled && tap[atString].strumOccured);
                    bool withinWindow = (lastNotePlayedTick != 0) &&
                        ((uint32_t)(noteTickCounter - lastNotePlayedTick) < TAP_WINDOW_TICKS);
                    bool afterStrum = lastNoteWasStrum;
                    bool tapWithoutStrumOk = tapWithoutStrumEnabled;
                    bool normalTapOk = !tapWithoutStrumEnabled && withinWindow && afterStrum;
                    if (mayTap && (tapWithoutStrumOk || normalTapOk)) {
                      tapNoteOn(&tap[atString], atString, 1);
                      lastNotePlayedTick = noteTickCounter;
                      lastNoteWasStrum = false;  // for normal tapping, next tap needs a strum
                      tap[atString].tapNoteIsPlayed = true;
                      lastNoteOnTick[atString] = noteTickCounter;
                      hammerOnLockoutUntilTick[atString] = noteTickCounter + HAMMER_ON_LOCKOUT_TICKS;
                      lastFretAtNoteOn[atString] = tap[atString].fret;
                      lastPlayedNote[atString] = tap[atString].note;
                      lastPlayedNoteIn24[atString] = tap[atString].noteIn24;
                      lastPlayedPitchBend[atString] = tap[atString].pitchBend;
                      lastPlayedStrumDirection[atString] = strum[atString].strumDirection;
                    }
                  }
                }
              }
              tap[atString].tapPressed = true;
              tap[atString].previousFretSample =  currTapAverage;

            }
            else{
              tap[atString].debounceCount++;
            }

          }
        }
        else{ //tapPressed = true

          if(fAbsoluteDiff(currTapAverage, tap[atString].previousFretSample) >=
               500)  //fret value changed
          {
            tap[atString].tapPressed = false;
            if (tap[atString].tapNoteIsPlayed) {
              tapNoteOff(&tap[atString], atString, 1);
              tap[atString].tapNoteIsPlayed = false;
            }
          }
          if(currTapAverage >= fretBarMaxValue[atString])
            tap[atString].strumOccured = false;
        }



      }

      // Hammer-on: when window expires, turn off the sustained note (no ADC needed).
      if (hammerOnEnabled && (stringEnabledArray[atString] == true) && !strum[atString].inStrum
          && (lastNoteOnTick[atString] != 0)
          && ((uint32_t)(noteTickCounter - lastNoteOnTick[atString]) >= HAMMER_ON_WINDOW_TICKS))
      {
        strum[atString].note = lastPlayedNote[atString];
        strum[atString].noteIn24 = lastPlayedNoteIn24[atString];
        strum[atString].pitchBend = lastPlayedPitchBend[atString];
        strum[atString].strumDirection = lastPlayedStrumDirection[atString];
        noteOff(&strum[atString], atString, 1);
        lastNoteOnTick[atString] = 0;
      }
      // Hammer-on: fret change (no new strum) within window after last note; lockout avoids double note
      // Run only every 2nd cycle to reduce ADC load (yields after read/block help avoid freezes).
      else if (hammerOnEnabled && ((processIoCounter & 1) == 0)
          && (stringEnabledArray[atString] == true) && !strum[atString].inStrum
          && (lastNoteOnTick[atString] != 0)
          && ((uint32_t)(noteTickCounter - lastNoteOnTick[atString]) < HAMMER_ON_WINDOW_TICKS)
          && (noteTickCounter >= hammerOnLockoutUntilTick[atString]))
      {
        float hammerOnFretVal;
        if ((tappingEnabled || tapWithoutStrumEnabled))
          hammerOnFretVal = currTapAverage;
        else {
          hammerOnFretVal = (float)readAndAverageString(atString);
          vTaskDelay(0);  /* yield after ADC read to avoid semaphore contention */
        }
        tap[atString].adcFretAverage = hammerOnFretVal;
        getTapNote(&tap[atString], atString);
        uint8_t newFret = tap[atString].fret;
        if (newFret != 0 && newFret != lastFretAtNoteOn[atString]) {
          if (newFret != hammerOnLastFret[atString]) {
            hammerOnLastFret[atString] = newFret;
            hammerOnDebounceCount[atString] = 0;
          } else {
            hammerOnDebounceCount[atString]++;
            if (hammerOnDebounceCount[atString] >= HAMMER_ON_DEBOUNCE) {
              strum[atString].note = lastPlayedNote[atString];
              strum[atString].noteIn24 = lastPlayedNoteIn24[atString];
              strum[atString].pitchBend = lastPlayedPitchBend[atString];
              strum[atString].strumDirection = lastPlayedStrumDirection[atString];
              noteOff(&strum[atString], atString, 1);
              strum[atString].adcFretAverage = hammerOnFretVal;
              getNote(&strum[atString], atString);
              vTaskDelay(0);  /* yield before noteOn to reduce freezes */
              noteOn(&strum[atString], atString, 1);
              lastNoteOnTick[atString] = noteTickCounter;
              hammerOnLockoutUntilTick[atString] = noteTickCounter + (HAMMER_ON_LOCKOUT_TICKS / 2);
              lastFretAtNoteOn[atString] = strum[atString].fret;
              lastPlayedNote[atString] = strum[atString].note;
              lastPlayedNoteIn24[atString] = strum[atString].noteIn24;
              lastPlayedPitchBend[atString] = strum[atString].pitchBend;
              lastPlayedStrumDirection[atString] = strum[atString].strumDirection;
              hammerOnDebounceCount[atString] = 0;
            }
          }
        } else if (newFret == 0) {
          /* Fret released (open string): turn off current note for sustained instruments */
          if (hammerOnLastFret[atString] != 0) {
            hammerOnLastFret[atString] = 0;
            hammerOnDebounceCount[atString] = 0;
          } else {
            hammerOnDebounceCount[atString]++;
            if (hammerOnDebounceCount[atString] >= HAMMER_ON_DEBOUNCE) {
              strum[atString].note = lastPlayedNote[atString];
              strum[atString].noteIn24 = lastPlayedNoteIn24[atString];
              strum[atString].pitchBend = lastPlayedPitchBend[atString];
              strum[atString].strumDirection = lastPlayedStrumDirection[atString];
              noteOff(&strum[atString], atString, 1);
              lastNoteOnTick[atString] = 0;
              hammerOnDebounceCount[atString] = 0;
            }
          }
        }
        vTaskDelay(0);  /* yield after hammer-on block when entered to reduce freezes */
      }

      vTaskDelay(0);  /* yield after each string to avoid blocking too long and reduce freezes */
      if (s_etar_log_ckpt) {
        ESP_LOGI(TAG, "freeze ckpt F_s%u", (unsigned)atString);
      }
      //////////  Chord Selection    ////////////////////////////////////////
      ///////////////////////////////////////////////////////////////////////
      //it seems that this section doesnt work
      if (chordEnabled)
      {

        // Take averaged sample of frets to reduce noise
        fretSample = 0;
        (void)fretSample;
        if (atString == STRING_FOUR)
        {

          currentFretAverage = readAndAverageString(STRING_FOUR);

          // We need to make sure all strings are out of a strum before we can
          // change the chord type because without this check the chord can be
          // changed in a strum resulting in the note of the new chord being
          // turned off instead of the playing notes.
          if ((strum[STRING_ONE].inStrum == false) &&
              (strum[STRING_TWO].inStrum == false) &&
              (strum[STRING_THREE].inStrum == false) &&
              (strum[STRING_FOUR].inStrum == false))
          {

            if (currentFretAverage >=
                (fretBarMaxValue[STRING_FOUR] - (float)fretNoiseMargin))
            {
              chordDebounce = 0;
            }
            else
            {
              // Debouncing to make sure fret is actually pressed
              if (chordDebounce == 0)
              {
                previousFretAverage = currentFretAverage;
                chordDebounce++;
              }
              else if ((fAbsoluteDiff(previousFretAverage,
                                      currentFretAverage) <=
                        (int)fretNoiseMargin) &&
                       (chordDebounce < tappingDebounceNum))
              {
                chordDebounce++;
                previousFretAverage = currentFretAverage;
              }
              else
              {
                chordDebounce = 0;
              }
            }

            if (chordDebounce >= tappingDebounceNum)
            {
              tempChordSel = fretVoltageToFretNumber(currentFretAverage);
              tempChordSel -= 10; // To make it fit the chord Table index
              if ((tempChordSel >= 0) && (tempChordSel <= 14))
              {
                chordType = tempChordSel;
              }
            }
          }
        }

      } // Chord selection end
      if (chordEnabled)
        vTaskDelay(0);  /* yield after chord ADC read to avoid freeze when playing fast in chord mode */

      //////////  Vibrato            ////////////////////////////////////////
      ///////////////////////////////////////////////////////////////////////
      //in every processIO loop, read accelerometer and apply vibrato
      if (processIoCounter % 1 == 0) 
   //  if (mainTimingCounter % 2 == 0) //tried values greater than 1, didn't produce smooth vibrato , even as low as 5
      {
        if (vibratoEnabled)
        {
          // vibrato using joystick **** changed to accelerometer ADC8 is
          // accelerometer. samaple joystick 10 times and average

          //find the average of accelerometer, 
          joyValue = 0;
          for (int i=0 ; i < 1 ; i++) // averaging didnt help much, so back to reading 1 sample
          {
            joyValue += (float)readAccelerometer();
          }
          joyValue = joyValue / 1 ;

          //to shift the center point of the accelerometer. when holding in  position, z axis reading jumps from 0 to 65K
          //by doing this, the center will be 32k 
          if (joyValue >= 0 && joyValue < 32768) 
          {
            joyValue += 32768;
          }
          else
          {
            joyValue -= 32768;
          } 


          //truncate joyValue as it fluctuates too much
          joyValue = (int)(joyValue/100); 

          joystickDeflection =  (joyValue - 320) * (float)vibratoValue * 5; //320 is the aproximate value in holding position, 5 is amplification found by experiment
          

        

          //ESP_LOGD(TAG, "Accel = %f, deflection = %f", joyValue, joystickDeflection);

          // if joystick deflected , send PitchBend events to synth chip and USB
          if (fAbsoluteDiff(joystickDeflection, 0) > 100   &&  //not to have pitchbend for little shakes
              joyValue < 400 )  // joyValue greater than 400 means eTar is laid down so no vibrato
          {

            // USB
            // to prevent usb congestion, send vibrato pitch pend every 10
            // ProcessIO call

            if (isUSBConnected())  //this function doesnt detect if usb is connected!!!!!!!!!!!!
            {
              if (latestPitchbends[atString] == 0x2000)
              {
                tempPitchBend = (PBCenter) + (int)joystickDeflection + fineTuningPB;
                midiTx(0xE,0xE0, (tempPitchBend & 0x00FF),
                            ((tempPitchBend & 0xFF00) >> 8));
                // delayFor(100);
              }
              else if (latestPitchbends[atString] == 0x2800)
              {
                tempPitchBend =
                    (PBQuarterToneOffset + PBCenter) + (int)joystickDeflection + fineTuningPB;
                midiTx(0xE,0xE1, (tempPitchBend & 0x00FF),
                            ((tempPitchBend & 0xFF00) >> 8));
                // delayFor(100);
              }
            
            } else{
              // UART
              if (latestPitchbends[atString] == 0x2000)
              {
                tempPitchBend = (PBCenter) + (int)joystickDeflection + fineTuningPB; // 1500;//
                inputToUART(0xE0, (tempPitchBend & 0x00FF),
                            ((tempPitchBend & 0xFF00) >> 8));
              }
              else if (latestPitchbends[atString] == 0x2800)
              {
                tempPitchBend =
                    (PBQuarterToneOffset + PBCenter) +
                    (int)joystickDeflection + fineTuningPB; //(2 * 1500); //joystickDeflection;
                inputToUART(0xE1, (tempPitchBend & 0x00FF),
                            ((tempPitchBend & 0xFF00) >> 8));
              }
            }
            
          }
        } // if vibrato enabled
      }

    } // Main for loop for strumming

    // Staccato: only when enabled; then run only when no string in strum and every 2nd tick to avoid dropped notes
    if (staccatoEnable && pluckedInstrument)
    {
      bool anyInStrum = strum[0].inStrum || strum[1].inStrum || strum[2].inStrum || strum[3].inStrum;
      if (!anyInStrum && (processIoCounter % 2 == 0))
      {
        float adc[NUM_STRINGS];
        bool allPressed = true;
        for (int s = 0; s < NUM_STRINGS; s++) {
          adc[s] = (float)readAndAverageString(s);
          if (adc[s] >= fretBarMaxValue[s])
            allPressed = false;
        }
        vTaskDelay(0);  /* single yield after 4 reads */
        if (allPressed) {
          uint8_t frets[NUM_STRINGS];
          bool sameFret = true;
          for (int s = 0; s < NUM_STRINGS; s++) {
            frets[s] = getFretNumberForString((uint8_t)s, adc[s]);
            if (s > 0 && frets[s] != frets[0])
              sameFret = false;
          }
          if (sameFret && frets[0] > 0) {
            silenceAllNotesFromGesture();
          } else {
            staccatoOccured = false;
          }
        } else {
          staccatoOccured = false;
        }
      }
    }

    /* String drift calibration: after strum work so it never delays strum detection */
    if (processIoCounter % 100 == 0) {
      for (int m = 0; m < NUM_STRINGS; m++) {
        tempFretADC = (float)readAndAverageString(m);
        if (fretBarMaxValue[m] - tempFretADC < STRING_DRIFT_THRESHOLD) {
          fretBarMaxValue[m] = tempFretADC;
        }
        vTaskDelay(0);
      }
    }
  }   // Strum Detection enabled
  if (s_etar_log_ckpt) {
    ESP_LOGI(TAG, "freeze ckpt F after strum loop");
  }

  // always recalibrate strums on power change
  if (externalPowerChanged()
#ifdef AUTO_STRUM_CALIBRATION
      ////////// Misc Timers and Calibration //////////
      // Auto Strum Calibration when Strum Calib Timer reacher the set timeout
      // value
      //|| (strumCalibCounter >= ((unsigned long)strumCalibTimeout * 1000UL) &&
      //   autoStrumCalibration == true)
#endif
  )
  { // the number from app is multiplied by
    // 1000 to provide bigger time.
    //ESP_LOGD(TAG, "Recalibrating Strums");
    if ((!strum[STRING_ONE].inStrum) && (!strum[STRING_TWO].inStrum) &&
        (!strum[STRING_THREE].inStrum) && (!strum[STRING_FOUR].inStrum))
    {
      strumCalibrate();
      strumCalibCounter = 0;
    }
  }
  if (s_etar_log_ckpt) {
    ESP_LOGI(TAG, "freeze ckpt G after strumCalibrate");
  }

  // always recalibrate strings on power change
  if (externalPowerChanged()

#ifdef AUTO_STRING_CALIBRATION


      // every few second, calls stringCalibrate() where

      //|| (stringCalibCounter >=
      //        ((unsigned long)stringCalibTimeout *
      //         1000UL) // every 1 second if there is no strum, autoclibrate
      //    && autoStringCalibration == true)
#endif
  )
  { // the number from app is multiplied
    // by 1000 to provide bigger time.
    //ESP_LOGD(TAG, "Recalibrating Strings");
    if (!strum[STRING_ONE].inStrum)
      stringCalibrate(0, true); // recalibrate
    if (!strum[STRING_TWO].inStrum)
      stringCalibrate(1, true); // recalibrate
    if (!strum[STRING_THREE].inStrum)
      stringCalibrate(2, true); // recalibrate
    if (!strum[STRING_FOUR].inStrum)
      stringCalibrate(3, true); // recalibrate

    stringCalibCounter =
        0; // if those many seconds have passed,
           // bring the counter to zero
  }

  if (externalPowerChanged())
  {
    externalPowerChangeAck();
  }
  if (s_etar_log_ckpt) {
    ESP_LOGI(TAG, "freeze ckpt H ProcessIO exit");
  }

#ifdef CHYME_WHEN_IDLE
  if (millis() - idleTimer >=
      300000) // after 300 seconds of being idle, make noise
  {

    inputToUART(0x90, 96, 64); // high pitch C , low volume
    inputToUART(0x80, 96, 64);
    inputToUART(0xE0, 0x00, 0x40); // no pitchbend
    delay();
    delay();
    delay();
    inputToUART(0x90, 100, 64); // high pitch E , low volume
    inputToUART(0x80, 100, 64);
    inputToUART(0xE0, 0x00, 0x40); // no pitchbend
    delay();
    delay();
    delay();
    inputToUART(0x90, 103, 64); // high pitch G , low volume
    inputToUART(0x80, 103, 64);
    inputToUART(0xE0, 0x00, 0x40); // no pitchbend
    idleTimer = millis();
  }
#endif



  
  

} // ProcessIO

// #pragma code

/**
 *	Sleep Function                                                         *
 **/
void checkSleep()
{
  // 300000 = 5 Min      30000 = 30 seconds
  // if eTar is not played for x mins, watch dog timer gets enabled and goes to
  // sleep mode. WDT wakes cpu every N second (defined by WDTPS), checks if
  // the first strum is changed or not. if changed, gets out of sleep mode and
  // returns from  checkSleep() function to do ProcessIO(), if no strum, gets
  // back to sleep again.
  /* Do not enter light sleep when BLE is connected; it freezes the device. */
  if (isBLEConnected()) {
    return;
  }
  if (/* false && */ ((millis() - idleTimer) >= (60000UL * SLEEP_DELAY_MINUTES))) //60000 is one minute wich is multiplied by SLEEP_DELAY_MINUTES
  {
    ESP_LOGD(TAG, "Sleep mode");
    // accVal = readAndAverageStrum(STRUM_ONE);
    // prevAccVal = accVal;


    inSleepMode = true;

    ESP_LOGD(TAG, "Sleep");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    esp_sleep_enable_timer_wakeup(60000000ULL * SHUTDOWN_DELAY_MINUTES); // delay in microseconds
    esp_sleep_enable_gpio_wakeup(); // allow waking up through PWR button
    
    // shutdown most IOs

    powerLed(LED_OFF);
    batteryLed(0,LED_OFF); // forcing led off
    setLed(LED_BT, false); // turn bluetooth led off

    ledLoop(); // ensure leds go off
    
    mute(true);
    disableSpeaker();
    disableHeadphone();
    disableDAC();
    //disableStrumPower();
    vTaskDelay(pdMS_TO_TICKS(5));

    // keep IOs this way while sleeping

    esp_sleep_enable_gpio_switch(false);
    esp_light_sleep_start();  //enters light sleep here and wakes up here

    ESP_LOGD(TAG, "Woke up by cause #%d", esp_sleep_get_wakeup_cause());
    esp_sleep_enable_gpio_switch(true);

    powerLed(LED_SOLID);
    ledLoop();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
      // we woke due to shutdown timer -> power off
      ESP_LOGI(TAG, "Shutting down.");
      poweroff();
      return;
    }
    enableStrumPower(); // it wasn't disabled
    mute(false);

    if (isHeadPhoneConnected()) {
      disableSpeaker();
      enableHeadphone();
    } else {
      disableHeadphone();
      enableSpeaker();
    }

    inSleepMode = false;
    idleTimer = millis();

    // for (int i = 0; i < 16; i++) {
    //   inputToUART(0xB0 + i, 0x07, curVol); // Set Volume on Synth Chip
    // }

    // for (int i = 0; i < 16; i++) {
    //   inputToUART(0xB0 + i, 0x40, 0x7F); // Set Sustain on Synth Chip
    // }

    // for (int i = 0; i < 16; i++) {
    //   inputToUART(0xC0 + i, synthInstrument,
    //               synthInstrument); // Program Change - 104 Sitar
    // }

 

    // recalibrate

    /**
        Calibrations
     **/
    // read strum sensors at rest, put their values as their mid value
    strumCalibrate();
    strumCalibCounter = 0;

    ESP_LOGD(TAG, "Calibrating Strings");
    for (int i = 0; i < NUM_STRINGS; i++) {
      stringCalibrate(i, false); // initial calibration
    }
    stringCalibCounter = 0;

    joystickCalibrate();

    for (int i = 0; i < NUM_STRINGS; i++) {
      tap[i].previousFretSample = fretBarMaxValue[i];
      tap[i].adcFretAverage = fretBarMaxValue[i];
      strum[i].adcPrevFret = fretBarMaxValue[i];
      strum[i].adcFretAverage = fretBarMaxValue[i];
      latestPitchbends[i] = 0x2000; // for vibrato
      ESP_LOGD(TAG, "%d max = %f", i, fretBarMaxValue[i]);
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // stop_timer();
    // ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_enable_ulp_wakeup()); // wakeup
    // on ADC change

    // while (accVal >= (prevAccVal - 15) && accVal <= (prevAccVal + 15))
    // {

    //   // mLED_2_Off() //to see how wakes up every N second
    //   ESP_ERROR_CHECK_WITHOUT_ABORT(esp_light_sleep_start()); // go to sleep
    //   // prevAccVal = accVal;
    //   accVal = readAndAverageStrum(STRUM_ONE);
    // }
    // start_timer();
    // idleTimer = millis();
    // inSleepMode = false;
  }




}

#ifdef INST_SETAR
void changeSetting(TAP *tap, uint8_t atString) {
  uint8_t settingFret = tap->fret;
  switch (settingFret) {
    case 1:
      // Tuning
      if (atString == 0)
        handleMessage(0x0E, 0);  // tuningIndex 0, do-sol-do-do
      else if (atString == 1)
        handleMessage(0x0E, 2);  // tuningIndex 2, do-sol-re-re
      else if (atString == 2)
        handleMessage(0x0E, 3);  // tuningIndex 3, do-fa-do-do
      else if (atString == 3)
        handleMessage(0x0E, 26);  // tuningIndex 26, mi-la-re-sol
      break;
    case 2:
      // Instrument
      if (atString == 0)
        handleMessage(0x16, 104);  // 104, sitar
      else if (atString == 1)
        handleMessage(0x16, 107);  // 107, koto
      else if (atString == 2)
        handleMessage(0x16, 24);  // 24, nylon guitar
      else if (atString == 3)
        handleMessage(0x16, 41);  // 41, viola
      break;
    case 3:
      // Tapping
      if (atString == 0)
        handleMessage(0x06, 1);  // tapping enabled
      else if (atString == 1){ //disable both
        handleMessage(0x06, 0);  // tapping disabled
        handleMessage(0x33, 0);  // tapping without strumming disabled
      }
      else if (atString == 2)
        handleMessage(0x33, 1);  // tapping without strumming enabled
      else if (atString == 3)
        handleMessage(0x33, 0);  // tapping without strumming disabled
      break;
    case 4:
      // Pedal/quartetones
      if (atString == 0)
        handleMessage(0x15, 1);  // quarter tones enabled
      else if (atString == 1)
        handleMessage(0x15, 0);  // quarter tones  disabled
      else if (atString == 2)
        handleMessage(0x01, 1);  // sustain pedal enabled
      else if (atString == 3)
        handleMessage(0x01, 0);  // sustain pedal disabled
      break;
    case 5:
      // chords/drums
      if (atString == 0)
        handleMessage(0x35, 1);  // chord mode enabled
      else if (atString == 1)
        handleMessage(0x35, 0);  // chord mode  disabled
      else if (atString == 2)
        handleMessage(0x32, 1);  // percussion enabled
      else if (atString == 3)
        handleMessage(0x32, 0);  // percussion enabled
      break;
    case 6:
      // Transpose
      if (atString == 0) {
        transposeValue += 7;
        handleMessage(0x19, transposeValue);  // Transpose to 5th
      } else if (atString == 1) {
        transposeValue++;
        handleMessage(0x19,
                               transposeValue);  // incrment transpose by 1
      } else if (atString == 2) {
        transposeValue--;
        handleMessage(0x19,
                               transposeValue);  // decrement transpose by 1
      } else if (atString == 3) {
        transposeValue = 0;
        handleMessage(0x19, transposeValue);  // zero transpose
      }
      break;
    case 7:
      // vibrato
      if (atString == 0) {
        handleMessage(0x0D, 10);  // set vibrato value to max
      } else if (atString == 1) {
        handleMessage(0x0D, vibratoValue++);  // increase vibrato
      } else if (atString == 2) {
        handleMessage(0x0D, vibratoValue--);  // decrease vibrato
      } else if (atString == 3) {
        handleMessage(0x0D, 0);  // zero vibrato
      }
      break;
    case 8:
      // Pitch system
      if (atString == 0)
        handleMessage(0x34, EDO_24_PITCH);  // 24-edo
      else if (atString == 1)
        handleMessage(0x34, EDO_51_TURKISH_PITCH);  // 51- edo-turkish
      else if (atString == 2)
        handleMessage(0x34, JAZAYERI_PITCH);  // persian 1 -jazayeri
      else if (atString == 3)
        handleMessage(0x34, EDO_51_SHARIF_PITCH);  // persian 2 -sharif
      break;
    case 9:
      // Accessory 9
      if (atString == 0)
        handleMessage(0x34, EDO_24_PITCH);  // 24-edo
      else if (atString == 1)
        handleMessage(0x34, EDO_51_TURKISH_PITCH);  // 51- edo-turkish
      else if (atString == 2)
        handleMessage(0x34, JAZAYERI_PITCH);  // persian 1 -jazayeri
      else if (atString == 3)
        handleMessage(0x34, EDO_51_SHARIF_PITCH);  // persian 2 -sharif
      break;

      
    case 10:
      // Metronome 10
      if (atString == 0)
        handleMessage(0x4E, 1);  // metronome on
      else if (atString == 1)
        handleMessage(0x4E, 0);  // metrnome off
      else if (atString == 2)
      {
        tempoValue--;
        handleMessage(0x4F, tempoValue);  // decrease metrnome speed by one
      }
      else if (atString == 3){
        tempoValue++;
        handleMessage(0x4F, tempoValue);  // increase metrnome speed by one 
      }
      break;

    case 11:
      // Metronome beats 11
      if (atString == 0)
      {
        metronomeNumBeats = 2;
        handleMessage(0x50, metronomeNumBeats);  // 2 beats 
      }
      else if (atString == 1){
        metronomeNumBeats++;
        handleMessage(0x50, metronomeNumBeats);  // increase number of beats by one
      }
      else if (atString == 2){
        metronomeNumBeats--;
        handleMessage(0x50, metronomeNumBeats);  // decrease number of beats by one
      }
      else if (atString == 3){
        metronomeNumBeats = 4;
        handleMessage(0x50, metronomeNumBeats);  // 4 beats
      }
      break;
    case 12:
      // Stacato,lefthand
      if (atString == 0)
        handleMessage(0x14, 1);  // left hand enabled
      else if (atString == 1)
        handleMessage(0x14, 0);  // left hand disabled
      else if (atString == 2)
        handleMessage(0x09, 1);  // staccato enabled
      else if (atString == 3)
        handleMessage(0x09, 0);  // staccato disabled
      break;
    case 13:
      // strum sensitivity
      if (atString == 0)
        handleMessage(
            0x36, 4);  // max strum sensitivity ( derivative threshold)
      else if (atString == 1){
        strumSensitivityFactor = strumSensitivityFactor + 1;
        handleMessage(
            0x36, strumSensitivityFactor);  // increase strum sensitivity


      }
      else if (atString == 2){
        strumSensitivityFactor = strumSensitivityFactor - 1;
        handleMessage(
            0x36, strumSensitivityFactor);  // decrease strum sensitivity
      }

      else if (atString == 3)
        handleMessage(
            0x36, 1);  // min strum sensitivity ( derivative threshold)
      break;
    case 14:
      // metronome volume 14
      if (atString == 0){
        metronomeVol = 127;
        handleMessage(0x51, metronomeVol);  // max volume
      }
      else if (atString == 1){
        metronomeVol += 5;
        handleMessage(0x51, metronomeVol);  // increase volume by one
      }
      else if (atString == 2){
        metronomeVol -= 5;
        handleMessage(0x51, metronomeVol);  // decrease volume by one
      }
      else if (atString == 3){
        metronomeVol = 30;
        handleMessage(0x51, metronomeVol);  // min volumechords
      }
      break;
    case 15:
      // Rec/Play 15
      if (atString == 0)
        handleMessage(0x52, 0);  // record
      else if (atString == 1)
        handleMessage(0x52, 1);  // stop
      else if (atString == 2)
        handleMessage(0x52, 2);  // play
      else if (atString == 3)
        handleMessage(0x52, 3);  // pause

      break;
    case 16:
      // Resonance , dynamics
      if (atString == 0)
        handleMessage(0x45, 0);  // constant velocity disabled , i.e have dynamics
      else if (atString == 1)
        handleMessage(0x45, 1);  // constant velocity enabled
      else if (atString == 2)
        handleMessage(0x1A, 1);  // resonate enabled
      else if (atString == 3)
        handleMessage(0x1A, 0);  // resonate disabled
      break;
    case 17:

      // Pitch change
      if (atString == 0) {
      } else if (atString == 1) {
        pitchChangeValue++;
        handleMessage(0x4D,
                               pitchChangeValue);  // incrment Pitch change by 1
      } else if (atString == 2) {
        pitchChangeValue--;
        handleMessage(0x4D,
                               pitchChangeValue);  // decrement Pitch change by 1
      } else if (atString == 3) {
        pitchChangeValue = 0;
        handleMessage(0x4D, pitchChangeValue);  // zero Pitch change
      }
      break;

    case 18:
      // settings 18
      if (atString == 1){
        handleMessage(0x54, 1);  // save settings
      }
      else if (atString == 2){
        handleMessage(0x54, 2);  // load settings
      }
      else if (atString == 3){
        handleMessage(0x54, 3);  // dump settings into terminal
      }
      break;
    case 19:
      // enable/disable strings
      if (atString == 0) {
        if (stringEnabledArray[0] == true)
          handleMessage(0x49, 0);  // disable string 1
        else
          handleMessage(0x49, 1);  // enable string 1
      } else if (atString == 1) {
        if (stringEnabledArray[1] == true)
          handleMessage(0x4A, 0);  // disable string 2
        else
          handleMessage(0x4A, 1);  // enable string 2

      } else if (atString == 2) {
        if (stringEnabledArray[2] == true)
          handleMessage(0x4B, 0);  // disable string 3
        else
          handleMessage(0x4B, 1);  // enable string 3
      } else if (atString == 3) {
        if (stringEnabledArray[3] == true)
          handleMessage(0x4C, 0);  // disable string 4
        else
          handleMessage(0x4C, 1);  // enable string 4
      }
      break;
    case 20:
      // MIDI Channels
      if (atString == 3)
        handleMessage(0x47, 0);  // midi channels 1,2
      else if (atString == 2)
        handleMessage(0x47, 1);  // midi channels 3,4
      else if (atString == 1)
        handleMessage(0x47, 2);  // midi channels 5,6
      else if (atString == 0)
        handleMessage(0x47, 3);  // midi channels 7,8
      break;
      case 21:
      // play midi files
      if (atString == 0)
        handleMessage(0x53, 1);  // play midi file 1
      else if (atString == 1)
        handleMessage(0x53, 2);  // play midi file 2
      else if (atString == 2)
        handleMessage(0x53, 3);  //play midi file 3
      else if (atString == 3)
        handleMessage(0x53, 4);  //play midi file 4
      break;

  }
}
#endif

#ifdef INST_UKULELE

void changeSetting(TAP *tap, uint8_t atString) {
  uint8_t settingFret = tap->fret / 2; //frets are in quarter tones so divide by 2 to get semitone for uke
  if (settingFret == 0)
    settingFret = 1; // to avoid fret 0 **** for some reason this doesnt work so skipped settingFret 1

  switch (settingFret) {
    case 2:
      // Tuning
      if (atString == 0)
        handleMessage(0x0E, 14);  // tuningIndex 0, A4 E4 C4 G4 Uke 1
      else if (atString == 1)
        handleMessage(0x0E, 16);  // tuningIndex 2, A4 E4 C4 G3  Uke 2
      else if (atString == 2)
        handleMessage(0x0E, 25);  // tuningIndex 3, G4 D3 A4 E3 Base guitar
      else if (atString == 3)
        handleMessage(0x0E, 17);  // tuningIndex 26, A4 D4 G3 C3 Banjo
      break;
    case 3:
      // Instrument
      if (atString == 0)
        handleMessage(0x16, 104);  // 104, sitar
      else if (atString == 1)
        handleMessage(0x16, 107);  // 107, koto
      else if (atString == 2)
        handleMessage(0x16, 24);  // 24, nylon guitar
      else if (atString == 3)
        handleMessage(0x16, 41);  // 41, viola
      break;
    case 4:
      // Tapping
      if (atString == 0)
        handleMessage(0x06, 1);  // tapping enabled
      else if (atString == 1){ //disable both
        handleMessage(0x06, 0);  // tapping disabled
        handleMessage(0x33, 0);  // tapping without strumming disabled
      }
      else if (atString == 2)
        handleMessage(0x33, 1);  // tapping without strumming enabled
      else if (atString == 3)
        handleMessage(0x33, 0);  // tapping without strumming disabled
      break;
    case 5:
      // Pedal/left hand
      if (atString == 0)
        handleMessage(0x14, 1);  // left hand enabled
      else if (atString == 1)
        handleMessage(0x14, 0);  // left hand disabled
      else if (atString == 2)
        handleMessage(0x01, 1);  // sustain pedal enabled
      else if (atString == 3)
        handleMessage(0x01, 0);  // sustain pedal disabled
      break;
    case 6:
      // chords/drums
      if (atString == 0)
        handleMessage(0x35, 1);  // chord mode enabled
      else if (atString == 1)
        handleMessage(0x35, 0);  // chord mode  disabled
      else if (atString == 2)
        handleMessage(0x32, 1);  // percussion enabled
      else if (atString == 3)
        handleMessage(0x32, 0);  // percussion enabled
      break;
    case 7:
      // Transpose
      if (atString == 0) {
        transposeValue += 7;
        handleMessage(0x19, transposeValue);  // Transpose to 5th
      } else if (atString == 1) {
        transposeValue++;
        handleMessage(0x19,
                               transposeValue);  // incrment transpose by 1
      } else if (atString == 2) {
        transposeValue--;
        handleMessage(0x19,
                               transposeValue);  // decrement transpose by 1
      } else if (atString == 3) {
        transposeValue = 0;
        handleMessage(0x19, transposeValue);  // zero transpose
      }
      break;
    case 8:
      // vibrato
      if (atString == 0) {
        handleMessage(0x0D, 10);  // set vibrato value to max
      } else if (atString == 1) {
        handleMessage(0x0D, vibratoValue++);  // increase vibrato
      } else if (atString == 2) {
        handleMessage(0x0D, vibratoValue--);  // decrease vibrato
      } else if (atString == 3) {
        handleMessage(0x0D, 0);  // zero vibrato
      }
      break;
    case 9:

      // Metronome 8
      if (atString == 0)
        handleMessage(0x4E, 1);  // metronome on
      else if (atString == 1)
        handleMessage(0x4E, 0);  // metrnome off
      else if (atString == 2)
      {
        tempoValue--;
        handleMessage(0x4F, tempoValue);  // decrease metrnome speed by one
      }
      else if (atString == 3){
        tempoValue++;
        handleMessage(0x4F, tempoValue);  // increase metrnome speed by one 
      }
      break;

    case 10:
      // Metronome beats 9
      if (atString == 0)
      {
        metronomeNumBeats = 2;
        handleMessage(0x50, metronomeNumBeats);  // 2 beats 
      }
      else if (atString == 1){
        metronomeNumBeats++;
        handleMessage(0x50, metronomeNumBeats);  // increase number of beats by one
      }
      else if (atString == 2){
        metronomeNumBeats--;
        handleMessage(0x50, metronomeNumBeats);  // decrease number of beats by one
      }
      else if (atString == 3){
        metronomeNumBeats = 4;
        handleMessage(0x50, metronomeNumBeats);  // 4 beats
      }
      break;
    case 11:

      if (atString == 0){
        metronomeVol = 127;
        handleMessage(0x51, metronomeVol);  // max volume
      }
      else if (atString == 1){
        metronomeVol += 5;
        handleMessage(0x51, metronomeVol);  // increase volume by one
      }
      else if (atString == 2){
        metronomeVol -= 5;
        handleMessage(0x51, metronomeVol);  // decrease volume by one
      }
      else if (atString == 3){
        metronomeVol = 30;
        handleMessage(0x51, metronomeVol);  // min volumechords
      }
      break;

    case 12:

      // strum sensitivity
      if (atString == 0)
        handleMessage(
            0x36, 4);  // max strum sensitivity ( derivative threshold)
      else if (atString == 1){
        strumSensitivityFactor = strumSensitivityFactor + 1;
        handleMessage(
            0x36, strumSensitivityFactor);  // increase strum sensitivity


      }
      else if (atString == 2){
        strumSensitivityFactor = strumSensitivityFactor - 1;
        handleMessage(
            0x36, strumSensitivityFactor);  // decrease strum sensitivity
      }

      else if (atString == 3)
        handleMessage(
            0x36, 1);  // min strum sensitivity ( derivative threshold)
      break;



  }
}
#endif

void resetTap(TAP * tap , uint8_t atString)
{
  tap[atString].prevFret = 0;
  tap[atString].debounceCount = 0;
  tap[atString].prevNote = 0;
  tap[atString].previousFretSample = 0;
  tap[atString].prevPitchBend = 0;
  tap[atString].tapNoteIsPlayed = false;
  tap[atString].tapPressed = false;

}

#ifndef ESP_PLATFORM
// ******************************************************************************************************
// ************** USB Callback Functions
// ****************************************************************
// ******************************************************************************************************
// The USB firmware stack will call the callback functions USBCBxxx() in
// response to certain USB related events.  For example, if the host PC is
// powering down, it will stop sending out Start of Frame (SOF) packets to your
// device.  In response to this, all USB devices are supposed to decrease their
// power consumption from the USB Vbus to <2.5mA each.  The USB module detects
// this condition (which according to the USB specifications is 3+ms of no bus
// activity/SOF packets) and then calls the USBCBSuspend() function.  You should
// modify these callback functions to take appropriate actions for each of these
// conditions.  For example, in the USBCBSuspend(), you may wish to add code
// that will decrease power consumption from Vbus to <2.5mA (such as by clock
// switching, turning off LEDs, putting the microcontroller to sleep, etc.).
// Then, in the USBCBWakeFromSuspend() function, you may then wish to add code
// that undoes the power saving things done in the USBCBSuspend() function.

// The USBCBSendResume() function is special, in that the USB stack will not
// automatically call this function.  This function is meant to be called from
// the application firmware instead.  See the additional comments near the
// function.

/******************************************************************************
 * Function:        void USBCBSuspend(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        Call back that is invoked when a USB suspend is detected
 *
 * Note:            None
 *****************************************************************************/
void USBCBSuspend(void)
{
  // Example power saving code.  Insert appropriate code here for the desired
  // application behavior.  If the microcontroller will be put to sleep, a
  // process similar to that shown below may be used:

  // ConfigureIOPinsForLowPower();
  // SaveStateOfAllInterruptEnableBits();
  // DisableAllInterruptEnableBits();
  // EnableOnlyTheInterruptsWhichWillBeUsedToWakeTheMicro();	//should enable
  // at least USBActivityIF as a wake source Sleep();
  // RestoreStateOfAllPreviouslySavedInterruptEnableBits();	//Preferrably,
  // this should be done in the USBCBWakeFromSuspend() function instead.
  // RestoreIOPinsToNormal();									//Preferrably, this
  // should be done in the USBCBWakeFromSuspend() function instead.

  // IMPORTANT NOTE: Do not clear the USBActivityIF (ACTVIF) bit here.  This bit
  // is cleared inside the usb_device.c file.  Clearing USBActivityIF here will
  // cause things to not work as intended.

#if defined(__C30__)
#if 0
    U1EIR = 0xFFFF;
    U1IR = 0xFFFF;
    U1OTGIR = 0xFFFF;
    IFS5bits.USB1IF = 0;
    IEC5bits.USB1IE = 1;
    U1OTGIEbits.ACTVIE = 1;
    U1OTGIRbits.ACTVIF = 1;
    Sleep();
#endif
#endif
}

/******************************************************************************
 * Function:        void _USB1Interrupt(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This function is called when the USB interrupt bit is set
 *					In this example the interrupt is only used when
 *the device goes to sleep when it receives a USB suspend command
 *
 * Note:            None
 *****************************************************************************/
#if 0

void __attribute__((interrupt)) _USB1Interrupt(void) {
#if !defined(self_powered)
    if (U1OTGIRbits.ACTVIF) {
        IEC5bits.USB1IE = 0;
        U1OTGIEbits.ACTVIE = 0;
        IFS5bits.USB1IF = 0;

        //USBClearInterruptFlag(USBActivityIFReg,USBActivityIFBitNum);
        USBClearInterruptFlag(USBIdleIFReg, USBIdleIFBitNum);
        //USBSuspendControl = 0;
    }
#endif
}
#endif

/******************************************************************************
 * Function:        void USBCBWakeFromSuspend(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The host may put USB peripheral devices in low power
 *					suspend mode (by "sending" 3+ms of idle).  Once
 *in suspend mode, the host may wake the device back up by sending non- idle
 *state signalling.
 *
 *					This call back is invoked when a wakeup from USB
 *suspend is detected.
 *
 * Note:            None
 *****************************************************************************/
void USBCBWakeFromSuspend(void)
{
  // If clock switching or other power savings measures were taken when
  // executing the USBCBSuspend() function, now would be a good time to
  // switch back to normal full power run mode conditions.  The host allows
  // a few milliseconds of wakeup time, after which the device must be
  // fully back to normal, and capable of receiving and processing USB
  // packets.  In order to do this, the USB module must receive proper
  // clocking (IE: 48MHz clock must be available to SIE for full speed USB
  // operation).
}

/********************************************************************
 * Function:        void USBCB_SOF_Handler(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The USB host sends out a SOF packet to full-speed
 *                  devices every 1 ms. This interrupt may be useful
 *                  for isochronous pipes. End designers should
 *                  implement callback routine as necessary.
 *
 * Note:            None
 *******************************************************************/
void USBCB_SOF_Handler(void)
{
  // No need to clear UIRbits.SOFIF to 0 here.
  // Callback caller is already doing that.

  if (ms_Counter != 0)
  {
    ms_Counter--;
  }
}

/*******************************************************************
 * Function:        void USBCBErrorHandler(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The purpose of this callback is mainly for
 *                  debugging during development. Check UEIR to see
 *                  which error causes the interrupt.
 *
 * Note:            None
 *******************************************************************/
void USBCBErrorHandler(void)
{
  // No need to clear UEIR to 0 here.
  // Callback caller is already doing that.

  // Typically, user firmware does not need to do anything special
  // if a USB error occurs.  For example, if the host sends an OUT
  // packet to your device, but the packet gets corrupted (ex:
  // because of a bad connection, or the user unplugs the
  // USB cable during the transmission) this will typically set
  // one or more USB error interrupt flags.  Nothing specific
  // needs to be done however, since the SIE will automatically
  // send a "NAK" packet to the host.  In response to this, the
  // host will normally retry to send the packet again, and no
  // data loss occurs.  The system will typically recover
  // automatically, without the need for application firmware
  // intervention.

  // Nevertheless, this callback function is provided, such as
  // for debugging purposes.
}

/*******************************************************************
 * Function:        void USBCBCheckOtherReq(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        When SETUP packets arrive from the host, some
 * 					firmware must process the request and
 *respond appropriately to fulfill the request.  Some of the SETUP packets will
 *be for standard USB "chapter 9" (as in, fulfilling chapter 9 of the official
 *USB specifications) requests, while others may be specific to the USB device
 *class that is being implemented.  For example, a HID class device needs to be
 *able to respond to "GET REPORT" type of requests.  This is not a standard USB
 *chapter 9 request, and therefore not handled by usb_device.c.  Instead this
 *request should be handled by class specific firmware, such as that contained
 *in usb_function_hid.c.
 *
 * Note:            None
 *******************************************************************/
void USBCBCheckOtherReq(void) {} // end

/*******************************************************************
 * Function:        void USBCBStdSetDscHandler(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The USBCBStdSetDscHandler() callback function is
 *					called when a SETUP, bRequest: SET_DESCRIPTOR
 *request arrives.  Typically SET_DESCRIPTOR requests are not used in most
 *applications, and it is optional to support this type of request.
 *
 * Note:            None
 *******************************************************************/
void USBCBStdSetDscHandler(void)
{
  // Must claim session ownership if supporting this request
} // end

/*******************************************************************
 * Function:        void USBCBInitEP(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This function is called when the device becomes
 *                  initialized, which occurs after the host sends a
 * 					SET_CONFIGURATION (wValue not = 0) request.
 *This callback function should initialize the endpoints for the device's usage
 *according to the current configuration.
 *
 * Note:            None
 *******************************************************************/
void USBCBInitEP(void)
{
  // enable the HID endpoint
  USBEnableEndpoint(MIDI_EP, USB_OUT_ENABLED | USB_IN_ENABLED |
                                 USB_HANDSHAKE_ENABLED | USB_DISALLOW_SETUP);

  // Re-arm the OUT endpoint for the next packet
  USBRxHandle = USBRxOnePacket(MIDI_EP, (uint8_t *)&ReceivedDataBuffer, 4);
}

/********************************************************************
 * Function:        void USBCBSendResume(void)
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        The USB specifications allow some types of USB
 * 					peripheral devices to wake up a host PC
 (such
 *					as if it is in a low power suspend to RAM
 state).
 *					This can be a very useful feature in
 some *					USB applications, such as an Infrared
 remote *					control	receiver.  If a user
 presses the "power"
 *					button on a remote control, it is nice that
 the
 *					IR receiver can detect this signalling, and
 then *					send a USB "command" to the PC to wake
 up.
 *
 *					The USBCBSendResume() "callback" function
 is used
 *					to send this special USB signalling which
 wakes
 *					up the PC.  This function may be called
 by *					application firmware to wake up the PC.
 This *					function should only be called when:
 *
 *					1.  The USB driver used on the host PC
 supports *						the remote wakeup
 capability.
 *					2.  The USB configuration descriptor
 indicates
 *						the device is remote wakeup capable
 in the *						bmAttributes field. *
 3.  The USB host PC is currently sleeping,
 *						and has previously sent your device
 a SET
 *						FEATURE setup packet which "armed"
 the *						remote wakeup capability.
 *
 *					This callback should send a RESUME signal
 that
 *                  has the period of 1-15ms.
 *
 * Note:            Interrupt vs. Polling
 *                  -Primary clock
 *                  -Secondary clock ***** MAKE NOTES ABOUT THIS *******
 *                   > Can switch to primary first by calling
 USBCBWakeFromSuspend()

 *                  The modifiable section in this routine should be changed
 *                  to meet the application needs. Current implementation
 *                  temporary blocks other functions from executing for a
 *                  period of 1-13 ms depending on the core frequency.
 *
 *                  According to USB 2.0 specification section 7.1.7.7,
 *                  "The remote wakeup device must hold the resume signaling
 *                  for at lest 1 ms but for no more than 15 ms."
 *                  The idea here is to use a delay counter loop, using a
 *                  common value that would work over a wide range of core
 *                  frequencies.
 *                  That value selected is 1800. See table below:
 *                  ==========================================================
 *                  Core Freq(MHz)      MIP         RESUME Signal Period (ms)
 *                  ==========================================================
 *                      48              12          1.05
 *                       4              1           12.6
 *                  ==========================================================
 *                  * These timing could be incorrect when using code
 *                    optimization or extended instruction mode,
 *                    or when having other interrupts enabled.
 *                    Make sure to verify using the MPLAB SIM's Stopwatch
 *                    and verify the actual signal on an oscilloscope.
 *******************************************************************/
void USBCBSendResume(void)
{
  static uint16_t delay_count;

  USBResumeControl = 1; // Start RESUME signaling

  delay_count = 1800U; // Set RESUME line for 1-13 ms
  do
  {
    delay_count--;
  } while (delay_count);
  USBResumeControl = 0;
}

/*******************************************************************
 * Function:        bool USER_USB_CALLBACK_EVENT_HANDLER(
 *                        USB_EVENT event, void *pdata, uint16_t size)
 *
 * PreCondition:    None
 *
 * Input:           USB_EVENT event - the type of event
 *                  void *pdata - pointer to the event data
 *                  uint16_t size - size of the event data
 *
 * Output:          None
 *
 * Side Effects:    None
 *
 * Overview:        This function is called from the USB stack to
 *                  notify a user application that a USB event
 *                  occured.  This callback is in interrupt context
 *                  when the USB_INTERRUPT option is selected.
 *
 * Note:            None
 *******************************************************************/

bool USER_USB_CALLBACK_EVENT_HANDLER(USB_EVENT event, void *pdata, uint16_t size)
{
  switch (event)
  {
  case EVENT_CONFIGURED:
    USBCBInitEP();
    break;
  case EVENT_SET_DESCRIPTOR:
    USBCBStdSetDscHandler();
    break;
  case EVENT_EP0_REQUEST:
    USBCBCheckOtherReq();
    break;
  case EVENT_SOF:
    USBCB_SOF_Handler();
    break;
  case EVENT_SUSPEND:
    USBCBSuspend();
    break;
  case EVENT_RESUME:
    USBCBWakeFromSuspend();
    break;
  case EVENT_BUS_ERROR:
    USBCBErrorHandler();
    break;
  case EVENT_TRANSFER:
    Nop();
    break;
  default:
    break;
  }
  return true;
}

#endif // !ESP_PLATFORM