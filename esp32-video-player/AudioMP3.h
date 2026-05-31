/*******************************************************************************
 * Audio MP3 Decoder for ESP32-2432S028
 * 
 * Uses internal DAC on GPIO 25 with PAM8403 amplifier
 * Lightweight MP3 decoder for minimal CPU impact
 ******************************************************************************/
#ifndef _AUDIOMP3_H_
#define _AUDIOMP3_H_

#include <Arduino.h>
#include <driver/i2s.h>

// Audio configuration
#define AUDIO_SAMPLE_RATE     44100
#define AUDIO_DAC_CHANNEL     DAC_CHANNEL_1  // GPIO 25
#define AUDIO_BUFFER_SIZE     4096
#define AUDIO_FRAME_BUFFER   8192

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
    
    // Initialize audio output (I2S -> DAC)
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

private:
    // Audio task function
    static void audioTask(void* parameter);
    void audioTaskImpl();
    
    // MP3 frame decoding
    int decodeMP3Frame();
    
    // Output samples to DAC via I2S
    void outputSamples(int16_t* samples, int count);
    
    // Parse MP3 header
    bool parseMP3Header(const uint8_t* header, int* bitrate, int* samplerate, int* channels);
    
    // Find next MP3 frame
    int findFrameSync(const uint8_t* buffer, int size, int* outFrameSize);
    
    // Decode Huffman bits
    int huffmanDecode(int16_t* output, const uint8_t* input, int bitsLeft, 
                      const uint8_t* table, int tableSize);
    
    // IMDCT implementation
    void imdct(float* input, float* output, int n);
    
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
    
    // MP3 decode state
    int m_bitrate;
    int m_sampleRate;
    int m_channels;
    int m_frameSize;
    
    // Buffer for MP3 frame
    uint8_t m_mp3Buffer[AUDIO_BUFFER_SIZE * 2];
    int16_t m_pcmBuffer[AUDIO_FRAME_BUFFER];
    int m_pcmBufferPos;
    int m_pcmBufferUsed;
    
    // Sync
    uint32_t m_syncOffset;
    uint32_t m_startTime;
};

#endif // _AUDIOMP3_H_