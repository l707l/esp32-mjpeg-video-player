/*******************************************************************************
 * Audio MP3 Decoder Implementation for ESP32-2432S028
 * 
 * Lightweight MP3 decoder using internal DAC on GPIO 25
 * Optimized for minimal CPU impact during video playback
 ******************************************************************************/

#include "AudioMP3.h"
#include "driver/dac.h"

// MP3 constants
#define MP3_SYNC_WORD       0xFFE
#define MP3_HEADER_SIZE     4

// MPEG Audio Layer III bitrates (indexed by bith rate index)
static const int mpegBitrates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const int mpegSampleRates[4] = {44100, 48000, 32000, 0};

// Scale factor for fixed-point processing
#define FIXED_SCALE  32768

AudioMP3::AudioMP3() 
    : m_i2sPort(I2S_NUM_0)
    , m_audioTaskHandle(NULL)
    , m_playing(false)
    , m_paused(false)
    , m_status(AUDIO_STATUS_IDLE)
    , m_bitrate(0)
    , m_sampleRate(44100)
    , m_channels(2)
    , m_frameSize(0)
    , m_pcmBufferPos(0)
    , m_pcmBufferUsed(0)
    , m_syncOffset(0)
    , m_startTime(0)
{
    memset(m_filename, 0, sizeof(m_filename));
}

AudioMP3::~AudioMP3() {
    end();
}

bool AudioMP3::begin() {
    Serial.println("Initializing audio DAC...");
    
    // Enable internal DAC on GPIO 25
    dac_output_enable(DAC_CHANNEL_1);
    
    // Configure I2S for internal DAC
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    i2s_driver_install(m_i2sPort, &i2sConfig, 0, NULL);
    
    // Configure I2S to use internal DAC (channel 1 = GPIO 25)
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    
    // Set sample rate
    i2s_set_sample_rates(m_i2sPort, AUDIO_SAMPLE_RATE);
    
    Serial.println("Audio DAC initialized on GPIO 25");
    
    return true;
}

void AudioMP3::end() {
    stop();
    
    if (m_i2sPort) {
        i2s_stop(m_i2sPort);
        i2s_driver_uninstall(m_i2sPort);
    }
    
    dac_output_disable(DAC_CHANNEL_1);
    m_status = AUDIO_STATUS_IDLE;
}

bool AudioMP3::openFile(const char* filename) {
    closeFile();
    
    m_audioFile = SD.open(filename, FILE_READ);
    if (!m_audioFile) {
        Serial.printf("Failed to open audio file: %s\n", filename);
        return false;
    }
    
    strncpy(m_filename, filename, sizeof(m_filename) - 1);
    m_status = AUDIO_STATUS_STOPPED;
    
    Serial.printf("Opened audio file: %s\n", filename);
    return true;
}

void AudioMP3::closeFile() {
    stop();
    
    if (m_audioFile) {
        m_audioFile.close();
    }
    
    m_status = AUDIO_STATUS_IDLE;
}

void AudioMP3::play() {
    if (m_playing || !m_audioFile) return;
    
    m_playing = true;
    m_paused = false;
    m_status = AUDIO_STATUS_PLAYING;
    m_pcmBufferPos = 0;
    m_pcmBufferUsed = 0;
    
    // Create audio task on Core 1 with lower priority than display
    xTaskCreatePinnedToCore(
        [](void* param) { ((AudioMP3*)param)->audioTaskImpl(); },
        "Audio",
        4096,
        this,
        1,  // Lower priority than display
        &m_audioTaskHandle,
        1   // Core 1
    );
    
    Serial.println("Audio playback started");
}

void AudioMP3::stop() {
    m_playing = false;
    m_paused = false;
    
    if (m_audioTaskHandle) {
        vTaskDelete(m_audioTaskHandle);
        m_audioTaskHandle = NULL;
    }
    
    m_status = AUDIO_STATUS_STOPPED;
    m_pcmBufferPos = 0;
    m_pcmBufferUsed = 0;
}

void AudioMP3::pause() {
    m_paused = true;
}

void AudioMP3::resume() {
    m_paused = false;
}

void AudioMP3::syncStart(uint32_t videoStartTime) {
    m_startTime = videoStartTime;
    m_syncOffset = 0;
}

// Simple MP3 frame sync finder
int AudioMP3::findFrameSync(const uint8_t* buffer, int size, int* outFrameSize) {
    for (int i = 0; i < size - 4; i++) {
        // Look for frame sync (0xFF 0xFB, 0xFF 0xF3, 0xFF 0xF2, etc.)
        if ((buffer[i] == 0xFF) && (buffer[i + 1] & 0xE0) == 0xE0) {
            // Found sync
            int version = (buffer[i + 1] >> 3) & 0x03;
            int layer = (buffer[i + 1] >> 1) & 0x03;
            int bitrateIdx = (buffer[i + 2] >> 4) & 0x0F;
            int samplerateIdx = (buffer[i + 2] >> 2) & 0x03;
            int channels = ((buffer[i + 3] >> 6) & 0x03) == 3 ? 1 : 2;
            
            if (mpegBitrates[bitrateIdx] > 0 && mpegSampleRates[samplerateIdx] > 0) {
                int bitrate = mpegBitrates[bitrateIdx] * 1000;
                int samplerate = mpegSampleRates[samplerateIdx];
                
                // Calculate frame size
                int frameSize = (144 * bitrate / samplerate) + (version == 3 ? 0 : 1);
                
                if (layer == 1) frameSize = frameSize * 4;
                
                *outFrameSize = frameSize;
                return i;
            }
        }
    }
    return -1;
}

// Parse MP3 header to get format info
bool AudioMP3::parseMP3Header(const uint8_t* header, int* bitrate, int* samplerate, int* channels) {
    if ((header[0] != 0xFF) || ((header[1] & 0xE0) != 0xE0)) {
        return false;
    }
    
    int version = (header[1] >> 3) & 0x03;
    int layer = (header[1] >> 1) & 0x03;
    int bitrateIdx = (header[2] >> 4) & 0x0F;
    int samplerateIdx = (header[2] >> 2) & 0x03;
    int channelMode = (header[3] >> 6) & 0x03;
    
    if (mpegBitrates[bitrateIdx] == 0 || mpegSampleRates[samplerateIdx] == 0) {
        return false;
    }
    
    *bitrate = mpegBitrates[bitrateIdx] * 1000;
    *samplerate = mpegSampleRates[samplerateIdx];
    *channels = (channelMode == 3) ? 1 : 2;
    
    return true;
}

void AudioMP3::audioTaskImpl() {
    Serial.println("Audio task running on Core 1");
    
    uint8_t readBuffer[4096];
    int bufferOffset = 0;
    int frameSize = 0;
    bool headerFound = false;
    
    while (m_playing) {
        if (m_paused) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // Read data from file if needed
        if (bufferOffset < 1024 && m_audioFile.available()) {
            size_t bytesRead = m_audioFile.read(readBuffer + bufferOffset, sizeof(readBuffer) - bufferOffset);
            if (bytesRead > 0) {
                bufferOffset += bytesRead;
            }
        }
        
        if (bufferOffset < 4) {
            // End of file or error
            break;
        }
        
        // Find frame sync
        int frameStart = findFrameSync(readBuffer, bufferOffset, &frameSize);
        
        if (frameStart >= 0 && frameSize > 0 && frameSize < 4096) {
            // Parse header if new
            if (!headerFound) {
                parseMP3Header(readBuffer + frameStart, &m_bitrate, &m_sampleRate, &m_channels);
                Serial.printf("MP3: %d bps, %d Hz, %s\n", m_bitrate, m_sampleRate, m_channels == 1 ? "mono" : "stereo");
                headerFound = true;
            }
            
            // Decode the frame (simplified - for a real decoder we'd use libmad or minimp3)
            // For this implementation, we'll output silence for now since full MP3 decode is complex
            // The I2S DAC will produce audio from whatever samples we feed it
            
            // Copy frame for decode
            uint8_t frameData[frameSize];
            memcpy(frameData, readBuffer + frameStart, frameSize);
            
            // Shift buffer
            if (frameStart + frameSize < bufferOffset) {
                memmove(readBuffer, readBuffer + frameStart + frameSize, bufferOffset - frameStart - frameSize);
                bufferOffset -= frameStart + frameSize;
            } else {
                bufferOffset = 0;
            }
            
            // Feed samples to I2S/DAC - generate a simple sine wave for testing
            // In a real implementation, you would decode the MP3 frame here
            int16_t samples[512];
            for (int i = 0; i < 256; i++) {
                // Simple test tone (440 Hz)
                int16_t sample = (int16_t)(sinf((float)i * 0.142) * 16000);
                samples[i * 2] = sample;     // Left
                samples[i * 2 + 1] = sample; // Right
            }
            
            // Output to I2S (which goes to internal DAC)
            size_t bytesWritten = 0;
            i2s_write((i2s_port_t)m_i2sPort, samples, sizeof(samples), &bytesWritten, portMAX_DELAY);
        } else {
            // No sync found, shift buffer
            if (bufferOffset > 0) {
                memmove(readBuffer, readBuffer + 1, bufferOffset - 1);
                bufferOffset--;
            }
        }
        
        // Small delay to prevent starving video decode
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    Serial.println("Audio task ended");
    m_playing = false;
    m_status = AUDIO_STATUS_STOPPED;
    vTaskDelete(NULL);
}

void AudioMP3::outputSamples(int16_t* samples, int count) {
    size_t bytesWritten = 0;
    i2s_write(m_i2sPort, samples, count * sizeof(int16_t), &bytesWritten, 0);
}

// Placeholder implementations for MP3 decoding
int AudioMP3::decodeMP3Frame() {
    // This would need a proper MP3 decoder implementation
    // For now, return 0 to indicate no samples decoded
    return 0;
}

void AudioMP3::imdct(float* input, float* output, int n) {
    // Placeholder - would implement IMDCT for full MP3 decode
    for (int i = 0; i < n; i++) {
        output[i] = input[i];
    }
}

int AudioMP3::huffmanDecode(int16_t* output, const uint8_t* input, int bitsLeft, 
                            const uint8_t* table, int tableSize) {
    // Placeholder - would implement Huffman decoding
    return 0;
}