#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <thread>
#include <chrono>
#include "application/service/TranslationService.hpp"
#include "infrastructure/engine/MockTranslationEngine.hpp"
#include "infrastructure/repository/InMemoryHistoryRepository.hpp"

using namespace LinguaAlpaca;

TEST_CASE("TranslationService - Basic Translation Test", "[application][service]") {
    auto engine = std::make_shared<Infrastructure::Engine::MockTranslationEngine>();
    auto historyRepo = std::make_shared<Infrastructure::Repository::InMemoryHistoryRepository>();
    Application::Service::TranslationService service(engine, historyRepo);

    SECTION("Translating known greeting string returns expected output") {
        Application::DTO::TranslationRequestDto req;
        req.text = "Hello, welcome to LinguaAlpaca!";
        req.sourceLanguage = Domain::Model::LanguageCode::English;
        req.targetLanguage = Domain::Model::LanguageCode::Chinese;

        auto resp = service.ExecuteTranslation(req);

        REQUIRE(resp.success == true);
        REQUIRE(resp.translatedText == "你好，欢迎使用灵驼译翻译器！");
        REQUIRE(resp.sourceCharCount == 31);
        REQUIRE(historyRepo->GetAllRecords().size() == 1);
    }

    SECTION("Translating empty string handles cleanly") {
        Application::DTO::TranslationRequestDto req;
        req.text = "";
        req.sourceLanguage = Domain::Model::LanguageCode::English;
        req.targetLanguage = Domain::Model::LanguageCode::Chinese;

        auto resp = service.ExecuteTranslation(req);

        REQUIRE(resp.success == true);
        REQUIRE(resp.translatedText == "");
        REQUIRE(resp.sourceCharCount == 0);
    }
}

TEST_CASE("TranslationService - Stream Translation Test", "[application][service]") {
    auto engine = std::make_shared<Infrastructure::Engine::MockTranslationEngine>();
    auto historyRepo = std::make_shared<Infrastructure::Repository::InMemoryHistoryRepository>();
    Application::Service::TranslationService service(engine, historyRepo);

    SECTION("Streaming translation streams tokens asynchronously") {
        Application::DTO::TranslationRequestDto req;
        req.text = "Hello World";
        req.sourceLanguage = Domain::Model::LanguageCode::English;
        req.targetLanguage = Domain::Model::LanguageCode::Chinese;

        std::string accumulated = "";
        bool finished = false;

        service.ExecuteStreamTranslation(
            req,
            [&accumulated](const std::string& token) {
                accumulated += token;
            },
            [&finished](bool success, const std::string& fullText, const std::string& error) {
                REQUIRE(success == true);
                finished = true;
            }
        );

        // 等待异步流执行完成
        int timeout = 50;
        while (!finished && timeout-- > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        REQUIRE(finished == true);
        REQUIRE(accumulated == "你好，世界");
    }
}

TEST_CASE("TranslationService - Quick Preview Test", "[application][service]") {
    auto engine = std::make_shared<Infrastructure::Engine::MockTranslationEngine>();
    auto historyRepo = std::make_shared<Infrastructure::Repository::InMemoryHistoryRepository>();
    Application::Service::TranslationService service(engine, historyRepo);

    std::string preview = service.QuickPreview("Hello", Domain::Model::LanguageCode::English, Domain::Model::LanguageCode::Chinese);
    REQUIRE(preview == "你好，欢迎使用灵驼译！");
}
