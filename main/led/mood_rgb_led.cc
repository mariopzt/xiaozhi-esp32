#include "mood_rgb_led.h"

#include "application.h"
#include "memory_store.h"

#include <algorithm>
#include <esp_log.h>

namespace {
constexpr uint8_t kLowBrightness = 8;
constexpr uint8_t kMediumBrightness = 22;
constexpr uint8_t kHighBrightness = 40;
}

#define TAG "MoodRgbLed"

MoodRgbLed::MoodRgbLed(gpio_num_t gpio) {
    assert(gpio != GPIO_NUM_NC);

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = 1;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            auto* led = static_cast<MoodRgbLed*>(arg);
            std::lock_guard<std::mutex> lock(led->mutex_);
            if (!led->pulse_enabled_ || led->led_strip_ == nullptr) {
                return;
            }
            led->ShowColor(led->pulse_scale_);
            if (led->pulse_up_) {
                led->pulse_scale_ = std::min<uint8_t>(led->pulse_max_scale_, led->pulse_scale_ + 6);
                if (led->pulse_scale_ >= led->pulse_max_scale_) {
                    led->pulse_up_ = false;
                }
            } else {
                led->pulse_scale_ = std::max<uint8_t>(led->pulse_min_scale_, led->pulse_scale_ - 6);
                if (led->pulse_scale_ <= led->pulse_min_scale_) {
                    led->pulse_up_ = true;
                }
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mood_rgb",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_));
}

MoodRgbLed::~MoodRgbLed() {
    StopPulse();
    if (timer_ != nullptr) {
        esp_timer_delete(timer_);
    }
    if (led_strip_ != nullptr) {
        led_strip_del(led_strip_);
    }
}

void MoodRgbLed::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
}

void MoodRgbLed::ShowColor(uint8_t scale) {
    if (led_strip_ == nullptr) {
        return;
    }
    uint32_t scaled_r = static_cast<uint32_t>(r_) * scale / 100;
    uint32_t scaled_g = static_cast<uint32_t>(g_) * scale / 100;
    uint32_t scaled_b = static_cast<uint32_t>(b_) * scale / 100;
    led_strip_set_pixel(led_strip_, 0, scaled_r, scaled_g, scaled_b);
    led_strip_refresh(led_strip_);
}

void MoodRgbLed::TurnOff() {
    StopPulse();
    if (led_strip_ != nullptr) {
        led_strip_clear(led_strip_);
    }
}

void MoodRgbLed::StartPulse(uint8_t min_scale, uint8_t max_scale, int interval_ms) {
    StopPulse();
    pulse_min_scale_ = min_scale;
    pulse_max_scale_ = max_scale;
    pulse_scale_ = min_scale;
    pulse_up_ = true;
    pulse_enabled_ = true;
    ShowColor(min_scale);
    esp_timer_start_periodic(timer_, interval_ms * 1000);
}

void MoodRgbLed::StopPulse() {
    pulse_enabled_ = false;
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
    }
}

void MoodRgbLed::OnStateChanged() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    auto& memory = MemoryStore::GetInstance();

    auto mood = memory.GetSessionMood();
    if (mood.empty() || mood == "neutral") {
        mood = memory.GetRelationshipTone();
    }

    uint8_t base_r = 10;
    uint8_t base_g = 10;
    uint8_t base_b = 10;

    if (mood == "frustrated") {
        base_r = 32; base_g = 0; base_b = 0;
    } else if (mood == "playful") {
        base_r = 28; base_g = 12; base_b = 0;
    } else if (mood == "warm") {
        base_r = 30; base_g = 8; base_b = 12;
    } else if (mood == "direct") {
        base_r = 0; base_g = 10; base_b = 30;
    } else if (mood == "calm" || mood == "calm_and_brief") {
        base_r = 0; base_g = 20; base_b = 12;
    } else if (mood == "close") {
        base_r = 30; base_g = 6; base_b = 18;
    }

    switch (state) {
        case kDeviceStateStarting:
            SetColor(0, 0, 32);
            StartPulse(20, 100, 90);
            break;
        case kDeviceStateWifiConfiguring:
            SetColor(0, 0, 24);
            StartPulse(25, 100, 180);
            break;
        case kDeviceStateIdle:
            SetColor(base_r, base_g, base_b);
            StopPulse();
            ShowColor(kMediumBrightness);
            break;
        case kDeviceStateConnecting:
            SetColor(14, 0, 28);
            StartPulse(25, 100, 100);
            break;
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            if (app.IsVoiceDetected()) {
                SetColor(0, 28, 28);
                StopPulse();
                ShowColor(kHighBrightness);
            } else {
                SetColor(0, 12, 26);
                StopPulse();
                ShowColor(kMediumBrightness);
            }
            break;
        case kDeviceStateSpeaking:
            SetColor(0, 30, 8);
            StartPulse(35, 100, 90);
            break;
        case kDeviceStateUpgrading:
            SetColor(0, 28, 0);
            StartPulse(25, 100, 80);
            break;
        case kDeviceStateActivating:
            SetColor(0, 20, 0);
            StartPulse(25, 100, 180);
            break;
        default:
            ESP_LOGW(TAG, "Unknown LED state: %d", state);
            SetColor(base_r, base_g, base_b);
            StopPulse();
            ShowColor(kLowBrightness);
            break;
    }
}