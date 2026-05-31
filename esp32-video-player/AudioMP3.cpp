/*******************************************************************************
 * Audio MP3 Decoder Implementation for ESP32-2432S028
 * 
 * Uses internal DAC on GPIO 25 + PAM8403 amplifier
 * Libhelix MP3 decoder for minimal CPU impact
 ******************************************************************************/

#include "AudioMP3.h"
#include "libhelix/MP3DecoderHelix.h"

// MP3 Decoder instance
static MP3DecoderHelix* s_mp3Decoder = nullptr;
static AudioMP3* s_audioInstance = nullptr;

// Callback when MP3 frame is decoded
static void mp3DataCallback(MP3FrameInfo& info, short* pcm_buffer, size_t pcmSamples, void* ref) {
    if (s_audioInstance && pcmSamples > 0) {
        s_audioInstance->writePCM(pcm_buffer, pcmSamples);
    }
}

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
    Serial.println("Initializing audio with libhelix MP3 decoder...");
    
    // Enable internal DAC on GPIO 25
    dac_output_enable(DAC_CHANNEL_1);
    
    // Configure I2S for internal DAC (built-in DAC mode)
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
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    i2s_set_sample_rates(m_i2sPort, AUDIO_SAMPLE_RATE);
    
    Serial.println("Audio DAC initialized on GPIO 25 (I2S -> Built-in DAC)");
    
    return true;
}

void AudioMP3::end() {
    stop();
    
    if (m_i2sPort != I2S_NUM_0) return;
    i2s_stop(m_i2sPort);
    i2s_driver_uninstall(m_i2sPort);
    
    dac_output_disable(DAC_CHANNEL_1);
    
    if (s_mp3Decoder) {
        delete s_mp3Decoder;
        s_mp3Decoder = nullptr;
    }
    
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
    
    if (s_mp3Decoder) {
        s_mp3Decoder->end();
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
    
    // Create MP3 decoder with callback
    if (!s_mp3Decoder) {
        s_mp3Decoder = new MP3DecoderHelix(mp3DataCallback);
        s_mp3Decoder->setMaxFrameSize(1600);  // ~128kbps max frame
        s_mp3Decoder->setMaxPCMSize(8192);
        s_mp3Decoder->begin();
    }
    
    s_audioInstance = this;
    
    // Create audio task on Core 1 with lower priority than display
    xTaskCreatePinnedToCore(
        [](void* param) { ((AudioMP3*)param)->audioTaskImpl(); },
        "Audio",
        8192,  // More stack for MP3 decoding
        this,
        1,  // Lower priority than display
        &m_audioTaskHandle,
        1   // Core 1
    );
    
    Serial.println("Audio playback started (libhelix MP3)");
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

void AudioMP3::writePCM(short* pcmSamples, size_t count) {
    // Output PCM samples to I2S/DAC
    size_t bytesWritten = 0;
    i2s_write(m_i2sPort, pcmSamples, count * sizeof(short), &bytesWritten, portMAX_DELAY);
}

void AudioMP3::audioTaskImpl() {
    Serial.println("Audio task started (libhelix decoder)");
    
    uint8_t mp3Chunk[2048];
    int id3SkipCount = 0;
    bool firstFrame = true;
    
    // Skip possible ID3v2 header at start
    if (m_audioFile.available() >= 10) {
        uint8_t header[10];
        m_audioFile.read(header, 10);
        // ID3v2 tag: "ID3" = 0x49 0x44 0x33
        if (!(header[0] == 'I' && header[1] == 'D' && header[2] == '3')) {
            // Not ID3, seek back
            m_audioFile.seek(m_audioFile.position() - 10);
        } else {
            // ID3v2, skip rest of header and data
            uint32_t tagSize = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) 
                             | ((header[8] & 0x7F) << 7) | (header[9] & 0x7F);
            tagSize += 10; // include header
            Serial.printf("Skipping ID3v2 tag: %u bytes\n", tagSize);
            m_audioFile.seek(tagSize);
        }
    }
    
    while (m_playing) {
        if (m_paused) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // Read MP3 data from SD
        if (m_audioFile.available() > 0) {
            int toRead = min((int)sizeof(mp3Chunk), m_audioFile.available());
            int bytesRead = m_audioFile.read(mp3Chunk, toRead);
            
            if (bytesRead > 0 && s_mp3Decoder) {
                // Write to MP3 decoder (libhelix handles frame sync, decode, callback)
                s_mp3Decoder->write(mp3Chunk, bytesRead);
                
                if (firstFrame) {
                    MP3FrameInfo info = s_mp3Decoder->audioInfo();
                    if (info.bitrate > 0) {
                        Serial.printf("MP3: %d bps, %d Hz, ch=%d\n", 
                            info.bitrate, info.samprate, info.nChans);
                        firstFrame = false;
                    }
                }
            }
        } else {
            // End of file
            break;
        }
        
        // Small yield to prevent starving video
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Flush any remaining decoded audio
    if (s_mp3Decoder) {
        s_mp3Decoder->flush();
    }
    
    Serial.println("Audio task ended");
    m_playing = false;
    m_status = AUDIO_STATUS_STOPPED;
    vTaskDelete(NULL);
}

// Placeholder - not used with libhelix callback approach
int AudioMP3::decodeMP3Frame() {
    return 0;
}

void AudioMP3::imdct(float* input, float* output, int n) {
    for (int i = 0; i < n; i++) output[i] = input[i];
}

int AudioMP3::huffmanDecode(int16_t* output, const uint8_t* input, int bitsLeft, 
                            const uint8_t* table, int tableSize) {
    return 0;
}
