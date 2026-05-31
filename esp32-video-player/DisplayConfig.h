/*******************************************************************************
 * Display Configuration for ESP32-2432S028 (CYD)
 * 
 * Hardware: ILI9341 240x320 SPI display
 * SPI: DC=2, CS=15, MOSI=13, MISO=12, SCK=14
 * BL=21 (backlight control)
 ******************************************************************************/
#ifndef _DISPLAYCONFIG_H_
#define _DISPLAYCONFIG_H_

// Display pins
#define DISPLAY_DC_PIN    2
#define DISPLAY_CS_PIN    15
#define DISPLAY_MOSI_PIN  13
#define DISPLAY_MISO_PIN  12
#define DISPLAY_SCK_PIN   14
#define DISPLAY_BL_PIN    21

// SPI speeds (Hz)
#define DISPLAY_SPI_SPEED 80000000L    // 80MHz for display
#define DISPLAY_SPI_MODE  SPI_MODE0

// Display dimensions
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

// Frame buffer size (RGB565)
#define FRAME_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)

// Color definitions
#define RGB565_BLACK   0x0000
#define RGB565_WHITE   0xFFFF
#define RGB565_RED     0xF800
#define RGB565_GREEN   0x07E0
#define RGB565_BLUE    0x001F

#endif // _DISPLAYCONFIG_H_