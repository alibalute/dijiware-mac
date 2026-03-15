#include "main.h"
#include "midi.h"
#include "util.h"
// #include "adc.h"
#include "usb.h"
#include "pic-midi.h"
#include "usbmidi.h"
#include "esp_log.h"
#include "interfaces.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "pic-midi";





#ifndef membrane
//(1/41) = 0.024390243902439
//0.024390243902439 / 2 = 0.01219512195122
//starting at 1, subtracted 0.024390243902439 forty times sequentially
//then offset each value by 0.01219512195122
//0.99 instead of 1 at position 0 for increased accuracy
//see 'Fretboard Rev. C Ratios' on google drive for more info



#ifdef INST_UKULELE
    #ifdef HALF_CIRCUIT_MEMBRANE
    const  float fretRatio[13] = {
        0.9615384615,
        0.8846153846,
        0.8076923077,
        0.7307692308,
        0.6538461538,
        0.5769230769,
        0.5000000000,
        0.4230769231,
        0.3461538462,
        0.2692307692,
        0.1923076923,
        0.1153846154,
        0.0
    };
    #endif

    #ifdef FULL_CIRCUIT_MEMBRANE
    const  float fretRatio[13] = {
        0.5281846427,
        0.5002219263,
        0.4718153573,
        0.4438526409,
        0.403018198,
        0.3586329339,
        0.3106968486,
        0.2698624057,
        0.2219263205,
        0.166888593,
        0.1043053706,
        0.04749223258,
        0.0
    };
    #endif

#endif

#ifdef INST_SETAR
const  float fretRatio[41] = {
    //0.99,
    0.987804877,
    0.963414634147561,
    0.939024390245122,
    0.914634146342683,
    0.890243902440244,
    0.865853658537805,
    0.841463414635366,
    0.817073170732927,
    0.792682926830488,
    0.768292682928049,
    0.74390243902561,
    0.719512195123171,
    0.695121951220732,
    0.670731707318293,
    0.646341463415854,
    0.621951219513415,
    0.597560975610976,
    0.573170731708537,
    0.548780487806098,
    0.524390243903658,
    0.50000000000122,
    0.47560975609878,
    0.451219512196342,
    0.426829268293902,
    0.402439024391463,
    0.378048780489024,
    0.353658536586585,
    0.329268292684146,
    0.304878048781707,
    0.280487804879268,
    0.256097560976829,
    0.23170731707439,
    0.207317073171951,
    0.182926829269512,
    0.158536585367073,
    0.134146341464634,
    0.109756097562195,
    0.085365853659756,
    0.060975609757317,
    0.036585365854878,
    0.0
};
#endif

const  short pitchAdjustment[4][24] = {  //the first index is pitch system, the second index is note offset in 24-base
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},//EDO_24
    {0, 0,-346,-91,120,0,-256,25,386,0,-80,0,-501,-93,80,0,-379,-11,260,0,-112,171,330,0},//jazayeri
    {0,1662,541,1275,155,1816,696,1430,309,1971,850,1584,464,1198,77,1739,618,1352,232,1893,773,1507,386,1121},//turkish 51edo
    {0,-120,-241,-361,482,-603,-723,120,0,-120,-241,-361,-482,-603,241,120,0,-120,-241,-361,-482,-603,-723,-843},//sharif
};

//array of instrument ID's for the midi synth chip
const  uint8_t instrumentArray[28] = {0, 1, 2, 3, 9, 14, 24/*acoustic nylon guitar*/, 41/*viola*/, 46, 47, 52, 53, 54, 56, 57, 60, 71, 75, 76, 77, 86, 98, 104/*sitar*/, 106, 107/*koto*/, 116, 117, 127};

//array of transpose values
const  uint8_t transposeArray[26] = {0, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

//Preset Tuning Table
const  uint8_t baseTable[40][4] = {
    { C4,  G3,  C4,  C3},
    { C4,  G3,  C4,  F3},
    { C4,  G3,  D4,  D3},
    { C4,  F3,  C4,  C3},
    { C4,  G3,  A3,  D3},
    { C4,  F3,  AS4, AS3},  //tork sib
    { C4,  G3,  AS4,  F3},
    { C4,  F3,  DS4,  DS3},  //tork mib
    { C4,  G3,  Ak3,  D3},
    { C4,  F3,  G3,  C3},          //homayoon la koron
    { C4,  F3,  Gk3,  DS3},  //homayoon sol koron
    { C4,  C4,  G3,  G3},  //tanboor
    { C4,  C4,  F3,  F3},   //tanboor
    { D4,  A3,  E3,  B2},   //oud
    { A4,  E4,  C4,  G4},   //ukulele-1
    { A4,  E4,  D4,  A3},  //pipa
    { A4,  E4,  C4,  G3},  //ukelele-2
    { A4,  D4,  G3,  C3},  //4-banjo
    { E4,  B3,  G3,  D3},  //4-banjo
    { D4,  B3,  G3,  C3},  //5-banjo
    { C4,  C4,  G3,  C3},   //5-banjo
    { D4,  A3,  FS3,  D3},  //5-banjo
    { E4,  CS4,  A3,  DS3},//5-banjo
    { D4,  B3,  G3,  D3},  //5-banjo
    { E4,  A3,  D3,  G2}, //mandoline
    { G4,  D3,  A4,  E3},//bass guitar
    { E5,  A4,  D4,  G3},//violin
    { A4,  D4,  G3,  C3},//viola
    { A3,  D3,  G2,  G2},//cello
    { A4,  D4,  G3,  G3},//bozuk
    { A4,  D4,  E3,  E3},//asik
    { A4,  D4,  FS3,  FS3},//misket
    { D4,  G3,  A2,  A2},//baglama
    { F4,  D4,  A3,  A3},//mustizat
    { G4,  A3,  A2,  A2},//abdal
    { A4,  D4,  D3,  D3},//zurna
    { E4,  A3,  A2,  A2},//husseini
    { F4,  A3,  A2,  A2},//acemasiran
    { FS4,  A3,  A2,  A2},//huzzam
    { C4,  G3,  D3,  D3}//azari sazi
};

const  uint8_t chordTable[15][5]={
    {3,0,4,7},  //Major
    {3,0,3,7},  //Minor
    {3,0,4,8},  //Augmented
    {3,0,3,6},  //Diminished
    {4,0,4,7,11},  //M7
    {4,0,3,7,10},  //m7
    {4,0,4,7,10},  //D7
    {4,0,3,6,10},  //hd7
    {4,0,3,6,9},  //d7
    {3,0,0,0},
    {3,0,0,0},
    {3,0,0,0},
    {3,0,0,0},
    {3,0,0,0},
    {3,0,0,0}
};


uint8_t returnBaseTable(uint8_t r, uint8_t c) {
    return baseTable[r][c];
}

int fretVoltageToFretNumber(float fretVoltage)
{
    float fretVoltageRatio = fretVoltage / fretBarMaxValue[0];
    int fretNumber = 0,i;
    //look at the resistance ratio at the finger depressing point and compare it to the fret resistance table to find the note offset
    //for ( i = 0 ; i < 160 ; i++) {   //check all traces
#ifdef INST_SETAR
     for (i = 0; i < 40; i++) { //check all traces
#endif
#ifdef INST_UKULELE
     for (i = 0; i < 12; i++) { //check all traces
#endif
        if (fretVoltageRatio >= fretRatio[0]) { //If open string
            fretNumber = 0;
            break;
        } else if ((fretVoltageRatio > fretRatio[i + 1]) && (fretVoltageRatio <= fretRatio[i])) {
            fretNumber = i + 1;
            break;
        }
    }
#ifdef INST_SETAR
     return fretNumber;
#endif
#ifdef INST_UKULELE
     return fretNumber*2;
#endif
    
}
/**
  uint32_t sendMidiNote(uint8_t note,
                   unsigned long velocity,
                             int pitchBend)

    - note:      the note number to be played (60 = middle C)
    - velocity:  note velocity as a raw ADC average (0x0-0x3FF)
    - pitchBend: value to bend the pitch by. Bytes are swapped

  This function plays the note sent with a velocity relative
  to the velocity passed in (scaled to be from 0x0 to 0x7F).
  It then send a pitch bend to match the pitch bend of the
  note. A pitch bend of 0x0040 (sent as 0x4000) means no pitch
  bend.
 **/
void  midiTx(uint8_t code, uint8_t status, uint8_t pitchLSB, uint8_t velocityMSB) {
    midi1.Val = 0; // Clear the send data by default

    //Midi1 is the note that we wish to play
    midi1.cableNum = 0; // Cable number 0  //4bits
    midi1.code = code; // 0x8, 0x9, 0xA, etc. //4bits
    midi1.status = status; // above with channel // 1 byte
    midi1.pitch = pitchLSB; // The note to play  // 1 byte
    midi1.velocity = velocityMSB; // How hard it was played  // 1byte
    //
    usbmidi_send_message((uint8_t *)&midi1, 4); // Send the data
    // USBTxHandle = USBTxOnePacket(MIDI_EP, (uint8_t*) & midi1, 4); // Send the
    // data
    // while (USBHandleBusy(USBTxHandle));
}

/**
  uint8_t findVelocity(float avg, float pot_mid_value, uint8_t prev_velocity)

    - avg: deflection from strum POT center (magnitude)
    - pot_mid_value: unused (avg is already deviation)
    - prev_velocity: last velocity sent for this string (0 = no previous, skip limiting)

  Maps strum strength to MIDI velocity (40–127) and limits how much
  velocity can change from one note to the next to avoid spikes from
  erroneous strum readings.
 **/
#define STRUM_VEL_IN_MIN  50.f   /* deflection below this → min velocity */
#define STRUM_VEL_IN_MAX  300.f  /* deflection above this → max velocity */
#define STRUM_VEL_OUT_MIN 10     /* minimum velocity (keeps notes audible) */
#define STRUM_VEL_OUT_MAX 127
#define STRUM_VEL_MAX_DELTA 28  /* max change per note (prevents erroneous spikes) */

uint8_t findVelocity(float avg, float pot_mid_value, uint8_t prev_velocity) {
    (void)pot_mid_value; /* unused; avg is already deviation from mid */

    if (tapWithoutStrumEnabled)
        return (uint8_t)STRUM_VEL_OUT_MAX;

    /* Clamp input to valid range */
    if (avg < STRUM_VEL_IN_MIN)
        avg = STRUM_VEL_IN_MIN;
    else if (avg > STRUM_VEL_IN_MAX)
        avg = STRUM_VEL_IN_MAX;

    /* Map deflection to 0..1 with sqrt for better resolution at soft strums */
    float norm = (avg - STRUM_VEL_IN_MIN) / (STRUM_VEL_IN_MAX - STRUM_VEL_IN_MIN);
    float v = (float)STRUM_VEL_OUT_MIN
              + (float)(STRUM_VEL_OUT_MAX - STRUM_VEL_OUT_MIN) * sqrtf(norm);

    int velocityVal = (int)(v + 0.5f);
    if (velocityVal < STRUM_VEL_OUT_MIN)
        velocityVal = STRUM_VEL_OUT_MIN;
    else if (velocityVal > STRUM_VEL_OUT_MAX)
        velocityVal = STRUM_VEL_OUT_MAX;

    /* Limit change from previous velocity to avoid spikes from bad readings */
    if (prev_velocity >= STRUM_VEL_OUT_MIN) {
        int delta = velocityVal - (int)prev_velocity;
        if (delta > STRUM_VEL_MAX_DELTA)
            velocityVal = (int)prev_velocity + STRUM_VEL_MAX_DELTA;
        else if (delta < -STRUM_VEL_MAX_DELTA)
            velocityVal = (int)prev_velocity - STRUM_VEL_MAX_DELTA;
        if (velocityVal < STRUM_VEL_OUT_MIN)
            velocityVal = STRUM_VEL_OUT_MIN;
        else if (velocityVal > STRUM_VEL_OUT_MAX)
            velocityVal = STRUM_VEL_OUT_MAX;
    }

    ESP_LOGD(TAG, "velocity value = %d (avg=%.0f)", velocityVal, (double)avg);
    return (uint8_t)velocityVal;
}



/**
  void noteOn(uint32_t adcFret,
             uint8_t c,
             unsigned long velocity)

    - adcFret:  ADC Value of this string
    - c:        the number of the string being played
    - velocity: ADC average of the strum POT

  This function takes the raw ADC values read from a string
  and strum and turns it into a MIDI note by linking data from
  the pitch bend table and base table and
 **/
void noteOn(STRUM * strum, uint8_t c, uint8_t r) {
    static uint32_t s_noteon_count = 0;
    s_noteon_count++;
    if (s_noteon_count % 300 == 0) {
        ESP_LOGI(TAG, "freeze ckpt noteOn n=%lu c=%u", (unsigned long)s_noteon_count, (unsigned)c);
    }
    uint8_t n;
    uint8_t channel = semitoneChannel;
    uint8_t velocityValue;

    uint8_t note12;// = strum->note; // 12-based midi note number calculated in getNote()
    uint8_t note24;// = strum->noteIn24; //24-based midi note number calculated in getNote()
    uint16_t pitchBend ;//= strum->pitchBend; //either 2000 or 2800 calculated by getNote()

    //Pitch bends for the notes that can be played
    uint16_t adjustedPitchBend = 0;
//    uint16_t majorThirdPitchBend;
//    uint16_t minorThirdPitchBend;
//    uint16_t perfectFifthPitchBend;

    //Get local copies of strum structure fields

    note12 = strum->note;
    note24 = strum->noteIn24;
    pitchBend = strum->pitchBend;


    //Determine the velocity to send with the note
    if(constantVelocityEnable==false){
        if(pluckedInstrument == true) // for plucked instrument get strumming derivative  for velocity
            velocityValue = findVelocity(strum->currentSampleN, 0, strum->strumVelocity);
        else // for bowed instrument get deflection for velocity
            velocityValue = findVelocity(strum->currentSampleN, 0, strum->strumVelocity);

        strum->strumVelocity = velocityValue;
        /* 1ms delay so BLE/watchdog get CPU; reduces freezes when constant velocity is off */
        vTaskDelay(pdMS_TO_TICKS(1));
    }else {
        velocityValue = constVelocityValue;
        strum->strumVelocity = velocityValue;
        /* Yield so BLE/ADC get CPU; reduces freezes when constant velocity is on */
        vTaskDelay(0);
    }


//    //Determine the pitch bend values to use for the note and chords if enabled.
//    if(pitchBend == NOTE_SEMITONE){
//        if(percussionInstrument){
//            channel=CHANNEL_PERCUSSION;
//        }else{
//            channel=semitoneChannel;
//        }
//        //channel = (percussionInstrument)?(CHANNEL_PERCUSSION):(CHANNEL_SEMITONE);
//        adjustedPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][note24 % 24]));
//        minorThirdPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(note24+6) % 24]));
//        majorThirdPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(note24+8) % 24]));
//        perfectFifthPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(note24+14) % 24]));
//    }
//    else if(pitchBend == NOTE_QUARTERTONE){
//        if(percussionInstrument){
//            channel=CHANNEL_PERCUSSION;
//        }else{
//            channel=quartertoneChannel;
//        }
//        adjustedPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][note24 % 24]));
//        minorThirdPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(note24+6) % 24]));
//        majorThirdPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(note24+8) % 24]));
//        perfectFifthPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(note24+14) % 24]));
//    }
//    //USB Tx
//    if (isUSBConnected() == true) {
//        //Turn on semitone note that was played
//        midiTx(0x9, 0x90+channel, note12, velocityValue);
//        midiTx(0xE, 0xE0+channel, (adjustedPitchBend & 0x00FF), (adjustedPitchBend & 0xFF00) >> 8);
//
//        if(chordEnabled && !percussionInstrument){
//            if(strum->strumDirection == UP){
//                //major third , 4 semitones up
//                midiTx(0x9, 0x90+channel, note12 + 4, velocityValue);
//                midiTx(0xE, 0xE0+channel, (majorThirdPitchBend & 0xFF), (majorThirdPitchBend & 0xFF00) >> 8);
//
//            } else if(strum->strumDirection == DOWN){
//                //minor third , 3 semitones up
//                midiTx(0x9, 0x90+channel, note12 + 3, velocityValue);
//                midiTx(0xE, 0xE0+channel, (minorThirdPitchBend & 0xFF), (minorThirdPitchBend & 0xFF00) >> 8);
//            }
//
//            //perfect fifth, 7 semitones up
//            midiTx(0x9, 0x90+channel, note12 + 7, velocityValue);
//            midiTx(0xE, 0xE0+channel, (perfectFifthPitchBend & 0xFF), (perfectFifthPitchBend & 0xFF00) >> 8);
//        }


        if (!chordEnabled) {
            //ESP_LOGD(TAG , "fineTuningPB = %d", fineTuningPB );
            if (pitchBend == NOTE_SEMITONE) {
                if (percussionInstrument) {
                    channel = CHANNEL_PERCUSSION;
                } else {
                    channel = semitoneChannel;
                }

                adjustedPitchBend = PBCenter + PBCenterOffset +
                        (2 * (pitchAdjustment[pitchSystem][ (note24 % 24) ]) ) + fineTuningPB;

            } else if (pitchBend == NOTE_QUARTERTONE) {
                if (percussionInstrument) {
                    channel = CHANNEL_PERCUSSION;
                } else {
                    channel = quartertoneChannel;
                }
                adjustedPitchBend = (PBCenter + PBCenterOffset + PBQuarterToneOffset)+
                        (2 * (pitchAdjustment[pitchSystem][ (note24 % 24) ])) + fineTuningPB;
            }
            midiTx(0xE, 0xE0 + channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
            midiTx(0x9, 0x90 + channel, note12, velocityValue);

#ifdef uart_en
            

            //This is to differentiate between up and down strums for the eTarLearn app,
            // however sending this to the synth chip might cause trouble.
            // So for the time being, since there is no android app that can communicate with the eTar , I comment it out
            //if app is capable of communicating with eTar, in order not to send it to the synth chip, 
            //blemidi_send_message() should be used instead
            
            // if (strum->strumDirection == UP) {
            //     inputToUART(0xFB, c, UP); //send string number c , and  strum direction for android app
            // } else {
            //     inputToUART(0xFB, c, DOWN); //send string number c , and  strum direction for android app
            // }

            inputToUART(0xE0 + channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
            inputToUART(0x90 + channel, note12, velocityValue);


#endif

    }else{ //if chord enabled
        for(n=1; n<=chordTable[chordType][CHORD_NUM_OF_NOTES]; n++){

            if(percussionInstrument){
                channel=CHANNEL_PERCUSSION;
            }

            if(pitchBend == NOTE_SEMITONE){
                if(!percussionInstrument){
                    channel=semitoneChannel;
                }
                adjustedPitchBend = (PBCenter+PBCenterOffset)+
                        (2*(pitchAdjustment[pitchSystem][ (note24+(chordTable[chordType][n]*2)) % 24 ] )) + fineTuningPB;

            }else if(pitchBend == NOTE_QUARTERTONE){
                if(!percussionInstrument){
                    channel=quartertoneChannel;
                }
                adjustedPitchBend = (PBCenter+PBCenterOffset+PBQuarterToneOffset)+
                        (2*(pitchAdjustment[pitchSystem][ (note24+(chordTable[chordType][n]*2)) % 24 ] )) + fineTuningPB;
            }

            if(strum->strumDirection==UP){
                midiTx(0xE, 0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
                midiTx(0x9, 0x90+channel, note12+chordTable[chordType][n], velocityValue);
                #ifdef uart_en
                inputToUART(0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
                inputToUART(0x90+channel, note12 + chordTable[chordType][n], velocityValue);

                //inputToUART(0xFB, c , UP); //send string number c , and  strum direction for android app

                #endif
            }else{
                midiTx(0xE, 0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
                midiTx(0x9, 0x90+channel, note12+chordTable[1][n], velocityValue);
                #ifdef uart_en
                inputToUART(0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
                inputToUART(0x90+channel, note12 + chordTable[1][n], velocityValue);

                //inputToUART(0xFB, c , DOWN); //send string number c , and  strum direction for android app
                #endif
            }
        }
    }


//    //UART Tx
//#ifdef uart_en
//    //Turn the played note on
//    inputToUART(0x90+channel, note12, velocityValue);
//    inputToUART(0xE0+channel, adjustedPitchBend & 0x00FF, (adjustedPitchBend & 0xFF00) >> 8);
//
//    if(chordEnabled && !percussionInstrument){
//        if(strum->strumDirection == UP){
//            //major third , 4 semitones up
//            inputToUART(0x90+channel, note12 + 4, velocityValue);
//            inputToUART(0xE0+channel, (majorThirdPitchBend & 0xFF), (majorThirdPitchBend & 0xFF00) >> 8);
//
//        } else if(strum->strumDirection == DOWN){
//            //minor third , 3 semitones up
//            inputToUART(0x90+channel, note12 + 3, velocityValue);
//            inputToUART(0xE0+channel, (minorThirdPitchBend & 0xFF), (minorThirdPitchBend & 0xFF00) >> 8);
//        }
//
//        //perfect fifth, 7 semitones up
//        inputToUART(0x90+channel, note12 + 7, velocityValue);
//        inputToUART(0xE0+channel, (perfectFifthPitchBend & 0xFF), (perfectFifthPitchBend & 0xFF00) >> 8);
//    }
//#endif

        //Capture the values for use when turning the notes off
        strum->adcPrevFret = strum->adcFret;
        strum->prevNote = strum->note;
        strum->prevPitchBend =pitchBend;
        if (pitchBend == NOTE_SEMITONE) {
            strum->prev24Note = (strum->note * 2);
        } else if (pitchBend == NOTE_QUARTERTONE) {
            strum->prev24Note = (strum->note * 2) + 1;
        }
        strum->prevVelocity = strum->strumVelocity;

        if (vibratoEnabled == true) {
            vibratoPause = false;
        }
        /* Yield after full noteOn so BLE/MIDI/ADC get CPU and freezes are reduced (constant velocity and dynamics) */
        vTaskDelay(0);
}

void tapNoteOn(TAP * tap, uint8_t c, uint8_t r) {

    uint8_t channel = 0;
    
    uint8_t note12;
    uint8_t note24;
    uint16_t tempPitchBend = 0;

    //Get local copies of structure fields
    note12=tap->note;
    note24=tap->noteIn24;

    //Pause the vibrato
    if (vibratoEnabled == true) {
        vibratoPause = true;
        vibratoSampleCounter = 0;
    }

        //Adjusted to play on USB connection
    if(tap->pitchBend == NOTE_SEMITONE){
        if(percussionInstrument){
            channel=CHANNEL_PERCUSSION;
        }else{
            channel=semitoneChannel;
        }
        tempPitchBend = (PBCenter+PBCenterOffset) +  (2*(pitchAdjustment[pitchSystem][note24 % 24]));
    }
    else if(tap->pitchBend == NOTE_QUARTERTONE){
        if(percussionInstrument){
            channel=CHANNEL_PERCUSSION;
        }else{
            channel=quartertoneChannel;
        }
        tempPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) +  (2*(pitchAdjustment[pitchSystem][note24 % 24]));
    }





    if ( tapWithoutStrumEnabled ){
        if(note24 != workingBaseTable[c]){ //when tap without strum enabled , do not play open string ..(working base table is based on 24 notes)
            midiTx(0xE, 0xE0+channel, (tempPitchBend & 0xFF), (tempPitchBend & 0xFF00) >> 8);
            midiTx(0x9, 0x90+channel, note12, tap->velocity); //3C = 60

            #ifdef uart_en
            inputToUART(0xE0+channel, (tempPitchBend&0x00FF), ((tempPitchBend&0xFF00)>>8));
            inputToUART(0x90+channel, note12, tap->velocity);

            #endif

        }
    }
    else{
            midiTx(0xE, 0xE0+channel, (tempPitchBend & 0xFF), (tempPitchBend & 0xFF00) >> 8);
            midiTx(0x9, 0x90+channel, note12, tap->velocity); //3C = 60

            #ifdef uart_en
            inputToUART(0xE0+channel, (tempPitchBend&0x00FF), ((tempPitchBend&0xFF00)>>8));
            inputToUART(0x90+channel, note12, tap->velocity);

            #endif

    }
#endif
    
    //Capture the values for turning the notes off etc.
    tap->prevNote = tap->note;
    tap->prevPitchBend = tap->pitchBend;

    //Reenable the vibrato?
    if (vibratoEnabled == true) {
        vibratoPause = false;
    }

}

/**
  void noteOff(uint32_t prevNote,
              uint8_t c)

    - prevNote: ADC Value of the last played note
    - c:        the number of the string played

  This function calls noteOn with the last note played and a
  velocity of 0 to effectively turn the note off.
            _asm goto _startup _endasm
        }
        #endif
        #pragma code REMAPPED_HIGH_INTERRUPT_VECTOR = REMAPPED_HIGH_INTERRUPT_VECTOR_ADDRESS
        void Remapped_High_ISR (void)
        {
             _asm goto YourHighPriorityISRCode _endasm
        }
        #pragma code REMAPPED_LOW_INTERRUPT_VECTOR = REMAPPED_LOW_INTERRUPT_VECTOR_ADDRESS
        void Remapped_Low_ISR (void)
        {
             _asm goto YourLowPriorityISRCode _endasm
        }
 **/

void noteOff(STRUM * strum, uint8_t c, uint8_t r) {
    static uint32_t s_noteoff_count = 0;
    s_noteoff_count++;
    if (s_noteoff_count % 300 == 0) {
        ESP_LOGI(TAG, "freeze ckpt noteOff n=%lu c=%u", (unsigned long)s_noteoff_count, (unsigned)c);
    }
    uint8_t n;
    uint8_t channel = 0;

    uint8_t note12;// = strum->note; // 12-based midi note number calculated in getNote()
    uint8_t note24;// = strum->noteIn24; //24-based midi note number calculated in getNote()
    uint16_t pitchBend;

    uint16_t adjustedPitchBend = 0;

//    uint8_t noteBase12;
//    uint8_t noteBase24;
//    uint16_t tempPitchBend;
//    uint16_t majorThirdPitchBend;
//    uint16_t minorThirdPitchBend;
//    uint16_t perfectFifthPitchBend;



        //Get local copies of the strum structure fields
//    noteBase12 = strum->note;
//    noteBase24 = strum->noteIn24;
    note12 = strum->note;
    note24 = strum->noteIn24;
    pitchBend = strum->pitchBend;

    if (vibratoEnabled == true) {
        vibratoPause = true;
        vibratoSampleCounter = 0;
    }


//    //Determine channel and pitch bend values to use. Pitch bends wont be heard
//    //but putting them here helps keep the center for the next note
//    if(pitchBend == NOTE_SEMITONE){
//        if(percussionInstrument){
//            channel=CHANNEL_PERCUSSION;
//        }else{
//            channel=semitoneChannel;
//        }
//        tempPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][noteBase24 % 24]));
//        minorThirdPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(noteBase24+6) % 24]));
//        majorThirdPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(noteBase24+8) % 24]));
//        perfectFifthPitchBend = (PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(noteBase24+14) % 24]));
//    }
//    else if(pitchBend == NOTE_QUARTERTONE){
//        if(percussionInstrument){
//            channel=CHANNEL_PERCUSSION;
//        }else{
//            channel=quartertoneChannel;
//        }
//        tempPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][noteBase24 % 24]));
//        minorThirdPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(noteBase24+6) % 24]));
//        majorThirdPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(noteBase24+8) % 24]));
//        perfectFifthPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) + (2*(pitchAdjustment[pitchSystem][(noteBase24+14) % 24]));
//    }
//    //USB
//    if (isUSBConnected() == true) {
//        //Turn the played note off
//        midiTx(0x8, 0x80+channel, noteBase12, 0x7F);
//        midiTx(0xE, 0xE0+channel, (tempPitchBend & 0x00FF), (tempPitchBend & 0xFF00) >> 8 );
//
//        if(chordEnabled && !percussionInstrument){
//            if(strum->strumDirection == UP){
//                //major third , 4 semitones up
//                midiTx(0x8, 0x80+channel, noteBase12 + 4, 0x7F);
//                midiTx(0xE, 0xE0+channel, (majorThirdPitchBend & 0xFF), (majorThirdPitchBend & 0xFF00) >> 8);
//
//            } else {//if(strum->strumDirection == DOWN)
//                //minor third , 3 semitones up
//                midiTx(0x8, 0x80+channel, noteBase12 + 3, 0x7F);
//                midiTx(0xE, 0xE0+channel, (minorThirdPitchBend & 0xFF), (minorThirdPitchBend & 0xFF00) >> 8);
//            }
//
//            //perfect fifth, 7 semitones up
//            midiTx(0x8, 0x80+channel, noteBase12 + 7, 0x7F);
//            midiTx(0xE, 0xE0+channel, (perfectFifthPitchBend & 0xFF), (perfectFifthPitchBend & 0xFF00) >> 8);
//        }
//    }

//#ifdef uart_en
//        //Turn the played note off
//        inputToUART(0x80+channel, noteBase12, 0x7F);
//        inputToUART(0xE0+channel, (tempPitchBend & 0xFF), (tempPitchBend & 0xFF00) >> 8 );
//               // delayFor(10000);
//
//        if(chordEnabled && !percussionInstrument){
//            if(strum->strumDirection == UP){
//                //major third , 4 semitones up
//                inputToUART(0x80+channel, noteBase12 + 4, 0x7F);
//                inputToUART(0xE0+channel, (majorThirdPitchBend & 0xFF), (majorThirdPitchBend & 0xFF00) >> 8);
//                //delayFor(10000);
//
//            } else {//if(strum->strumDirection == DOWN)
//                //minor third , 3 semitones up
//                inputToUART(0x80+channel, noteBase12 + 3, 0x7F);
//                inputToUART(0xE0+channel, (minorThirdPitchBend & 0xFF), (minorThirdPitchBend & 0xFF00) >> 8);
//                //delayFor(10000);
//            }
//
//            //perfect fifth, 7 semitones up
//            inputToUART(0x80+channel, noteBase12 + 7, 0x7F);
//            inputToUART(0xE0+channel, (perfectFifthPitchBend & 0xFF), (perfectFifthPitchBend & 0xFF00) >> 8);
//                //delayFor(10000);
//        }
//#endif
    if(!chordEnabled){
        if(pitchBend == NOTE_SEMITONE){
            if(!percussionInstrument){
                channel=semitoneChannel;
            }
            adjustedPitchBend = (PBCenter+PBCenterOffset)+
                    (2*(pitchAdjustment[pitchSystem][ (note24 % 24) ])) + fineTuningPB;

        }else if(pitchBend == NOTE_QUARTERTONE){
            if(!percussionInstrument){
                channel=quartertoneChannel;
            }
            adjustedPitchBend = (PBCenter+PBCenterOffset+PBQuarterToneOffset)+
                    (2*(pitchAdjustment[pitchSystem][ (note24 % 24) ])) + fineTuningPB;
        }

        midiTx(0xE, 0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
        midiTx(0x8, 0x80+channel, note12, 0x7F);
        

        #ifdef uart_en
        inputToUART(0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
        inputToUART(0x80+channel, note12, 0x7F);
        
        #endif

    }else{
        for(n=1; n<=chordTable[chordType][CHORD_NUM_OF_NOTES]; n++){

            if(percussionInstrument){
                channel=CHANNEL_PERCUSSION;
            }

            if(pitchBend == NOTE_SEMITONE){
                if(!percussionInstrument){
                    channel=semitoneChannel;
                }
                adjustedPitchBend = (PBCenter+PBCenterOffset)+
                        (2*(pitchAdjustment[pitchSystem][ (note24+(chordTable[chordType][n]*2)) % 24 ] )) + fineTuningPB;

            }else if(pitchBend == NOTE_QUARTERTONE){
                if(!percussionInstrument){
                    channel=quartertoneChannel;
                }
                adjustedPitchBend = (PBCenter+PBCenterOffset+PBQuarterToneOffset)+
                        (2*(pitchAdjustment[pitchSystem][ (note24+(chordTable[chordType][n]*2)) % 24 ] )) + fineTuningPB;
            }

            if(strum->strumDirection==UP){
                midiTx(0x8, 0x80+channel, note12+chordTable[chordType][n], 0x7F);
                midiTx(0xE, 0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);

                #ifdef uart_en
                inputToUART(0x80+channel, note12 + chordTable[chordType][n], 0x7F);
                inputToUART(0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
                #endif
            }else{
                midiTx(0x8, 0x80+channel, note12+chordTable[1][n], 0x7F);
                midiTx(0xE, 0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);

                #ifdef uart_en
                inputToUART(0x80+channel, note12 + chordTable[1][n], 0x7F);
                inputToUART(0xE0+channel, (adjustedPitchBend & 0xFF), (adjustedPitchBend & 0xFF00) >> 8);
                #endif
            }
        }
    }


    if (vibratoEnabled == true) {
        vibratoPause = false;
    }
}

void tapNoteOff(TAP * tap, uint8_t c, uint8_t r) {
    uint8_t channel;
    uint8_t note12;
    uint8_t note24;
    uint16_t tempPitchBend;

    note12 = tap->note;
    note24 = tap->noteIn24;

    if (vibratoEnabled == true) {
        vibratoPause = true;
        vibratoSampleCounter = 0;
    }

    //Adjusted to play on USB connection
    if(tap->pitchBend == NOTE_SEMITONE){
        if(percussionInstrument){
            channel=CHANNEL_PERCUSSION;
        }else{
            channel=semitoneChannel;
        }
        tempPitchBend = (PBCenter+PBCenterOffset) +  (2*(pitchAdjustment[pitchSystem][note24 % 24]));
    }
    else{
        if(percussionInstrument){
            channel=CHANNEL_PERCUSSION;
        }else{
            channel=quartertoneChannel;
        }
        tempPitchBend = (PBQuarterToneOffset+PBCenter+PBCenterOffset) +  (2*(pitchAdjustment[pitchSystem][note24 % 24]));
    }

    //USB
    //if (isUSBConnected() == true) {
        midiTx(0x8, 0x80+channel, note12, tap->velocity); //3C = 60
        midiTx(0xE, 0xE0+channel, (tempPitchBend & 0xFF), (tempPitchBend & 0xFF00) >> 8);
    //}
    //UART
#ifdef uart_en
    inputToUART(0x80+channel, note12, tap->velocity);
    inputToUART(0xE0+channel, (tempPitchBend&0x00FF), ((tempPitchBend&0xFF00)>>8));
#endif

    if (vibratoEnabled == true) {
        vibratoPause = false;
    }
}

// if string is pressed between two frets, the lower one on the neck is picked as the note

void getNote(STRUM * strum, uint8_t c) {
    int i, noteOffset = 0;
    uint8_t tempNote;
    float fretVoltageRatio = (strum->adcFretAverage / fretBarMaxValue[c]);
    ESP_LOGD(TAG, "ratio = %f", fretVoltageRatio);
    //myRatio x;

    //x.f = fretVoltageRatio;
    //inputToUART(0x3E, x.b[0], x.b[1]);
    //inputToUART(0x3F, x.b[2], x.b[3]);

#ifdef binarySearch
    if (fretVoltageRatio >= fretRatio[0]) {
        noteOffset = 0;
    } else {
        noteOffset = binary_search(fretRatio, fretVoltageRatio, 0, 64);
    }
#else
#ifdef membrane
    //look at the resistance ratio at the finger depressing point and compare it to the fret resistance table to find the note offset
    for (i = 0; i < 41; i++) { //check all traces
        if (fretVoltageRatio >= fretRatio[0]) { //If open string
            noteOffset = 0;
            break;
        } else if ((fretVoltageRatio > fretRatio[i + 1]) && (fretVoltageRatio <= fretRatio[i])) {
            noteOffset = i;
            break;
        }
    }
#else
    //look at the resistance ratio at the finger depressing point and compare it to the fret resistance table to find the note offset
    //for ( i = 0 ; i < 160 ; i++) {   //check all traces

#ifdef INST_SETAR
     for (i = 0; i < 40; i++) { //check all traces
#endif
#ifdef INST_UKULELE
     for (i = 0; i < 12; i++) { //check all traces
#endif
        if (fretVoltageRatio >= fretRatio[0]) { //If open string
            noteOffset = 0;
            //stringCalibrate(c);
            break;
        } else if ((fretVoltageRatio > fretRatio[i + 1]) && (fretVoltageRatio <= fretRatio[i])) {
            noteOffset = i + 1;
            break;
        }
     }
#endif
#endif


#ifdef INST_UKULELE
       noteOffset = noteOffset*2;
#endif

#ifdef INST_SETAR
       
    // if acoustic tar or setar frets are the only ones that should be active. The missing frets on setar will be considered as the next existing fret
    if (acousticSetarFretsEnabled){
        switch (noteOffset)
        {
            case 1:
            case 2:
                noteOffset = 3;
                break;
            case 5:
                noteOffset = 6;
                break;
            case 9:
                noteOffset = 10;
                break;
            case 11:
                noteOffset = 12;
                break;
            case 15:
                noteOffset = 16;
                break;
            case 19:
                noteOffset = 20;
                break;
            case 23:
                noteOffset = 24;
                break;
            case 25:
                noteOffset = 26;
                break;
            case 29:
                noteOffset = 30;
                break;
            case 33:
                noteOffset = 34;
                break;
            case 35:
                noteOffset = 36;
                break;
            case 37:
                noteOffset = 38;
                break;
            case 39:
                noteOffset = 40;
                break;

         }
     }

#endif

    strum->fret = noteOffset;
    tempNote = workingBaseTable[c] + noteOffset;
    strum->note = (tempNote / 2);

    if (quarterNotesEnabled == false) {
        if (tempNote % 2 == 0) {
            strum->pitchBend = 0x2000;
        } else if (tempNote % 2 == 1) {
            strum->note = strum->note + 1;
            strum->pitchBend = 0x2000;
        }
        strum->noteIn24 = strum->note * 2;
    } else if (quarterNotesEnabled == true) {
        if (tempNote % 2 == 0) {
            strum->pitchBend = 0x2000;
            strum->noteIn24 = strum->note * 2;
        } else if (tempNote % 2 == 1) {
            strum->pitchBend = 0x2800;
            strum->noteIn24 = (strum->note * 2) + 1;
        }
    }

    /*
    if ( absoluteDiff(tempNote, strum->prev24Note) == 1 ) {
            strum->note = strum->prevNote;
            strum->pitchBend = strum->prevPitchBend;
    }
     */
}

void getTapNote(TAP * tap, uint8_t c) {
    int i, noteOffset = 0;
    uint8_t tempNote;

    float fretVoltageRatio = (tap->adcFretAverage / fretBarMaxValue[c]);

#ifdef binarySearch
    if (fretVoltageRatio >= fretRatio[0]) {
        noteOffset = 0;
    } else {
        noteOffset = binary_search(fretRatio, fretVoltageRatio, 0, 64);
    }
#else
#ifdef membrane
    //look at the resistance ratio at the finger depressing point and compare it to the fret resistance table to find the note offset
    for (i = 0; i < 40; i++) { //check all traces
        if (fretVoltageRatio >= fretRatio[0]) { //If open string
            noteOffset = 0;
            break;
        } else if ((fretVoltageRatio > fretRatio[i + 1]) && (fretVoltageRatio <= fretRatio[i])) {
            noteOffset = i;
            break;
        }
    }
#else
    //look at the resistance ratio at the finger depressing point and compare it to the fret resistance table to find the note offset
#ifdef INST_SETAR
     for (i = 0; i < 40; i++) { //check all traces
#endif
#ifdef INST_UKULELE
     for (i = 0; i < 12; i++) { //check all traces
#endif
        if (fretVoltageRatio >= fretRatio[0]) { //If open string
            noteOffset = 0;
            break;
        } else if ((fretVoltageRatio > fretRatio[i + 1]) && (fretVoltageRatio <= fretRatio[i])) {
            noteOffset = i + 1;
            break;
        }
    }
#endif
#endif

     #ifdef INST_UKULELE
       noteOffset = noteOffset*2;
#endif

    tap->fret = noteOffset;
    tempNote = workingBaseTable[c] + noteOffset;
    tap->note = (tempNote / 2) ;

    if (quarterNotesEnabled == false) {
        if (tempNote % 2 == 0) {
            tap->pitchBend = 0x2000;
        } else if (tempNote % 2 == 1) {
            tap->pitchBend = 0x2000;
            tap->note = tap->note + 1;
        }
        tap->noteIn24 = tap->note * 2;
    } else if (quarterNotesEnabled == true) {
        if (tempNote % 2 == 0) {
            tap->pitchBend = 0x2000;
            tap->noteIn24 = tap->note * 2;
        } else if (tempNote % 2 == 1) {
            tap->pitchBend = 0x2800;
            tap->noteIn24 = (tap->note * 2) + 1;
        }
    }
}

/**
  uint16_t int fromMid(uint32_t val)

    - val: ADC strum average value

  This function returns the difference between the midpoint of
  the strum POT (main.c:POT_MID_VALUE) and the strum's average
  as a positive number.
 **/
uint16_t fromMid(uint32_t val, uint32_t mid, int c) {
    if (val > mid) {
        //strumUpDown[c] = 1;
        return (val - mid);
    } else {
        //strumUpDown[c] = 0;
        return (mid - val);
    }
}



#ifdef binarySearch

int binary_search(float A[], float key, int imin, int imax) {
    int imid;
    // test if array is empty
    if (imax < imin) {
        // set is empty, so return value showing not found
        return 99;
    } else {
        if ((imax - imin) == 1) {
            return imin;
        }

        // calculate midpoint to cut set in half
        imid = imin + ((imax - imin) / 2);

        // three-way comparison
        if (A[imid] > key) {
            // key is in lower subset
            return binary_search(A, key, imid, imax);
        } else if (A[imid] <= key) {
            // key is in upper subset
            return binary_search(A, key, imin, imid);
        }
    }
}
#endif
