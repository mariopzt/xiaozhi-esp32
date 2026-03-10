#include "no_audio_codec.h"

#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <cstdlib>

#define TAG "NoAudioCodec"

static int16_t ConvertI2s32ToI16(int32_t raw_value, float input_gain) {
    int32_t value = raw_value >>
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
        14;
#else
        12;
#endif
    if (input_gain > 0.0f) {
        int64_t amplified = static_cast<int64_t>(value * input_gain);
        if (amplified > INT16_MAX) {
            value = INT16_MAX;
        } else if (amplified < INT16_MIN) {
            value = INT16_MIN;
        } else {
            value = static_cast<int32_t>(amplified);
        }
    }
    return (value > INT16_MAX) ? INT16_MAX : (value < -INT16_MAX) ? -INT16_MAX : static_cast<int16_t>(value);
}

NoAudioCodec::~NoAudioCodec() {
    if (rx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    if (tx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    }
}

NoAudioCodecDuplex::NoAudioCodecDuplex(int input_sample_rate, int output_sample_rate, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    duplex_ = true;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
			#ifdef   I2S_HW_VERSION_2
				.ext_clk_freq_hz = 0,
			#endif

        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            #ifdef   I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
            #endif

        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Duplex channels created");
}


NoAudioCodecSimplex::NoAudioCodecSimplex(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din) {
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // Create a new channel for speaker
    i2s_chan_config_t chan_cfg = {
        .id = (i2s_port_t)0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
			#ifdef   I2S_HW_VERSION_2
				.ext_clk_freq_hz = 0,
			#endif

        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            #ifdef   I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
            #endif

        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    // Create a new channel for MIC
    chan_cfg.id = (i2s_port_t)1;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));
    std_cfg.clk_cfg.sample_rate_hz = (uint32_t)input_sample_rate_;
    std_cfg.gpio_cfg.bclk = mic_sck;
    std_cfg.gpio_cfg.ws = mic_ws;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = mic_din;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Simplex channels created");
}

NoAudioCodecSimplex::NoAudioCodecSimplex(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, i2s_std_slot_mask_t spk_slot_mask, gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din, i2s_std_slot_mask_t mic_slot_mask){
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // Create a new channel for speaker
    i2s_chan_config_t chan_cfg = {
        .id = (i2s_port_t)0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
			#ifdef   I2S_HW_VERSION_2
				.ext_clk_freq_hz = 0,
			#endif

        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = spk_slot_mask,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            #ifdef   I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
            #endif

        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    // Create a new channel for MIC
    chan_cfg.id = (i2s_port_t)1;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));
    std_cfg.clk_cfg.sample_rate_hz = (uint32_t)input_sample_rate_;
    std_cfg.slot_cfg.slot_mode = mic_slot_mask == I2S_STD_SLOT_BOTH ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask = mic_slot_mask;
    std_cfg.gpio_cfg.bclk = mic_sck;
    std_cfg.gpio_cfg.ws = mic_ws;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = mic_din;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    rx_capture_stereo_ = mic_slot_mask == I2S_STD_SLOT_BOTH;
    ESP_LOGI(TAG, "Simplex channels created");
}

int NoAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    std::vector<int32_t> buffer(samples);

    // output_volume_: 0-100
    // volume_factor_: 0-65536
    int32_t volume_factor = pow(double(output_volume_) / 100.0, 2) * 65536;
    for (int i = 0; i < samples; i++) {
        int64_t temp = int64_t(data[i]) * volume_factor; // 使用 int64_t 进行乘法运算
        if (temp > INT32_MAX) {
            buffer[i] = INT32_MAX;
        } else if (temp < INT32_MIN) {
            buffer[i] = INT32_MIN;
        } else {
            buffer[i] = static_cast<int32_t>(temp);
        }
    }

    size_t bytes_written;
    ESP_ERROR_CHECK(i2s_channel_write(tx_handle_, buffer.data(), samples * sizeof(int32_t), &bytes_written, portMAX_DELAY));
    return bytes_written / sizeof(int32_t);
}

int NoAudioCodec::Read(int16_t* dest, int samples) {
    size_t bytes_read;
    constexpr TickType_t kReadTimeoutTicks = pdMS_TO_TICKS(200);

    size_t channels = rx_capture_stereo_ ? 2 : 1;
    std::vector<int32_t> bit32_buffer(samples * channels);
    if (i2s_channel_read(rx_handle_, bit32_buffer.data(), bit32_buffer.size() * sizeof(int32_t), &bytes_read, kReadTimeoutTicks) != ESP_OK) {
        return 0;
    }

    if (!rx_capture_stereo_) {
        samples = bytes_read / sizeof(int32_t);
        for (int i = 0; i < samples; i++) {
            dest[i] = ConvertI2s32ToI16(bit32_buffer[i], input_gain_);
        }
        return samples;
    }

    size_t frames = bytes_read / (sizeof(int32_t) * 2);
    int64_t left_energy = 0;
    int64_t right_energy = 0;
    int left_clipped = 0;
    int right_clipped = 0;
    std::vector<int16_t> left_frame(frames);
    std::vector<int16_t> right_frame(frames);

    for (size_t i = 0; i < frames; ++i) {
        int16_t left = ConvertI2s32ToI16(bit32_buffer[i * 2], input_gain_);
        int16_t right = ConvertI2s32ToI16(bit32_buffer[i * 2 + 1], input_gain_);
        left_frame[i] = left;
        right_frame[i] = right;
        left_energy += std::abs(static_cast<int>(left));
        right_energy += std::abs(static_cast<int>(right));
        left_clipped += (left == INT16_MAX || left == -INT16_MAX) ? 1 : 0;
        right_clipped += (right == INT16_MAX || right == -INT16_MAX) ? 1 : 0;
    }

    bool use_right = false;
    if (left_clipped != right_clipped) {
        use_right = right_clipped < left_clipped;
    } else if (left_energy != right_energy) {
        use_right = right_energy > left_energy;
    }

    const auto& selected = use_right ? right_frame : left_frame;
    for (size_t i = 0; i < frames; ++i) {
        dest[i] = selected[i];
    }
    return static_cast<int>(frames);
}

void NoAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    } else {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    AudioCodec::EnableInput(enable);
}

void NoAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    } else {
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    }
    AudioCodec::EnableOutput(enable);
}

// Delegating constructor: calls the main constructor with default slot mask
NoAudioCodecSimplexPdm::NoAudioCodecSimplexPdm(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, gpio_num_t mic_sck, gpio_num_t mic_din) 
    : NoAudioCodecSimplexPdm(input_sample_rate, output_sample_rate, spk_bclk, spk_ws, spk_dout, I2S_STD_SLOT_LEFT, mic_sck, mic_din) {
    // All initialization is handled by the delegated constructor
}

NoAudioCodecSimplexPdm::NoAudioCodecSimplexPdm(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, i2s_std_slot_mask_t spk_slot_mask, gpio_num_t mic_sck, gpio_num_t mic_din) {
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // Create a new channel for speaker
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)1, I2S_ROLE_MASTER);
    tx_chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    tx_chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    tx_chan_cfg.auto_clear_after_cb = true;
    tx_chan_cfg.auto_clear_before_cb = false;
    tx_chan_cfg.intr_priority = 0;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_handle_, NULL));


    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
			#ifdef   I2S_HW_VERSION_2
				.ext_clk_freq_hz = 0,
			#endif

        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = spk_slot_mask,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            #ifdef   I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
            #endif

        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_std_cfg));
#if SOC_I2S_SUPPORTS_PDM_RX
    // Create a new channel for MIC in PDM mode
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_handle_));
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG((uint32_t)input_sample_rate_),
        /* The data bit-width of PDM mode is fixed to 16 */
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = mic_sck,
            .din = mic_din,

            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_handle_, &pdm_rx_cfg));
#else
    ESP_LOGE(TAG, "PDM is not supported");
#endif
    ESP_LOGI(TAG, "Simplex channels created");
}

int NoAudioCodecSimplexPdm::Read(int16_t* dest, int samples) {
    size_t bytes_read;

    // PDM 解调后的数据位宽为 16 位，直接读取到目标缓冲区
    if (i2s_channel_read(rx_handle_, dest, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Read Failed!");
        return 0;
    }

    samples = bytes_read / sizeof(int16_t);
    if (input_gain_ > 0) {
        int gain_factor = (int)input_gain_;
        for (int i = 0; i < samples; i++) {
            int32_t amplified = dest[i] * gain_factor;
            dest[i] = (amplified > INT16_MAX) ? INT16_MAX : (amplified < -INT16_MAX) ? -INT16_MAX : (int16_t)amplified;
        }
    }
    return samples;
}
