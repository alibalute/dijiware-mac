/**
 * @file metronome.c
 * @author Ali
 * @brief
 * @version 0.1
 * @date 2023-12-30
 *
 * Copyright (c) 2023 elemental ID
 *
 */


#include "esp_log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

#include <driver/gpio.h>
#include <driver/uart.h>

#include "metronome.h"

static const char *TAG = "metronome";


// MIDI Commands
#define NOTE_ON  0x99  //channel 9 for percussion
#define NOTE_OFF 0x89  //channel 9 for percussion



// Metronome Configuration

#define METRONOME_NOTE_1 68  // MIDI note for metronome click
#define METRONOME_NOTE_2 75  // MIDI note for metronome click


static TimerHandle_t xTimer;
int bpm = 60; //default metronome speed
int numBeat = 1; //default number of beats
int atBeat = 0;
int metronomeVolume = 64; //default metronome volume

// Function to send MIDI message to the synth chip
void sendMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    inputToUART(status, data1, data2);
}



void timerCallback(TimerHandle_t xTimer) {
   
    if(atBeat == 0){
        sendMidiMessage(NOTE_ON, METRONOME_NOTE_1, metronomeVolume);
    }
    else{
        sendMidiMessage(NOTE_ON, METRONOME_NOTE_2, metronomeVolume);
    }
    atBeat++;
    if(atBeat >= numBeat ){
        atBeat = 0;
    }  
    
}

void change_metronome_volume(int value){


    metronomeVolume = value; //either 127 for max or 40 for min
 
}
void change_metronome_numBeats(int value){


    numBeat = value; //either 2 or 4 f

}


int get_metronome_bpm(void) { return bpm; }
int get_metronome_numBeats(void) { return numBeat; }
int get_metronome_volume(void) { return metronomeVolume; }

void start_metronome_timer(void){


    if (xTimer != NULL){
        xTimerDelete(xTimer , 0);
    }

    xTimer = xTimerCreate(
        "MyTimer",             // Timer name
        pdMS_TO_TICKS(60000 / 60),   // 60000ms and 60 beats per min 
        pdTRUE,                // Auto-reload
        0,                     // Timer ID (not used in this example)
        timerCallback         // Timer callback function
    );
    if (xTimer != NULL) {
       xTimerStart(xTimer, 0);
    }
}

void stop_metronome_timer(void){


    if (xTimer != NULL){
        xTimerDelete(xTimer , 0);
        xTimer = NULL;
    }
}

void change_metronome_speed(uint8_t value)
{
    atBeat = 0; //to start from the first beat after the change

    bpm = value; 
    if (xTimer != NULL){
        xTimerStop(xTimer , 0);
        BaseType_t success = xTimerChangePeriod(xTimer, pdMS_TO_TICKS(60000/bpm), 0);
        if (success == pdPASS) {
            // Timer period changed successfully
            xTimerStart(xTimer , 0);
        }

    }



    // if (xTimer != NULL){
    //     xTimerDelete(xTimer , 0);
    // }

    // xTimer = xTimerCreate(
    //     "MyTimer",             // Timer name
    //     pdMS_TO_TICKS(60000 / bpm),   // Timer period in ticks (example: 1000 ms)
    //     pdTRUE,                // Auto-reload
    //     0,                     // Timer ID (not used in this example)
    //     timerCallback         // Timer callback function
    // );
    // if (xTimer != NULL) {
    //     xTimerStart(xTimer, 0);
    // }


}




