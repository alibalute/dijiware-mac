/**
 * @file led.c
 * @author Phil Hilger, Waterloo Tech (phil.hilger@waterlooti.com)
 * @brief 
 * @version 0.1
 * @date 2022-09-22
 * 
 * @copyright Copyright (c) 2022 WTI
 * 
 */

#include "led.h"
#include "driver/ledc.h"
#include "pins.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "Led";

static LedMode powerLedMode = LED_SOLID, batteryLedMode = LED_OFF;
static uint32_t batteryVoltage = 0;
uint8_t batteryLevel = 0;

bool IRAM_ATTR fadeCB(const ledc_cb_param_t *param, void *user_arg);

void initLeds() {
  esp_log_level_set("ledc", ESP_LOG_WARN);
  ledc_channel_config_t batteryLedGConfig = {.channel = LEDC_CHANNEL_0,
                                             .gpio_num = LED_BATT_G,
                                             .speed_mode = LEDC_LOW_SPEED_MODE,
                                             .timer_sel = LEDC_TIMER_0,
                                             .flags.output_invert = 0,
                                             .duty = 0,
                                             .hpoint = 0};
  ledc_channel_config_t batteryLedRConfig = {.channel = LEDC_CHANNEL_1,
                                             .gpio_num = LED_BATT_R,
                                             .speed_mode = LEDC_LOW_SPEED_MODE,
                                             .timer_sel = LEDC_TIMER_0,
                                             .flags.output_invert = 0,
                                             .duty = 0,
                                             .hpoint = 0};

  ledc_channel_config_t powerLedConfig = {.channel = LEDC_CHANNEL_2,
                                             .gpio_num = LED_PWR,
                                             .speed_mode = LEDC_LOW_SPEED_MODE,
                                             .timer_sel = LEDC_TIMER_0,
                                             .flags.output_invert = 1,
                                             .duty = 0,
                                             .hpoint = 0};

  // ledc_timer_config_t batteryLedTimerConfig = {
  //     .speed_mode = LEDC_LOW_SPEED_MODE,
  //     .duty_resolution = LEDC_TIMER_8_BIT,
  //     .timer_num = LEDC_TIMER_0,
  //     .freq_hz = 1000,
  //     .clk_cfg = LEDC_AUTO_CLK,
  // };

  // ESP_ERROR_CHECK(ledc_timer_config(&batteryLedTimerConfig));

  ledc_timer_config_t ledTimerConfig = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 1000,
      .clk_cfg = LEDC_AUTO_CLK,
  };

  ESP_ERROR_CHECK(ledc_timer_config(&ledTimerConfig));

  ESP_ERROR_CHECK(ledc_channel_config(&batteryLedGConfig));
  ESP_ERROR_CHECK(ledc_channel_config(&batteryLedRConfig));

  ESP_ERROR_CHECK(ledc_channel_config(&powerLedConfig));
  ESP_ERROR_CHECK(ledc_fade_func_install(0));

  ledc_cbs_t fadeCallback = {
    .fade_cb = fadeCB,
  };
  ledc_cb_register(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, &fadeCallback, NULL);
  ledc_cb_register(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, &fadeCallback, NULL);
}

volatile bool fading[LEDC_CHANNEL_MAX];
volatile bool direction[LEDC_CHANNEL_MAX];

bool IRAM_ATTR fadeCB(const ledc_cb_param_t *param, void *user_arg) {
  int channel = param->channel;
  direction[channel] = (direction[channel] == false);
  fading[channel] = false;
  return true;
}

void batteryLed(uint32_t voltage, LedMode mode) {
  // return; // TEMPORARY
  batteryLedMode = mode;
  batteryVoltage = voltage;

  batteryLevel = (uint8_t)( 100*((float)voltage - 3400.0)/(4000.0-3400.0)); //from 100 to 0  max=4000  min=3400
  if (batteryLevel > 100)
    batteryLevel = 100;

  uint8_t r, g;
  if (mode == LED_OFF) {
    g = 0;
    r = 0;
  }
  else{

    g = batteryLevel ;
    r = (100 - batteryLevel)*2; //since red LED is dimmer than the green one, more weight was given to r comparing to g
  
 
  //   //since red LED is dimmer than the green one, more weight was given to r comparing to g
  //   if (voltage >= 4050) { //based on experiment 4050 was the maximum batt voltage when fully charged
  //     // green 100%, red 0%
  //     g = 50;
  //     r = 0;
  //     batteryLevel = 10; //full
  //   } else if (voltage >= 4000 && voltage < 4050) {
  //     g = 45;
  //     r = 20;
  //     batteryLevel = 9; 
  //   } else if (voltage >= 3950 && voltage < 4000) {
  //     g = 40;
  //     r = 30;
  //     batteryLevel = 8; 
  //   } else if (voltage >= 3900 && voltage < 3950) {
  //     g = 35;
  //     r = 40;
  //     batteryLevel = 7; 
  //   } else if (voltage >= 3850 && voltage < 3900) {
  //     g = 30;
  //     r = 50;
  //     batteryLevel = 6; 
  //   } else if (voltage >= 3800 && voltage < 3850) {
  //     g = 25;
  //     r = 60;
  //     batteryLevel = 5; 
  //   } else if (voltage >= 3770 && voltage < 3800) {
  //     g = 20;
  //     r = 70;
  //     batteryLevel = 4; 
  //   } else if (voltage >= 3730 && voltage < 3770) {
  //     g = 10;
  //     r = 85;
  //     batteryLevel = 3; 
  //   } else if (voltage >= 3700 && voltage < 3730) {
  //     g = 5;
  //     r = 95;
  //     mode = LED_BLINK_FAST;
  //     batteryLevel = 2; 
  //   } else if (voltage >= 3680 && voltage < 3700) {
  //     g = 5;
  //     r = 100;
  //     mode = LED_BLINK_FAST;
  //     batteryLevel = 1; 
  //   } else  {   //i.e. < 3690
  //     g = 0;
  //     r = 100;
  //     //mode = LED_BLINK_FAST;
  //     batteryLevel = 0; 
  //   } 
   }
  
  if (mode == LED_SOLID || mode == LED_OFF) {
    // ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    // ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, g);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, r);
    fading[LEDC_CHANNEL_0] = true;
    fading[LEDC_CHANNEL_1] = true;
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  } else {
    uint32_t period = 2000;
    switch (mode) {
    case LED_BLINK_SLOW:
      period = 5000;
      break;
    case LED_BLINK_FAST:
      period = 1000;
      break;
    case LED_BLINK_EXTRA_FAST:
      period = 400;
      break;
    case LED_BLINK:
      period = 2000;
    default:
      break;
    }
    if (!fading[LEDC_CHANNEL_0]) {
      // handle channels 0 and 1 synchronously
      if (g>0) {
        // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
        //               direction[LEDC_CHANNEL_0] == false ? 0 : g);
        ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                    direction[LEDC_CHANNEL_0] == false ? 255 : 0,
                                    period/2,
                                    LEDC_FADE_NO_WAIT);
        fading[LEDC_CHANNEL_0] = true;
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      }
      if (r > 0) {
        // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1,
        //               direction[LEDC_CHANNEL_1] == false ? 0 : r);
        ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1,
                                     direction[LEDC_CHANNEL_0] == false ? 255
                                                                        : 0,
                                     period / 2, LEDC_FADE_NO_WAIT);
        // fading[LEDC_CHANNEL_1] = true;
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
      }
    }
  }
}

void powerLed(LedMode mode) {
  powerLedMode = mode;
  uint8_t g;
  if (mode == LED_SOLID || mode == LED_OFF) {
    g = (mode == LED_OFF) ? 0 : 50U; //used to be 255U which is very bright
    // ESP_LOGD(TAG, "Power LED solid");
    // ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, g);
    fading[LEDC_CHANNEL_2] = false;
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
  } else {
    g = 50U; //used to be 255U which is very bright
    // ESP_LOGD(TAG, "Power LED blinking");
    uint32_t period = 1000;
    switch (mode) {
    case LED_BLINK_SLOW:
      period = 5000;
      break;
    case LED_BLINK_FAST:
      period = 1000;
      break;
    case LED_BLINK:
      period = 2000;
    default:
      break;
    }
    if (!fading[LEDC_CHANNEL_2]) {
      // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, direction[LEDC_CHANNEL_2] == false ? 0 : 255);
      ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2,
                                   direction[LEDC_CHANNEL_2] == false ? 5 : 0, //used to be 255 which is very bright
                                   period / 2, LEDC_FADE_NO_WAIT);
      fading[LEDC_CHANNEL_2] = true;
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
    }
  }
}

void ledLoop() {
  if (!fading[LEDC_CHANNEL_0] && batteryLedMode != LED_SOLID && batteryLedMode != LED_OFF) {
    // ESP_LOGD(TAG, "battery led call");
    batteryLed(batteryVoltage, batteryLedMode);
  }
  if (!fading[LEDC_CHANNEL_2] && powerLedMode != LED_SOLID && powerLedMode != LED_OFF) {
    // ESP_LOGD(TAG, "power led call");
    powerLed(powerLedMode);
  }
}