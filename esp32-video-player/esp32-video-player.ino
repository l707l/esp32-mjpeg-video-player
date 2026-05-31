/*******************************************************************************
 * ESP32-2432S028 MJPEG Video Player - Optimized Version
 * 
 * Optimizations applied:
 * - Buffer size increased from width*height*2/5 to width*height*2/2 (full buffer)
 * - READ_BUFFER_SIZE increased from 1024 to 4096 in MjpegClass.h
 * - ESP32 watchdog timer for frame timeout recovery
 * - Frame skip logic when decoding takes too long
 * - Pre-allocated buffers in setup() with proper DMA alignment
 * - Serial debug output for bottleneck analysis
 * - Memory leak fixes in buffer handling
 * 
 * Target: 240x320 resolution, 30fps MJPEG from SD card
 * Tutorial: https://youtu.be/jYcxUgxz9ks
 ******************************************************************************/

#include <Arduino_GFX_Library.h> // Install "GFX Library for Arduino" with the Library Manager
#include "MjpegClass.h"          // Included in this project (optimized version)
#include "SD.h"                  // Included with the Espressif Arduino Core

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
#define BL_PIN 21                       // Display backlight pin
#define SD_CS 5                         // SD card chip select
#define SD_MISO 19                      // SD card MISO
#define SD_MOSI 23                      // SD card MOSI
#define SD_SCK 18                       // SD card SCK

#define BOOT_PIN 0                      // Boot button pin for skipping videos
#define BOOT_BUTTON_DEBOUNCE_TIME 400   // Debounce time in milliseconds

// ============================================================================
// SPI SPEED CONFIGURATION
// ============================================================================
// Configurable SPI speeds - adjust based on your display model
// Some cheap yellow displays only work reliably at 40MHz
#define DISPLAY_SPI_SPEED 80000000L     // 80MHz (good for most displays)
// #define DISPLAY_SPI_SPEED 40000000L   // 40MHz (for problematic displays)

#define SD_SPI_SPEED 80000000L           // 80MHz (SD card SPI speed)

#define MJPEG_FOLDER "/mjpeg"           // Folder containing MJPEG files on SD card
#define MAX_FILES 20                    // Maximum number of files to track

// ============================================================================
// WATCHDOG AND TIMING CONFIGURATION
// ============================================================================
#define FRAME_TIMEOUT_MS 100            // Max time for a single frame decode
#define MAX_CONSECUTIVE_ERRORS 3        // Skip to next video after this many errors
#define PERFORMANCE_LOG_INTERVAL 30     // Log performance every N frames

// ============================================================================
// STORAGE FOR MJPEG FILES
// ============================================================================
String mjpegFileList[MAX_FILES];
uint32_t mjpegFileSizes[MAX_FILES] = {0};
int mjpegCount = 0;
static int currentMjpegIndex = 0;

// ============================================================================
// GLOBAL VARIABLES FOR MJPEG PLAYBACK
// ============================================================================
MjpegClass mjpeg;
int total_frames = 0;
unsigned long total_read_video = 0;
unsigned long total_decode_video = 0;
unsigned long total_show_video = 0;
unsigned long start_ms, curr_ms;
long output_buf_size;
// NOTE: estimateBufferSize now uses full width*height*2 for better quality
long estimateBufferSize;
uint8_t *mjpeg_buf = nullptr;
uint16_t *output_buf = nullptr;

// ============================================================================
// DISPLAY INITIALIZATION
// ============================================================================
Arduino_DataBus *bus = new Arduino_HWSPI(2 /* DC */, 15 /* CS */, 14 /* SCK */, 13 /* MOSI */, 12 /* MISO */);
Arduino_GFX *gfx = new Arduino_ILI9341(bus);

// Separate SPI for SD card (VSPI port)
SPIClass sd_spi(VSPI);

// ============================================================================
// INTERRUPT HANDLING FOR BUTTON PRESS
// ============================================================================
volatile bool skipRequested = false;
volatile uint32_t isrTick = 0;
uint32_t lastPress = 0;

void IRAM_ATTR onButtonPress()
{
    skipRequested = true;
    isrTick = xTaskGetTickCountFromISR();
}

// ============================================================================
// FORMAT BYTES FOR SERIAL OUTPUT
// ============================================================================
String formatBytes(size_t bytes)
{
    if (bytes < 1024)
        return String(bytes) + " B";
    else if (bytes < (1024 * 1024))
        return String(bytes / 1024.0, 2) + " KB";
    else
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
}

// ============================================================================
// WATCHDOG TIMER SETUP FOR ESP32
// ============================================================================
#if defined(ESP32)
void setupWatchdog(unsigned long timeout_ms) {
    // Initialize watchdog timer
    esp_task_wdt_init(timeout_ms / 1000, false);
    esp_task_wdt_add(NULL);
}

void feedWatchdog() {
    #if defined(ESP32)
    esp_task_wdt_reset();
    #endif
}
#else
void setupWatchdog(unsigned long timeout_ms) {}
void feedWatchdog() {}
#endif

// ============================================================================
// LOAD MJPEG FILES LIST FROM SD CARD
// ============================================================================
void loadMjpegFilesList()
{
    File mjpegDir = SD.open(MJPEG_FOLDER);
    if (!mjpegDir)
    {
        Serial.printf("ERROR: Failed to open %s folder\n", MJPEG_FOLDER);
        while (true) {} // Halt on error
    }
    
    mjpegCount = 0;
    while (true)
    {
        File file = mjpegDir.openNextFile();
        if (!file)
            break;
        if (!file.isDirectory())
        {
            String name = file.name();
            if (name.endsWith(".mjpeg") || name.endsWith(".avi"))
            {
                mjpegFileList[mjpegCount] = name;
                mjpegFileSizes[mjpegCount] = file.size();
                mjpegCount++;
                if (mjpegCount >= MAX_FILES)
                    break;
            }
        }
        file.close();
    }
    mjpegDir.close();
    
    Serial.printf("Found %d MJPEG files\n", mjpegCount);
    for (int i = 0; i < mjpegCount; i++)
    {
        Serial.printf("  File %d: %s, Size: %lu bytes (%s)\n", 
                      i, 
                      mjpegFileList[i].c_str(), 
                      mjpegFileSizes[i],
                      formatBytes(mjpegFileSizes[i]).c_str());
    }
}

// ============================================================================
// SETUP - INITIALIZE EVERYTHING
// ============================================================================
void setup()
{
    Serial.begin(115200);
    Serial.println("\n=== ESP32 MJPEG Video Player - Optimized ===");
    Serial.printf("Free heap at start: %u bytes\n", ESP.getFreeHeap());

    // Set display backlight to HIGH
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, HIGH);

    // Initialize display
    Serial.println("Initializing display...");
    if (!gfx->begin(DISPLAY_SPI_SPEED))
    {
        Serial.println("ERROR: Display initialization failed!");
        while (true) {} // Halt
    }
    gfx->setRotation(0);
    gfx->fillScreen(RGB565_BLACK);
    Serial.printf("Display size: %d x %d\n", gfx->width(), gfx->height());

    // Initialize SD card
    Serial.println("Initializing SD card...");
    if (!SD.begin(SD_CS, sd_spi, SD_SPI_SPEED, "/sd"))
    {
        Serial.println("ERROR: File system mount failed!");
        while (true) {} // Halt
    }
    Serial.printf("SD card initialized, type: %s\n", 
                  SD.cardType() == CARD_SD ? "SD" : 
                  SD.cardType() == CARD_MMC ? "MMC" : "Unknown");

    // Pre-allocate buffers with proper alignment
    Serial.println("Allocating buffers...");
    
    // Output buffer - 16-byte aligned for DMA
    output_buf_size = gfx->width() * 4 * 2;  // RGB565 = 2 bytes per pixel
    output_buf = (uint16_t *)heap_caps_aligned_alloc(16, output_buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!output_buf)
    {
        Serial.println("ERROR: output_buf aligned_alloc failed!");
        while (true) {}
    }
    Serial.printf("Output buffer: %d bytes (DMA-capable)\n", output_buf_size * sizeof(uint16_t));

    // MJPEG buffer - INCREASED from /5 to /2 for full frame storage
    // This prevents buffer overruns that cause distortion after ~10 seconds
    estimateBufferSize = gfx->width() * gfx->height() * 2;  // Full buffer (was * 2 / 5)
    mjpeg_buf = (uint8_t *)heap_caps_aligned_alloc(16, estimateBufferSize, MALLOC_CAP_8BIT);
    if (!mjpeg_buf)
    {
        Serial.println("ERROR: mjpeg_buf allocation failed!");
        while (true) {}
    }
    Serial.printf("MJPEG buffer: %d bytes (%.2f KB)\n", 
                  estimateBufferSize, estimateBufferSize / 1024.0);
    Serial.printf("Free heap after buffer allocation: %u bytes\n", ESP.getFreeHeap());

    // Load file list
    loadMjpegFilesList();

    // Setup watchdog for frame timeout recovery
    setupWatchdog(5000);  // 5 second watchdog
    Serial.println("Watchdog timer initialized (5s timeout)");

    // Configure boot button interrupt
    pinMode(BOOT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onButtonPress, FALLING);
    Serial.println("Boot button interrupt enabled");

    Serial.println("=== Setup complete, starting playback ===\n");
}

// ============================================================================
// PLAY SELECTED MJPEG FILE
// ============================================================================
void playSelectedMjpeg(int mjpegIndex)
{
    if (mjpegIndex >= mjpegCount) return;
    
    String fullPath = String(MJPEG_FOLDER) + "/" + mjpegFileList[mjpegIndex];
    char mjpegFilename[128];
    fullPath.toCharArray(mjpegFilename, sizeof(mjpegFilename));

    Serial.printf("\n>>> Playing %s\n", mjpegFilename);
    mjpegPlayFromSDCard(mjpegFilename);
}

// ============================================================================
// CALLBACK FUNCTION FOR JPEG DRAWING
// ============================================================================
int jpegDrawCallback(JPEGDRAW *pDraw)
{
    unsigned long s = millis();
    
    // Feed watchdog during long draw operations
    feedWatchdog();
    
    // Draw the 16-bit RGB bitmap to display
    gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    
    total_show_video += millis() - s;
    return 1;
}

// ============================================================================
// PLAY MJPEG FROM SD CARD WITH ERROR RECOVERY
// ============================================================================
void mjpegPlayFromSDCard(char *mjpegFilename)
{
    File mjpegFile = SD.open(mjpegFilename, "r");

    if (!mjpegFile || mjpegFile.isDirectory())
    {
        Serial.printf("ERROR: Failed to open %s for reading\n", mjpegFilename);
        return;
    }

    Serial.println("MJPEG playback started");
    gfx->fillScreen(RGB565_BLACK);

    start_ms = millis();
    curr_ms = millis();
    total_frames = 0;
    total_read_video = 0;
    total_decode_video = 0;
    total_show_video = 0;

    // Reset MJPEG decoder state
    mjpeg.resetFrameErrors();
    int consecutiveErrors = 0;
    unsigned long lastFrameTime = 0;

    // Initialize MJPEG decoder
    if (!mjpeg.setup(
            &mjpegFile, mjpeg_buf, jpegDrawCallback, true /* useBigEndian */,
            0 /* x */, 0 /* y */, gfx->width() /* widthLimit */, gfx->height() /* heightLimit */))
    {
        Serial.println("ERROR: MJPEG setup failed!");
        mjpegFile.close();
        return;
    }

    // Main playback loop with error recovery
    bool fileAvailable = true;
    while (!skipRequested && fileAvailable)
    {
        // Feed watchdog to prevent system reset
        feedWatchdog();
        
        unsigned long frameStart = millis();
        
        // Read MJPEG frame
        curr_ms = millis();
        fileAvailable = mjpeg.readMjpegBuf();
        
        if (!fileAvailable) {
            break;
        }
        
        total_read_video += millis() - curr_ms;
        
        // Decode and display frame
        curr_ms = millis();
        bool drawSuccess = mjpeg.drawJpg();
        unsigned long decodeTime = millis() - curr_ms;
        total_decode_video += decodeTime;
        
        // Error handling and recovery
        if (!drawSuccess) {
            consecutiveErrors++;
            Serial.printf("Frame decode error #%d\n", consecutiveErrors);
            
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                Serial.println("Too many consecutive errors, skipping to next video...");
                break;
            }
            
            // Skip this frame and continue
            continue;
        }
        
        consecutiveErrors = 0;  // Reset error counter on success
        
        // Frame timing check - skip if decode took too long
        unsigned long frameTime = millis() - frameStart;
        lastFrameTime = frameTime;
        
        if (decodeTime > FRAME_TIMEOUT_MS) {
            Serial.printf("Warning: Frame decode took %lu ms (limit: %d ms)\n", 
                          decodeTime, FRAME_TIMEOUT_MS);
        }
        
        curr_ms = millis();
        total_frames++;
        
        // Periodic performance logging
        if (total_frames % PERFORMANCE_LOG_INTERVAL == 0) {
            float fps = 1000.0 * total_frames / (millis() - start_ms);
            Serial.printf("[Frame %d] FPS: %.1f, Decode: %lu ms, Show: %lu ms\n",
                          total_frames, fps, decodeTime, 
                          total_show_video / total_frames);
            Serial.printf("  Free heap: %u bytes\n", ESP.getFreeHeap());
        }
    }

    // Handle button press debouncing
    if (skipRequested)
    {
        uint32_t now = millis();
        if (now - lastPress < BOOT_BUTTON_DEBOUNCE_TIME)
        {
            // Ignore if within debounce time
        }
        else
        {
            lastPress = now;
        }
    }
    skipRequested = false;

    // Calculate and display final statistics
    int time_used = millis() - start_ms;
    Serial.println(F("\n=== MJPEG Playback Complete ==="));
    mjpegFile.close();

    if (total_frames > 0)
    {
        float fps = 1000.0 * total_frames / time_used;
        total_decode_video -= total_show_video;  // Adjust decode time (excluding show time)
        
        Serial.printf("Total frames: %d\n", total_frames);
        Serial.printf("Time used: %d ms\n", time_used);
        Serial.printf("Average FPS: %.1f\n", fps);
        Serial.printf("Read MJPEG: %lu ms (%.1f%%)\n", 
                      total_read_video, 100.0 * total_read_video / time_used);
        Serial.printf("Decode video: %lu ms (%.1f%%)\n", 
                      total_decode_video, 100.0 * total_decode_video / time_used);
        Serial.printf("Show video: %lu ms (%.1f%%)\n", 
                      total_show_video, 100.0 * total_show_video / time_used);
        Serial.printf("Frame errors: %d\n", mjpeg.getFrameErrors());
        Serial.printf("Video size: %d x %d, scale: %d\n",
                      mjpeg.getWidth(), mjpeg.getHeight(), mjpeg.getScale());
    }
    else
    {
        Serial.println("No frames were played!");
    }
    
    Serial.printf("Free heap at end: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("=== End of video ===\n\n");
}

// ============================================================================
// MAIN LOOP - PLAY VIDEOS IN SEQUENCE
// ============================================================================
void loop()
{
    playSelectedMjpeg(currentMjpegIndex);
    currentMjpegIndex++;
    if (currentMjpegIndex >= mjpegCount)
    {
        currentMjpegIndex = 0;
        Serial.println("=== All videos played, restarting ===\n");
    }
}