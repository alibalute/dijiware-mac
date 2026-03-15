/**
 * @file spi.c
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#include "spi.h"

void spi_init(uint8_t hostId, gpio_num_t miso, gpio_num_t mosi,
              gpio_num_t sclk) {
  const spi_bus_config_t spiConfig = {
      .mosi_io_num = mosi,
      .miso_io_num = miso,
      .sclk_io_num = sclk,
      .quadhd_io_num = -1,
      .quadwp_io_num = -1,
      .max_transfer_sz = 32, // default
      .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS |
               SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_MISO |
               SPICOMMON_BUSFLAG_SCLK,
      .intr_flags = ESP_INTR_FLAG_IRAM,
  };

  ESP_ERROR_CHECK(spi_bus_initialize(hostId, &spiConfig, SPI_DMA_CH_AUTO));
  //vTaskDelay(pdMS_TO_TICKS(100));
}
