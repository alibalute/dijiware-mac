#include "etar.h"
#include "util.h"
#include "midi.h"
#include "adc.h"
#include "pic-midi.h"
#include "usbmidi.h"
#include "blemidi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "settings.h"
#include "midi_player.h"
#include "spiffs.h"
#include "metronome.h"

#include "esp_log.h"
#include "mbedtls/base64.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"

static const char *TAG = "util";

/* Strum calibrate touches ADC/SPI; must not run from BLE/USB callbacks (races eTar on xADCSemaphore). */
static atomic_bool g_strum_calib_pending = ATOMIC_VAR_INIT(false);

extern void setUSBCableConnected(bool connected);
extern bool isBLEConnected(void);
extern bool isUSBConnected(void);
extern bool bluetoothConnected;
extern uint8_t tuningIndex;

cJSON *settings;

char* midiFile = "/spiffs/1.mid";  // Define the pointer, initialized to default midi file
bool midiPause = false;
bool midiStop = false;



//these are already defined in midi.h, but is not clear why compiler doesnt see them
//pitch systems  , this should be matching the list of freq table on the app side.
#define EDO_24_PITCH             0
#define JAZAYERI_PITCH           1
#define EDO_51_TURKISH_PITCH     2
#define EDO_51_SHARIF_PITCH      3
#define CUSTOM_PITCH             4  //array for this item, will never exist on the PIC side. if this is selected, the pitch system will be set on 0, and pitch bends will be calculated on the app side.

bool instrumentSelected = false;

const sustainTable instrumentSustainTable={
    {
        0,    //Acoustic Grand Piano
        0,    //Bright Grand Piano
        0,    //Electric Grand Piano
        0,    //Honky Tonk Grand Piano
        0,    //Electric Piano 1
        0,    //Electric Piano 2
        0,    //Harpsichord
        0,    //Clavi
        0,    //Celesta
        0,    //Glockenspiel
        1,    //Music Box
        0,    //Vibraphone
        0,    //Marimba
        0,    //Xylophone
        0,    //Tubular Bells
        0,    //Dulcimer
        1,    //Drawbar Organ
        1,    //Percussive Organ
        1,    //Rock Organ
        1,    //Church Organ
        1,    //Reed Organ
        1,    //Accordion
        1,    //Harmonica
        1,    //Tango Accordion
        0,    //Acoustic Guitar (nylon)
        0,    //Acoustic Guitar (steel)
        0,    //Electric Guitar (jazz)
        0,    //Electric Guitar (clean)
        0,    //Electric Guitar (muted)
        1,    //Overdriven Guitar
        1,    //Distortion Guitar
        0,    //Guitar Harmonics
        0,    //Acoustic Bass
        0,    //Electric Bass (finger)
        0,    //Electric Bass (pick)
        0,    //Fretless Bass
        0,    //Slap Bass 1
        0,    //Slap Bass 2
        0,    //Synth Bass 1
        0,    //Synth Bass 2
        1,    //Violin
        1,    //Viola-41
        1,    //Cello
        1,    //Contrabass
        1,    //Tremolo Strings
        0,    //Pizzicato Strings
        0,    //Orchestral Harp
        0,    //Timpani
        1,    //String Ensembles 1
        1,    //String Ensembles 2
        1,    //Synth Strings 1
        1,    //Synth Strings 2
        1,    //Choir Aahs
        1,    //Voice Oohs
        1,    //Synth Voice
        0,    //Orchestra Hit
        1,    //Trumpet
        1,    //Trombone
        1,    //Tuba
        1,    //Muted Trumpet
        1,    //French Horn
        1,    //Brass Section
        1,    //Synth Brass 1
        1,    //Synth Brass 2
        1,    //Soprano Sax
        1,    //Alto Sax
        1,    //Tenor Sax
        1,    //Baritone Sax
        1,    //Oboe
        1,    //English Horn
        1,    //Bassoon
        1,    //Clarinet
        1,    //Piccolo
        1,    //Flute
        1,    //Recorder
        1,    //Pan Flute
        1,    //Blown Bottle
        1,    //Shakuhachi
        1,    //Whistle
        1,    //Ocarina
        1,    //Square Lead (Lead 1)
        1,    //Saw Lead (Lead)
        1,    //Calliope Lead (Lead 3)
        1,    //Chiff Lead (Lead 4)
        1,    //Charang Lead (Lead 5)
        1,    //Voice Lead (Lead 6)
        1,    //Fifths Lead (Lead 7)
        1,    //Bass + Lead (Lead 8)
        1,    //New Age (Pad 1)
        1,    //Warm Pad (Pad 2)
        1,    //Polysynth (Pad 3)
        1,    //Choir (Pad 4)
        0,    //Bowed (Pad 5)
        0,    //Metallic (Pad 6)
        1,    //Halo (Pad 7)
        1,    //Sweep (Pad 8)
        1,    //Rain (FX 1)
        1,    //Sound Track (FX 2)
        0,    //Crystal (FX 3)
        0,    //Atmosphere (FX 4)
        0,    //Brightness (FX 5)
        1,    //Goblins (FX 6)
        1,    //Echoes (FX 7)
        1,    //Sci-Fi (FX 8)
        0,    //Sitar-104
        0,    //Banjo
        0,    //Shamisen
        0,    //Koto-107
        0,    //Kalimba
        1,    //Bag Pipe
        1,    //Fiddle
        1,    //Shanai
        0,    //Tinkle Bell
        0,    //Agogo
        0,    //Pitched Percussion
        0,    //Woodblock
        0,    //Taiko Drum
        0,    //Melodic Tom
        0,    //Synth Drum
        0,    //Reverse Cymbal
        0,    //Guitar Fret Noise
        0,    //Breath Noise
        1,    //Seashore
        0,    //Bird Tweet
        0,    //Telephone Ring
        1,    //Helicopter
        1,    //Applause
        0,    //Gunshot
        0
    }
};


//uint8_t tmpSettings[32];

#ifdef INST_UKULELE
uint8_t righthandBaseTable[4] = {A4,E4,C4,G4};
uint8_t lefthandBaseTable[4] = {G4,C4,E4,A4};
#endif

#ifdef INST_SETAR
uint8_t righthandBaseTable[4] = {C4,G3,C4,C3};
uint8_t lefthandBaseTable[4] = {C3,C4,G3,C4};
#endif

//Joystick
extern float joystickMidValue;
extern  float joystickDeflection;

extern bool percussionInstrument;
extern uint8_t vibratoValue;
extern bool tapWithoutStrumEnabled;
extern const short pitchAdjustment[4][24];
extern bool chordEnabled;
extern bool stringEnabledArray[4];



/**
*	Calibrate Functions                                                    *
\******************************************************************************/
void util_schedule_strum_calibrate(void) {
  atomic_store_explicit(&g_strum_calib_pending, true, memory_order_release);
}

void util_run_pending_strum_calibrate_from_etar(void) {
  if (!atomic_exchange_explicit(&g_strum_calib_pending, false, memory_order_acq_rel))
    return;
  strumCalibrate();
}

void strumCalibrate(void) {
  ESP_LOGD(TAG, "Calibrating Strums");
  for (int i = 0; i < NUM_STRINGS; i++) {
    potMidValue[i] = (float)readAndAverageStrum(
        i); // read the first four ADCs as strum pots at rest
    }
}

void stringCalibrate(int i, bool recalibrate)
{
    float maxValue;

    //find the average of max value of the string
    maxValue = 0;
    for (int j = 0; j < 10; j++){
        maxValue += (float)readAndAverageString(i);
    }
    maxValue /= 10;
    //ESP_LOGD(TAG, " string  %d max value = %f" , i , maxValue );

    // recalibrate means initial calibration  has already been done, looking for
    // drifts
    if (recalibrate)
    {
        if (fabs(fretBarMaxValue[i] - maxValue) < 500.0) //if the difference is bigger than this, it means that a fret has been pressed
        {
            // if the max value drifts it will replace it . the margin of
            // differene should be small so it wontdo anything if a fret is
            // pressed
            fretBarMaxValue[i] = maxValue;
        }
    }
    else
        fretBarMaxValue[i] = maxValue; // initial calibration

  #ifdef membrane
      lastCalibVal = fretBarMaxValue[3]; //Set last calib val for calibration
      minStep = fretBarMaxValue[3] / 40; //Set minimum step for calibration
  #endif
}

void joystickCalibrate(void)
{
  ESP_LOGD(TAG, "Calibrating Joystick");
  joystickMidValue = (float)readAccelerometer();
}

/**
*	Message Handler Functions                                              *
\******************************************************************************/

/** Apply a loaded settings JSON object (instrument, tuning, etc.) via handleMessage. */
static void apply_loaded_settings(cJSON *loaded_settings) {
    if (loaded_settings == NULL) return;
    /* Apply MIDI channel first so instrument (0x16) and other messages use the correct channel */
    { int v = get_numerical_setting(loaded_settings, "midiChannel"); handleMessage(0x47, (uint8_t)v); }
    /* Instrument: missing/non-numeric JSON used to become 0 → GM program 0 (piano) */
    {
      cJSON *inst = cJSON_GetObjectItemCaseSensitive(loaded_settings, "instrument");
      if (inst != NULL && cJSON_IsNumber(inst)) {
        synthInstrument = (uint8_t)(int)inst->valuedouble;
      } else {
        synthInstrument = 24;
      }
      handleMessage(0x16, synthInstrument);
    }
    chordEnabled = (bool)get_numerical_setting(loaded_settings, "chords");
    handleMessage(0x35, chordEnabled);
    tuningIndex = (uint8_t)get_numerical_setting(loaded_settings, "tuning");
    handleMessage(0x0E, tuningIndex);
    { int tap = get_numerical_setting(loaded_settings, "tapping"); handleMessage(0x06, tap ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "transpose"); handleMessage(0x19, (int8_t)v); }
    { int v = get_numerical_setting(loaded_settings, "vibrato"); handleMessage(0x0D, (uint8_t)v); }
    { int v = get_numerical_setting(loaded_settings, "leftHand"); handleMessage(0x14, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "quarterTones"); handleMessage(0x15, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "staccato"); handleMessage(0x09, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "sustain"); handleMessage(0x01, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "resonate"); handleMessage(0x1A, v ? 1 : 0); }
    {
        cJSON *svj = cJSON_GetObjectItemCaseSensitive(loaded_settings, "sympatheticVolume");
        int sv = 50;
        if (svj != NULL && cJSON_IsNumber(svj)) {
            sv = (int)svj->valuedouble;
            if (sv < 0) {
                sv = 0;
            }
            if (sv > 100) {
                sv = 100;
            }
        }
        handleMessage(0x56, (uint8_t)sv);
    }
    { int v = get_numerical_setting(loaded_settings, "percussion"); handleMessage(0x32, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "tapWithoutStrum"); handleMessage(0x33, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "pitchSystem"); handleMessage(0x34, (uint8_t)v); }
    { int v = get_numerical_setting(loaded_settings, "constantVelocity"); handleMessage(0x45, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "effects"); handleMessage(0x1B, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "pitchChange");
        fineTuningPB = (int16_t)(v * 100);
    }
    { int v = get_numerical_setting(loaded_settings, "string1"); handleMessage(0x49, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "string2"); handleMessage(0x4A, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "string3"); handleMessage(0x4B, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "string4"); handleMessage(0x4C, v ? 1 : 0); }
    { int v = get_numerical_setting(loaded_settings, "metronomeBpm"); handleMessage(0x4F, (uint8_t)v); }
    { int v = get_numerical_setting(loaded_settings, "metronomeBeats"); handleMessage(0x50, (uint8_t)v); }
    { int v = get_numerical_setting(loaded_settings, "metronomeVol"); handleMessage(0x51, (uint8_t)v); }
}

/** Same fields as save (0x54 data==1); used when settings.json is missing. */
static cJSON *build_runtime_settings_json(void)
{
    cJSON *out = cJSON_CreateObject();
    if (!out) {
        return NULL;
    }
    cJSON_AddNumberToObject(out, "instrument", synthInstrument);
    cJSON_AddNumberToObject(out, "chords", (int)chordEnabled);
    cJSON_AddNumberToObject(out, "tuning", tuningIndex);
    cJSON_AddNumberToObject(out, "tapping", (int)hammerOnEnabled);
    cJSON_AddNumberToObject(out, "transpose", transposeValue);
    cJSON_AddNumberToObject(out, "vibrato", vibratoValue);
    cJSON_AddNumberToObject(out, "leftHand", (int)leftHandEnabled);
    cJSON_AddNumberToObject(out, "quarterTones", (int)quarterNotesEnabled);
    cJSON_AddNumberToObject(out, "staccato", (int)staccatoEnable);
    cJSON_AddNumberToObject(out, "sustain", (int)sustainEnabled);
    cJSON_AddNumberToObject(out, "resonate", (int)resonateEnabled);
    cJSON_AddNumberToObject(out, "sympatheticVolume", (int)sympatheticVelocityPercent);
    cJSON_AddNumberToObject(out, "percussion", (int)percussionInstrument);
    cJSON_AddNumberToObject(out, "tapWithoutStrum", (int)tapWithoutStrumEnabled);
    cJSON_AddNumberToObject(out, "pitchSystem", pitchSystem);
    cJSON_AddNumberToObject(out, "constantVelocity", (int)constantVelocityEnable);
    cJSON_AddNumberToObject(out, "effects", (int)effectsEnabled);
    cJSON_AddNumberToObject(out, "pitchChange", pitchChangeValue);
    {
        int ch = (semitoneChannel == MIDI_CHANNEL_1) ? 0 : (semitoneChannel == MIDI_CHANNEL_3) ? 1 : (semitoneChannel == MIDI_CHANNEL_5) ? 2 : (semitoneChannel == MIDI_CHANNEL_7) ? 3 : 0;
        cJSON_AddNumberToObject(out, "midiChannel", ch);
    }
    cJSON_AddNumberToObject(out, "string1", (int)stringEnabledArray[0]);
    cJSON_AddNumberToObject(out, "string2", (int)stringEnabledArray[1]);
    cJSON_AddNumberToObject(out, "string3", (int)stringEnabledArray[2]);
    cJSON_AddNumberToObject(out, "string4", (int)stringEnabledArray[3]);
    cJSON_AddNumberToObject(out, "metronomeBpm", get_metronome_bpm());
    cJSON_AddNumberToObject(out, "metronomeBeats", get_metronome_numBeats());
    cJSON_AddNumberToObject(out, "metronomeVol", get_metronome_volume());
    return out;
}

void send_ble_settings_snapshot(void)
{
    if (!isBLEConnected()) {
        return;
    }
    init_spiffs();
    cJSON *j = load_settings_from_flash("/spiffs/settings.json");
    if (j == NULL) {
        j = build_runtime_settings_json();
    }
    if (j == NULL) {
        ESP_LOGW(TAG, "BLE settings snapshot: no JSON");
        return;
    }
    char *plain = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (plain == NULL) {
        return;
    }
    size_t inlen = strlen(plain);
    size_t b64_cap = 4 * ((inlen + 2) / 3) + 8;
    uint8_t *b64 = (uint8_t *)malloc(b64_cap);
    if (b64 == NULL) {
        cJSON_free(plain);
        return;
    }
    size_t olen = 0;
    int br = mbedtls_base64_encode(b64, b64_cap, &olen, (unsigned char *)plain, inlen);
    cJSON_free(plain);
    if (br != 0) {
        free(b64);
        ESP_LOGW(TAG, "BLE settings snapshot: base64 failed %d", br);
        return;
    }
    /* SysEx: F0 7D 53 53 + base64(JSON) + F7 (educational ID 0x7D, 'SS' = settings sync) */
    size_t syx_len = 4 + olen + 1;
    uint8_t *syx = (uint8_t *)malloc(syx_len);
    if (syx == NULL) {
        free(b64);
        return;
    }
    syx[0] = 0xF0;
    syx[1] = 0x7D;
    syx[2] = 0x53;
    syx[3] = 0x53;
    memcpy(syx + 4, b64, olen);
    syx[4 + olen] = 0xF7;
    free(b64);
    blemidi_send_message(0, syx, syx_len);
    free(syx);
    ESP_LOGI(TAG, "BLE settings snapshot sent (%u bytes sysex)", (unsigned)syx_len);
}

void load_settings_at_boot(void) {
    init_spiffs();
    cJSON *loaded = load_settings_from_flash("/spiffs/settings.json");
    if (loaded != NULL) {
        apply_loaded_settings(loaded);
        cJSON_Delete(loaded);
        ESP_LOGI(TAG, "Settings loaded at boot");
    } else {
        ESP_LOGD(TAG, "No settings file at boot, using defaults");
    }
}

/* Can be called from buttonsTask, BLE callback, etarTask (changeSetting), and main (load_settings).
 * Touches many globals; if intermittent crashes persist, consider protecting with a mutex. */
void handleMessage(int8_t code, int8_t data){
    int n;
    myRatio r;

    //sustain change
    if (code == 0x01) {
        if (data == 0x01){
            pluckedInstrument = true;
            sustainEnabled = true;
            midiTx(0xB, 0xB0+semitoneChannel, 0x40, 100);
            midiTx(0xB, 0xB0+quartertoneChannel, 0x40, 100);
            #ifdef uart_en
                inputToUART(0xB0+semitoneChannel, 0x40, 100);
                inputToUART(0xB0+quartertoneChannel, 0x40, 100);
            #endif
        } else if (data == 0x00){
            pluckedInstrument = false;
            sustainEnabled = false;
            midiTx(0xB, 0xB0+semitoneChannel, 0x40, 0);
            midiTx(0xB, 0xB0+quartertoneChannel, 0x40, 0);
            #ifdef uart_en
                inputToUART(0xB0+semitoneChannel, 0x40, 0);
                inputToUART(0xB0+quartertoneChannel, 0x40, 0);
            #endif
        }
    } else if (code == 0x02) { 			//calibrate button (deferred to eTar — ADC mutex)
        util_schedule_strum_calibrate();
        //stringCalibrate();
    } else if (code == 0x03) { 			//setting strum start threshold
        strumStartThreshold = data;
    } else if (code == 0x04) { 			//setting strum return threshold
        strumReturnThreshold = data;
    } else if (code == 0x05) { 			//setting the time allowed between strums (0–255 ms when sent as single byte)
        requiredTimeStepsBetweenStrums = (uint16_t)(uint8_t)data;
    } else if (code == 0x06) {				//enables and disables tapping (hammer on)
        if (data == 0x01){
            hammerOnEnabled = true;
        } else if (data == 0x00){
            hammerOnEnabled = false;
        }
    } else if (code == 0x07) { 			//setting fret noise margin
        fretNoiseMargin = data;
    } else if (code == 0x08) { 			//set tapping debounce number
        tappingDebounceNum = data;
    } else if (code == 0x09) {
        if(data==1){
            staccatoEnable=true;
        }
        else if(data==0){
            staccatoEnable=false;
        }
    } else if (code == 0x0A) { 			//set tapping volume (midi velocity of tapping)
        //tappingVolume = data;
    } else if (code == 0x0B) { 			//set auto calibration time out
        strumCalibTimeout = data;
    } else if (code == 0x0D) { 			//set vibrato 
       vibratoValue = data;   //vibrato value on the app side (used to set  vibrato sample time)
        if (vibratoValue == 0)
            vibratoEnabled = false;  //not only makes vibrato value zero, it stops sending vibrato pitch bends too
        else
            vibratoEnabled = true;

//       midiTx(0xC,0xC0, 0x49, (data*12) );    //Attack
//       midiTx(0xC,0xC1, 0x49, (data*12) );    //Attack

    } else if (code == 0x0E) { 			//tuning button
        tuningIndex = data;						//changes the base table to a different tuningIndex 

        for (n = 0; n < 4; n++) {
            workingBaseTable[n] = returnBaseTable(tuningIndex,n) + transposeValue*2;
            righthandBaseTable[n] = returnBaseTable(tuningIndex,n) + transposeValue*2;
            lefthandBaseTable[3-n] = returnBaseTable(tuningIndex,n) + transposeValue*2;
        }
    } else if (code == 0x0F) { 			//string 1 tune
        if (leftHandEnabled == false) {
            workingBaseTable[0] = data + transposeValue*2;
        } else {
            workingBaseTable[3] = data + transposeValue*2;
        }
    } else if (code == 0x11) { 			//string 2 tune
        if (leftHandEnabled == false) {
            workingBaseTable[1] = data + transposeValue*2;
        } else {
            workingBaseTable[2] = data + transposeValue*2;
        }
    } else if (code == 0x12) { 			//string 3 tune
        if (leftHandEnabled == false) {
            workingBaseTable[2] = data + transposeValue*2;
        } else {
            workingBaseTable[1] = data + transposeValue*2;
        }
    } else if (code == 0x13) { 			//string 4 tune
        if (leftHandEnabled == false) {
            workingBaseTable[3] = data + transposeValue*2;
        } else {
            workingBaseTable[0] = data + transposeValue*2;
        }
    } else if (code == 0x14) { 			//switch to left hand mode

   /*     if (data == 0x01){
            leftHandEnabled = true;
        } else if (data == 0x00){
            leftHandEnabled = false;
        }*/

        //leftHandEnabled = !leftHandEnabled;

        if(data == 0x01){//enable left hand
            for(n=0; n<4; n++){
                workingBaseTable[n]=lefthandBaseTable[n] ;
            }
        }
        else if (data == 0x00){//enable right hand
            for(n=0; n<4; n++){
                workingBaseTable[n]=righthandBaseTable[n];
            }
        }

//        for (n = 0; n < 4; n++){
//            tempBaseTable[n] = workingBaseTable[3-n];
//        }
//
//        for (n = 0; n < 4; n++){
//            workingBaseTable[n] = tempBaseTable[n];
//        }
    } else if (code == 0x15) { 			//enable/disable quarter notes
        if (data == 0x01){
            quarterNotesEnabled = true;
        } else if (data == 0x00){
            quarterNotesEnabled = false;
        }
    } else if (code == 0x16) { //set the on board synth instrument 
        instrumentSelected=true;
        percussionInstrument = false; // if perviously percussion was enabled, disable it

        synthInstrument= (uint8_t)data;
        inputToUART(0xC0+semitoneChannel, 0x00, synthInstrument);
        inputToUART(0xC0+quartertoneChannel, 0x00,synthInstrument);

        midiTx(0xC, 0xC0+semitoneChannel,synthInstrument, 0);
        midiTx(0xC, 0xC0+quartertoneChannel,synthInstrument, 0);
        #ifdef uart_en
            /* Use unsigned for table index so instrument IDs 128+ work when data is passed as int8_t */
            { uint8_t inst = (uint8_t)data;
            if( ((instrumentSustainTable.table[(inst/8)])&((1)<<(inst%8))) > 0 ){
                sustainEnabled = false;
                pluckedInstrument = false;
                //send the sustain message (0)
                midiTx(0xB, 0xB0+semitoneChannel, 0x40, 0);
                midiTx(0xB, 0xB0+quartertoneChannel, 0x40, 0);
                #ifdef uart_en
                    inputToUART(0xB0+semitoneChannel, 0x40, 0);
                    inputToUART(0xB0+quartertoneChannel, 0x40, 0);
                #endif

            }else{
                sustainEnabled = true;
                pluckedInstrument = true;
                //send the sustain message (1)
                midiTx(0xB, 0xB0+semitoneChannel, 0x40, 100);
                midiTx(0xB, 0xB0+quartertoneChannel, 0x40, 100);
                #ifdef uart_en
                    inputToUART(0xB0+semitoneChannel, 0x40, 100);
                    inputToUART(0xB0+quartertoneChannel, 0x40, 100);
                #endif

            }

            }
        #endif
    } else if (code == 0x17) { 			//set the volume of the onboard synth
        #ifdef uart_en
            curVol=data;
//            for (n = 0; n < 16; n++) {
//                inputToUART(0xB0+n, 0x07, (data));
//            }

                inputToUART(0xB0+semitoneChannel, 0x07, (data));
                inputToUART(0xB0+quartertoneChannel, 0x07, (data));
                midiTx(0xB,0xB0+semitoneChannel, 0x07, (data));
                midiTx(0xB,0xB0+quartertoneChannel, 0x07, (data));
        #endif
    } else if (code == 0x18) { 			//set the reverb value of the onboard synth
        #ifdef uart_en
            if (effectsEnabled == 1) {
                for (n = 0; n < 4; n++) {
                    inputToUART(0xB0+n, 0x5B, (data));
                }
            }
        #endif
    } else if (code == 0x19) { 			//transpose value
        if(connectionTarget==TARGET_ETAR){
            transposeValue = data;
        }else if(connectionTarget==TARGET_WINDOWS_ETAR){
            transposeValue = data;
        }else{
            transposeValue = data;
        }
        //ESP_LOGD(TAG, "transposeValue= %d  ,  transposevlue=%x" ,  transposeValue , transposeValue);
        //when transpose is occured ,  the working basetable should be updated
       for (n = 0;n < 4; n++) {
            workingBaseTable[n] = returnBaseTable(tuningIndex , n) + transposeValue*2; //transpose value is doubled because base table is based on 24 notes
            righthandBaseTable[n] = returnBaseTable(tuningIndex,n) + transposeValue*2;
            lefthandBaseTable[3-n] = returnBaseTable(tuningIndex,n) + transposeValue*2;

        }

    } else if (code == 0x1A){
        if (data == 0x00){
            resonateEnabled = false;
        } else if (data == 0x01){
            resonateEnabled = true;
        }
    } else if (code == 0x56) {
        /* Sympathetic velocity as percent of main strum (0–100); DijiApp BLE / settings JSON */
        uint8_t p = data;
        if (p > 100) {
            p = 100;
        }
        sympatheticVelocityPercent = p;
    }else if (code == 0x1B) { 			//effects toggle
        if (data == 0x00) {
            effectsEnabled = false;
        } else if (data == 0x01) {
            effectsEnabled = true;
        }

//    } else if (code == 0x1C) {  //app request writing current setting values to flash
//        tmpSettings[0] = 0x01;
//        tmpSettings[1] = strumStartThreshold;
//        tmpSettings[2] = strumReturnThreshold;
//        tmpSettings[3] = requiredTimeStepsBetweenStrums;
//        tmpSettings[4] = fretNoiseMargin;
//        tmpSettings[5] = strumCalibTimeout;
//        //tmpSettings[6] = tappingVolume;
//        tmpSettings[7] = tappingDebounceNum;
//        tmpSettings[8] = vibratoValue;
//
//        for (n = 9; n < 32; n++){
//            tmpSettings[n] = 0xFF;
//        }
//
//        writeSettingsToFlash(0x7000, tmpSettings);
//    } else if (code == 0x1D){  //app requests reading settings from flash and sending it to the app
//        //bluetoothConnected = true;
//        if (readFromFlash(0x7000) == 0x01){
//            tmpSettings[0] = readFromFlash(0x7001);
//            tmpSettings[1] = readFromFlash(0x7002);
//            tmpSettings[2] = readFromFlash(0x7003);
//            tmpSettings[3] = readFromFlash(0x7004);
//            tmpSettings[4] = readFromFlash(0x7005);
//            tmpSettings[5] = readFromFlash(0x7006);
//            tmpSettings[6] = readFromFlash(0x7007);
////            tmpSettings[7] = readFromFlash(0x7008);
////
////            for (n = 0; n < 8; n++){
////                inputToUART(0xF5, tmpSettings[n], n);
////            }
//        } else {
//            inputToUART(0xF6, 0, 0);
//        }
    } else if (code == 0x1E) {
        //String 1 Max Value
        r.f = fretBarMaxValue[0];
        inputToUART(0x30, r.b[0], r.b[1]);
        inputToUART(0x31, r.b[2], r.b[3]);
    } else if (code == 0x1F) {
        //String 2 Max Value
        r.f = fretBarMaxValue[1];
        inputToUART(0x32, r.b[0], r.b[1]);
        inputToUART(0x33, r.b[2], r.b[3]);
    } else if (code == 0x20) {
        //String 3 Max Value
        r.f = fretBarMaxValue[2];
        inputToUART(0x34, r.b[0], r.b[1]);
        inputToUART(0x35, r.b[2], r.b[3]);
    } else if (code == 0x21) {
        //String 4 Max Value
        r.f = fretBarMaxValue[3];
        inputToUART(0x36, r.b[0], r.b[1]);
        inputToUART(0x37, r.b[2], r.b[3]);
    } else if (code == 0x22) {
        //Strum 1 Rest Value
        r.f = potMidValue[0];
        inputToUART(0x38, r.b[0], r.b[1]);
        inputToUART(0x39, r.b[2], r.b[3]);
    } else if (code == 0x23) {
        //Strum 2 Rest Value
        r.f = potMidValue[1];
        inputToUART(0x3A, r.b[0], r.b[1]);
        inputToUART(0x3B, r.b[2], r.b[3]);
    } else if (code == 0x24) {
        //Strum 3 Rest Value
        r.f = potMidValue[2];
        inputToUART(0x3C, r.b[0], r.b[1]);
        inputToUART(0x3D, r.b[2], r.b[3]);
    } else if (code == 0x25) {
        //Strum 4 Rest Value
        r.f = potMidValue[3];
        inputToUART(0x3E, r.b[0], r.b[1]);
        inputToUART(0x3F, r.b[2], r.b[3]);
    } else if (code == 0x26){
        //Accelerometer ADC
        int u;
        uint32_t accelVal = 0;
        for (u = 0; u < 10; u++){
            accelVal += readADC(8);
        }
        accelVal = accelVal / 10;
        inputToUART(0xF7, (accelVal & 0xFF), ((accelVal & 0xFF00) >> 8));
    } else if (code == 0x27){
        //VCC Test
        int u;
        uint32_t vccVal = 0;
        for (u = 0; u < 10; u++){
            vccVal += readADC(0);
        }
        vccVal = vccVal / 10;
        inputToUART(0xF8, (vccVal & 0xFF), ((vccVal & 0xFF00) >> 8));
    } else if (code == 0x28){
        //Note Sending Test
        int q;
        for (q = 0; q < 21; q++){
            inputToUART(0x90, 50+q, 127);
            inputToUART(0xE0, 0x00, 0x40);
            delay();
            delay();
            delay();
            delay();
            delay();
            inputToUART(0x80, 50+q, 127);
            inputToUART(0xE0, 0x00, 0x40);
            delay();
            delay();
            delay();
            delay();
            delay();
        }
    } else if (code == 0x29){
        //Respond to Communication Test
        inputToUART(0xF4, 0x00, 0x00);
    } else if (code == 0x30){
        bluetoothConnected = false; //turn the speaker on
        // enableSpeaker();
    } else if (code == 0x31){
        bluetoothConnected = true;   //turn the speakr off
        // disableSpeaker();
    } else if (code == 0x32){
        if (data == 0x00){
            percussionInstrument = false;
        } else if (data == 0x01){
            percussionInstrument = true;
        }
    } else if (code == 0x33){
        if (data == 0x00){
            tapWithoutStrumEnabled = false;
        } else if (data == 0x01){
            tapWithoutStrumEnabled = true;
        }
    }
    else if (code == 0x34){
        if (data == CUSTOM_PITCH)
            pitchSystem = EDO_24_PITCH;  //custom pitch uses the custom table on the app side
        else
            pitchSystem = data;

    }else if (code == 0x35){
        if (data == 0x00){
            chordEnabled = false;
        } else if (data == 0x01){
            chordEnabled = true;
        }
    } 

    
    ///// Advanced Settings /////
    else if(code==0x36){

        strumSensitivityFactor = data;
        if (strumSensitivityFactor <= 0)
            strumSensitivityFactor = 1;
        if (strumSensitivityFactor > 3)
            strumSensitivityFactor = 3;
    }
    else if(code==0x37){
        strumStartThreshold = data;
    }
    else if(code==0x38){
        strumReturnThreshold=data;
    }
    else if(code==0x39){
        tappingDebounceNum=data;
    }
    else if(code==0x40){
        if(data==TARGET_ETAR){//No connection just eTar
            connectionTarget=TARGET_ETAR;
            PBCenter=ETAR_SEMITONE_CENTER;
            PBQuarterToneOffset=ETAR_QUARTERTONE_OFFSET;
        }
        else if(data==TARGET_WINDOWS){//Generic windows connection
            PBCenter=WIN_SEMITONE_CENTER;//OSX
            PBQuarterToneOffset=WIN_QUARTERTONE_OFFSET;
        }
        else if(data==TARGET_WINDOWS_ETAR){//Windows Etar app
            PBCenter=WIN_ETARAPP_SEMITONE_CENTER;
            PBQuarterToneOffset=WIN_ETARAPP_QUARTERTONE_OFFSET;
        }
        else if(data==TARGET_ANDROID){//Android (Midi Voyager)
            PBCenter=ANDROID_SEMITONE_CENTER;
            PBQuarterToneOffset=ANDROID_QUARTERTONE_OFFSET;
        }
        else if(data==TARGET_OSX){//Mac
            PBCenter=OSX_SEMITONE_CENTER;
            PBQuarterToneOffset=OSX_QUARTERTONE_OFFSET;
        }
        else if(data==TARGET_IOS_GB){//IOS/GarageBand
            PBCenter=IOS_GB_SEMITONE_CENTER;
            PBQuarterToneOffset=IOS_GB_QUARTERTONE_OFFSET;
        }
        else if(data==TARGET_IOS_BS16I){//IOS/BS16i
            PBCenter=IOS_BS16I_SEMITONE_CENTER;
            PBQuarterToneOffset=IOS_BS16I_QUARTERTONE_OFFSET;
        }
    }
    else if(code==0x41){
        if(data==0x01){
            noteOnReleaseEnabled = true;
        }
        else if(data==0x00){
            noteOnReleaseEnabled=false;
        }
    }
    else if(code==0x42){
        derivativeReturnThreshold=(-1)*((int)data);
    }
    else if(code==0x43){
            PBCenterOffset=data;
    }
    else if(code==0x44){
        velocityMultiplier = data;
    }
    else if(code==0x45){
        if(data == 0x00){  
            constantVelocityEnable=false; //dynamics on, means constant velocity disabled
        }else{
            constVelocityValue=127;
            constantVelocityEnable=true;  //dynamics off, means constant velocity enabled
        }
    }else if(code==0x46){
        if(data==0){
            strumSampleInterval=1;
        }else{
            strumSampleInterval=data;
        }
    }
    else if(code==0x47){
        if(data==0){
                semitoneChannel=MIDI_CHANNEL_1;
                quartertoneChannel=MIDI_CHANNEL_2;
        }
        else if(data==1){
                semitoneChannel=MIDI_CHANNEL_3;
                quartertoneChannel=MIDI_CHANNEL_4;
        }
        else if(data==2){
                semitoneChannel=MIDI_CHANNEL_5;
                quartertoneChannel=MIDI_CHANNEL_6;
        }
        else if(data==3){
                semitoneChannel=MIDI_CHANNEL_7;
                quartertoneChannel=MIDI_CHANNEL_8;
        }
        else if(data==4){
                semitoneChannel=MIDI_CHANNEL_9;
                quartertoneChannel=MIDI_CHANNEL_10;
        }
        else if(data==5){
                semitoneChannel=MIDI_CHANNEL_11;
                quartertoneChannel=MIDI_CHANNEL_12;
        }
        else if(data==6){
                semitoneChannel=MIDI_CHANNEL_13;
                quartertoneChannel=MIDI_CHANNEL_14;
        }
        else{
                semitoneChannel=MIDI_CHANNEL_15;
                quartertoneChannel=MIDI_CHANNEL_16;
        }
        channelChange=true;
    }
    else if(code==0x48){
       //legatoDelay=data;
    }
    
    else if(code==0x49){
        if(data==0x01){
            stringEnabledArray[0] = true;
        }
        else if(data==0x00){
            stringEnabledArray[0] = false;
        }
    }
    else if(code==0x4A){
        if(data==0x01){
            stringEnabledArray[1] = true;
        }
        else if(data==0x00){
           stringEnabledArray[1] = false;
        }
    }
    else if(code==0x4B){
        if(data==0x01){
            stringEnabledArray[2] = true;
        }
        else if(data==0x00){
            stringEnabledArray[2] = false;
        }
    }
    else if(code==0x4C){
        if(data==0x01){
            stringEnabledArray[3] = true;
        }
        else if(data==0x00){
           stringEnabledArray[3] = false;
        }
    }
    else if(code==0x4D){  //this doesnt exist on eTar app yet
         uint16_t adjustedPitchBend;

        fineTuningPB =  data * 100 ; //fineTuning pitch bend
        //ESP_LOGD(TAG, "fineTuningPB:\n%d", fineTuningPB);


        adjustedPitchBend = PBCenter + PBCenterOffset + fineTuningPB;
        // (PBCenter+PBCenterOffset)+
        //             (2*(pitchAdjustment[pitchSystem][ (note24 % 24) ])) + fineTuningPB;


        inputToUART(0xE0,  (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
        inputToUART (0x90 , 0x3C , 0x7F); //play a note to see the change
       

    }
    //metronome on/off
    else if(code==0x4E)  //this doesnt exist on eTar app yet
    {
        if (data == 1){
            start_metronome_timer();
        }
        else if (data == 0){
            stop_metronome_timer();
        }


    }
    //metronome speed
    else if(code==0x4F)  //this doesnt exist on eTar app yet
    {
        change_metronome_speed(data);
    }

    //metronome number of beats
    else if(code==0x50)  //this doesnt exist on eTar app yet
    {
        change_metronome_numBeats(data);  //increase by one
    }

    //metronome volume
    else if(code==0x51)  //this doesnt exist on eTar app yet
    {

        change_metronome_volume(data);     //chnage metronome volume 

    }

    //rec/play
    else if(code==0x52)  //this doesnt exist on eTar app yet
    {
        if (data == 0){ //rec
        }
        else if (data == 1){//stop
             midiStop = true;   
        }

        else if(data == 2){//play

            if(midiPause){ //pressing play after pause
                midiPause = false; //let the sequencer task continue to play the paused midi file
                if (midiStop){ //if pause and stop both
                    midiStop = false;
                    play_midi_file();
                }

            }
            else{ 
                midiStop = false;
                play_midi_file();
            }
            
            

        }
        else if(data == 3){//pause
            midiPause = true;
        }
    }

    //play midi files
    else if(code==0x53)  //this doesnt exist on eTar app yet
    {
        char fileName[50];  // Adjust the array size as needed

        // Use sprintf to concatenate strings
        sprintf(fileName, "/spiffs/%d.mid", data); //data is the index of midi file in the spiffs e.g. /spiffs/1.mid
        setMidiFile(fileName);

    }
    //save/load settings
    else if(code==0x54)  //this doesnt exist on eTar app yet
    {

        if (data == 1){  //save settings
            ESP_LOGI(TAG, "Save settings triggered");
            // Initialize SPIFFS
            init_spiffs();

            settings = build_runtime_settings_json();
            if (settings == NULL) {
                ESP_LOGE(TAG, "Save settings: alloc failed");
                return;
            }

            // Save settings to flash
            if (save_settings_to_flash("/spiffs/settings.json", settings)) {
                char *written = cJSON_PrintUnformatted(settings);
                ESP_LOGI(TAG, "Write to flash successful. Written: %s", written ? written : "");
                if (written) {
                    cJSON_free(written);
                }
            } else {
                ESP_LOGE(TAG, "Write to flash failed");
            }

            // Free cJSON object
            cJSON_Delete(settings);

           
        }



        else if(data == 2){ // factory reset: load factory.json and apply
            init_spiffs();
            cJSON *loaded_settings = load_settings_from_flash("/spiffs/factory.json");
            if (loaded_settings != NULL) {
                char *loaded_str = cJSON_Print(loaded_settings);
                apply_loaded_settings(loaded_settings);
                ESP_LOGI(TAG, "Factory reset applied:\n%s", loaded_str);
                cJSON_free(loaded_str);
                cJSON_Delete(loaded_settings);
            }
        }
        else if(data == 3){  // dump settings file to log (view without loading)
            init_spiffs();
            cJSON *loaded_settings = load_settings_from_flash("/spiffs/settings.json");
            if (loaded_settings != NULL) {
                char *loaded_str = cJSON_Print(loaded_settings);
                ESP_LOGI(TAG, "Settings file contents:\n%s", loaded_str);
                cJSON_free(loaded_str);
                cJSON_Delete(loaded_settings);
            } else {
                ESP_LOGW(TAG, "No settings file or failed to read");
            }
        }        
    }
    else if(code==0x55) { //this doesnt exist on eTar app yet
        reset(); //turn the etar off

    }




        #ifdef membrane
    if (code == 0x20) {				//Fret Calib Mode ON
        /* mLED_1_Off(); */
        /* mLED_3_On(); */
        autoStrumCalibration = false;
        fretCalibMode = true;
    } else if (code == 0x21) { 			//Fret Calib Mode OFF
        /* mLED_1_On(); */
        /* mLED_3_Off(); */
        autoStrumCalibration = true;
        fretCalibMode = false;
        calibFretNum = 0;
    } else if (code == 0x22) { 			//Report Ratios from memory
        for (n = 0; n < 164; n++){
            r.b[n % 4] = readFromFlash(0x7E00 + n);
            if (((n + 1) % 4) == 0){
                inputToUART(0x30, r.b[0], r.b[1]);
                inputToUART(0x31, r.b[2], r.b[3]);
            }
        }
    } else if (code == 0x23) { 			//Report Ratios from array
        for (n = 0; n < 41; n++){
            r.f = fretRatio[n];
            inputToUART(0x30, r.b[0], r.b[1]);
            inputToUART(0x31, r.b[2], r.b[3]);
        }
    } else if (code == 0x24) { 			//Report Min Step
        r.f = minStep;
        inputToUART(0x30, r.b[0], r.b[1]);
        inputToUART(0x31, r.b[2], r.b[3]);
    }
    #endif
}



void handleUsbMessage(uint8_t usbCode, uint8_t usbData){
    int n;      //iterator, set to zero when used
    myRatio r;  //temperary var to hold membrane data for USB processing
    ESP_LOGI(TAG, "USB Msg: %02x %02x", usbCode, usbData);

    if (usbCode == 0x80) { 					//sustain change
        if (usbData == 0x01){					//sustain on
            sustainEnabled = true;
            for (n = 0; n < 4; n++){
                //midiTx(0xB, 0xB0+n, 0x40, 0x7F);

                #ifdef uart_en
                    inputToUART(0xB0+n, 0x40, 0x7F);
                #endif
            }
        } else if (usbData == 0x00){			//sustain off
            sustainEnabled = false;
            for (n = 0; n < 4; n++){
                //midiTx(0xB, 0xB0+n, 0x40, 0x00);

                #ifdef uart_en
                    inputToUART(0xB0+n, 0x40, 0x00);
                #endif
            }
        }
    } else if (usbCode == 0x81) { 			//calibrate button (deferred to eTar)
        util_schedule_strum_calibrate();
        //stringCalibrate();
    } else if (usbCode == 0x82) { 			//setting strum start threshold
        strumStartThreshold = usbData ;
    } else if (usbCode == 0x83) { 			//setting strum return threshold
        strumReturnThreshold = usbData ;
    } else if (usbCode == 0x84) { 			//setting the time allowed between strums
        requiredTimeStepsBetweenStrums = (unsigned long)usbData;
    } else if (usbCode == 0x85) { 			//enables and disables tapping
        if (usbData == 0x01){
            tappingEnabled = true;
        } else if (usbData == 0x00){
            tappingEnabled = false;
        }
    } else if (usbCode == 0x86) { //setting fret noise margin
        fretNoiseMargin = usbData;
    } else if (usbCode == 0x87) { //set tapping debounce number
        tappingDebounceNum = usbData;
    } else if (usbCode == 0x88) { //set strumming debounce number
        //strummingDebounceNum = usbData;
    } else if (usbCode == 0x89) { //set tapping volume (midi velocity of tapping)
        //tappingVolume = usbData;
    } else if (usbCode == 0x8A){
        if (usbData == 0x00){
            resonateEnabled = false;
        } else if (usbData == 0x01){
            resonateEnabled = true;
        }
    } else if (usbCode == 0x8B) {
        uint8_t p = usbData;
        if (p > 100) {
            p = 100;
        }
        sympatheticVelocityPercent = p;
    }else if (usbCode == 0x90) { //set auto calibration time out
        strumCalibTimeout = usbData;
    } else if (usbCode == 0x91) { //enables and disables vibrato
        if (usbData == 0x01){
            vibratoEnabled = true;
        } else if (usbData == 0x00){
            vibratoEnabled = false;
        }
    } else if (usbCode == 0x92) {
        vibratoValue = usbData; //vibrato value on the app side
        if (vibratoValue == 0)
            vibratoEnabled = false;  //not only makes vibrato value zero, it stops sending vibrato pitch bends too
        else
            vibratoEnabled = true;
    } else if (usbCode == 0x93) { //tuning button
        tuningIndex = usbData;	//changes the base table to a different tuningIndex 

        for (n = 0; n < 4; n++) {
            workingBaseTable[n] = returnBaseTable(tuningIndex,n);
        }
    } else if (usbCode == 0x95) { //string 1 tune
        if (leftHandEnabled == false) {
            workingBaseTable[0] = usbData;
        } else {
            workingBaseTable[3] = usbData;
        }
    } else if (usbCode == 0x96) { //string 2 tune
        if (leftHandEnabled == false) {
            workingBaseTable[1] = usbData;
        } else {
            workingBaseTable[2] = usbData;
        }
    } else if (usbCode == 0x97) { //string 3 tune
        if (leftHandEnabled == false) {
            workingBaseTable[2] = usbData;
        } else {
            workingBaseTable[1] = usbData;
        }
    } else if (usbCode == 0x98) { //string 4 tune
        if (leftHandEnabled == false) {
            workingBaseTable[3] = usbData;
        } else {
            workingBaseTable[0] = usbData;
        }
    } else if (usbCode == 0x99) { //switch to left hand mode
        uint8_t tempBaseTable[4];
        if (usbData == 0x01){
            leftHandEnabled = true;
        } else if (usbData == 0x00){
            leftHandEnabled = false;
        }

        for (n = 0; n < 4; n++){
            tempBaseTable[n] = workingBaseTable[3-n];
        }

        for (n = 0; n < 4; n++){
            workingBaseTable[n] = tempBaseTable[n];
        }
    } else if (usbCode == 0x9A) { //enable/disable quarter notes
        if (usbData == 0x01){
            quarterNotesEnabled = true;
        } else if (usbData == 0x00){
            quarterNotesEnabled = false;
        }
    } else if (usbCode == 0x9B) { //set the on board synth instrument
        #ifdef uart_en
            for (n = 0; n < 4; n++){
                inputToUART(0xC0+n, 0x00,usbData);
            }
        #endif
    } else if (usbCode == 0x9C) { //set the volume of the onboard synth
        #ifdef uart_en
//            for (n = 0; n < 4; n++) {
//                //usbData is *12 so it goes from 1-120 instead of 1-10 (volume slider)
//                inputToUART(0xB0+n, 0x07, (usbData*12));
//            }
        midiTx(0xB,0xB0+semitoneChannel, 0x07, (usbData*12));
        midiTx(0xB,0xB0+quartertoneChannel, 0x07, (usbData*12));
        inputToUART(0xB0+semitoneChannel, 0x07, (usbData*12));
        inputToUART(0xB0+quartertoneChannel, 0x07, (usbData*12));
        #endif
    } else if (usbCode == 0x9D) { //set the reverb value of the onboard synth
        #ifdef uart_en
            if (effectsEnabled == true){
                for (n = 0; n < 4; n++) {
                    inputToUART(0xB0+n, 0x5B, (usbData));
                }
            }
        #endif
    } else if (usbCode == 0x9E) { //transpose value
        transposeValue = usbData +12;
    } else if (usbCode == 0x9F) { //effects toggle
        if (usbData == 0x00) {
            effectsEnabled = false;
            for (n = 0; n < 4; n++) {
                inputToUART(0xB0+n, 0x5B, 0x00);
            }
        } else if (usbData == 0x01) {
            effectsEnabled = true;
        }
//    } else if (usbCode == 0xA0) { //app requests writing setting values into flash
//        tmpSettings[0] = 0x01;
//        tmpSettings[1] = strumStartThreshold;
//        tmpSettings[2] = strumReturnThreshold;
//        tmpSettings[3] = requiredTimeStepsBetweenStrums;
//        tmpSettings[4] = fretNoiseMargin;
//        tmpSettings[5] = strumCalibTimeout;
//        //tmpSettings[6] = tappingVolume;
//        tmpSettings[7] = tappingDebounceNum;
//        tmpSettings[8] = vibratoValue;
//
//        for (n = 9; n < 32; n++){
//            tmpSettings[n] = 0xFF;
//        }
//
//        writeSettingsToFlash(0x7000, tmpSettings);
//    } else if (usbCode == 0xA1){     // app requests to read settings from the flash and send it to app
//        if (readFromFlash(0x7000) == 0x01){
//            tmpSettings[0] = readFromFlash(0x7001);
//            tmpSettings[1] = readFromFlash(0x7002);
//            tmpSettings[2] = readFromFlash(0x7003);
//            tmpSettings[3] = readFromFlash(0x7004);
//            tmpSettings[4] = readFromFlash(0x7005);
//            tmpSettings[5] = readFromFlash(0x7006);
//            tmpSettings[6] = readFromFlash(0x7007);
//            tmpSettings[7] = readFromFlash(0x7008);
//            for (n = 0; n < 8; n++){
//                midiTx(0x3, 0xF1, tmpSettings[n], 0);
//            }
//        } else {
//            midiTx(0x3, 0xF2, 0, 0);
//        }
    } else if (usbCode == 0xA2) {
        //String 1 Max Value
        r.f = fretBarMaxValue[0];
        midiTx(0x3, 0xE0, r.b[0], r.b[1]);
        midiTx(0x3, 0xE1, r.b[2], r.b[3]);
    } else if (usbCode == 0xA3) {
        //String 2 Max Value
        r.f = fretBarMaxValue[1];
        midiTx(0x3, 0xE2, r.b[0], r.b[1]);
        midiTx(0x3, 0xE3, r.b[2], r.b[3]);
    } else if (usbCode == 0xA4) {
        //String 3 Max Value
        r.f = fretBarMaxValue[2];
        midiTx(0x3, 0xE4, r.b[0], r.b[1]);
        midiTx(0x3, 0xE5, r.b[2], r.b[3]);
    } else if (usbCode == 0xA5) {
        //String 4 Max Value
        r.f = fretBarMaxValue[3];
        midiTx(0x3, 0xE6, r.b[0], r.b[1]);
        midiTx(0x3, 0xE7, r.b[2], r.b[3]);
    } else if (usbCode == 0xA6) {
        //Strum 1 Rest Value
        r.f = potMidValue[0];
        midiTx(0x3, 0xE8, r.b[0], r.b[1]);
        midiTx(0x3, 0xE9, r.b[2], r.b[3]);
    } else if (usbCode == 0xA7) {
        //Strum 2 Rest Value
        r.f = potMidValue[1];
        midiTx(0x3, 0xEA, r.b[0], r.b[1]);
        midiTx(0x3, 0xEB, r.b[2], r.b[3]);
    } else if (usbCode == 0xA8) {
        //Strum 3 Rest Value
        r.f = potMidValue[2];
        midiTx(0x3, 0xEC, r.b[0], r.b[1]);
        midiTx(0x3, 0xED, r.b[2], r.b[3]);
    } else if (usbCode == 0xA9) {
        //Strum 4 Rest Value
        r.f = potMidValue[3];
        midiTx(0x3, 0xEE, r.b[0], r.b[1]);
        midiTx(0x3, 0xEF, r.b[2], r.b[3]);
    } else if (usbCode == 0xAA){
        //Accelerometer ADC
        int u;
        uint32_t accelVal = 0;
        for (u = 0; u < 10; u++){
            accelVal += readADC(8);
        }
        accelVal = accelVal / 10;
        midiTx(0x3, 0xF3, ((accelVal * 2) & 0xFF), (((accelVal * 2) >> 8) & 0xFF));
    } else if (usbCode == 0xAB){
        //VCC Test
        int u;
        uint32_t vccVal = 0;
        for (u = 0; u < 10; u++){
            vccVal += readADC(0);
        }
        vccVal = vccVal / 10;
        midiTx(0x3, 0xF7, ((vccVal * 2) & 0xFF), (((vccVal * 2) >> 8) & 0xFF));
    } else if (usbCode == 0xAC){
        //Note Sending Test
        int q;
        for (q = 0; q < 21; q++){
            midiTx(0xE, 0xE0, 0x00, 0x40);
            midiTx(0x9, 0x90, 50+q, 127);

            delay();
            delay();
            delay();
            delay();
            delay();
            midiTx(0xE, 0xE0, 0x00, 0x40);
            midiTx(0x8, 0x80, 50+q, 127);

            delay();
            delay();
            delay();
            delay();
            delay();
        }
    } else if (usbCode == 0xAD){
        //Respond to Communication Test
        inputToUART(0xF8, 0x00, 0x00);
    } else if (usbCode == 0xAE){
        //enable/disable usb, related to usb device selection and disconnect button ( in winmm.cpp , midiDisconnect() )
       if (usbData == 0x00)
            setUSBCableConnected(false);
       if (usbData == 0x01)
            setUSBCableConnected(true);

    //percussion instrument got sellected on the app side
    } else if (usbCode == 0xAF){

        if (usbData == 0x01){					//percussion on
            percussionInstrument = true;

        } else if (usbData == 0x00)			//percussion off
            percussionInstrument = false;
    

    } else if (usbCode == 0xB0){

        if (usbData == 0x01){					//percussion on
            tapWithoutStrumEnabled = true;

        } else if (usbData == 0x00)			//percussion off
            tapWithoutStrumEnabled = false;
    }else if (usbCode == 0xB1){                        //setting pitch system

        if (usbData == CUSTOM_PITCH)
            pitchSystem = EDO_24_PITCH;  //custom pitch uses the custom table on the app side
        else
            pitchSystem = usbData;
    }
    else if (usbCode == 0xB2){
        if (usbData == 0x00){
            chordEnabled = false;
        } else if (usbData == 0x01){
            chordEnabled = true;
        }
    }

    #ifdef membrane
    if (usbCode == 0x40) {
        /* mLED_1_Off(); */
        /* mLED_3_On(); */
        autoStrumCalibration = false;
        fretCalibMode = true;
    } else if (usbCode == 0x41) {
        /* mLED_1_On(); */
        /* mLED_3_Off(); */
        autoStrumCalibration = true;
        fretCalibMode = false;
        calibFretNum = 0;
    }
    #endif
}



/**
*	Misc Functions                                                         *
\******************************************************************************/
void delay(void)
{
    // int delayCount;

    // delayCount = 1000;
    // do
    // {
    //     delayCount--;
    // }while(delayCount);
    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms
}
void delayFor(int inputDelay){

    //     int delayCount;

    // delayCount = inputDelay;
    // do
    // {
    //     delayCount--;
    // }while(delayCount);
    vTaskDelay(pdMS_TO_TICKS(inputDelay)); // given delay in ms
}


int absoluteDiff(int x, int y) {
	if (x >= y){
		return x - y;
	} else {
		return y - x;
	}
}

float fAbsoluteDiff(float x, float y) {
	if (x >= y){
		return x - y;
	} else {
		return y - x;
	}
}



void handleMidiMessage(uint8_t midi_status, uint8_t *msg, size_t len, size_t continued_sysex_pos)
{
  (void)continued_sysex_pos;
  // ESP_LOGD(TAG,"Status %d [%d,%d]",midi_status,msg[0],msg[1]);
  if (midi_status>=0x80 && midi_status<=0xbf) {
    if (len==2) {
      inputToUART(midi_status, msg[0],msg[1]);
    }
  } else {

    if( midi_status==0xfe){//0xfe is an unused status byte that we use it here to send some control messages to eTar
        /* BLE apps may send 0xFE (Active Sensing) with no payload.
         * Only treat 0xFE as our private control frame when 2 bytes are present. */
        if (len >= 2) {
          handleMessage(msg[0], msg[1]);
        } else {
          ESP_LOGD(TAG, "Ignoring 0xFE with short payload (len=%u)", (unsigned)len);
        }
    }
    else{
        ESP_LOGD(TAG, "Status %d unhandled", midi_status);
    }
  }
}

#define UART_MIDI_SEND_RETRIES 4  /* retry on semaphore contention (no-sound boot fix) */

/* Boot debug: counts during boot MIDI burst (reset at burst start, read at intro done) */
static uint32_t s_boot_uart_fail_count = 0;
static uint32_t s_boot_uart_retry_count = 0;  /* sends that needed >1 attempt */

void boot_uart_stats_reset(void) {
  s_boot_uart_fail_count = 0;
  s_boot_uart_retry_count = 0;
}

void boot_uart_stats_log(const char *tag) {
  if (s_boot_uart_fail_count > 0 || s_boot_uart_retry_count > 0) {
    ESP_LOGW(tag, "boot UART stats: fails=%lu retries=%lu", (unsigned long)s_boot_uart_fail_count, (unsigned long)s_boot_uart_retry_count);
  }
}

uint32_t boot_uart_get_fail_count(void) { return s_boot_uart_fail_count; }
uint32_t boot_uart_get_retry_count(void) { return s_boot_uart_retry_count; }

void inputToUART(uint8_t byte1, uint8_t byte2, uint8_t byte3) {
  uint8_t bytes[3];
  bytes[0] = byte1;
  bytes[1] = byte2;
  bytes[2] = byte3;

  int ret;
  int last_ret = 0;
  for (int r = 0; r < UART_MIDI_SEND_RETRIES; r++) {
    ret = uartmidi_send_message(0, bytes, 3);
    last_ret = ret;
    if (ret == 0) {
      if (r > 0) {
        s_boot_uart_retry_count++;
        ESP_LOGW(TAG, "uartmidi_send_message OK after %d retries (contention)", r + 1);
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  if (ret != 0) {
    s_boot_uart_fail_count++;
    /* -1 = invalid port, -2 = UART not enabled, positive = ESP_FAIL (e.g. semaphore timeout) */
    const char *cause = (last_ret == -2) ? "UART not enabled" :
                        (last_ret == -1) ? "invalid port" :
                        (last_ret > 0)   ? "semaphore timeout" : "other";
    ESP_LOGE(TAG, "uartmidi_send_message failed after %d retries, ret=%d (%s): %02x %02x %02x",
             UART_MIDI_SEND_RETRIES, last_ret, cause, byte1, byte2, byte3);
  }

  if (isBLEConnected()) blemidi_send_message(0, bytes, 3);

  // while(PIR1bits.TXIF == 0){};
  // WriteUSART(byte1);
  // while(PIR1bits.TXIF == 0){};
  // WriteUSART(byte2);
  // while(PIR1bits.TXIF == 0){};
  // WriteUSART(byte3);
}

/** Send a 2-byte MIDI message (e.g. Program Change). Avoids sending a spurious
 *  third byte that some synths can interpret as note-on. */
void inputToUART2(uint8_t byte1, uint8_t byte2) {
  uint8_t bytes[2] = { byte1, byte2 };
  int ret;
  for (int r = 0; r < UART_MIDI_SEND_RETRIES; r++) {
    ret = uartmidi_send_message(0, bytes, 2);
    if (ret == 0) break;
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  if (ret != 0) {
    ESP_LOGE(TAG, "uartmidi_send_message (2-byte) failed after %d retries, ret=%d: %02x %02x",
             UART_MIDI_SEND_RETRIES, ret, byte1, byte2);
  }
  if (isBLEConnected()) {
    blemidi_send_message(0, bytes, 2);
  }
}


void setMidiFile(const char* newString) {
    // Release the previous memory if any
    //if(midiFile != NULL ){
        //free(midiFile);
   //}
    
    // Allocate memory for the new string
    midiFile = malloc(strlen(newString) + 1);

    // Copy the new string into the allocated memory
    strcpy(midiFile, newString);
}