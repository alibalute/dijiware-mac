/**
 * @file spiffs.h
 * @author Ali Tehrani
 * @brief
 * @version 0.1
 * @date 2023-03-17
 *
 * Copyright (c) 2023 Elemental ID
 *
 */

#ifndef __SPIFFS_H_
#define __SPIFFS_H_

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

void init_spiffs();

#endif // __SPIFFS_H_