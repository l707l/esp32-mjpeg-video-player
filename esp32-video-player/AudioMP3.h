/*******************************************************************************
 * Audio MP3 Decoder for ESP32-2432S028
 * 
 * Uses internal DAC on GPIO 25 with PAM8403 amplifier
 * Libhelix MP3 decoder for minimal CPU impact
 ******************************************************************************/
#ifndef _AUDIOMP3_H_
#define _AUDIOMP3_H_

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/dac.h>
#include "libhelix/MP3DecoderHelix.h"

// Audio configuration
#define AUDIO_SAMPLE_RATE     44100
#define AUDIO_DAC_CHANNEL     DAC_CHANNEL_1  // GPIO 25

// MP3 decoding status
enum AudioStatus {
    AUDIO_STATUS_IDLE = 0,
    AUDIO_STATUS_PLAYING,
    AUDIO_STATUS_STOPPED,
    AUDIO_STATUS_ERROR
};

class AudioMP3 {
public:
    AudioMP3();
    ~AudioMP3();
    
    // Initialize audio output (I2S -> Built-in DAC)
    bool begin();
    
    // Stop audio playback
    void end();
    
    // Open MP3 file for playback
    bool openFile(const char* filename);
    
    // Close current file
    void closeFile();
    
    // Start audio playback task on Core 1
    void play();
    
    // Stop playback
    void stop();
    
    // Pause/Resume
    void pause();
    void resume();
    
    // Check if audio is playing
    bool isPlaying() { return m_playing; }
    
    // Get current status
    AudioStatus getStatus() { return m_status; }
    
    // Sync with video (approximate)
    void syncStart(uint32_t videoStartTime);
    uint32_t getSyncOffset() { return m_syncOffset; }

    // Called by MP3 decoder callback to output PCM samples
    void writePCM(short* pcmSamples, size_t count);

private:
    // Audio task function
    static void audioTask(void* parameter);
    void audioTaskImpl();
    
    // Placeholder - libhelix handles decode
    int decodeMP3Frame();
    void imdct(float* input, float* output, int n);
    int huffmanDecode(int16_t* output, const uint8_t* input, int bitsLeft, 
                      const uint8_t* table, int tableSize);
    
    // Output I2S configuration
    i2s_port_t m_i2sPort;
    TaskHandle_t m_audioTaskHandle;
    
    // File handling
    File m_audioFile;
    char m_filename[256];
    
    // Playback state
    volatile bool m_playing;
    volatile bool m_paused;
    AudioStatus m_status;
    
    // Audio format
    int m_bitrate;
    int m_sampleRate;
    int m_channels;
    int m_frameSize;
    
    // PCM buffer state
    int m_pcmBufferPos;
    int m_pcmBufferUsed;
    
    // Sync
    uint32_t m_syncOffset;
    uint32_t m_startTime;
};

#endif // _AUDIOMP3_H_
