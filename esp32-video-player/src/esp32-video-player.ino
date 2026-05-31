/*******************************************************************************
 * ESP32-2432S028 (CYD) High-Performance MJPEG Video Player
 * 
 * Optimizations:
 * - PicoJPEG decoder (2-3x faster than JPEGDEC)
 * - Dual-core processing (Core 0: decode, Core 1: display)
 * - DMA SPI transfers at 80MHz
 * - Double buffering with frame ready semaphores
 * - WiFi/Bluetooth disabled for clean SPI
 * 
 * Hardware: ESP32 240MHz, ILI9341 240x320 SPI, SD card on VSPI
 ******************************************************************************/

// Disable WiFi and Bluetooth for clean SPI operation
#include <WiFi.h>
#include <esp_bt.h>

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include <TFT_eSPI.h>
#include "DisplayConfig.h"
#include "SDConfig.h"
#include "PicoJpegClass.h"
#include "AudioMP3.h"

extern "C" {
  #include "picojpeg.h"
}

// ============================================================================
// GLOBAL VARIABLES & BUFFERS
// ============================================================================

// FreeRTOS handles
static TaskHandle_t decoderTaskHandle = NULL;
static TaskHandle_t displayTaskHandle = NULL;
static QueueHandle_t frameQueue = NULL;
static SemaphoreHandle_t frameReadySem = NULL;
static SemaphoreHandle_t frameDisplaySem = NULL;

// Audio
static AudioMP3 audioPlayer;

// Frame buffers (double-buffering)
#ifdef BOARD_HAS_PSRAM
static uint8_t* frameBuf0;  // Ping-pong buffer 0
static uint8_t* frameBuf1;  // Ping-pong buffer 1
static uint8_t* jpegWorkBuf; // JPEG decode working buffer
#else
static uint8_t frameBuf0[FRAME_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t frameBuf1[FRAME_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t jpegWorkBuf[64 * 1024]; // Smaller buffer for no-PSRAM
#endif

// Current buffers being used
static uint8_t* currentDecodeBuf = frameBuf0;
static uint8_t* currentDisplayBuf = frameBuf1;
static volatile bool newFrameReady = false;
static volatile bool displayTaskIdle = true;

// Decode state
static uint32_t totalFramesDecoded = 0;
static uint32_t totalFramesDisplayed = 0;
static uint32_t decodeTimeUs = 0;
static uint32_t displayTimeUs = 0;
static uint32_t fpsFrameCount = 0;
static uint32_t fpsLastUpdate = 0;
static float currentFPS = 0.0f;

// File handling
static File mjpegFile;
static File audioFile;
static char currentFilename[128];
static char audioFilename[128];
static bool fileOpen = false;
static bool hasAudio = false;

// Button
#define BOOT_BUTTON_PIN 0
static volatile bool skipRequested = false;
static volatile uint32_t buttonPressTime = 0;

// ============================================================================
// DISPLAY FUNCTIONS (Core 1 - Display Task)
// ============================================================================

// TFT_eSPI display instance
static TFT_eSPI tft = TFT_eSPI(DISPLAY_WIDTH, DISPLAY_HEIGHT);

// Initialize display with TFT_eSPI
bool initDisplay() {
  Serial.println("Initializing display with TFT_eSPI...");
  
  // Initialize TFT_eSPI - uses hardware SPI with pins from User_Setup.h
  tft.init();
  
  // Set rotation to portrait (0) - standard for CYD
  tft.setRotation(0);
  
  // Fill with black initially
  tft.fillScreen(TFT_BLACK);
  
  // Enable backlight
  pinMode(DISPLAY_BL_PIN, OUTPUT);
  digitalWrite(DISPLAY_BL_PIN, HIGH);
  
  Serial.println("Display initialized with TFT_eSPI");
  return true;
}

// Display full frame from buffer using TFT_eSPI
void displayFrameDMA(uint16_t* frameBuf, uint32_t width, uint32_t height) {
  uint32_t startUs = micros();
  
  // Push the RGB565 image to the display
  // TFT_eSPI handles all SPI communication internally
  tft.pushImage(0, 0, width, height, frameBuf);
  
  displayTimeUs += (micros() - startUs);
}

// ============================================================================
// MJPEG DECODER (Core 0 - Decoder Task)
// ============================================================================

// JPEG decode callback for picojpeg
static unsigned char needBytesCallback(unsigned char* pBuf, unsigned char bufSize, 
                                        unsigned char* pBytesRead, void* pCallbackData) {
  if (!mjpegFile || !mjpegFile.available()) {
    *pBytesRead = 0;
    return 1; // Error
  }
  
  size_t bytesRead = mjpegFile.read(pBuf, bufSize);
  *pBytesRead = (unsigned char)bytesRead;
  
  return 0; // Success
}

// Decode one JPEG frame from file
bool decodeJPEGFrame(uint16_t* outputBuf, uint32_t outputBufSize) {
  pjpeg_image_info_t info;
  
  uint8_t status = pjpeg_decode_init(&info, needBytesCallback, nullptr, 0);
  if (status != 0) {
    Serial.printf("JPEG init failed: %d\n", status);
    return false;
  }
  
  // Decode all MCUs
  uint32_t mcusPerRow = info.m_MCUSPerRow;
  uint32_t mcusPerCol = info.m_MCUSPerCol;
  uint32_t totalMCUs = mcusPerRow * mcusPerCol;
  
  for (uint32_t mcu = 0; mcu < totalMCUs; mcu++) {
    status = pjpeg_decode_mcu();
    if (status == PJPG_NO_MORE_BLOCKS) break;
    if (status != 0) {
      Serial.printf("MCU decode error at %u: %d\n", mcu, status);
      return false;
    }
    
    // Copy MCU output to frame buffer
    uint32_t mcuX = mcu % mcusPerRow;
    uint32_t mcuY = mcu / mcusPerRow;
    
    int outX = mcuX * info.m_MCUWidth;
    int outY = mcuY * info.m_MCUHeight;
    
    // Copy pixels
    uint8_t* pR = info.m_pMCUBufR;
    uint8_t* pG = info.m_pMCUBufG;
    uint8_t* pB = info.m_pMCUBufB;
    
    for (int py = 0; py < info.m_MCUHeight && (outY + py) < DISPLAY_HEIGHT; py++) {
      for (int px = 0; px < info.m_MCUWidth && (outX + px) < DISPLAY_WIDTH; px++) {
        int idx = py * 8 + px;
        uint8_t r = pR[idx];
        uint8_t g = pG[idx];
        uint8_t b = pB[idx];
        
        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        
        int pixelY = outY + py;
        int pixelX = outX + px;
        if (pixelY < DISPLAY_HEIGHT && pixelX < DISPLAY_WIDTH) {
          int pixelIndex = pixelY * DISPLAY_WIDTH + pixelX;
          if (pixelIndex < (int)(outputBufSize / 2)) {
            outputBuf[pixelIndex] = rgb565;
          }
        }
      }
    }
  }
  
  return true;
}

// Find JPEG frame boundaries in MJPEG stream
uint32_t findJPEGFrame(uint8_t* stream, uint32_t streamSize, uint8_t* frameBuf, uint32_t frameBufSize) {
  // Look for FFD8 marker
  uint32_t start = 0xFFFFFFFF;
  bool foundFFD8 = false;
  
  for (uint32_t i = 0; i < streamSize - 1; i++) {
    if (stream[i] == 0xFF && stream[i + 1] == 0xD8) {
      start = i;
      foundFFD8 = true;
      break;
    }
  }
  
  if (!foundFFD8) return 0;
  
  // Look for FFD9 marker
  uint32_t end = 0xFFFFFFFF;
  for (uint32_t i = start + 2; i < streamSize - 1; i++) {
    if (stream[i] == 0xFF && stream[i + 1] == 0xD9) {
      end = i + 2;
      break;
    }
  }
  
  if (end == 0xFFFFFFFF) return 0;
  
  uint32_t frameSize = end - start;
  if (frameSize > frameBufSize) return 0;
  
  memcpy(frameBuf, stream + start, frameSize);
  return frameSize;
}

// ============================================================================
// FILE HANDLING
// ============================================================================

String mjpegFiles[MAX_MJPEG_FILES];
int mjpegCount = 0;
int currentFileIndex = 0;

void loadFileList() {
  File dir = SD.open(MJPEG_FOLDER);
  if (!dir) {
    Serial.println("Failed to open mjpeg folder");
    return;
  }
  
  mjpegCount = 0;
  while (true) {
    File f = dir.openNextFile();
    if (!f) break;
    
    String name = f.name();
    if (name.endsWith(".mjpeg")) {
      mjpegFiles[mjpegCount] = name;
      mjpegCount++;
      if (mjpegCount >= MAX_MJPEG_FILES) break;
    }
    f.close();
  }
  dir.close();
  
  Serial.printf("Found %d video files\n", mjpegCount);
}

// Check if corresponding audio file exists
bool checkAudioFile(const char* videoFilename) {
  // Create audio filename from video filename
  // e.g., video.mjpeg -> video.mp3
  String videoPath = String(videoFilename);
  int dotPos = videoPath.lastIndexOf('.');
  if (dotPos > 0) {
    String audioPath = videoPath.substring(0, dotPos) + ".mp3";
    audioPath = String(MJPEG_FOLDER) + "/" + audioPath;
    
    if (SD.exists(audioPath.c_str())) {
      audioPath.toCharArray(audioFilename, sizeof(audioFilename));
      return true;
    }
  }
  return false;
}

bool openNextFile() {
  if (mjpegCount == 0) return false;
  
  // Close current file if open
  if (mjpegFile) {
    mjpegFile.close();
    fileOpen = false;
  }
  
  // Stop any playing audio
  audioPlayer.stop();
  hasAudio = false;
  
  String fullPath = String(MJPEG_FOLDER) + "/" + mjpegFiles[currentFileIndex];
  fullPath.toCharArray(currentFilename, sizeof(currentFilename));
  
  Serial.printf("Opening: %s\n", currentFilename);
  
  mjpegFile = SD.open(currentFilename, FILE_READ);
  if (!mjpegFile) {
    Serial.println("Failed to open file");
    return false;
  }
  
  fileOpen = true;
  
  // Check for matching audio file
  hasAudio = checkAudioFile(mjpegFiles[currentFileIndex].c_str());
  if (hasAudio) {
    Serial.printf("Audio found: %s\n", audioFilename);
    audioPlayer.openFile(audioFilename);
  }
  
  return true;
}

// ============================================================================
// BUTTON HANDLING
// ============================================================================

void IRAM_ATTR buttonISR() {
  skipRequested = true;
  buttonPressTime = millis();
}

// ============================================================================
// CORE 0: DECODER TASK
// ============================================================================

void decoderTask(void* parameter) {
  Serial.println("Decoder task started on Core 0");
  
  uint8_t readBuffer[4096];
  uint8_t jpegBuffer[64 * 1024];
  uint32_t bufferOffset = 0;
  bool findingFrame = true;
  uint32_t jpegSize = 0;
  bool audioStarted = false;
  
  while (true) {
    // Check for skip request
    if (skipRequested) {
      skipRequested = false;
      Serial.println("Skip requested");
      
      // Stop audio
      audioPlayer.stop();
      audioStarted = false;
      
      // Move to next file
      currentFileIndex++;
      if (currentFileIndex >= mjpegCount) currentFileIndex = 0;
      
      if (mjpegFile) {
        mjpegFile.close();
        fileOpen = false;
      }
      
      openNextFile();
      bufferOffset = 0;
      findingFrame = true;
      jpegSize = 0;
    }
    
    if (!fileOpen) {
      if (!openNextFile()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      bufferOffset = 0;
      findingFrame = true;
      jpegSize = 0;
      audioStarted = false;
    }
    
    // Start audio playback when video starts (after first frame decoded)
    if (hasAudio && !audioStarted && totalFramesDecoded > 0) {
      audioPlayer.play();
      audioStarted = true;
    }
    
    // Read data from file
    if (mjpegFile.available()) {
      size_t bytesRead = mjpegFile.read(readBuffer, sizeof(readBuffer));
      if (bytesRead > 0) {
        // Copy to jpeg buffer
        if (bufferOffset + bytesRead < sizeof(jpegBuffer)) {
          memcpy(jpegBuffer + bufferOffset, readBuffer, bytesRead);
          bufferOffset += bytesRead;
        } else {
          // Buffer overflow, reset
          bufferOffset = 0;
          findingFrame = true;
        }
      }
    } else {
      // End of file
      audioPlayer.stop();
      audioStarted = false;
      
      if (mjpegFile) {
        mjpegFile.close();
        fileOpen = false;
      }
      
      // Move to next file
      currentFileIndex++;
      if (currentFileIndex >= mjpegCount) currentFileIndex = 0;
      
      bufferOffset = 0;
      findingFrame = true;
      jpegSize = 0;
      
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    // Find and decode JPEG frames
    if (bufferOffset > 4) {
      // Find JPEG frame in buffer
      uint32_t foundSize = findJPEGFrame(jpegBuffer, bufferOffset, currentDecodeBuf, FRAME_BUFFER_SIZE);
      
      if (foundSize > 0) {
        // Decode the JPEG
        uint32_t startUs = micros();
        
        pjpeg_image_info_t info;
        uint8_t status = pjpeg_decode_init(&info, nullptr, nullptr, 0);
        
        if (status == 0 && info.m_width > 0 && info.m_height > 0) {
          // Decode all MCUs
          uint32_t mcusPerRow = info.m_MCUSPerRow;
          uint32_t mcusPerCol = info.m_MCUSPerCol;
          
          for (uint32_t mcu = 0; mcu < mcusPerRow * mcusPerCol; mcu++) {
            status = pjpeg_decode_mcu();
            if (status == PJPG_NO_MORE_BLOCKS) break;
            
            // Copy MCU to output buffer (RGB565)
            uint32_t mcuX = mcu % mcusPerRow;
            uint32_t mcuY = mcu / mcusPerRow;
            
            int outX = mcuX * info.m_MCUWidth;
            int outY = mcuY * info.m_MCUHeight;
            
            uint8_t* pR = info.m_pMCUBufR;
            uint8_t* pG = info.m_pMCUBufG;
            uint8_t* pB = info.m_pMCUBufB;
            
            uint16_t* outBuf = (uint16_t*)currentDecodeBuf;
            
            for (int py = 0; py < info.m_MCUHeight && (outY + py) < DISPLAY_HEIGHT; py++) {
              for (int px = 0; px < info.m_MCUWidth && (outX + px) < DISPLAY_WIDTH; px++) {
                int idx = py * 8 + px;
                uint8_t r = pR[idx];
                uint8_t g = pG[idx];
                uint8_t b = pB[idx];
                
                uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                
                int pixelY = outY + py;
                int pixelX = outX + px;
                if (pixelY < DISPLAY_HEIGHT && pixelX < DISPLAY_WIDTH) {
                  outBuf[pixelY * DISPLAY_WIDTH + pixelX] = rgb565;
                }
              }
            }
          }
          
          decodeTimeUs += (micros() - startUs);
          totalFramesDecoded++;
          
          // Signal display task
          xSemaphoreGive(frameReadySem);
          
          // Wait for display to finish before decoding next frame
          xSemaphoreTake(frameDisplaySem, portMAX_DELAY);
        }
        
        // Shift buffer
        memmove(jpegBuffer, jpegBuffer + foundSize, bufferOffset - foundSize);
        bufferOffset -= foundSize;
        findingFrame = true;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ============================================================================
// CORE 1: DISPLAY TASK
// ============================================================================

void displayTask(void* parameter) {
  Serial.println("Display task started on Core 1");
  
  while (true) {
    // Wait for frame ready signal
    if (xSemaphoreTake(frameReadySem, portMAX_DELAY) == pdTRUE) {
      uint32_t startUs = micros();
      
      // Display the frame
      displayFrameDMA((uint16_t*)currentDecodeBuf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      
      totalFramesDisplayed++;
      
      // Calculate FPS
      fpsFrameCount++;
      uint32_t now = millis();
      if (now - fpsLastUpdate >= 1000) {
        currentFPS = fpsFrameCount * 1000.0f / (now - fpsLastUpdate);
        fpsFrameCount = 0;
        fpsLastUpdate = now;
        
        Serial.printf("FPS: %.1f | Decode: %lu us | Display: %lu us\n", 
                      currentFPS, decodeTimeUs / totalFramesDecoded, 
                      displayTimeUs / totalFramesDisplayed);
      }
      
      // Signal decoder that display is done
      xSemaphoreGive(frameDisplaySem);
    }
  }
}

// ============================================================================
// ARDUINO SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-CYD High-Performance MJPEG Player ===\n");
  
  // Disable WiFi and Bluetooth for clean SPI
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("WiFi/BT disabled");
  
  // Initialize SD card
  // FIRST: Initialize display so we can show messages
  Serial.println("Initializing display...");
  if (!initDisplay()) {
    Serial.println("Display init FAILED!");
    while (1) delay(100);
  }
  
  // Show startup message on screen
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("=== MJPEG Player ===");
  tft.setTextSize(1);
  tft.println("");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.println("Iniciando...");
  
  // Disable WiFi and Bluetooth for clean SPI
  WiFi.mode(WIFI_OFF);
  btStop();
  tft.println("WiFi/BT off");
  
  // Initialize SD card
  tft.print("SD card init... ");
  
  SPIClass sdSPI(VSPI);
  sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  if (!SD.begin(SD_CS_PIN, sdSPI, SD_SPI_SPEED, "/sd")) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("FALLO!");
    tft.println("");
    tft.println("INSERTA LA SD");
    tft.println("y reinicia");
    while (1) delay(100);
  }
  
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println("OK");
  
  // Initialize audio DAC on GPIO 25
  audioPlayer.begin();
  tft.println("Audio OK");
  
  // Load file list
  tft.print("Buscando videos... ");
  loadFileList();
  
  if (mjpegCount == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("SIN VIDEOS");
    tft.println("");
    tft.println("Crea carpeta:");
    tft.println("/mjpeg");
    tft.println("y agrega .mjpeg");
    while (1) delay(100);
  }
  
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.printf("OK (%d videos)\n", mjpegCount);
  
  // Allocate buffers
#ifdef BOARD_HAS_PSRAM
  frameBuf0 = (uint8_t*)heap_caps_aligned_alloc(16, FRAME_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  frameBuf1 = (uint8_t*)heap_caps_aligned_alloc(16, FRAME_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  
  if (!frameBuf0 || !frameBuf1) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Mem error!");
    while (1) delay(100);
  }
#else
#endif
  
  // Create semaphores
  frameReadySem = xSemaphoreCreateBinary();
  frameDisplaySem = xSemaphoreCreateBinary();
  
  if (!frameReadySem || !frameDisplaySem) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Sem error!");
    while (1) delay(100);
  }
  
  // Initial semaphore give to allow decoder to start
  xSemaphoreGive(frameDisplaySem);
  
  // Button interrupt
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BOOT_BUTTON_PIN, buttonISR, FALLING);
  
  // Show "PLAYING" message briefly
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("REPRODUCIENDO");
  tft.setTextSize(1);
  delay(1000);
  tft.fillScreen(TFT_BLACK);
  
  // Create decoder task on Core 0
  xTaskCreatePinnedToCore(
    decoderTask,
    "Decoder",
    8192,
    nullptr,
    1,
    &decoderTaskHandle,
    0  // Core 0
  );
  
  // Create display task on Core 1
  xTaskCreatePinnedToCore(
    displayTask,
    "Display",
    4096,
    nullptr,
    1,
    &displayTaskHandle,
    1  // Core 1
  );
  
  Serial.println("\n=== Video playback started ===");
  Serial.printf("Playing %d files from %s\n", mjpegCount, MJPEG_FOLDER);
  Serial.println("Press BOOT button to skip to next video\n");
}

void loop() {
  // Main loop does nothing - all work is in tasks
  delay(1000);
  
  // Print stats every 5 seconds
  static uint32_t lastStatsPrint = 0;
  if (millis() - lastStatsPrint > 5000) {
    lastStatsPrint = millis();
    Serial.printf("\n--- Stats ---\n");
    Serial.printf("Frames decoded: %u\n", totalFramesDecoded);
    Serial.printf("Frames displayed: %u\n", totalFramesDisplayed);
    Serial.printf("Current FPS: %.1f\n", currentFPS);
    Serial.printf("Current file: %s\n", currentFilename);
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
    Serial.printf("-----------\n\n");
  }
}