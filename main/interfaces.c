/**
 * @file interfaces.cpp
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#include "interfaces.h"
#include "esp_log.h"
#include "main.h"

static const char *TAG = "interface";

static bool hpDetected = false;
static bool cmdButtonPressed = false;
static bool externalPower = false;
static bool charging = false;
static bool powerBoost = false;
static bool accDetected = false;

bool bluetoothConnected = false; // updated in util.c

bool isHeadPhoneConnected(void) { return hpDetected; }

bool isCmdButtonPressed(void) { return cmdButtonPressed; }

bool isExternalPower(void) { return externalPower; }
bool isCharging(void) { return charging; }
bool isPowerBoost(void) { return powerBoost; }

void enableSpeaker(void) {
  ESP_LOGI(TAG, "Enable Speaker");
  mcp23s08_set_level(EX_N_SPK_MUTE, 1);
}

void disableSpeaker(void) {
  ESP_LOGI(TAG, "Disable Speaker");
  mcp23s08_set_level(EX_N_SPK_MUTE, 0);
}

void enableHeadphone(void) {
  ESP_LOGI(TAG, "Enable Headphone");
  mcp23s08_set_level(EX_N_HP_MUTE, 1);
}

void disableHeadphone(void) {
  ESP_LOGI(TAG, "Disable Headphone");
  mcp23s08_set_level(EX_N_HP_MUTE, 0);
}

void enableDAC(void) {
  ESP_LOGI(TAG, "Enable Audio");
  mcp23s08_set_level(EX_N_DAC_MUTE, 1);
}

void disableDAC(void) {
  ESP_LOGI(TAG, "Disable Audio");
  mcp23s08_set_level(EX_N_DAC_MUTE, 0);
}

void enableStrumPower(void) {
  ESP_LOGI(TAG, "Enable StrumPower");
  mcp23s08_set_level(EX_EN4V, 1); // enable strum power
}

void enable5VSwitch(void) {
  ESP_LOGI(TAG, "Enable 5V");
  mcp23s08_set_level(EX_EN5V, 1); // enable audio buck/boost
}

void disableStrumPower(void) {
  ESP_LOGI(TAG, "Disable StrumPower");
  mcp23s08_set_level(EX_EN4V, 0); // enable strum power
}

void disableAudioBoost(void) {
  ESP_LOGI(TAG, "Disable 5V");
  mcp23s08_set_level(EX_EN5V, 0); // disable audio buck/boost
}

void resetAudioChip(void) {
  ESP_LOGI(TAG, "Audio Reset");
  // low pulse
  mcp23s08_set_level(EX_N_VS_RST, 0); // RST low
  vTaskDelay(pdMS_TO_TICKS(5));
  mcp23s08_set_level(EX_N_VS_RST, 1); // RST high
  vTaskDelay(pdMS_TO_TICKS(5));
}

void setLed(gpio_num_t pin, bool level) {
  switch (pin) {
  case LED_PWR:
  case LED_BT:
    gpio_set_level(pin, !level);
    break;
  case LED_BATT_G:
  case LED_BATT_R:
    // gpio_set_level(pin, level);
    break;
  default:
    break;
  }
}

void checkInputs() {
  bool bluetoothConnected = false;
  static bool previousBluetoothConnected = false;
  bool headPhoneDetect = false;
  static bool previousHeadPhoneDetect = false;
  static bool accEnabled = false;
  uint32_t lastCheck = millis();

  cmdButtonPressed = (gpio_get_level(CMD_BTN) == 0);

  // only check expander pins every 500ms
  // if (millis()-lastCheck < 500U) {
  //   return;
  // }
  // lastCheck = millis();

  hpDetected = mcp23s08_get_level(EX_HP_DETECT); //headphone detected
  externalPower = (mcp23s08_get_level(EX_N_ACOK) == 0);   // N_ACOK: active low = power present
  charging = (mcp23s08_get_level(EX_N_CHG_DETECT) == 0);  // N_CHG_DETECT: active low = charging
  powerBoost = (mcp23s08_get_level(EX_N_BOOST) == 0);     // N_BOOST: active low = on battery boost
  // accDetected = (gpio_get_level(ACC_DETECT) > 0);

  // if (accDetected && !accEnabled) {
  //   accEnabled = true;
  //   mcp23s08_set_level(EX_N_VS_RST, 0); // put VS in reset mode
  //   enableDAC();                        // unmute DAC
  // } else if (!accDetected && accEnabled) {
  //   accEnabled = false;
  //   disableDAC();         // mute DAC
  //   vs1103b_resetAudio(); // reenable VS chip
  // }

  headPhoneDetect = isHeadPhoneConnected();
  // Headphone / Speaker Switching
  // if bluetooth connection  changed
  if (bluetoothConnected != previousBluetoothConnected) {
    previousBluetoothConnected = bluetoothConnected;

    // ESP_LOGD(TAG, "Bluetooth = %s", bluetoothConnected ? "ON" : "OFF");
    if (bluetoothConnected == false) {
      if (headPhoneDetect) { // headphone in
        disableSpeaker();
        enableHeadphone();
      } else { // headphone out
        disableHeadphone();
        enableSpeaker();
      }
    } else { // bluetooth connected
      disableHeadphone();
      disableSpeaker();
    }
    previousHeadPhoneDetect =
        headPhoneDetect; // if headphone switching happened for bluetooth
                         // don't do it again
  }
  // if headphone connection changed
  if (previousHeadPhoneDetect !=
      headPhoneDetect) // this will happen only if bluetooth connection
                       // hasnt chaged
  {
    previousHeadPhoneDetect = headPhoneDetect;
    // ESP_LOGD(TAG, "Headphone = %s", headPhoneDetect ? "ON" : "OFF");
    if (headPhoneDetect) { // headphone in
      disableSpeaker();
      enableHeadphone();
    } else { // headphone out
      disableHeadphone();
      enableSpeaker();
    }
  }
}