/**
 * @file common.h
 * @author Phil Hilger (phil@peergum.com)
 * @brief 
 * @version 0.1
 * @date 2022-12-12
 * 
 * @copyright Copyright (c) 2022, PeerGum
 * 
 */

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define MINIMUM_BATTERY_LEVEL 3300 // this is the battert level at which system will power off. TODO: Needs to be characterized
#define FAT_MOUNT_POINT "/fat"

#define ETAR_AP_SSID "Dijilele"
#define ETAR_AP_PASSWD "dijilele123"

#define SLEEP_DELAY_MINUTES 30   //sleeps after 30 mins if no activity detected and no connection to Bluetooth or USB
#define SHUTDOWN_DELAY_MINUTES 60 //sleep delay more than 60 mins causes power off