#include <catch2/catch.hpp>
#include <wx/wx.h>
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/ClipboardHelper.hpp"
#include "core/ScreenTextExtractor.hpp"

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
    SECTION("Calculates bottom-right anchor from selection points") {
        // Reverse selection (drag from bottom-right to top-left)
        ExtractedSelection res = ScreenTextExtractor::ExtractSelection(500, 400, 200, 100, false);
        // Anchor should always be bottom-right (maxX + 6, maxY + 8)
        REQUIRE(res.anchorX >= 500);
        REQUIRE(res.anchorY >= 400);
    }
}


