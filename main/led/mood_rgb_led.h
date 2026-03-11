#ifndef _MOOD_RGB_LED_H_
#define _MOOD_RGB_LED_H_

#include "led.h"
#include <driver/gpio.h>
#include <led_strip.h>
#include <esp_timer.h>
#include <mutex>

class MoodRgbLed : public Led {
public:
    explicit MoodRgbLed(gpio_num_t gpio);
    ~MoodRgbLed() override;

    void OnStateChanged() override;

private:
    std::mutex mutex_;
    led_strip_handle_t led_strip_ = nullptr;
    esp_timer_handle_t timer_ = nullptr;
    uint8_t r_ = 0;
    uint8_t g_ = 0;
    uint8_t b_ = 0;
    bool pulse_up_ = true;
    bool pulse_enabled_ = false;
    uint8_t pulse_min_scale_ = 32;
    uint8_t pulse_max_scale_ = 100;
    uint8_t pulse_scale_ = 32;

    void SetColor(uint8_t r, uint8_t g, uint8_t b);
    void ShowColor(uint8_t scale = 100);
    void TurnOff();
    void StartPulse(uint8_t min_scale, uint8_t max_scale, int interval_ms);
    void StopPulse();
};

#endif