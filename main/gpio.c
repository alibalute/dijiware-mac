/**
 * @file gpio.cpp
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#include "gpio.h"
#include "pins.h"
#include "soc/io_mux_reg.h"
#include "esp_log.h"

static const char *TAG = "gpio";

// #include "soc/gpio_sig_map.h"

void gpio_init(void)
{

  // floating output pins
  gpio_config_t outputFloatingPins = {
      .pin_bit_mask = (1ULL << MIDI_TX)   |
                       (1ULL << LED_BATT_G) | (1ULL << LED_BATT_R) 
      ,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&outputFloatingPins));

  ESP_LOGI(TAG, "WAIT2");
  // vTaskDelay(pdMS_TO_TICKS(1000));

  // gpio_iomux_out(N_ADC_CS, FUNC_GPIO0_GPIO0, false);
  // gpio_iomux_out(N_VS_CS, FUNC_GPIO36_GPIO36, false);

  // pull-ups
  gpio_config_t outputPullUpPins = {
      .pin_bit_mask = (1ULL << LED_PWR) |
                      (1ULL << LED_BT) | (1ULL << N_AXL_CS) | (1ULL << N_VS_CS) | (1ULL << N_VS_XDCS) |
                      (1ULL << N_EX1_CS) | (1ULL << N_EX2_CS) | (1ULL << N_ADC_CS) |
                      (1ULL << N_SD_CS) | (1ULL << SCLK) | (1ULL << MOSI) |
                      (1ULL << PWR_EN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&outputPullUpPins));

  ESP_LOGI(TAG, "WAIT3");
  // vTaskDelay(pdMS_TO_TICKS(3000));

  // //pull - downs
  // gpio_config_t outputPullDownPins = {
  //     .pin_bit_mask = /* (1ULL << BATT_MEAS_EN) | */ /* (1ULL << PWR_EN) | */
  //     /* (1ULL << HP_EN) | */                        /* (1ULL << CODEC_PWR_EN) | */
  //     /* (1ULL << N_SPK_MUTE) | (1ULL << SCLK) |  (1ULL << MIDI_TX) */,
  //     .mode = GPIO_MODE_OUTPUT,
  //     .pull_up_en = GPIO_PULLUP_DISABLE,
  //     .pull_down_en = GPIO_PULLDOWN_ENABLE,
  //     .intr_type = GPIO_INTR_DISABLE,
  // };
  // ESP_ERROR_CHECK(gpio_config(&outputPullDownPins));

  // define input pins
  gpio_config_t inputFloatingPins = {
      .pin_bit_mask = (1ULL << MIDI_RX) | (1ULL << PWR_BTN) |
                      (1ULL << CMD_BTN) | (1ULL << MISO) | (1ULL << ACC_DETECT) /*  | (1ULL << EX_INT) */,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&inputFloatingPins));

  gpio_config_t inputPullUpPins = {
      .pin_bit_mask = (1ULL << VS_DREQ),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&inputPullUpPins));

  // gpio_config_t inputPullDownPins = {
  //     .pin_bit_mask = /* (1ULL << MISO) |  */(1ULL << ACC_DETECT),
  //     .mode = GPIO_MODE_INPUT,
  //     .pull_up_en = GPIO_PULLUP_DISABLE,
  //     .pull_down_en = GPIO_PULLDOWN_ENABLE,
  //     .intr_type = GPIO_INTR_DISABLE,
  // };
  // ESP_ERROR_CHECK(gpio_config(&inputPullDownPins));

  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "WAIT4");
  // vTaskDelay(pdMS_TO_TICKS(1000));

  gpio_set_level(N_AXL_CS, 1);
  gpio_set_level(N_ADC_CS, 1);
  gpio_set_level(N_VS_CS, 1);
  gpio_set_level(N_VS_XDCS, 1);
  gpio_set_level(N_EX1_CS, 1);
  gpio_set_level(N_EX2_CS, 1);
  gpio_set_level(N_SD_CS, 1);
  gpio_set_level(SCLK, 1);
  gpio_set_level(LED_BT, 0); //keep this LED on at boot, otherwise it will glitches off
  gpio_set_level(LED_PWR, 0);//keep this LED on at boot, otherwise it will glitches off
  // gpio_set_level(LED_BATT_R, 0);
  // gpio_set_level(LED_BATT_G, 0);

  gpio_wakeup_enable(PWR_BTN, GPIO_INTR_HIGH_LEVEL);
}
