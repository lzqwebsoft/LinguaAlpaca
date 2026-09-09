#include <catch2/catch.hpp>
#include <wx/wx.h>
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/ClipboardHelper.hpp"
#include "core/ScreenTextExtractor.hpp"
#include "core/WinTtsHelper.hpp"

using namespace LinguaAlpaca;

TEST_CASE("ConfigManager - Selection Translate Configuration Serialization", "[core][selection_config]") {
    auto configManager = std::make_shared<ConfigManager>();

    SECTION("Save and reload selection translation settings") {
        configManager->SaveSelectionConfig(true, 1, 2, false);
        AppConfig cfg = configManager->GetConfig();

        REQUIRE(cfg.selectionTranslateEnabled == true);
        REQUIRE(cfg.selectionTriggerMode == 1);
        REQUIRE(cfg.selectionModifierKey == 2);
        REQUIRE(cfg.preserveClipboard == false);

        // Reset to default
        configManager->SaveSelectionConfig(true, 0, 0, true);
        AppConfig resetCfg = configManager->GetConfig();
        REQUIRE(resetCfg.selectionTriggerMode == 0);
        REQUIRE(resetCfg.preserveClipboard == true);
    }
}

TEST_CASE("ClipboardHelper - UTF-8 Text Setting and Retrieval", "[core][clipboard]") {
    SECTION("Set and retrieve text from clipboard") {
        std::string testText = "LinguaAlpaca 划词翻译测试文本 123";
        bool ok = ClipboardHelper::SetClipboardText(testText);
        REQUIRE(ok == true);

        std::string retrieved = ClipboardHelper::GetClipboardText();
        REQUIRE(retrieved == testText);
    }
}

TEST_CASE("Logger - Real-time notification and memory history", "[core][logger]") {
    SECTION("Log and receive via listener") {
        Logger& logger = Logger::GetInstance();
        logger.ClearLogs();

        bool received = false;
        std::string receivedMsg;
        size_t id = logger.AddListener([&](const LogMessage& msg) {
            received = true;
            receivedMsg = msg.message;
        });

        logger.Info("UnitTest", "Test log message for logger");

        REQUIRE(received == true);
        REQUIRE(receivedMsg == "Test log message for logger");

        auto history = logger.GetRecentLogs();
        REQUIRE(history.empty() == false);
        REQUIRE(history.back().tag == "UnitTest");

        logger.RemoveListener(id);
    }

    SECTION("Log configuration persistence") {
        auto configManager = std::make_shared<ConfigManager>();
        configManager->SaveLogConfig(true);
        REQUIRE(configManager->GetConfig().saveLogToFile == true);
        REQUIRE(Logger::GetInstance().IsFileLoggingEnabled() == true);

        configManager->SaveLogConfig(false);
        REQUIRE(configManager->GetConfig().saveLogToFile == false);
        REQUIRE(Logger::GetInstance().IsFileLoggingEnabled() == false);
    }
}

TEST_CASE("ScreenTextExtractor - Anchor coordinate calculation", "[core][extractor]") {
    SECTION("Calculates anchor from mouse release points") {
        ExtractedSelection res = ScreenTextExtractor::ExtractSelection(500, 400, 200, 100, false);
        // Anchor should always be mouse up position (endX, endY)
        REQUIRE(res.anchorX == 200);
        REQUIRE(res.anchorY == 100);
    }
}

TEST_CASE("WinTtsHelper - Basic TTS controls", "[core][tts]") {
    SECTION("Empty text speak returns false") {
        WinTtsHelper& tts = WinTtsHelper::GetInstance();
        REQUIRE(tts.Speak("") == false);
        REQUIRE(tts.Speak(L"") == false);
    }

    SECTION("Stop and volume rate adjustments") {
        WinTtsHelper& tts = WinTtsHelper::GetInstance();
        tts.SetRate(0);
        tts.SetVolume(100);
        tts.Stop();
        REQUIRE(tts.IsSpeaking() == false);
    }
}

#include "core/SelectionService.hpp"

TEST_CASE("SelectionService - Lifecycle and Config Management", "[core][selection_service]") {
    auto configManager = std::make_shared<ConfigManager>();
    SelectionService service(configManager);

    SECTION("Initial state is not running") {
        REQUIRE(service.IsRunning() == false);
    }

    SECTION("Start and Stop lifecycle") {
        bool started = service.Start();
        REQUIRE(started == true);
        REQUIRE(service.IsRunning() == true);

        // Re-starting when already running returns true
        REQUIRE(service.Start() == true);

        service.Stop();
        REQUIRE(service.IsRunning() == false);
    }

    SECTION("Callback registration and config update") {
        bool callbackInvoked = false;
        service.SetCallback([&](int, int, const std::string&) {
            callbackInvoked = true;
        });

        AppConfig cfg = configManager->GetConfig();
        cfg.selectionTranslateEnabled = false;
        cfg.selectionTriggerMode = 2;
        service.ApplyConfig(cfg);

        // Service stopped clean
        service.Stop();
        REQUIRE(service.IsRunning() == false);
    }
}

TEST_CASE("WinTtsHelper - Text to Speech Functionality", "[core][tts]") {
    WinTtsHelper& tts = WinTtsHelper::GetInstance();

    SECTION("Empty text handling") {
        REQUIRE(tts.Speak("", LanguageCode::AutoDetect) == false);
        REQUIRE(tts.Speak(std::wstring(L""), LanguageCode::AutoDetect) == false);
    }

    SECTION("Rate and volume adjustments") {
        tts.SetRate(5);
        tts.SetVolume(80);
        tts.SetRate(-5);
        tts.SetVolume(100);
    }

    SECTION("Speak and Stop lifecycle") {
        bool ok = tts.Speak("LinguaAlpaca TTS Test", LanguageCode::English);
        REQUIRE(ok == true);
        tts.Stop();
        REQUIRE(tts.IsSpeaking() == false);

        bool okZh = tts.Speak(L"灵驼翻译朗读测试", LanguageCode::Chinese);
        REQUIRE(okZh == true);
        tts.Stop();
        REQUIRE(tts.IsSpeaking() == false);
    }
}
