#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "display/display.h"
#include "led/single_led.h"
#include "settings.h"

#include <esp_log.h>

#define TAG "robotcabeza-esp32-inmp441"

class RobotCabezaEsp32Inmp441Board : public WifiBoard {
private:
    Button boot_button_;
    Display* display_ = nullptr;

    void RestoreOfficialXiaozhiConfiguration() {
        Settings websocket_settings("websocket", true);
        bool updated = false;

        if (!websocket_settings.GetString("url").empty()) {
            websocket_settings.EraseKey("url");
            updated = true;
        }
        if (websocket_settings.GetInt("version") != 0) {
            websocket_settings.EraseKey("version");
            updated = true;
        }
        if (!websocket_settings.GetString("token").empty()) {
            websocket_settings.EraseKey("token");
            updated = true;
        }

        Settings wifi_settings("wifi", true);
        if (!wifi_settings.GetString("ota_url").empty()) {
            wifi_settings.EraseKey("ota_url");
            updated = true;
        }

        if (updated) {
            ESP_LOGI(TAG, "Restored official Xiaozhi websocket and OTA configuration");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        boot_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            if (state == kDeviceStateAudioTesting) {
                ESP_LOGI(TAG, "Stopping local audio test and playing it back");
                app.ExitAudioTestingMode();
                return;
            }
            ESP_LOGI(TAG, "Starting local audio test");
            app.EnterAudioTestingMode();
        });
    }

public:
    RobotCabezaEsp32Inmp441Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing board");
        RestoreOfficialXiaozhiConfiguration();
        InitializeButtons();
        display_ = new NoDisplay();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            I2S_STD_SLOT_LEFT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN,
            I2S_STD_SLOT_LEFT
        );
        static bool tuned = false;
        if (!tuned) {
            audio_codec.SetInputGain(2.4f);
            tuned = true;
        }
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(RobotCabezaEsp32Inmp441Board);
