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

    if (!init_i2s(audio_info_.sample_rate, audio_info_.channels)) {
        ESP_LOGE(TAG, "I2S init failed, draining audio queue");
        goto cleanup;
    }

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

cleanup:
    drain_queue();
    sync_.audio_eos = true;

    ESP_LOGI(TAG, "audio_task done");
}

}  // namespace mp4

#endif // BOARD_HAS_AUDIO
