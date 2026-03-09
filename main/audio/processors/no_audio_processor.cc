#include "no_audio_processor.h"
#include <esp_log.h>
#include <cstdlib>
#include <esp_timer.h>

#define TAG "NoAudioProcessor"

namespace {
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
// Plain ESP32 + INMP441 needs a gentler VAD so short syllables are not cut off.
constexpr int kVadThresholdIdle = 24;
constexpr int kVadThresholdWithSpeaker = 260;
constexpr int kVadStopFramesIdle = 48;
constexpr int kVadStartFramesWithSpeaker = 6;
constexpr int kVadStopFramesWithSpeaker = 14;
#else
constexpr int kVadThresholdIdle = 600;
constexpr int kVadThresholdWithSpeaker = 1800;
constexpr int kVadStopFramesIdle = 12;
constexpr int kVadStartFramesWithSpeaker = 8;
constexpr int kVadStopFramesWithSpeaker = 14;
#endif
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
constexpr int kVadStartFrames = 3;
#else
constexpr int kVadStartFrames = 4;
#endif
}

void NoAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;
    output_buffer_.reserve(frame_samples_);
}

void NoAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (!is_running_) {
        return;
    }

    std::vector<int16_t> mono_chunk;
    mono_chunk.reserve(codec_->input_channels() == 2 ? data.size() / 2 : data.size());

    // Convert stereo to mono if needed
    if (codec_->input_channels() == 2) {
        for (size_t i = 0, j = 0; i < data.size() / 2; ++i, j += 2) {
            mono_chunk.push_back(data[j]);
        }
    } else {
        mono_chunk = std::move(data);
    }

    ConditionInput(mono_chunk);
    UpdateVadState(mono_chunk);

    if (!output_callback_) {
        return;
    }

    output_buffer_.insert(output_buffer_.end(), mono_chunk.begin(), mono_chunk.end());

    // Output complete frames when buffer has enough data
    while (output_buffer_.size() >= (size_t)frame_samples_) {
        if (output_buffer_.size() == (size_t)frame_samples_) {
            output_callback_(std::move(output_buffer_));
            output_buffer_.clear();
            output_buffer_.reserve(frame_samples_);
        } else {
            output_callback_(std::vector<int16_t>(output_buffer_.begin(), output_buffer_.begin() + frame_samples_));
            output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
        }
    }
}

void NoAudioProcessor::ConditionInput(std::vector<int16_t>& mono_chunk) {
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
    if (mono_chunk.empty()) {
        return;
    }

    int64_t sum = 0;
    int peak_abs = 0;
    int clipped = 0;
    for (int16_t sample : mono_chunk) {
        sum += sample;
        int value = std::abs(static_cast<int>(sample));
        if (value > peak_abs) {
            peak_abs = value;
        }
        if (value >= 32760) {
            clipped++;
        }
    }

    int dc_offset = static_cast<int>(sum / static_cast<int64_t>(mono_chunk.size()));
    for (auto& sample : mono_chunk) {
        int adjusted = static_cast<int>(sample) - dc_offset;
        sample = adjusted > INT16_MAX ? INT16_MAX : adjusted < INT16_MIN ? INT16_MIN : static_cast<int16_t>(adjusted);
    }

    int64_t sum_abs = 0;
    peak_abs = 0;
    for (int16_t sample : mono_chunk) {
        int value = std::abs(static_cast<int>(sample));
        sum_abs += value;
        if (value > peak_abs) {
            peak_abs = value;
        }
    }

    int avg_abs = static_cast<int>(sum_abs / static_cast<int64_t>(mono_chunk.size()));
    if (avg_abs >= 30 && peak_abs > 0 && peak_abs < 10000) {
        float scale = std::min(3.0f, 10000.0f / static_cast<float>(peak_abs));
        for (auto& sample : mono_chunk) {
            int adjusted = static_cast<int>(sample * scale);
            sample = adjusted > INT16_MAX ? INT16_MAX : adjusted < INT16_MIN ? INT16_MIN : static_cast<int16_t>(adjusted);
        }
        peak_abs = static_cast<int>(peak_abs * scale);
    }

    if (peak_abs > 12000) {
        float scale = 12000.0f / static_cast<float>(peak_abs);
        for (auto& sample : mono_chunk) {
            int adjusted = static_cast<int>(sample * scale);
            sample = adjusted > INT16_MAX ? INT16_MAX : adjusted < INT16_MIN ? INT16_MIN : static_cast<int16_t>(adjusted);
        }
    } else if (clipped > 0) {
        for (auto& sample : mono_chunk) {
            sample /= 2;
        }
    }
#else
    (void)mono_chunk;
#endif
}

void NoAudioProcessor::Start() {
    is_running_ = true;
    vad_speaking_ = false;
    vad_start_frames_ = 0;
    vad_stop_frames_ = 0;
    last_level_log_us_ = 0;
}

void NoAudioProcessor::Stop() {
    is_running_ = false;
    output_buffer_.clear();
    if (vad_speaking_ && vad_state_change_callback_) {
        vad_state_change_callback_(false);
    }
    vad_speaking_ = false;
    vad_start_frames_ = 0;
    vad_stop_frames_ = 0;
}

bool NoAudioProcessor::IsRunning() {
    return is_running_;
}

void NoAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void NoAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

size_t NoAudioProcessor::GetFeedSize() {
    if (!codec_) {
        return 0;
    }
    return frame_samples_;
}

void NoAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
        ESP_LOGE(TAG, "Device AEC is not supported");
    }
}

void NoAudioProcessor::SetSpeakerActive(bool active) {
    speaker_active_.store(active, std::memory_order_relaxed);
}

int NoAudioProcessor::GetCurrentInputLevel() const {
    return current_input_level_.load(std::memory_order_relaxed);
}

void NoAudioProcessor::UpdateVadState(const std::vector<int16_t>& mono_chunk) {
    if (mono_chunk.empty() || !vad_state_change_callback_) {
        return;
    }

    int64_t sum_abs = 0;
    int peak_abs = 0;
    for (int16_t sample : mono_chunk) {
        int value = std::abs((int)sample);
        sum_abs += value;
        if (value > peak_abs) {
            peak_abs = value;
        }
    }

    int avg_abs = (int)(sum_abs / (int64_t)mono_chunk.size());
    current_input_level_.store(avg_abs, std::memory_order_relaxed);
    bool speaker_active = speaker_active_.load(std::memory_order_relaxed);
#if !CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
    speaker_active = speaker_active || (codec_ != nullptr && codec_->output_enabled());
#endif
    int threshold = speaker_active ? kVadThresholdWithSpeaker : kVadThresholdIdle;
    int vad_start_frames_target = speaker_active ? kVadStartFramesWithSpeaker : kVadStartFrames;
    int vad_stop_frames_target = speaker_active ? kVadStopFramesWithSpeaker : kVadStopFramesIdle;
    bool above_threshold = false;
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
    // INMP441 on plain ESP32 can produce isolated full-scale spikes on one sample.
    // Use average energy here so brief spikes do not keep the turn open forever.
    above_threshold = avg_abs >= threshold;
#else
    bool peak_trigger = peak_abs >= threshold * 2;
    above_threshold = avg_abs >= threshold || peak_trigger;
#endif

#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
    int64_t now_us = esp_timer_get_time();
    if (last_level_log_us_ == 0 || now_us - last_level_log_us_ >= 1000000) {
        ESP_LOGI(TAG, "MIC avg=%d peak=%d threshold=%d output=%d vad=%d",
                 avg_abs, peak_abs, threshold,
                 speaker_active,
                 above_threshold);
        last_level_log_us_ = now_us;
    }
#endif

    if (above_threshold) {
        vad_start_frames_++;
        vad_stop_frames_ = 0;
        if (!vad_speaking_ && vad_start_frames_ >= vad_start_frames_target) {
            vad_speaking_ = true;
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
            ESP_LOGI(TAG, "VAD speech start");
#endif
            vad_state_change_callback_(true);
        }
        return;
    }

    vad_start_frames_ = 0;
    if (vad_speaking_) {
        vad_stop_frames_++;
        if (vad_stop_frames_ >= vad_stop_frames_target) {
            vad_speaking_ = false;
            vad_stop_frames_ = 0;
#if CONFIG_BOARD_TYPE_ROBOTCABEZA_ESP32_INMP441
            ESP_LOGI(TAG, "VAD speech stop");
#endif
            vad_state_change_callback_(false);
        }
    }
}
