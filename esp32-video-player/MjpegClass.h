/*******************************************************************************
 * JPEGDEC Wrapper Class - Improved version
 * 
 * Changes:
 * - Better buffer boundary handling
 * - Support for corrupt MJPEG recovery
 * - Auto-scaling based on display size
 * - Dynamic quality adaptation on low memory
 ******************************************************************************/
#ifndef _MJPEGCLASS_H_
#define _MJPEGCLASS_H_

#define READ_BUFFER_SIZE 2048
#define MAXOUTPUTSIZE (MAX_BUFFERED_PIXELS / 16 / 16)

#if defined(ESP32) || defined(ESP8266)
#include <FS.h>
#else
#include <SD.h>
#endif

#include <JPEGDEC.h>

class MjpegClass
{
public:
  int getWidth() const { return _jpgWidth; }   
  int getHeight() const { return _jpgHeight; } 
  int getScale() const { return _scale; }

  // Setup with auto-scale for display fitment
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
    _frameCount = 0;
    _errorCount = 0;

    if (!_read_buf) {
      _read_buf = (uint8_t *)malloc(READ_BUFFER_SIZE);
    }

    if (!_read_buf) {
      return false;
    }

    return true;
  }

  // Read next JPEG frame from stream
  bool readMjpegBuf()
  {
    if (_inputindex == 0) {
      _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
      _inputindex += _buf_read;
    }
    _mjpeg_buf_offset = 0;
    int i = 0;
    bool found_FFD8 = false;
    
    while ((_buf_read > 0) && (!found_FFD8))
    {
      i = 0;
      while ((i < _buf_read - 1) && (!found_FFD8))
      {
        if ((_read_buf[i] == 0xFF) && (_read_buf[i + 1] == 0xD8))
        {
          found_FFD8 = true;
        }
        ++i;
      }
      if (!found_FFD8 && _buf_read > 0)
      {
        _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
        _inputindex += _buf_read;
      }
    }
    
    if (!found_FFD8) {
      _errorCount++;
      return false;
    }

    uint8_t *_p = _read_buf + i;
    _buf_read -= i;
    bool found_FFD9 = false;
    
    if (_buf_read > 0)
    {
      i = 0;
      while ((_buf_read > 0) && (!found_FFD9))
      {
        if ((_mjpeg_buf_offset > 0) && (_mjpeg_buf[_mjpeg_buf_offset - 1] == 0xFF) && (_p[0] == 0xD9))
        {
          found_FFD9 = true;
        }
        else
        {
          int searchEnd = (_buf_read < 512) ? _buf_read : 512;
          while ((i < searchEnd - 1) && (!found_FFD9))
          {
            if ((_p[i] == 0xFF) && (_p[i + 1] == 0xD9))
            {
              found_FFD9 = true;
              ++i;
            }
            ++i;
          }
        }

        // Copy data to MJPEG buffer
        int copySize = (_buf_read < _mjpeg_buf_offset + READ_BUFFER_SIZE * 2) ? _buf_read : 0;
        if (copySize > 0 && _mjpeg_buf_offset + copySize < 400000) {
          memcpy(_mjpeg_buf + _mjpeg_buf_offset, _p, i);
          _mjpeg_buf_offset += i;
        }

        size_t o = _buf_read - i;
        if (o > 0 && _buf_read > i)
        {
          memcpy(_read_buf, _p + i, _buf_read - i);
          _buf_read = _input->readBytes(_read_buf + (_buf_read - i), READ_BUFFER_SIZE - (_buf_read - i));
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
      
      _frameCount++;
      if (found_FFD9) {
        return true;
      }
    }

    return false;
  }

  // Draw the current JPEG frame
  bool drawJpg()
  {
    if (_mjpeg_buf_offset == 0) {
      return false;
    }
    
    _remain = _mjpeg_buf_offset;
    _jpeg.openRAM(_mjpeg_buf, _remain, _pfnDraw);
    
    if (_scale == -1)
    {
      int iMaxMCUs;
      _jpgWidth = _jpeg.getWidth();
      _jpgHeight = _jpeg.getHeight();
      
      float ratioW = (float)_jpgWidth / _widthLimit;
      float ratioH = (float)_jpgHeight / _heightLimit;
      float ratio = max(ratioW, ratioH);
      
      if (ratio <= 1) {
        _scale = 0;
        iMaxMCUs = _widthLimit / 16;
      } else if (ratio <= 2) {
        _scale = JPEG_SCALE_HALF;
        iMaxMCUs = _widthLimit / 8;
        _jpgWidth /= 2;
        _jpgHeight /= 2;
      } else if (ratio <= 4) {
        _scale = JPEG_SCALE_QUARTER;
        iMaxMCUs = _widthLimit / 4;
        _jpgWidth /= 4;
        _jpgHeight /= 4;
      } else {
        _scale = JPEG_SCALE_EIGHTH;
        iMaxMCUs = _widthLimit / 2;
        _jpgWidth /= 8;
        _jpgHeight /= 8;
      }
      _jpeg.setMaxOutputSize(iMaxMCUs);
      _x = (_jpgWidth > _widthLimit) ? 0 : ((_widthLimit - _jpgWidth) / 2);
      _y = (_heightLimit - _jpgHeight) / 2;
    }
    
    if (_useBigEndian) {
      _jpeg.setPixelType(RGB565_BIG_ENDIAN);
    }
    
    int iXOff = (_widthLimit - _jpeg.getWidth()) / 2;
    if (iXOff < 0) iXOff = 0;
    int iYOff = (_heightLimit - _jpeg.getHeight()) / 2;
    if (iYOff < 0) iYOff = 0;

    _jpeg.decode(iXOff, iYOff, _scale);
    _jpeg.close();

    return true;
  }

  int getFrameCount() const { return _frameCount; }
  int getErrorCount() const { return _errorCount; }
  void resetStats() { _frameCount = 0; _errorCount = 0; }

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
  int _frameCount = 0;
  int _errorCount = 0;
};

#endif // _MJPEGCLASS_H_