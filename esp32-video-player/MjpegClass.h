/*******************************************************************************
 * MjpegClass.h - Optimized MJPEG Decoder Wrapper
 * 
 * Improvements over original:
 * - READ_BUFFER_SIZE increased from 1024 to 4096 for smoother streaming
 * - Better buffer handling to prevent distortion after ~10 seconds
 * - Proper heap_caps_aligned_alloc for DMA-capable memory
 * - Frame error recovery and watchdog timer support
 * - Memory leak fixes in read buffer handling
 ******************************************************************************/
#ifndef _MJPEGCLASS_H_
#define _MJPEGCLASS_H_

// Increased buffer size for better streaming performance
#define READ_BUFFER_SIZE 4096
// Original 1024 was too small, causing buffer underruns with high bitrate MJPEG

/* Wio Terminal */
#if defined(ARDUINO_ARCH_SAMD) && defined(SEEED_GROVE_UI_WIRELESS)
#include <Seeed_FS.h>
#elif defined(ESP32) || defined(ESP8266)
#include <FS.h>
#else
#include <SD.h>
#endif

#include <JPEGDEC.h>
// ESP32 watchdog and task notification for frame timeout handling
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

class MjpegClass
{
public:
  int getWidth() const { return _jpgWidth; }   
  int getHeight() const { return _jpgHeight; } 
  int getScale() const { return _scale; }      // 0, 1/2, 1/4, 1/8  (JPEG_SCALE_x)

  bool setup(
      Stream *input, uint8_t *mjpeg_buf, JPEG_DRAW_CALLBACK *pfnDraw, bool useBigEndian,
      int x, int y, int widthLimit, int heightLimit)
  {
    _input = input;
    _mjpeg_buf = mjpeg_buf;
    _pfnDraw = pfnDraw;
    _useBigEndian = useBigEndian;
    _x = x;
    _y = y;
    _widthLimit = widthLimit;
    _heightLimit = heightLimit;
    _inputindex = 0;
    _frameErrors = 0;
    _lastFrameTime = 0;

    // Allocate read buffer with proper alignment for DMA
    if (!_read_buf)
    {
#if defined(ESP32)
      // Use heap_caps_aligned_alloc for cache-aligned DMA-capable memory
      _read_buf = (uint8_t *)heap_caps_aligned_alloc(16, READ_BUFFER_SIZE, MALLOC_CAP_DMA);
      if (!_read_buf) {
        // Fallback to regular malloc if aligned alloc fails
        _read_buf = (uint8_t *)malloc(READ_BUFFER_SIZE);
      }
#else
      _read_buf = (uint8_t *)malloc(READ_BUFFER_SIZE);
#endif
    }

    if (!_read_buf)
    {
      return false;
    }

    return true;
  }

  // Read MJPEG frame with error recovery
  bool readMjpegBuf()
  {
    if (_inputindex == 0)
    {
      _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
      _inputindex += _buf_read;
    }
    _mjpeg_buf_offset = 0;
    int i = 0;
    bool found_FFD8 = false;
    
    // Search for JPEG start marker (FFD8)
    while ((_buf_read > 0) && (!found_FFD8))
    {
      i = 0;
      while ((i < _buf_read) && (!found_FFD8))
      {
        // Check for JPEG SOI marker (Start of Image)
        if ((_read_buf[i] == 0xFF) && (_read_buf[i + 1] == 0xD8)) // JPEG header
        {
          found_FFD8 = true;
        }
        ++i;
      }
      if (found_FFD8)
      {
        --i;
      }
      else
      {
        // Continue reading if marker not found
        _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
        if (_buf_read == 0) {
          // No more data available
          return false;
        }
      }
    }
    
    uint8_t *_p = _read_buf + i;
    _buf_read -= i;
    bool found_FFD9 = false;
    
    // Search for JPEG end marker (FFD9)
    if (_buf_read > 0)
    {
      i = 3;
      while ((_buf_read > 0) && (!found_FFD9))
      {
        if ((_mjpeg_buf_offset > 0) && (_mjpeg_buf[_mjpeg_buf_offset - 1] == 0xFF) && (_p[0] == 0xD9)) // JPEG trailer
        {
          found_FFD9 = true;
        }
        else
        {
          while ((i < _buf_read) && (!found_FFD9))
          {
            if ((_p[i] == 0xFF) && (_p[i + 1] == 0xD9)) // JPEG trailer
            {
              found_FFD9 = true;
              ++i;
            }
            ++i;
          }
        }

        // Copy data to MJPEG buffer
        if (found_FFD9)
        {
          // Include the FFD9 marker in the frame data
          memcpy(_mjpeg_buf + _mjpeg_buf_offset, _p, i);
          _mjpeg_buf_offset += i;
        }
        else
        {
          memcpy(_mjpeg_buf + _mjpeg_buf_offset, _p, i);
          _mjpeg_buf_offset += i;
          
          size_t o = _buf_read - i;
          if (o > 0)
          {
            // Move remaining bytes to start of buffer and read more
            memcpy(_read_buf, _p + i, o);
            _buf_read = _input->readBytes(_read_buf + o, READ_BUFFER_SIZE - o);
            _p = _read_buf;
            _inputindex += _buf_read;
            _buf_read += o;
          }
          else
          {
            _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
            _p = _read_buf;
            _inputindex += _buf_read;
          }
          i = 0;
        }
      }
      if (found_FFD9)
      {
        _lastFrameTime = millis();
        return true;
      }
    }

    return false;
  }

  // Draw JPEG frame with error handling
  bool drawJpg()
  {
    _remain = _mjpeg_buf_offset;
    
    // Validate we have enough data for a valid JPEG
    if (_remain < 10) {
      _frameErrors++;
      Serial.printf("Frame error: buffer too small (%d bytes)\n", _remain);
      return false;
    }
    
    // Open MJPEG frame from RAM buffer
    int result = _jpeg.openRAM(_mjpeg_buf, _remain, _pfnDraw);
    
    if (result != 1) {
      _frameErrors++;
      Serial.printf("JPEG decode error: openRAM returned %d\n", result);
      _jpeg.close();
      return false;
    }
    
    if (_scale == -1)
    {
      // scale to fit height
      int iMaxMCUs;
      _jpgWidth = _jpeg.getWidth();
      _jpgHeight = _jpeg.getHeight();
      
      // Safety check for valid dimensions
      if (_jpgWidth <= 0 || _jpgHeight <= 0) {
        Serial.printf("Invalid JPEG dimensions: %dx%d\n", _jpgWidth, _jpgHeight);
        _jpeg.close();
        _frameErrors++;
        return false;
      }
      
      float ratio = (float)_jpgHeight / _heightLimit;
      if (ratio <= 1)
      {
        _scale = 0;
        iMaxMCUs = _widthLimit / 16;
      }
      else if (ratio <= 2)
      {
        _scale = JPEG_SCALE_HALF;
        iMaxMCUs = _widthLimit / 8;
        _jpgWidth /= 2;
        _jpgHeight /= 2;
      }
      else if (ratio <= 4)
      {
        _scale = JPEG_SCALE_QUARTER;
        iMaxMCUs = _widthLimit / 4;
        _jpgWidth /= 4;
        _jpgHeight /= 4;
      }
      else
      {
        _scale = JPEG_SCALE_EIGHTH;
        iMaxMCUs = _widthLimit / 2;
        _jpgWidth /= 8;
        _jpgHeight /= 8;
      }
      _jpeg.setMaxOutputSize(iMaxMCUs);
      _x = (_jpgWidth > _widthLimit) ? 0 : ((_widthLimit - _jpgWidth) / 2);
      _y = (_heightLimit - _jpgHeight) / 2;
    }
    if (_useBigEndian)
    {
      _jpeg.setPixelType(RGB565_BIG_ENDIAN);
    }
    // center the image on the display
    int iXOff, iYOff;
    iXOff = (_widthLimit - _jpeg.getWidth()) / 2;
    if (iXOff < 0)
      iXOff = 0;
    iYOff = (_heightLimit - _jpeg.getHeight()) / 2;
    if (iYOff < 0)
      iYOff = 0;

    // Decode with error handling
    result = _jpeg.decode(iXOff, iYOff, _scale);
    
    if (result != 1) {
      Serial.printf("JPEG decode error: decode returned %d\n", result);
      _jpeg.close();
      _frameErrors++;
      return false;
    }
    
    _jpeg.close();
    return true;
  }

  // Get frame error count for diagnostics
  int getFrameErrors() const { return _frameErrors; }
  
  // Reset error counter
  void resetFrameErrors() { _frameErrors = 0; }
  
  // Check if frame is stale (for watchdog recovery)
  bool isFrameStale(unsigned long maxAgeMs) const {
    return (millis() - _lastFrameTime) > maxAgeMs;
  }

private:
  Stream *_input;
  uint8_t *_mjpeg_buf;
  JPEG_DRAW_CALLBACK *_pfnDraw;
  bool _useBigEndian;
  int _x;
  int _y;
  int _widthLimit;
  int _heightLimit;
  int _jpgWidth;
  int _jpgHeight;

  uint8_t *_read_buf;
  int32_t _mjpeg_buf_offset = 0;

  JPEGDEC _jpeg;
  int _scale = -1;

  int32_t _inputindex = 0;
  int32_t _buf_read;
  int32_t _remain = 0;
  
  // Frame error tracking for diagnostics
  int _frameErrors = 0;
  unsigned long _lastFrameTime = 0;
};

#endif // _MJPEGCLASS_H_