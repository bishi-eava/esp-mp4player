#include "board_config.h"

#ifdef BOARD_HAS_AUDIO

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include "esp_audio_dec_default.h"
#include "esp_aac_dec.h"
#include "mp4_player.h"

static const char *TAG = "audio";

// PCM output buffer: 1024 samples * 2 channels * 2 bytes = 4096 bytes
#define PCM_BUF_SIZE  (1024 * 2 * sizeof(int16_t))

static i2s_chan_handle_t s_tx_chan = nullptr;

static esp_err_t init_i2s(unsigned sample_rate, unsigned channels)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 512;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
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

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized: %u Hz, %u ch", sample_rate, channels);
    return ESP_OK;
}

static void deinit_i2s(void)
{
    if (s_tx_chan) {
        i2s_channel_disable(s_tx_chan);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
    }
}

void audio_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    QueueHandle_t queue = ctx->audio_queue;

    ESP_LOGI(TAG, "audio_task: waiting for demux metadata...");

    // Wait for first audio message from demux_task (same pattern as decode_task)
    // By this point, demux_task has set audio_sample_rate/channels
    audio_msg_t first_msg;
    if (xQueuePeek(queue, &first_msg, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed waiting for first audio message");
        goto drain_and_exit;
    }

    // If first message is EOS (no audio track found), exit immediately
    if (first_msg.eos) {
        ESP_LOGI(TAG, "No audio data, exiting");
        xQueueReceive(queue, &first_msg, 0);  // consume EOS
        goto drain_and_exit;
    }

    ESP_LOGI(TAG, "audio_task started: %u Hz, %u ch",
             ctx->audio_sample_rate, ctx->audio_channels);

    if (init_i2s(ctx->audio_sample_rate, ctx->audio_channels) != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed, draining audio queue");
        goto drain_and_exit;
    }

    {
        // Register AAC decoder
        esp_audio_dec_register_default();

        // Open AAC decoder
        esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        aac_cfg.no_adts_header = true;  // MP4 contains raw AAC frames without ADTS

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
            goto drain_and_exit;
        }

        ESP_LOGI(TAG, "AAC decoder initialized");

        // PCM output buffer in internal RAM for DMA performance
        uint8_t *pcm_buf = (uint8_t *)heap_caps_malloc(PCM_BUF_SIZE, MALLOC_CAP_INTERNAL);
        if (!pcm_buf) {
            ESP_LOGE(TAG, "Failed to allocate PCM buffer");
            esp_audio_dec_close(dec_handle);
            deinit_i2s();
            goto drain_and_exit;
        }

        unsigned decoded_frames = 0;

        audio_msg_t msg;
        while (true) {
            if (xQueueReceive(queue, &msg, pdMS_TO_TICKS(5000)) != pdTRUE) {
                ESP_LOGW(TAG, "Audio queue receive timeout");
                continue;
            }

            if (msg.eos) {
                ESP_LOGI(TAG, "Audio EOS received");
                break;
            }

            // Decode AAC frame to PCM
            esp_audio_dec_in_raw_t in_raw = {};
            in_raw.buffer = msg.data;
            in_raw.len    = msg.size;

            esp_audio_dec_out_frame_t out_frame = {};
            out_frame.buffer = pcm_buf;
            out_frame.len    = PCM_BUF_SIZE;

            aerr = esp_audio_dec_process(dec_handle, &in_raw, &out_frame);
            heap_caps_free(msg.data);

            if (aerr != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(TAG, "AAC decode error: %d", aerr);
                continue;
            }

            if (out_frame.decoded_size > 0) {
                size_t bytes_written = 0;
                i2s_channel_write(s_tx_chan, pcm_buf, out_frame.decoded_size,
                                  &bytes_written, portMAX_DELAY);
                decoded_frames++;
            }
        }

        ESP_LOGI(TAG, "Audio playback complete: %u frames decoded", decoded_frames);

        heap_caps_free(pcm_buf);
        esp_audio_dec_close(dec_handle);
    }

    deinit_i2s();

drain_and_exit:
    {
        audio_msg_t msg;
        while (xQueueReceive(queue, &msg, 0) == pdTRUE) {
            if (msg.data) heap_caps_free(msg.data);
            if (msg.eos) break;
        }
    }
    ctx->audio_eos = true;

    ESP_LOGI(TAG, "audio_task done");
    vTaskDelete(nullptr);
}

#endif // BOARD_HAS_AUDIO
