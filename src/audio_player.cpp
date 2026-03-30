#include "board_config.h"

#ifdef BOARD_HAS_AUDIO

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "esp_audio_dec_default.h"
#include "esp_aac_dec.h"
#include "mp4_player.h"

#ifdef BOARD_AUDIO_CODEC_ES8311
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#endif

static const char *TAG = "audio";

namespace mp4 {

void AudioPipeline::task_func(void *arg)
{
    auto *self = static_cast<AudioPipeline *>(arg);
    self->run();
    xEventGroupSetBits(self->sync_.task_done, PipelineSync::kAudioDone);
    delete self;
    vTaskDelete(nullptr);
}

#ifdef BOARD_AUDIO_CODEC_ES8311

static i2c_master_bus_handle_t codec_i2c_bus_ = nullptr;
static i2c_master_dev_handle_t codec_i2c_dev_ = nullptr;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(codec_i2c_dev_, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t es8311_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(codec_i2c_dev_, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

// Clock coefficient table from official Espressif ES8311 driver
// MCLK derived from BCLK: mclk = sample_rate * bits * 2
struct es8311_coeff {
    uint32_t mclk;
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
};

static const es8311_coeff kCoeffDiv[] = {
    // mclk       rate   pre_div pre_multi adc_div dac_div fs lrck_h lrck_l bclk adc_osr dac_osr
    {1411200,  44100, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400,  44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000,  48000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000,  48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000,  32000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000,  32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000,  16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {512000,    8000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {768000,   24000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {705600,   22050, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

static const es8311_coeff *find_coeff(uint32_t mclk, uint32_t rate)
{
    for (size_t i = 0; i < sizeof(kCoeffDiv) / sizeof(kCoeffDiv[0]); i++) {
        if (kCoeffDiv[i].mclk == mclk && kCoeffDiv[i].rate == rate) return &kCoeffDiv[i];
    }
    return nullptr;
}

static bool init_audio_codec(unsigned sample_rate)
{
    ESP_LOGI(TAG, "Initializing ES8311 audio codec via I2C");

    // Init I2C master bus (new driver API, compatible with LovyanGFX)
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = BOARD_CODEC_I2C_PORT;
    bus_cfg.sda_io_num = BOARD_CODEC_I2C_SDA;
    bus_cfg.scl_io_num = BOARD_CODEC_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &codec_i2c_bus_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BOARD_CODEC_I2C_ADDR;
    dev_cfg.scl_speed_hz = BOARD_CODEC_I2C_FREQ;

    ret = i2c_master_bus_add_device(codec_i2c_bus_, &dev_cfg, &codec_i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(codec_i2c_bus_);
        codec_i2c_bus_ = nullptr;
        return false;
    }

    // Following the official Espressif es8311_init() sequence
    // MCLK from BCLK: mclk = sample_rate * 16bit * 2ch
    uint32_t mclk = sample_rate * 16 * 2;
    const es8311_coeff *coeff = find_coeff(mclk, sample_rate);
    if (!coeff) {
        ESP_LOGE(TAG, "No ES8311 clock coefficient for %uHz (mclk=%lu)", sample_rate, mclk);
        return false;
    }

    // Reset
    es8311_write_reg(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(0x00, 0x00);
    es8311_write_reg(0x00, 0x80);  // Power-on, CSM enabled

    // Clock config: MCLK from SCLK pin (bit7=1), enable all clocks
    es8311_write_reg(0x01, 0xBF);  // 0x3F | BIT(7) = MCLK from BCLK

    // Clock dividers (from coeff table)
    uint8_t reg02 = ((coeff->pre_div - 1) << 5) | (coeff->pre_multi << 3);
    es8311_write_reg(0x02, reg02);
    es8311_write_reg(0x03, (coeff->fs_mode << 6) | coeff->adc_osr);
    es8311_write_reg(0x04, coeff->dac_osr);
    es8311_write_reg(0x05, ((coeff->adc_div - 1) << 4) | (coeff->dac_div - 1));

    // BCLK divider
    uint8_t reg06 = (coeff->bclk_div < 19) ? (coeff->bclk_div - 1) : coeff->bclk_div;
    es8311_write_reg(0x06, reg06);

    // LRCK divider
    es8311_write_reg(0x07, coeff->lrck_h);
    es8311_write_reg(0x08, coeff->lrck_l);

    // SDP format: slave mode, I2S 16-bit
    uint8_t reg00;
    es8311_read_reg(0x00, &reg00);
    reg00 &= 0xBF;  // Slave mode (clear bit6)
    es8311_write_reg(0x00, reg00);
    es8311_write_reg(0x09, 0x0C);  // SDP In:  16-bit I2S (bits[3:2]=11 for 16bit)
    es8311_write_reg(0x0A, 0x0C);  // SDP Out: 16-bit I2S

    // Power up
    es8311_write_reg(0x0D, 0x01);  // Power up analog circuitry
    es8311_write_reg(0x0E, 0x02);  // Enable analog PGA, ADC modulator
    es8311_write_reg(0x12, 0x00);  // Power up DAC
    es8311_write_reg(0x13, 0x10);  // Enable output to HP drive
    es8311_write_reg(0x1C, 0x6A);  // ADC EQ bypass, cancel DC offset
    es8311_write_reg(0x37, 0x08);  // Bypass DAC equalizer

    // DAC hardware volume: attenuate before amplifier to prevent clipping
    es8311_write_reg(0x32, BOARD_CODEC_DAC_VOLUME);

    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "ES8311 codec initialized: %uHz, mclk=%lu", sample_rate, mclk);
    return true;
}

static void enable_amplifier(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BOARD_AMP_EN_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);
    gpio_set_level(BOARD_AMP_EN_PIN, 1);
    ESP_LOGI(TAG, "NS4150B amplifier enabled (GPIO %d)", BOARD_AMP_EN_PIN);
}

static void disable_amplifier(void)
{
    gpio_set_level(BOARD_AMP_EN_PIN, 0);
}

static void deinit_audio_codec(void)
{
    disable_amplifier();
    es8311_write_reg(0x13, 0x00);  // Power down DAC
    if (codec_i2c_dev_) {
        i2c_master_bus_rm_device(codec_i2c_dev_);
        codec_i2c_dev_ = nullptr;
    }
    if (codec_i2c_bus_) {
        i2c_del_master_bus(codec_i2c_bus_);
        codec_i2c_bus_ = nullptr;
    }
}

#endif // BOARD_AUDIO_CODEC_ES8311

bool AudioPipeline::init_i2s(unsigned sample_rate, unsigned channels)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = kI2sDmaDescNum;
    chan_cfg.dma_frame_num = kI2sDmaFrameNum;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_chan_, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return false;
    }

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = BOARD_I2S_BCLK;
    std_cfg.gpio_cfg.ws   = BOARD_I2S_LRCLK;
    std_cfg.gpio_cfg.dout = BOARD_I2S_DOUT;
    std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

    ret = i2s_channel_init_std_mode(tx_chan_, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_chan_);
        tx_chan_ = nullptr;
        return false;
    }

    ret = i2s_channel_enable(tx_chan_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_chan_);
        tx_chan_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "I2S initialized: %u Hz, %u ch", sample_rate, channels);
    return true;
}

void AudioPipeline::deinit_i2s()
{
    if (tx_chan_) {
        i2s_channel_disable(tx_chan_);
        i2s_del_channel(tx_chan_);
        tx_chan_ = nullptr;
    }
}

void AudioPipeline::drain_queue()
{
    AudioMsg msg;
    while (xQueueReceive(sync_.audio_queue, &msg, 0) == pdTRUE) {
        safe_free(msg.data);
        if (msg.eos) break;
    }
}

void AudioPipeline::run()
{
    QueueHandle_t queue = sync_.audio_queue;

    ESP_LOGI(TAG, "audio_task: waiting for demux metadata...");

    {
        AudioMsg first_msg;
        bool got_msg = false;
        while (!got_msg && !sync_.stop_requested) {
            got_msg = (xQueuePeek(queue, &first_msg, pdMS_TO_TICKS(500)) == pdTRUE);
        }
        if (!got_msg) {
            ESP_LOGI(TAG, "Stop requested before audio data arrived");
            goto cleanup;
        }

        if (first_msg.eos) {
            ESP_LOGI(TAG, "No audio data, exiting");
            xQueueReceive(queue, &first_msg, 0);
            goto cleanup;
        }
    }

    ESP_LOGI(TAG, "audio_task started: %u Hz, %u ch",
             audio_info_.sample_rate, audio_info_.channels);

#ifdef BOARD_AUDIO_CODEC_ES8311
    if (!init_audio_codec(audio_info_.sample_rate)) {
        ESP_LOGE(TAG, "ES8311 codec init failed, draining audio queue");
        goto cleanup;
    }
#endif

    if (!init_i2s(audio_info_.sample_rate, audio_info_.channels)) {
        ESP_LOGE(TAG, "I2S init failed, draining audio queue");
#ifdef BOARD_AUDIO_CODEC_ES8311
        deinit_audio_codec();
#endif
        goto cleanup;
    }

#ifdef BOARD_AUDIO_CODEC_ES8311
    enable_amplifier();
#endif

    {
        esp_audio_dec_register_default();

        esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        aac_cfg.no_adts_header = true;

        esp_audio_dec_cfg_t dec_cfg = {
            .type = ESP_AUDIO_TYPE_AAC,
            .cfg = &aac_cfg,
            .cfg_sz = sizeof(aac_cfg),
        };

        esp_audio_dec_handle_t dec_handle = nullptr;
        esp_audio_err_t aerr = esp_audio_dec_open(&dec_cfg, &dec_handle);
        if (aerr != ESP_AUDIO_ERR_OK || !dec_handle) {
            ESP_LOGE(TAG, "AAC decoder open failed: %d", aerr);
            deinit_i2s();
            goto cleanup;
        }

        ESP_LOGI(TAG, "AAC decoder initialized");

        uint8_t *pcm_buf = static_cast<uint8_t *>(internal_malloc(kPcmBufSize));
        if (!pcm_buf) {
            ESP_LOGE(TAG, "Failed to allocate PCM buffer");
            esp_audio_dec_close(dec_handle);
            deinit_i2s();
            goto cleanup;
        }

        unsigned decoded_frames = 0;
        int64_t total_dec_us = 0, total_i2s_us = 0;

        AudioMsg msg;
        while (true) {
            if (sync_.stop_requested) {
                ESP_LOGI(TAG, "Stop requested, exiting audio loop");
                break;
            }

            if (xQueueReceive(queue, &msg, pdMS_TO_TICKS(500)) != pdTRUE) {
                continue;  // will re-check stop_requested at top
            }

            if (msg.eos) {
                ESP_LOGI(TAG, "Audio EOS received");
                break;
            }

            esp_audio_dec_in_raw_t in_raw = {};
            in_raw.buffer = msg.data;
            in_raw.len    = msg.size;

            esp_audio_dec_out_frame_t out_frame = {};
            out_frame.buffer = pcm_buf;
            out_frame.len    = kPcmBufSize;

            int64_t t0 = esp_timer_get_time();
            aerr = esp_audio_dec_process(dec_handle, &in_raw, &out_frame);
            total_dec_us += esp_timer_get_time() - t0;
            psram_free(msg.data);

            if (aerr != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(TAG, "AAC decode error: %d", aerr);
                continue;
            }

            if (out_frame.decoded_size > 0) {
                // Apply volume scaling
                int vol = sync_.audio_volume;
                if (vol == 0) {
                    memset(pcm_buf, 0, out_frame.decoded_size);
                } else if (vol < 256) {
                    int16_t *samples = reinterpret_cast<int16_t *>(pcm_buf);
                    int num_samples = out_frame.decoded_size / sizeof(int16_t);
                    for (int i = 0; i < num_samples; i++) {
                        samples[i] = (int16_t)((samples[i] * vol) >> 8);
                    }
                }
                // vol==256: full volume, no scaling needed

                // I2S write with stop check (avoid portMAX_DELAY blocking)
                int64_t t_i2s = esp_timer_get_time();
                size_t remaining = out_frame.decoded_size;
                uint8_t *ptr = pcm_buf;
                while (remaining > 0 && !sync_.stop_requested) {
                    size_t written = 0;
                    i2s_channel_write(tx_chan_, ptr, remaining,
                                      &written, pdMS_TO_TICKS(100));
                    ptr += written;
                    remaining -= written;
                }
                total_i2s_us += esp_timer_get_time() - t_i2s;
                // Report playback position for A/V sync
                sync_.audio_playback_pts_ms = (int32_t)(msg.pts_us / 1000);
                decoded_frames++;
            }
        }

        ESP_LOGI(TAG, "Audio playback complete: %u frames decoded", decoded_frames);
        ESP_LOGI(TAG, "Audio timing: aac_dec=%lldms i2s_write=%lldms",
                 total_dec_us / 1000, total_i2s_us / 1000);

        safe_free(pcm_buf);
        esp_audio_dec_close(dec_handle);
    }

    deinit_i2s();
#ifdef BOARD_AUDIO_CODEC_ES8311
    deinit_audio_codec();
#endif

cleanup:
    drain_queue();
    sync_.audio_eos = true;

    ESP_LOGI(TAG, "audio_task done");
}

}  // namespace mp4

#endif // BOARD_HAS_AUDIO
