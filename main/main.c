/**
 * @file main.c
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-25
 *
 * @copyright Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "main.h"
#include "etar.h"
// #include "driver/timer.h"
#include "driver/gptimer.h"
#include "tla2518.h"
#include "mc3419.h"
#include "vs1103b.h"
#include "mcp23s08.h"
#include "led.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include <string.h>
#include "Wifi.h"
#include "interfaces.h"
#include "pic-midi.h"
#include "spiffs.h"
//#include "i2c_gui.h"
#include "driver/i2c.h"
#include "i2c_adc.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "blemidi.h"

// #define configCHECK_FOR_STACK_OVERFLOW 1  // this causes build error


static const char *TAG = "main";
#define PWR_BTN_HOLD_MS  500  /* hold power button this long (ms) to turn off */
#define PWR_BTN_DOUBLE_CLICK_MS  400  /* max ms between two short presses to count as double-click */

extern void handleMessage(uint8_t btCode, uint8_t btData);
extern void load_settings_at_boot(void);
extern void inputToUART(uint8_t, uint8_t, uint8_t);
extern bool isUSBConnected(void);

/** Set by etarTask when the boot intro notes have finished; main task waits for this before loading settings so MIDI does not interleave and cause spurious notes. */
extern volatile bool boot_intro_done;

static bool i2c_gui_initialized = false;

extern bool inSleepMode;

bool initialVolumeSet = false;

volatile uint32_t millisecs = 0;
static gptimer_handle_t gptimer = NULL;
bool externalPowerChange = false;
volatile bool muted = false;
char appVersion[32];

TaskHandle_t xWifiTask, xETarTask, xBatteryTask, xVolumeTask, xButtonsTask, xTimerTask, xGuiTask ;

// Semaphores
SemaphoreHandle_t xAudioSemaphore;
SemaphoreHandle_t xADCSemaphore;
SemaphoreHandle_t xExpanderSemaphore;

bool wifiOn = false;
bool wifiModeChanged = false;

bool shouldPoweroff = false;
static bool booted = false;
static uint32_t buttonTimer = 0, buttonTimerReleased = 0;
  static bool wasPressed = false, pressed = false;

/* Timer callback runs in timer/ISR context: only update millisecs here.
 * timer_check() runs in timerTask (task context) to avoid races with etarTask. */
bool timer_event_callback(gptimer_handle_t timer,
                          const gptimer_alarm_event_data_t *edata,
                          void *user_ctx) {
  if (timer == gptimer) {
    millisecs++;
  }
  return false;
}

/** Runs timer_check() every 1ms in task context to avoid races with etarTask (was previously in timer ISR). */
static void timerTask(void *pvParameter) {
  while (!shouldPoweroff) {
    timer_check();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  vTaskDelete(xTimerTask);
}

 #define I2C_SLAVE_ADDRESS 0x50

static esp_err_t i2c_slave_init()
{
    i2c_config_t conf;
    conf.mode = I2C_MODE_SLAVE;
    conf.sda_io_num = 9;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = 8;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.slave.addr_10bit_en = 0;
    conf.slave.slave_addr = I2C_SLAVE_ADDRESS;
    //conf.master.clk_speed = 400000;
    conf.clk_flags = 0;
    conf.slave.maximum_speed = 400000;
    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 128, 128, 0);
}

// //handling messages coming from the disply board through i2c
// void handleGuiMessages (uint8_t * data)
// {

//         ESP_LOGD(TAG , "handling the gui message ");
//         for (int i = 0; i < 2 ; i++) {
//             ESP_LOGD( TAG , "%02x ", data[i]);
//         }
//         //relay gui messages to handleMessage to be process consistently with the fretboard settings and the phone app
//         handleMessage(data[0], data[1]);

// }


uint32_t millis(void) { return millisecs; }

bool externalPowerChanged() { return externalPowerChange; }
void externalPowerChangeAck() { externalPowerChange = false; }

void init_timer(void) {
  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1 * 1000 * 1000, // 1µs
  };

  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

  gptimer_alarm_config_t alarm_config = {
      .alarm_count = 1000, // 1ms
      .reload_count = 0,
      .flags.auto_reload_on_alarm = true,
  };
  gptimer_set_alarm_action(gptimer, &alarm_config);

  gptimer_event_callbacks_t event_cb = {
      .on_alarm = timer_event_callback,
  };
  gptimer_register_event_callbacks(gptimer, &event_cb, NULL);

  gptimer_enable(gptimer);
  start_timer();

  // timer_config_t config = {
  //     .alarm_en = TIMER_ALARM_EN,
  //     .counter_en = TIMER_START,
  //     .intr_type = TIMER_INTR_LEVEL,
  //     .counter_dir = TIMER_COUNT_UP,
  //     .auto_reload = TIMER_AUTORELOAD_EN,
  //     .divider = 8000UL, // 80MHz/80000 = 1ms period
  //     .clk_src = TIMER_SRC_CLK_APB,
  // };
  // timer_init(TIMER_GROUP_0, TIMER_0, &config);
  // timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 10000);
  // timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_interrupt, NULL, 0);
}

void stop_timer(void) { gptimer_stop(gptimer); }
void start_timer(void) { gptimer_start(gptimer); }

void mute(bool shouldmute) {
  muted = shouldmute;
  if (muted) {
    //setVolume(0, false);
  }
}

// void guiTask(void *pvParamter) {
//   uint32_t now = millis();
//   while (!shouldPoweroff) {
//     if (millis() - now > 10000U) {
//       ESP_LOGD(TAG, "guiTask running");
//       now = millis();
//     }

//     uint8_t data[3];
//     int size = i2c_slave_read_buffer(I2C_NUM_0, data, 10, 1000 / portTICK_PERIOD_MS);
//     if (size > 0) {
//        ESP_LOGD(TAG,"Received %d bytes: ", size);
//         for (int i = 0; i < size; i++) {
//             ESP_LOGD(TAG,"%02x ", data[i]);
//         }
//         handleGuiMessages(data);

//     }
//     //send some parameters to the i2c master (display board)
//     uint8_t sendData[2] = {0,0}; 
//     uint16_t batt = readBattery();

//     sendData[0] = batt & 0x00FF;
//     sendData[1] = (batt >> 8) & 0x00FF;
//     i2c_slave_write_buffer(I2C_NUM_0, sendData, 2 , 1000 / portTICK_PERIOD_MS);
//     // ESP_LOGD(TAG,"batter level lower byte = %x \n", sendData[0] );
//     // ESP_LOGD(TAG,"batter level upper byte = %x \n", sendData[1] );

//     vTaskDelay(pdMS_TO_TICKS(200));
//   }
//   vTaskDelete(xGuiTask);
// }



void volumeTask(void *pvParamter) {
  uint8_t currentVolume = 0;
  bool wasMuted = false;
  bool usingHeadPhone = isHeadPhoneConnected();
  static bool powerChanged = false;
  uint32_t now = millis();

  ESP_LOGI(TAG, "Volume task started");
  vTaskDelay(pdMS_TO_TICKS(50));  /* ensure synth is ready for first SPI before setting volume */
  uint8_t newVolume = readVolume();
  /* Retry setVolume so no-sound boot is less likely if first SPI fails */
  bool volumeSetOk = false;
  for (int retry = 0; retry < 3; retry++) {
    if (setVolume(newVolume, false /* usingHeadPhone */) == ESP_OK) {
      volumeSetOk = true;
      if (retry > 0) ESP_LOGI(TAG, "Volume set OK on attempt %d", retry + 1);
      break;
    }
    ESP_LOGW(TAG, "Volume set failed (attempt %d), retrying", retry + 1);
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  if (!volumeSetOk) {
    ESP_LOGE(TAG, "Volume set failed after 3 retries (no-sound risk)");
  }
  vTaskDelay(pdMS_TO_TICKS(30));  /* let volume take effect before etar sends intro notes */
  /* Ping synth over SPI so we can see if it's responsive right before UART burst (no-sound boot diagnostic) */
  {
    uint16_t sci_status = 0;
    if (vs1103b_read_status(&sci_status) == ESP_OK) {
      ESP_LOGI(TAG, "synth ping OK before initialVolumeSet SCI_STATUS=0x%04x", (unsigned)sci_status);
    } else {
      ESP_LOGW(TAG, "synth ping FAILED before initialVolumeSet (SPI not ready)");
    }
  }
  initialVolumeSet = true;
  ESP_LOGI(TAG, "initialVolumeSet=1 vol=%u, deferring setVolume for 1.5s (boot)", (unsigned)newVolume);

  /* Don't call setVolume again for 1.5s so etarTask's boot MIDI burst gets xAudioSemaphore (no-sound boot fix) */
  const uint32_t bootBurstEndMs = millis() + 1500U;
  // run volume loop
  while (!shouldPoweroff) {
    if (millis() - now > 10000U) {
      ESP_LOGD(TAG, "volumeTask running");
      now = millis();
    }

    newVolume = readVolume();
    if (muted) {
      wasMuted = true;
      // muted, do nothing.
    } else if (externalPowerChange && !powerChanged) {
      // if power change, stop changing volume until calibrated
      powerChanged = true;
    } else if (!externalPowerChange &&
               (wasMuted || powerChanged ||
                ABS(newVolume, currentVolume) >=
                    5) /*  || usingHeadPhone != isHeadPhoneConnected() */) {
      /* Skip setVolume during boot burst window so uartmidi_send_message can get xAudioSemaphore */
      if (millis() >= bootBurstEndMs) {
        powerChanged = false;
        wasMuted = false;
        ESP_LOGD(TAG, "New volume: %u (prev %u)", newVolume, currentVolume);
        currentVolume = newVolume;
        uint8_t effectiveLevel = (batteryLevel > 0) ? batteryLevel : 100;
        float adjustedVolume = (float)currentVolume * (float)effectiveLevel / 100.0f;
        uint8_t vol = (uint8_t)adjustedVolume;
        /* Knob minimum ADC maps to 0–8; force full mute so volume reaches zero at minimum */
        if (currentVolume <= 8 || vol < 2) {
          vol = 0;
        }
        setVolume(vol, false /* usingHeadPhone */);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelete(xVolumeTask);
}

void muteOnPowerChange(void) {
  static bool extPower = false;
  bool newExtPower = isExternalPower();
  if (newExtPower != extPower) {
    ESP_LOGD(TAG, "Power change = %d", newExtPower);
    extPower = newExtPower;
    if (!extPower) {
      setVolume(0, false);
    }
    externalPowerChange = true;
  }
}

void poweroff(void) {
  ESP_LOGW(TAG, "Shutting down");
  powerLed(LED_OFF);
  ledLoop();
  mute(true);
  disableSpeaker();
  disableHeadphone();
  disableDAC();
  shouldPoweroff = true;
  // TODO: save whatever needs to be saved...
  vTaskDelay(pdMS_TO_TICKS(100));
  //gpio_set_direction(PWR_EN, GPIO_MODE_OUTPUT);
  #ifdef PCB_V2_2
    gpio_set_level(PWR_EN, 0);//version 2.2
  #endif
  #ifdef PCB_V2_3
    gpio_set_level(PWR_EN, 1);//version 2.3
  #endif

}

void reset(void) {
  ESP_LOGW(TAG, "Resetting");
  powerLed(LED_OFF);
  ledLoop();
  mute(true);
  disableSpeaker();
  disableHeadphone();
  disableDAC();
  shouldPoweroff = true;
  // TODO: save whatever needs to be saved...
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_restart();
}

/** Restart without power-off cleanup so boot runs like a fresh power-on. */
static void restart_fresh(void) {
  ESP_LOGI(TAG, "Fresh restart");
  esp_restart();
}

void batteryTask(void *pvParamter) {
  uint32_t currentBattery = readBattery(); //read battery voltage at the very beginning
  static bool pCharging, pPower, pBoost;
  uint32_t now = millis();
  while (!shouldPoweroff) {
    bool power = isExternalPower();
    bool boost = isPowerBoost();
    bool charging = isCharging();
    if (millis() - now > 10000U) {
      ESP_LOGD(TAG, "batteryTask running");
      now = millis();
    }
    if (inSleepMode) {
      // do nothing to avoid task conflicts
      vTaskDelay(100);
      continue;
    }
    if (boost != pBoost || power != pPower || charging != pCharging) {
      if (power != pPower) {
        // externalPowerChange = true; // will cause strums/strings
        // recalibration this is disabled now because of the false detections on
        // the charger IC
      }
      ESP_LOGI(TAG, "Power/charge state: CHG=%d ACOK=%d BOOST=%d", charging, power, boost);
      batteryLed(0, LED_OFF);
    }
    // gpio_set_level(BATT_MEAS_EN, 1);
    uint32_t newBattery = readBattery(); //read battery voltage
    ESP_LOGD(TAG, "batt = %lu", newBattery);

    if(newBattery < MINIMUM_BATTERY_LEVEL)
    {
      // Our battery level is too low to support all our electronics
      // Lets power off system
      ESP_LOGW(TAG, "Battery is at critical level. Powering off.");
      poweroff();
    }
    // gpio_set_level(BATT_MEAS_EN, 0);
    // readBattery();
    if (ABS(newBattery, currentBattery) <= 10) { //this means if the new reading is far from the previous reading, it should be ignored (like reading under load)
      ESP_LOGD(TAG, "New battery: %lu", newBattery);
      currentBattery = newBattery;
      // do something about it
    }
    if (!power) { //external power is not connected
      if (pCharging && !charging) {
        ESP_LOGI(TAG, "Power unplugged, discharging");
      }
      if (!boost && pBoost) {
        // asleep
        batteryLed(0, LED_OFF);
      } else {
        batteryLed(currentBattery, LED_SOLID ); //used to be LED_BLINK_SLOW
      }
    } else if (charging) {
      // status changed?
      if (!pCharging) {
        ESP_LOGI(TAG, "Charging");
      }
      batteryLed(currentBattery, LED_SOLID ); //used to be LED_BLINK
    } else {
      // not charging, power connected, status changed?
      if (pCharging) {
        ESP_LOGI(TAG, "Charged");
      }
      batteryLed(currentBattery, LED_SOLID);
    }
    pCharging = charging;
    pPower = power;
    pBoost = boost;


    uint8_t bytes[3];
    bytes[0] = 0x56;          //midi status code for battery level
    bytes[1] = batteryLevel; //this is calculated in led.c
    bytes[2] = 0;

    blemidi_send_message(0 , bytes, 3); //send battery level to the ble app

    vTaskDelay(pdMS_TO_TICKS(5000U));
  }
  vTaskDelete(xBatteryTask);
}

void pwrButtonCB(uint32_t duration) {
  ESP_LOGD(TAG, "PWR Button pressed for %lu ms", duration);
}

void cmdButtonCB(uint32_t duration) {
  ESP_LOGD(TAG, "CMD Button pressed for %lu ms", duration);
}

void buttonsTask(void *pvParam) {
  static bool pwrPressed = false, cmdPressed = false;
  static bool poweringOff = false, resetting = false;
  static uint32_t pwrTS = 0, cmdTS = 0, pwrDuration = 0, cmdDuration = 0;
  static uint32_t pwrFirstReleaseTS = 0;  /* for double-click detect: time of first short release, 0 = not waiting */
  uint32_t now = millis();

  #ifdef PCB_V2_2
    if (gpio_get_level(PWR_BTN) == 0) {
      // if button is already released here, then we have booted fine.
      booted = true;
    }
  #endif
  #ifdef PCB_V2_3
  if (gpio_get_level(PWR_BTN) > 0) {
    // if button is already released here, then we have booted fine.
    booted = true;
  }
  #endif


  

  while (!shouldPoweroff) {
    checkInputs();
    if (millis() - now > 10000U) {
      ESP_LOGD(TAG, "buttonsTask running");
      now = millis();
    }
  #ifdef PCB_V2_2   //in this version, power button gets high when pressed
    // check buttons
    if (gpio_get_level(PWR_BTN) > 0) {
      // pwr button pressed
      if (!pwrPressed) {
        pwrTS = millis();
        pwrPressed = true;
      } else if (booted && (millis() - pwrTS) > PWR_BTN_HOLD_MS && !poweringOff) {
        poweringOff = true;
        poweroff();
      }
    } else {
      // pwr button released
      booted = true;
      if (pwrPressed) {
        pwrDuration = millis() - pwrTS;
        uint32_t nowPwr = millis();
        pwrTS = 0;
        pwrPressed = false;
        if (pwrDuration > 50 && pwrDuration < PWR_BTN_HOLD_MS) {
          if (pwrFirstReleaseTS != 0 && (nowPwr - pwrFirstReleaseTS) <= PWR_BTN_DOUBLE_CLICK_MS) {
            pwrFirstReleaseTS = 0;
            ESP_LOGI(TAG, "Power button double-click: restarting");
            restart_fresh();
          }
          pwrFirstReleaseTS = nowPwr;
          pwrButtonCB(pwrDuration);
        } else {
          pwrFirstReleaseTS = 0;
          if (pwrDuration > 50) {
            pwrButtonCB(pwrDuration);
          }
        }
      }
    }
  #endif  

  #ifdef PCB_V2_3   //in this version, power button gets low when pressed
    // check buttons
    if (gpio_get_level(PWR_BTN) == 0) {
      // pwr button pressed
      if (!pwrPressed) {
        pwrTS = millis();
        pwrPressed = true;
      } else if (booted && (millis() - pwrTS) > PWR_BTN_HOLD_MS && !poweringOff) {
        poweringOff = true;
        poweroff();
      }
    } else {
      // pwr button released
      booted = true;
      if (pwrPressed) {
        pwrDuration = millis() - pwrTS;
        uint32_t nowPwr = millis();
        pwrTS = 0;
        pwrPressed = false;
        if (pwrDuration > 50 && pwrDuration < PWR_BTN_HOLD_MS) {
          if (pwrFirstReleaseTS != 0 && (nowPwr - pwrFirstReleaseTS) <= PWR_BTN_DOUBLE_CLICK_MS) {
            pwrFirstReleaseTS = 0;
            ESP_LOGI(TAG, "Power button double-click: restarting");
            restart_fresh();
          }
          pwrFirstReleaseTS = nowPwr;
          pwrButtonCB(pwrDuration);
        } else {
          pwrFirstReleaseTS = 0;
          if (pwrDuration > 50) {
            pwrButtonCB(pwrDuration);
          }
        }
      }
    }
  #endif  


  // Check button
  if (isCmdButtonPressed()){ // down button pressed
    if (!buttonTimer)
    {
      buttonTimer = millis();
      buttonTimerReleased = 0;
      pressed = true;
      wasPressed = false;
      ESP_LOGD(TAG, "pressed");
    }
  }
  else if (pressed && !isCmdButtonPressed()){
    buttonTimerReleased = millis();
    wasPressed = true;
    pressed = false;
    ESP_LOGD(TAG, "released");
  }
  else if (wasPressed && (buttonTimerReleased - buttonTimer) >= 400){
    buttonTimerReleased = 0;
    buttonTimer = 0;
    wasPressed = 0;
    ESP_LOGD(TAG, "chord");
    //toggle chord mode
    if(chordEnabled == false)
      handleMessage(0x35, 1); // chord mode enabled
    else
      handleMessage(0x35, 0); // chord mode  disabled
    
  }
  else if (wasPressed && (buttonTimerReleased - buttonTimer < 400 )){ // command button released in less than 400ms , so enter command mode

    buttonTimerReleased = 0;
    buttonTimer = 0;
    wasPressed = false;
    ESP_LOGD(TAG, "command");

    if (deviceState == PLAY_STATE){

      // turn off all sound
      for (int q = 0; q < 3; q++)
      {
  #ifdef uart_en
        inputToUART(0xB0 + q, 0x78, 0x00);
  #endif
        if (isUSBConnected())
        {
          midiTx(0xB, 0XB0 + q, 0x78, 0x00);
        }
      }
      //play a sound to show we are in command mode
      inputToUART(0x99, 61, 127);
      vTaskDelay(pdMS_TO_TICKS(100));
      inputToUART(0x89, 61, 127);


      deviceState = UI_STATE; // change this to UI_STATE if want to enable
                              // the UI_STATE
                              // stateChanged = true;
      ESP_LOGD(TAG, "UI State");
      // mute(true);
      powerLed(LED_BLINK_EXTRA_FAST);
    }
    else{ //UI_STATE
      mute(false);
      // Change to Play State
      if (isUSBConnected())
      {
        midiTx(0x3, 0xFA, 1, 0);
      }
      inputToUART(0xF3, 0x01, 0x00);

      inputToUART(0x99, 61, 127);
      vTaskDelay(pdMS_TO_TICKS(500));
      inputToUART(0x89, 61, 127);

      /* mLED_2_On(); */
      deviceState = PLAY_STATE;
      ESP_LOGD(TAG, "Play State");
      powerLed(LED_SOLID);
      // stateChanged = true;
    }
      
  }



    if (isCmdButtonPressed()) {
      // cmd button pressed
      if (!cmdPressed) {
        cmdTS = millis();
        cmdPressed = true;
      } else if (millis() - cmdTS > 5000 && !wifiModeChanged) {
        wifiOn = !wifiOn;
        wifiModeChanged = true;
        ESP_LOGD(TAG, "Wifi mode %s ", wifiOn ? " ACTIVATED" : "DESACTIVATED");
      }
    } else {
      // cmd button released
      if (cmdPressed) {
        cmdDuration = millis() - cmdTS;
        if (cmdDuration > 50) {
          cmdButtonCB(cmdDuration);
        }
        cmdPressed = false;
        cmdTS = 0;
        wifiModeChanged = false;
      }
    }
    ledLoop();
    vTaskDelay(pdMS_TO_TICKS(100)); //used to be 50
  }
  vTaskDelete(xButtonsTask);
}

void wifiTask(void *arg) {
  uint32_t now = millis();
  while (!shouldPoweroff) {
    if (millis() - now > 10000U) {
      ESP_LOGD(TAG, "wifiTask running");
      now = millis();
    }
    if (wifiOn) {
      if (!wifiInit()) {
        ESP_LOGE(TAG, "WiFi init failed, skipping WiFi start");
        wifiOn = false;
      } else {
        ESP_LOGI(TAG, "Starting WIFI");
        if (!wifiStart()) {
          ESP_LOGE(TAG, "WiFi start failed");
          wifiOn = false;
        } else {
          powerLed(LED_BLINK_EXTRA_FAST);
          while (wifiOn) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (millis() - now > 10000U) {
              ESP_LOGD(TAG, "wifiTask running");
              now = millis();
            }
          }
          ESP_LOGI(TAG, "Stopping WIFI");
          if (!wifiStop()) {
            ESP_LOGW(TAG, "WiFi stop failed");
          }
        }
      }
      powerLed(LED_SOLID);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  vTaskDelete(xWifiTask);
}

void startTasks(void) {
  /* Create volume task first so it can set initial volume before other tasks
   * use the audio path; yield so it runs and sets initialVolumeSet for etarTask. */
  xTaskCreate(volumeTask, "volumeTask", 4096, NULL, 8, &xVolumeTask);
  assert(xVolumeTask != NULL);
  vTaskDelay(pdMS_TO_TICKS(200));  /* let volumeTask and UART MIDI task init before etarTask runs */

  init_usb_serial();
  start_usb_midi_task();
  start_uart_midi_task();
  /* Prime synth UART with a few harmless bytes so any "first-byte loss" hits these, not etar's burst (no-sound boot fix) */
  vTaskDelay(pdMS_TO_TICKS(80));
  for (int q = 0; q < 5; q++) {
    inputToUART(0xB0, 0x07, 127);
    vTaskDelay(pdMS_TO_TICKS(25));
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  start_ble_midi_task(LED_BT);

  xTaskCreate(buttonsTask, "buttonsTask", 4096, NULL, 6, &xButtonsTask);   /* below audio; UI can tolerate a few ms */
  assert(xButtonsTask != NULL);
  xTaskCreate(etarTask, "etarTask", 10240, &xETarTask, 8 , &xETarTask);//eTarTask function is in etar.c so in order to have access to the task handle, we have to pass it to the function 
  assert(xETarTask != NULL);
  xTaskCreate(batteryTask, "batteryTask", 4096, NULL, 6, &xBatteryTask);  /* below audio; runs every 5s */
  assert(xBatteryTask != NULL);
  xTaskCreate(wifiTask, "wifiTask", 8192, NULL, 6, &xWifiTask);           /* below audio; background */
  assert(xWifiTask != NULL);
  xTaskCreate(timerTask, "timerTask", 2048, NULL, 7, &xTimerTask);        /* runs timer_check() every 1ms in task context */
  assert(xTimerTask != NULL);
  // xTaskCreate(guiTask, "guiTask", 4096 , NULL, 8, &xGuiTask);
  // assert(xGuiTask != NULL);
}



void app_main(void) {


  gpio_set_direction(PWR_EN, GPIO_MODE_OUTPUT);
  #ifdef PCB_V2_2
    gpio_set_level(PWR_EN, 1);
  #endif
    #ifdef PCB_V2_3
    gpio_set_level(PWR_EN, 0);
  #endif


  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("BLE", ESP_LOG_INFO);
  esp_log_level_set("USB", ESP_LOG_INFO);
  esp_log_level_set("pic-midi", ESP_LOG_DEBUG);
  esp_log_level_set("main", ESP_LOG_DEBUG);
  esp_log_level_set("util", ESP_LOG_DEBUG);


  const esp_app_desc_t *appDesc = esp_ota_get_app_description();
  strcpy(appVersion, appDesc->version);
  ESP_LOGD(TAG, "App version %s", appVersion);

  gpio_init();

  /* Turn on power and BT LEDs as soon as GPIO is ready (before slow inits) */
  gpio_set_level(LED_PWR, 0);  /* power LED on early to avoid late boot appearance */
  gpio_set_level(LED_BT, 0);

  // ESP ADC calibration
  adc_calibration_init();

  // setLed(LED_BATT_G, 1);
  // setLed(LED_BATT_R, 1);



  gpio_set_level(N_AXL_CS, 1);
  gpio_set_level(N_ADC_CS, 1);
  gpio_set_level(N_VS_CS, 1);
  gpio_set_level(N_VS_XDCS, 1);
  gpio_set_level(N_EX1_CS, 1);
  gpio_set_level(N_EX2_CS, 1);
  gpio_set_level(N_SD_CS, 1);
  gpio_set_level(SCLK, 1);
  gpio_set_level(MOSI, 1);
  gpio_set_level(VS_SCLK, 1);
  gpio_set_level(VS_MOSI, 1);

  // gpio_set_level(CODEC_PWR_EN, 1);
  spi_init(SPI2_HOST,MISO,MOSI,SCLK);
  spi_init(SPI3_HOST,VS_MISO,VS_MOSI,VS_SCLK);
  vTaskDelay(pdMS_TO_TICKS(50));
  mcp23s08_init(SPI2_HOST);

  disableSpeaker();
  disableHeadphone();
  disableDAC();
  enableStrumPower();
  //enable5VSwitch();
  disableAudioBoost();//disable 5V DC/DC

  tla2518_init(SPI2_HOST);
  mc3419_init(SPI2_HOST);

  vs1103b_init(SPI3_HOST);
  vTaskDelay(pdMS_TO_TICKS(100));  /* let synth chip stabilize after init for consistent boot (no sound) */
  if (isHeadPhoneConnected())
  {
    disableSpeaker();
    enableHeadphone();
  }
  else
  {
    disableHeadphone();
    enableSpeaker();
  }
  // while (1) {
  //   sampleChannels(0xff);
  //   vTaskDelay(pdMS_TO_TICKS(1000));
  // }



  init_spiffs();

 // Initialize i2c in slave mode (NOTE: eTar doesnt like installing i2c as master) this is for gui display (not present now)
  // esp_err_t ret = i2c_slave_init();
  // if (ret == ESP_OK)
  // {
  //   ESP_LOGD(TAG,"I2C installed and initialized successfully as slave \n");
  // }

//i2c_gui_initialized = true;






  init_timer();
  startTasks();

  ESP_LOGI(TAG, "main: waiting for initialVolumeSet (max 5s)");
  int waitCount = 0;
  for (; waitCount < 50 && !initialVolumeSet; waitCount++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  ESP_LOGI(TAG, "main: initialVolumeSet=%d after %d x 100ms", initialVolumeSet ? 1 : 0, waitCount);
  initLeds();
  powerLed(LED_SOLID); //
  gpio_set_level(LED_BT, 1); //turn off bt led when booted
  ledLoop();

  /* Wait for intro notes to finish so load_settings_at_boot() does not send MIDI
   * in parallel with the intro; that interleaving causes 1–2 spurious notes. */
  for (int wait = 0; wait < 80 && !boot_intro_done; wait++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  load_settings_at_boot();

  uint32_t main_last_hb = 0;
  while (1) {
    if (millis() - main_last_hb >= 5000U) {
      ESP_LOGI(TAG, "freeze heartbeat main");
      main_last_hb = millis();
    }
    ESP_LOGD(TAG, "Heartbeat");
    // esp_task_wdt_reset();

    // this was to get the state of eTar task
    // Get the state of the task
    // eTaskState taskState = eTaskGetState(xETarTask);

    // // Check the state of the task
    // switch(taskState) {
    //     case eRunning:
    //         // Task is currently running
    //             ESP_LOGD(TAG, "eTarTask runing");
    //         break;
    //     case eReady:
    //         // Task is ready to run but not running
    //         break;
    //     case eBlocked:
    //         // Task is blocked waiting for an event or semaphore
    //         ESP_LOGD(TAG, "eTarTask blocked");

    //         break;
    //     case eSuspended:
    //         // Task is suspended
    //         ESP_LOGD(TAG, "eTarTask suspended");

    //         break;
    //     case eDeleted:
    //         // Task has been deleted
    //         ESP_LOGD(TAG, "eTarTask deleted");
    //         break;
    //     default:
    //         // Handle error or unknown state
    //         ESP_LOGD(TAG, "eTarTask unknownstate");
    //         break;
    // }


    vTaskDelay(pdMS_TO_TICKS(1000));  /* 1s so freeze heartbeat runs every ~5s */


  }
}

// // Stack overflow handler function
// void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
//     // Stack overflow detected for task 'pcTaskName'
//     // You can handle the stack overflow event here
//     // For example, print an error message or take corrective action
//     printf("Stack overflow detected for task '%s'\n", pcTaskName);

//     // You might choose to reset the system or restart the task, depending on your application requirements
// }