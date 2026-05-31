/*******************************************************************************
 * PicoJpeg - Fast JPEG Decoder Wrapper for ESP32
 * 
 * Uses PicoJPEG (https://github.com/richgel999/picojpeg) which is 2-3x faster
 * than JPEGDEC for embedded systems. Optimized for sequential MCU decode with 
 * direct RGB565 output to frame buffer.
 * 
 * USAGE:
 * 1. Copy picojpeg.c and picojpeg.h to your sketch folder
 * 2. Include this header in your main sketch
 * 3. Create PicoJpegDecoder instance and call begin() then decodeMJPEG()
 ******************************************************************************/
#ifndef _PICOJPEGCLASS_H_
#define _PICOJPEGCLASS_H_

#include <Arduino.h>
#include "picojpeg.h"

// Frame dimensions
#define FRAME_WIDTH 240
#define FRAME_HEIGHT 320
#define FRAME_BUFFER_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2)

// Helper: Convert YCbCr to RGB565
inline uint16_t ycc_to_rgb565(uint8_t y, uint8_t cb, uint8_t cr) {
  int32_t c = y - 128;
  int32_t d = cb - 128;
  int32_t e = cr - 128;

  int32_t r = c + ((e * 362) >> 8);
  if (r < 0) r = 0; else if (r > 255) r = 255;
  
  int32_t g = c - ((d * 91 + e * 183) >> 8);
  if (g < 0) g = 0; else if (g > 255) g = 255;
  
  int32_t b = d + ((e * 181) >> 8);
  if (b < 0) b = 0; else if (b > 255) b = 255;

  return (uint16_t)((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

class PicoJpegDecoder {
public:
  PicoJpegDecoder() {
    memset(&_info, 0, sizeof(_info));
    _pOutput = nullptr;
    _outputWidth = 0;
    _outputHeight = 0;
    _MCUWidth = 0;
    _MCUHeight = 0;
    _MCUsPerRow = 0;
    _MCUsPerCol = 0;
    _currentMCU = 0;
    _totalMCUs = 0;
    _initialized = false;
    _reduce = 0;
  }

  ~PicoJpegDecoder() {
    close();
  }

  // Initialize decoder
  // Returns image dimensions info
  bool begin(uint8_t reduce = 0) {
    close();
    _reduce = reduce;
    _initialized = false;
    return true;
  }

  // Start decoding a JPEG frame
  // pJpegData: pointer to JPEG data
  // dataSize: size of JPEG data in bytes
  // pOutputBuffer: pointer to output RGB565 buffer
  // outputBufSize: size of output buffer in bytes
  // Returns: true if JPEG header parsed successfully
  bool decodeStart(uint8_t* pJpegData, uint32_t dataSize, uint16_t* pOutputBuffer, uint32_t outputBufSize) {
    _pJpegData = pJpegData;
    _dataSize = dataSize;
    _dataIndex = 0;
    _pOutput = pOutputBuffer;
    _outputBufSize = outputBufSize;
    _outputIndex = 0;
    _currentMCU = 0;

    uint8_t status = pjpeg_decode_init(&_info, needBytesCallback, this, _reduce);
    if (status != 0) {
      Serial.printf("PicoJPEG init failed: %d\n", status);
      return false;
    }

    _MCUWidth = _info.m_MCUWidth;
    _MCUHeight = _info.m_MCUHeight;
    _MCUsPerRow = _info.m_MCUSPerRow;
    _MCUsPerCol = _info.m_MCUSPerCol;
    _totalMCUs = _MCUsPerRow * _MCUsPerCol;
    
    _imageWidth = _info.m_width;
    _imageHeight = _info.m_height;

    // Calculate scale to fit display
    _scale = 0;
    int scaledW = _imageWidth;
    int scaledH = _imageHeight;
    while (scaledW > FRAME_WIDTH || scaledH > FRAME_HEIGHT) {
      scaledW = (scaledW + 1) / 2;
      scaledH = (scaledH + 1) / 2;
      _scale++;
      if (_scale > 3) break;
    }

    _initialized = true;
    return true;
  }

  // Decode next MCU block
  // Returns: 0 = success, 1 = no more blocks, >1 = error
  uint8_t decodeMCU() {
    if (!_initialized) return 2;

    uint8_t status = pjpeg_decode_mcu();
    
    if (status == PJPG_NO_MORE_BLOCKS) {
      _initialized = false;
      return 1;
    }

    if (status != 0) {
      Serial.printf("Decode MCU error: %d\n", status);
      return status;
    }

    // Copy decoded MCU to output buffer (RGB565)
    copyMCUToOutput();
    
    _currentMCU++;
    return 0;
  }

  // Decode entire frame (blocking)
  // Returns: true if frame completed
  bool decodeFullFrame() {
    while (_initialized) {
      uint8_t status = decodeMCU();
      if (status != 0) return (status == 1); // true if done, false if error
    }
    return true;
  }

  void close() {
    _initialized = false;
    _pOutput = nullptr;
  }

  // Getters
  int getImageWidth() const { return _imageWidth; }
  int getImageHeight() const { return _imageHeight; }
  int getScale() const { return _scale; }
  int getMCUsPerRow() const { return _MCUsPerRow; }
  int getMCUsPerCol() const { return _MCUsPerCol; }
  int getTotalMCUs() const { return _totalMCUs; }
  int getDecodedMCUs() const { return _currentMCU; }
  bool isInitialized() const { return _initialized; }

private:
  pjpeg_image_info_t _info;
  uint8_t* _pJpegData;
  uint32_t _dataSize;
  uint32_t _dataIndex;
  uint16_t* _pOutput;
  uint32_t _outputBufSize;
  uint32_t _outputIndex;
  int _MCUWidth;
  int _MCUHeight;
  int _MCUsPerRow;
  int _MCUsPerCol;
  int _totalMCUs;
  int _currentMCU;
  int _imageWidth;
  int _imageHeight;
  int _scale;
  uint8_t _reduce;
  bool _initialized;

  // Callback for picojpeg to request more data
  static unsigned char needBytesCallback(unsigned char* pBuf, unsigned char buf_size, unsigned char* pBytes_actually_read, void* pCallbackData) {
    PicoJpegDecoder* pThis = (PicoJpegDecoder*)pCallbackData;
    
    uint32_t remaining = pThis->_dataSize - pThis->_dataIndex;
    uint32_t toRead = (remaining < buf_size) ? remaining : buf_size;
    
    memcpy(pBuf, pThis->_pJpegData + pThis->_dataIndex, toRead);
    *pBytes_actually_read = (unsigned char)toRead;
    
    pThis->_dataIndex += toRead;
    
    return 0; // 0 = success
  }

  // Copy decoded MCU from picojpeg internal buffers to output as RGB565
  void copyMCUToOutput() {
    uint8_t* pR = _info.m_pMCUBufR;
    uint8_t* pG = _info.m_pMCUBufG;
    uint8_t* pB = _info.m_pMCUBufB;
    
    int mcusX = _currentMCU % _MCUsPerRow;
    int mcusY = _currentMCU / _MCUsPerRow;
    
    int startX = mcusX * _MCUWidth;
    int startY = mcusY * _MCUHeight;
    
    // Clamp to frame bounds
    int maxX = (startX + _MCUWidth > FRAME_WIDTH) ? FRAME_WIDTH - startX : _MCUWidth;
    int maxY = (startY + _MCUHeight > FRAME_HEIGHT) ? FRAME_HEIGHT - startY : _MCUHeight;
    if (maxX <= 0 || maxY <= 0) return;
    
    // Copy pixels
    for (int y = 0; y < maxY; y++) {
      for (int x = 0; x < maxX; x++) {
        int offset = y * 8 + x;
        uint8_t r = pR[offset];
        uint8_t g = pG[offset];
        uint8_t b = pB[offset];
        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        
        int outY = startY + y;
        int outX = startX + x;
        if (outY < FRAME_HEIGHT && outX < FRAME_WIDTH) {
          int pixelIndex = outY * FRAME_WIDTH + outX;
          if (pixelIndex < (int)(_outputBufSize / 2)) {
            _pOutput[pixelIndex] = rgb565;
          }
        }
      }
    }
  }
};

// Simple MJPEG frame parser - extracts JPEG frames from MJPEG stream
class MJPEGFrameParser {
public:
  MJPEGFrameParser() {
    _pBuffer = nullptr;
    _bufferSize = 0;
    _bufferUsed = 0;
  }

  // Find next JPEG frame in stream
  // Returns: frame size, or 0 if no complete frame found
  uint32_t findNextFrame(uint8_t* pStream, uint32_t streamSize, uint8_t* pFrameBuf, uint32_t frameBufSize) {
    uint32_t frameStart = 0xFFFFFFFF;
    uint32_t frameEnd = 0xFFFFFFFF;
    bool foundFFD8 = false;
    uint32_t i = 0;

    // Find FFD8 (JPEG start)
    while (i < streamSize - 1) {
      if (pStream[i] == 0xFF && pStream[i+1] == 0xD8) {
        frameStart = i;
        foundFFD8 = true;
        break;
      }
      i++;
    }

    if (!foundFFD8) return 0;

    // Find FFD9 (JPEG end)
    i = frameStart + 2;
    while (i < streamSize - 1) {
      if (pStream[i] == 0xFF && pStream[i+1] == 0xD9) {
        frameEnd = i + 2;
        break;
      }
      i++;
    }

    if (frameEnd == 0xFFFFFFFF) return 0;

    uint32_t frameSize = frameEnd - frameStart;
    if (frameSize > frameBufSize) return 0;

    memcpy(pFrameBuf, pStream + frameStart, frameSize);
    return frameSize;
  }

private:
  uint8_t* _pBuffer;
  uint32_t _bufferSize;
  uint32_t _bufferUsed;
};

#endif // _PICOJPEGCLASS_H_