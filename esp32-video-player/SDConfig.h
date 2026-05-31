/*******************************************************************************
 * SD Card Configuration for ESP32-2432S028 (CYD)
 * 
 * SD card on VSPI bus
 * CS=5, MOSI=23, MISO=19, SCK=18
 ******************************************************************************/
#ifndef _SDCONFIG_H_
#define _SDCONFIG_H_

// SD card pins
#define SD_CS_PIN    5
#define SD_MOSI_PIN  23
#define SD_MISO_PIN  19
#define SD_SCK_PIN   18

// SPI speed
#define SD_SPI_SPEED 80000000L  // 80MHz

// MJPEG folder on SD card
#define MJPEG_FOLDER "/mjpeg"
#define MJPEG_EXTENSION ".mjpeg"

// Maximum files to track
#define MAX_MJPEG_FILES 20

#endif // _SDCONFIG_H_