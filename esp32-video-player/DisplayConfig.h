/**
 * Display Configuration - ESP32 MJPEG Video Player
 * 
 * SELECT YOUR DISPLAY TYPE BY UNCOMMENTING THE APPROPRIATE LINE
 * Auto-detection is attempted, but explicit selection is more reliable.
 */

// ============================================
// DISPLAY SELECTION (pick one)
// ============================================
#define DISPLAY_ILI9341     // 2.8" 240x320 - most common for ESP32-2432S028 (CYD)
//#define DISPLAY_ILI9488   // 3.5" 320x480 - larger TFT displays
//#define DISPLAY_ST7789     // 2.8"-3.2" 240x320 - some CYD variants

// ============================================
// PIN CONFIGURATION (adjust for your wiring)
// ============================================
// Display pins (default for most ESP32 boards)
#define DC_PIN    2
#define CS_PIN    15
#define SCK_PIN   14
#define MOSI_PIN  13
#define MISO_PIN  12  // Not always used, set to -1 if not connected

// Backlight pin
#define BL_PIN    21   // Some boards use pin 27

// ============================================
// DISPLAY SETTINGS
// ============================================
#define DISPLAY_SPI_SPEED  80000000L   // 80MHz - works on most displays
//#define DISPLAY_SPI_SPEED 40000000L   // 40MHz - for cheaper/displays that fail at 80MHz

#define DISPLAY_ROTATION   0   // 0=normal, 1=90°, 2=180°, 3=270°
                              // Try 0 first, some displays need 2

// Display color order (usually false, some cheap displays need true)
#define DISPLAY_RGB_ORDER  0   // 0=RGB, 1=BGR (for some ILI9341 clones)
#define DISPLAY_BIG_ENDIAN 0   // 0=little-endian, 1=big-endian pixel format

// ============================================
// PRESET DISPLAY SETTINGS
// ============================================
// ILI9341 240x320 (default for Cheap Yellow Display)
#if defined(DISPLAY_ILI9341)
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#endif

// ILI9488 320x480
#if defined(DISPLAY_ILI9488)
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#endif

// ST7789 240x320
#if defined(DISPLAY_ST7789)
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#endif

// ============================================
// BUTTON PINS
// ============================================
#define BOOT_PIN   0    // BOOT button - skip to next video
#define PREV_PIN  -1    // Previous video button (optional, set to -1 to disable)
                         // Connect a button between PREV_PIN and GND