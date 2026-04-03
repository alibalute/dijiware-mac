/**
 * @file midi_player.c
 * @author Ali
 * @brief
 * @version 0.1
 * @date 2024-1-3
 *
 * Copyright (c) 2023 elemental ID
 *
 */
#include <stdio.h>
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "midi_player.h"
#include "spiffs.h"

static const char* TAG = "midi_player";

extern int bpm; // bpm is declared and initialized in metronome.c
extern int16_t transposeValue; // value of transpose

//in this code, parse_midi_file() opens the midi file and parses it into a MidiEvent vector, then starts the sequencer task to send these events to the synthesizer
//parse_midi_file() is called from util.c




MidiEvent *events = NULL; //vector of midi events which is alloacted by the parser
size_t eventsCount = 0;
uint16_t format;
uint16_t tracks;
uint16_t division;

double ticksPerQuarterNote = 480;  // Ticks per quarter note
double microsecondsPerQuarterNote ;  // Microseconds per quarter note (tempo) , this should be read from the midi file. it can be done later!! The code is there but doesnt work!

 // event time in ms
uint32_t event_time;


uint32_t convertDeltaTimeToTime(double deltaTime, double ticksPerQuarterNote, double microsecondsPerQuarterNote) {

    return (uint32_t)((deltaTime / ticksPerQuarterNote) * (microsecondsPerQuarterNote / 1000.0)); //in miliseconds
}

uint32_t swap_endianness(uint32_t value) {
    return ((value >> 24) & 0x000000FF) |
           ((value >> 8) & 0x0000FF00) |
           ((value << 8) & 0x00FF0000) |
           ((value << 24) & 0xFF000000);
}

int compare_events( void *a,  void *b) {
    return ((MidiEvent *)a)->time - ((MidiEvent *)b)->time;
}

void play_midi_file(void) {
    FILE *file = fopen(midiFile, "rb");

    if (file == NULL) {
        printf("Failed to open MIDI file\n");
        return;
    }

    uint8_t sig[4];
    if (fread(sig, 1, 4, file) != 4 || sig[0] != 'M' || sig[1] != 'T' || sig[2] != 'h' || sig[3] != 'd') {
        ESP_LOGW(TAG, "Invalid MIDI: missing MThd");
        fclose(file);
        return;
    }

    uint32_t header_length;
    if (fread(&header_length, 4, 1, file) != 1) {
        fclose(file);
        return;
    }
    header_length = swap_endianness(header_length);

    uint8_t hd[6];
    if (header_length < 6 || fread(hd, 1, 6, file) != 6) {
        fclose(file);
        return;
    }
    format = (uint16_t)((hd[0] << 8) | hd[1]);
    tracks = (uint16_t)((hd[2] << 8) | hd[3]);
    division = (uint16_t)((hd[4] << 8) | hd[5]);
    if (header_length > 6) {
        fseek(file, (long)(header_length - 6), SEEK_CUR);
    }

    if ((division & 0x8000) == 0 && division != 0) {
        ticksPerQuarterNote = (double)division;
    } else {
        ticksPerQuarterNote = 480.0;
    }

    if (fread(sig, 1, 4, file) != 4 || sig[0] != 'M' || sig[1] != 'T' || sig[2] != 'r' || sig[3] != 'k') {
        ESP_LOGW(TAG, "Invalid MIDI: missing MTrk");
        fclose(file);
        return;
    }

    uint32_t track_length;
    if (fread(&track_length, 4, 1, file) != 1) {
        fclose(file);
        return;
    }
    track_length = swap_endianness(track_length);

    long track_end = ftell(file) + (long)track_length;

    event_time = 0;
    eventsCount = 0;
    microsecondsPerQuarterNote = 1000000 * 60 / bpm;

    while (ftell(file) < track_end) {
        uint32_t delta_time = read_variable_length(file);
        uint32_t temp = convertDeltaTimeToTime((double)delta_time, ticksPerQuarterNote, microsecondsPerQuarterNote);
        uint8_t status_byte;
        if (fread(&status_byte, 1, 1, file) != 1) {
            break;
        }

        if (status_byte == 0xFF) {
            uint8_t metaType;
            if (fread(&metaType, 1, 1, file) != 1) {
                break;
            }
            uint32_t meta_len = readVLQ(file);
            if (metaType == 0x51 && meta_len == 3) {
                uint32_t tempo = 0;
                for (uint32_t i = 0; i < meta_len; i++) {
                    uint8_t byte;
                    if (fread(&byte, 1, 1, file) != 1) {
                        goto parse_done;
                    }
                    tempo = (tempo << 8) | byte;
                }
                microsecondsPerQuarterNote = (double)tempo;
                ESP_LOGD(TAG, "Tempo meta: %lu us/qn", (unsigned long)tempo);
            } else {
                for (uint32_t i = 0; i < meta_len; i++) {
                    uint8_t ign;
                    if (fread(&ign, 1, 1, file) != 1) {
                        break;
                    }
                }
            }
            continue;
        }

        uint8_t event_type = status_byte & 0xF0;

        switch (event_type) {
            case 0x80:
            case 0x90:
            case 0xE0:
                event_time += temp;

                uint8_t param1, param2;
                if (fread(&param1, 1, 1, file) != 1 || fread(&param2, 1, 1, file) != 1) {
                    goto parse_done;
                }

                {
                    MidiEvent event;
                    event.status = event_type;
                    event.note = param1;
                    event.velocity = param2;
                    event.time = event_time;

                    eventsCount++;
                    events = realloc(events, sizeof(MidiEvent) * eventsCount);
                    if (events == NULL) {
                        eventsCount = 0;
                        goto parse_done;
                    }
                    events[eventsCount - 1] = event;
                }
                break;

            default:
                break;
        }
    }
parse_done:
    xTaskCreate(&midi_sequencer_task, "midi_sequencer_task", 4096, NULL, 5, NULL);

    fclose(file);
}

// Function to read variable-length quantity (VLQ) from a file
uint32_t readVLQ(FILE *file) {
    uint32_t value = 0;
    uint8_t byte;

    do {
        fread(&byte, 1, 1, file);
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

uint32_t read_variable_length(FILE *file) {
    uint32_t value = 0;
    uint8_t byte;

    do {
        fread(&byte, 1, 1, file);
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

// MIDI output function (replace with actual MIDI output logic)
void send_midi_event( MidiEvent *event) {
    inputToUART(event->status , event->note + transposeValue , event->velocity );
}

// MIDI sequencer task
void midi_sequencer_task(void *pvParameters) {

    size_t num_events = eventsCount;

    // Sort MIDI events by timestamp (no need, as already are sorted in midi file)
    //qsort(events, num_events, sizeof(MidiEvent), compare_events);

    uint32_t current_time;
    uint32_t start_time = esp_log_timestamp() ;//this is the time we start playing notes


    for (size_t i = 0; i < num_events; ++i) {

        
        if(midiStop)
            break; //if midi stop is enabled, do not bother to continue sending midi events 
        
        // Wait until it's time to send the MIDI event. Keep checking current time until it passes the start-time + event-time
        do {
            current_time = esp_log_timestamp();
            vTaskDelay(1 / portTICK_PERIOD_MS);
            while(midiPause && !midiStop){ //wait until pause is removed, but if midi stop is enabled while waiting , exit this wait
                vTaskDelay(1 / portTICK_PERIOD_MS);
                start_time = esp_log_timestamp(); //when we pause, we should move the start-time forward so notes wont expire
           }

        } while (current_time < start_time + events[i].time   );

        // Send the MIDI event
        send_midi_event(&events[i]);
    }

    
    vTaskDelete(NULL);
}




// void read_file_from_spiffs() {
//     FILE *file = fopen(FILE_PATH, "r");
//         ESP_LOGD(TAG, "entered read from spiffs\n");

    
//     if (file == NULL) {
//         ESP_LOGE(TAG, "Failed to open file for reading");
//         return;
//     }

//     fseek(file, 0, SEEK_END);
//     long file_size = ftell(file);
//     fseek(file, 0, SEEK_SET);

//     ESP_LOGD(TAG, "midi file size %d" , (int)file_size );

//     char *file_content = (char *)malloc(file_size + 1);

//     if (file_content == NULL) {
//         ESP_LOGE(TAG, "Memory allocation error");
//         fclose(file);
//         return;
//     }

    
//     fread(file_content, 1, file_size, file);
//     file_content[file_size] = '\0';

//     fclose(file);

//     // Process the file content as needed
//     ESP_LOGD(TAG, "File Content:\n%s", file_content);

//     free(file_content);
// }

// void read_embedded_file() {
//     // Calculate the size of the embedded file
//     size_t file_size = jmayram_mid_end - jmaryam_mid_start;


//     // Access the file's data
//     printf("Embedded File Content:\n");
//     for (size_t i = 0; i < file_size; i++) {
//         putchar(jmaryam_mid_start[i]);
//     }
//     printf("\n");
// }