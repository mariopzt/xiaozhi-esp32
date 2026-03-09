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

    void EnsureLocalXiaozhiServerConfigured() {
        Settings websocket_settings("websocket", true);
        const auto current_url = websocket_settings.GetString("url");
        const auto current_version = websocket_settings.GetInt("version");
        bool updated = false;

        if (current_url != LOCAL_XIAOZHI_WS_URL) {
            websocket_settings.SetString("url", LOCAL_XIAOZHI_WS_URL);
            updated = true;
        }
        if (current_version != LOCAL_XIAOZHI_WS_VERSION) {
            websocket_settings.SetInt("version", LOCAL_XIAOZHI_WS_VERSION);
            updated = true;
        }
        if (!websocket_settings.GetString("token").empty()) {
            websocket_settings.EraseKey("token");
            updated = true;
        }

        Settings wifi_settings("wifi", true);
        if (wifi_settings.GetString("ota_url") != LOCAL_XIAOZHI_OTA_URL) {
            wifi_settings.SetString("ota_url", LOCAL_XIAOZHI_OTA_URL);
            updated = true;
        }

        if (updated) {
            ESP_LOGI(TAG, "Configured local Xiaozhi server ws=%s ota=%s",
                LOCAL_XIAOZHI_WS_URL, LOCAL_XIAOZHI_OTA_URL);
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
        EnsureLocalXiaozhiServerConfigured();
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
            audio_codec.SetInputGain(5.0f);
            tuned = true;
        }
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(RobotCabezaEsp32Inmp441Board);
