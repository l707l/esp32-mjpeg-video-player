/**
 * SD Card Configuration - ESP32 MJPEG Video Player
 * 
 * Supports both SPI and SDMMC modes
 * Default is SPI mode (most compatible)
 */

// ============================================
// SD CARD MODE
// ============================================
// Choose one: USE_SPI_MODE or USE_SD_MMC_MODE
// SPI is more compatible but slower
// SDMMC is faster but requires specific pin configuration

#define USE_SPI_MODE       // Default - uses VSPI bus
//#define USE_SD_MMC_MODE  // 1-bit SDMMC mode (uses specific pins)

// ============================================
// SPI MODE PINS (for USE_SPI_MODE)
// ============================================
#define SD_CS    5
#define SD_MISO  19
#define SD_MOSI  23
#define SD_SCK   18

#define SD_SPI_SPEED 80000000L   // 80MHz - works with most cards

// ============================================
// SDMMC MODE PINS (for USE_SD_MMC_MODE)
// ============================================
// Default SDMMC pins for ESP32:
// - DATA0: 2
// - CMD:   15  
// - CLK:   14
// Note: SDMMC uses 1-bit mode by default
// No additional pins needed (uses built-in defaults)

// ============================================
// SD CARD SETTINGS
// ============================================
#define SD_MOUNT_PATH  "/sd"       // Mount point for SD card
#define SD_MAX_FILES   50         // Maximum files to list
#define SD_MAX_FOLDERS 10         // Maximum subfolders to scan