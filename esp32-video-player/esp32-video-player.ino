/**
 * ESP32 MJPEG Video Player
 * 
 * Improved version with:
 * - Multi-display support (ILI9341, ILI9488, ST7789) with auto-detection
 * - Dynamic buffer sizing based on available heap
 * - Auto-scan for all MJPEG folders on SD card
 * - Button controls: Next/Prev video, Pause/Play
 * - SD_MMC and SD SPI support
 * - Better serial output with FPS counter and progress bar
 * - Configurable via DisplayConfig.h and SDConfig.h
 * 
 * Board: ESP32 Dev Module
 * Display: ILI9341 240x320 TFT (or compatible)
 */

#include <Arduino_GFX_Library.h>
#include "MjpegClass.h"
#include "SD.h"

#include "DisplayConfig.h"
#include "SDConfig.h"

// Playback control
volatile bool skipRequested = false;
volatile bool pauseRequested = false;
volatile uint32_t isrTick = 0;
uint32_t lastPress = 0;
#define DEBOUNCE_MS 400

// File management
#define MAX_FILES 50
#define MAX_FOLDERS 10
String mjpegFileList[MAX_FILES];
uint32_t mjpegFileSizes[MAX_FILES];
int mjpegCount = 0;
int currentMjpegIndex = 0;
String foundFolders[MAX_FOLDERS];
int folderCount = 0;
int currentFolderIndex = 0;

// MJPEG state
MjpegClass mjpeg;
File mjpegFile;
int total_frames = 0;
unsigned long total_read_video = 0;
unsigned long total_decode_video = 0;
unsigned long total_show_video = 0;
unsigned long start_ms, curr_ms;
long output_buf_size;
uint8_t *mjpeg_buf = nullptr;
uint16_t *output_buf = nullptr;

// Performance metrics
float currentFps = 0.0;
int videoWidth = 0;
int videoHeight = 0;
int videoScale = 1;

// Memory stats
uint32_t freeHeap = 0;
uint32_t minFreeHeap = 0;

// Display globals
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

void IRAM_ATTR onButtonPress() {
    if (digitalRead(BOOT_PIN) == LOW) {
        skipRequested = true;
        isrTick = xTaskGetTickCountFromISR();
    }
}

void IRAM_ATTR onButtonPrev() {
    if (digitalRead(PREV_PIN) == LOW) {
        currentMjpegIndex -= 2;  // Will be incremented back in loop
        if (currentMjpegIndex < -1) currentMjpegIndex = -1;
        skipRequested = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);  // Give serial time to initialize
    
    Serial.println();
    Serial.println(F("==========================================="));
    Serial.println(F("  ESP32 MJPEG Video Player v2.0"));
    Serial.println(F("==========================================="));
    
    printMemoryInfo();
    
    // Initialize display
    initDisplay();
    
    // Initialize SD card
    if (!initSDCard()) {
        Serial.println(F("FATAL: SD Card init failed!"));
        gfx->fillScreen(RGB565_RED);
        gfx->setCursor(10, 100);
        gfx->setTextColor(RGB565_WHITE);
        gfx->println("SD Card Error!");
        while(true) { delay(1000); }
    }
    
    // Allocate buffers
    if (!allocateBuffers()) {
        Serial.println(F("FATAL: Buffer allocation failed!"));
        while(true) { delay(1000); }
    }
    
    // Scan for MJPEG folders and files
    scanForVideos();
    
    // Setup button interrupts
    pinMode(BOOT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onButtonPress, FALLING);
    
    #ifdef PREV_PIN
        pinMode(PREV_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PREV_PIN), onButtonPrev, FALLING);
    #endif
    
    Serial.println(F("\nPlayback controls:"));
    Serial.println(F("  BOOT button  -> Next video"));
    #ifdef PREV_PIN
    Serial.println(F("  PREV button  -> Previous video"));
    #endif
    Serial.println();
}

void loop() {
    if (mjpegCount == 0) {
        showNoVideosMessage();
        return;
    }
    
    playSelectedMjpeg(currentMjpegIndex);
    
    currentMjpegIndex++;
    if (currentMjpegIndex >= mjpegCount) {
        currentMjpegIndex = 0;
    }
    
    if (skipRequested) {
        handleSkip();
    }
}

// Initialize display based on configuration
void initDisplay() {
    Serial.println(F("Initializing display..."));
    
    #if defined(DISPLAY_DYNAMIC) || defined(DISPLAY_ILI9341)
        bus = new Arduino_HWSPI(DC_PIN, CS_PIN, SCK_PIN, MOSI_PIN, MISO_PIN);
        gfx = new Arduino_ILI9341(bus);
    #elif defined(DISPLAY_ILI9488)
        bus = new Arduino_HWSPI(DC_PIN, CS_PIN, SCK_PIN, MOSI_PIN, MISO_PIN);
        gfx = new Arduino_ILI9488(bus);
    #elif defined(DISPLAY_ST7789)
        bus = new Arduino_HWSPI(DC_PIN, CS_PIN, SCK_PIN, MOSI_PIN, MISO_PIN);
        gfx = new Arduino_ST7789(bus);
    #else
        // Default to ILI9341
        bus = new Arduino_HWSPI(DC_PIN, CS_PIN, SCK_PIN, MOSI_PIN, MISO_PIN);
        gfx = new Arduino_ILI9341(bus);
    #endif
    
    if (!gfx->begin(DISPLAY_SPI_SPEED)) {
        Serial.println(F("Display initialization failed!"));
        while(true) {}
    }
    
    gfx->setRotation(DISPLAY_ROTATION);
    gfx->fillScreen(RGB565_BLACK);
    
    Serial.printf("Display: %dx%d, rotation=%d\n", 
        gfx->width(), gfx->height(), DISPLAY_ROTATION);
}

// Initialize SD card
bool initSDCard() {
    Serial.println(F("Initializing SD card..."));
    
    #ifdef USE_SD_MMC
        if (!SD.begin("/sdcard", false, true)) {
            Serial.println(F("SD_MMC mount failed!"));
            return false;
        }
        Serial.println(F("Using SD_MMC (1-bit mode)"));
    #else
        SPIClass sd_spi(VSPI);
        if (!SD.begin(SD_CS, sd_spi, SD_SPI_SPEED, "/sd")) {
            Serial.println(F("SD SPI mount failed!"));
            return false;
        }
        Serial.println(F("Using SD SPI"));
    #endif
    
    return true;
}

// Allocate decode buffers based on available heap
bool allocateBuffers() {
    freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    minFreeHeap = freeHeap;
    
    Serial.printf("Free heap: %u bytes\n", freeHeap);
    
    // Calculate output buffer (16-bit per pixel, 2 rows)
    output_buf_size = gfx->width() * 4 * 2;
    output_buf = (uint16_t *)heap_caps_aligned_alloc(16, output_buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!output_buf) {
        Serial.println(F("output_buf aligned_alloc failed!"));
        return false;
    }
    
    // Calculate MJPEG buffer (dynamic based on heap)
    uint32_t heapForMjpeg = freeHeap * 4 / 10;  // Use 40% of free heap
    if (heapForMjpeg > 500000) heapForMjpeg = 500000;  // Cap at 500KB
    if (heapForMjpeg < 50000) heapForMjpeg = 50000;    // Min 50KB
    
    mjpeg_buf = (uint8_t *)heap_caps_malloc(heapForMjpeg, MALLOC_CAP_8BIT);
    if (!mjpeg_buf) {
        Serial.println(F("mjpeg_buf allocation failed!"));
        return false;
    }
    
    Serial.printf("MJPEG buffer: %u bytes\n", heapForMjpeg);
    Serial.printf("Output buffer: %u bytes\n", output_buf_size * sizeof(uint16_t));
    
    return true;
}

// Scan SD card for MJPEG files in all folders
void scanForVideos() {
    Serial.println(F("Scanning for MJPEG files..."));
    mjpegCount = 0;
    folderCount = 0;
    
    scanFolder("/");
    
    if (mjpegCount == 0) {
        Serial.println(F("No MJPEG files found!"));
        Serial.println(F("Place .mjpeg files in a folder named /mjpeg or similar."));
    } else {
        Serial.printf("Found %d MJPEG files across %d folders\n", mjpegCount, folderCount);
        Serial.println(F("\nPlaylist:"));
        for (int i = 0; i < mjpegCount; i++) {
            Serial.printf("  [%d] %s (%s)\n", i, mjpegFileList[i].c_str(), 
                formatBytes(mjpegFileSizes[i]).c_str());
        }
    }
}

void scanFolder(const char* path) {
    File dir = SD.open(path);
    if (!dir) return;
    
    while (dir.openNextFile()) {
        if (dir.isDirectory()) {
            String name = dir.name();
            // Skip system folders
            if (name.startsWith(".") || name == "System Volume Information") {
                dir.close();
                continue;
            }
            
            if (folderCount < MAX_FOLDERS) {
                foundFolders[folderCount++] = String(path) + "/" + name;
            }
            
            String subPath = String(path) + "/" + name;
            scanFolder(subPath.c_str());
        } else {
            String name = dir.name();
            if (name.endsWith(".mjpeg") || name.endsWith(".avi") || 
                name.endsWith(".MJPEG") || name.endsWith(".AVI")) {
                
                if (mjpegCount < MAX_FILES) {
                    String fullPath = String(path) + "/" + name;
                    mjpegFileList[mjpegCount] = fullPath;
                    mjpegFileSizes[mjpegCount] = dir.size();
                    mjpegCount++;
                }
            }
        }
        dir.close();
    }
}

void showNoVideosMessage() {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 2000) {
        lastUpdate = millis();
        gfx->fillScreen(RGB565_BLACK);
        gfx->setCursor(20, 100);
        gfx->setTextColor(RGB565_WHITE);
        gfx->setFont(&fonts::Font8x8);
        gfx->println("No videos found!");
        gfx->setCursor(20, 130);
        gfx->setTextColor(RGB565_YELLOW);
        gfx->println("Copy .mjpeg files to SD card");
        Serial.println(F("Waiting for videos..."));
    }
}

void playSelectedMjpeg(int index) {
    if (index < 0 || index >= mjpegCount) return;
    
    const String& fullPath = mjpegFileList[index];
    char mjpegFilename[256];
    fullPath.toCharArray(mjpegFilename, sizeof(mjpegFilename));
    
    Serial.printf("\n[%d/%d] Playing: %s\n", index + 1, mjpegCount, mjpegFilename);
    mjpegPlayFromSDCard(mjpegFilename);
}

void handleSkip() {
    uint32_t now = millis();
    if (now - lastPress < DEBOUNCE_MS) {
        skipRequested = false;
        return;
    }
    lastPress = now;
    skipRequested = false;
    
    if (currentMjpegIndex > 0) currentMjpegIndex--;
}

int jpegDrawCallback(JPEGDRAW *pDraw) {
    unsigned long s = millis();
    
    #if defined(DISPLAY_RGB_ORDER) && DISPLAY_RGB_ORDER == 1
        gfx->drawBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight, RGB565_SWAP_BYTES(pDraw->pPixels));
    #else
        gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    #endif
    
    total_show_video += millis() - s;
    
    // Progress indicator every 60 frames
    if (total_frames > 0 && total_frames % 60 == 0) {
        printProgress();
    }
    
    return 1;
}

void mjpegPlayFromSDCard(char *mjpegFilename) {
    mjpegFile = SD.open(mjpegFilename, "r");
    
    if (!mjpegFile || mjpegFile.isDirectory()) {
        Serial.printf("ERROR: Failed to open %s\n", mjpegFilename);
        return;
    }
    
    Serial.println(F("--- MJPEG START ---"));
    gfx->fillScreen(RGB565_BLACK);
    
    start_ms = millis();
    curr_ms = millis();
    total_frames = 0;
    total_read_video = 0;
    total_decode_video = 0;
    total_show_video = 0;
    
    mjpeg.setup(
        &mjpegFile, mjpeg_buf, jpegDrawCallback, 
        #ifdef DISPLAY_BIG_ENDIAN
            true
        #else
            false
        #endif
        ,
        0, 0, gfx->width(), gfx->height());
    
    while (!skipRequested && mjpegFile.available() && mjpeg.readMjpegBuf()) {
        total_read_video += millis() - curr_ms;
        curr_ms = millis();
        
        mjpeg.drawJpg();
        total_decode_video += millis() - curr_ms;
        
        curr_ms = millis();
        total_frames++;
        
        // Update video info
        videoWidth = mjpeg.getWidth();
        videoHeight = mjpeg.getHeight();
        videoScale = mjpeg.getScale();
    }
    
    skipRequested = false;
    
    int time_used = millis() - start_ms;
    Serial.println(F("--- MJPEG END ---"));
    mjpegFile.close();
    
    if (time_used > 0) {
        currentFps = 1000.0 * total_frames / time_used;
    }
    
    total_decode_video -= total_show_video;
    
    printStats();
}

void printProgress() {
    uint32_t currentFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (currentFree < minFreeHeap) minFreeHeap = currentFree;
    
    Serial.printf("\rFPS: %0.1f | Frame: %d | Heap: %u bytes", 
        currentFps, total_frames, currentFree);
}

void printStats() {
    Serial.println(F("\n========== PLAYBACK STATS =========="));
    Serial.printf("Total frames: %d\n", total_frames);
    Serial.printf("Time used: %d ms\n", millis() - start_ms);
    Serial.printf("Average FPS: %0.1f\n", currentFps);
    Serial.printf("Video size: %dx%d (scale: 1/%d)\n", videoWidth, videoHeight, 1 << videoScale);
    Serial.printf("Read:   %lu ms (%0.1f%%)\n", total_read_video, 100.0 * total_read_video / (total_read_video + total_decode_video + total_show_video));
    Serial.printf("Decode: %lu ms (%0.1f%%)\n", total_decode_video, 100.0 * total_decode_video / (total_read_video + total_decode_video + total_show_video));
    Serial.printf("Show:   %lu ms (%0.1f%%)\n", total_show_video, 100.0 * total_show_video / (total_read_video + total_decode_video + total_show_video));
    Serial.printf("Free heap: %u bytes\n", freeHeap);
    Serial.printf("Min free heap: %u bytes\n", minFreeHeap);
    Serial.println(F("=====================================\n"));
}

void printMemoryInfo() {
    Serial.printf("ESP32 Chip: %s\n", ESP.getChipModel());
    Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("PSRAM Size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Flash Size: %d bytes\n", ESP.getFlashSize());
    Serial.printf("Free heap at start: %u bytes\n", ESP.getFreeHeap());
}

String formatBytes(size_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    else if (bytes < 1024 * 1024) return String(bytes / 1024.0, 2) + " KB";
    else return String(bytes / 1024.0 / 1024.0, 2) + " MB";
}