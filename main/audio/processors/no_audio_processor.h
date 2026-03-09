#ifndef DUMMY_AUDIO_PROCESSOR_H
#define DUMMY_AUDIO_PROCESSOR_H

#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

#include "audio_processor.h"
#include "audio_codec.h"

class NoAudioProcessor : public AudioProcessor {
public:
    NoAudioProcessor() = default;
    ~NoAudioProcessor() = default;

    void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) override;
    void Feed(std::vector<int16_t>&& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;
    void SetSpeakerActive(bool active) override;
    int GetCurrentInputLevel() const override;

private:
    AudioCodec* codec_ = nullptr;
    int frame_samples_ = 0;
    std::vector<int16_t> output_buffer_;
    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;
    std::atomic<bool> is_running_ = false;
    bool vad_speaking_ = false;
    int vad_start_frames_ = 0;
    int vad_stop_frames_ = 0;
    int64_t last_level_log_us_ = 0;
    int64_t input_warmup_until_us_ = 0;
    std::atomic<bool> speaker_active_ = false;
    std::atomic<int> current_input_level_ = 0;

    void ConditionInput(std::vector<int16_t>& mono_chunk);
    void UpdateVadState(const std::vector<int16_t>& mono_chunk);
};

#endif 
