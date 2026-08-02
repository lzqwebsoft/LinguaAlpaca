#include <catch2/catch.hpp>
#include <thread>
#include <chrono>
#include "infrastructure/engine/LlamaCppTranslationEngine.hpp"
#include "domain/model/TranslationTask.hpp"

using namespace LinguaAlpaca;

TEST_CASE("LlamaCppTranslationEngine - Initialization & Invalid Path Test", "[infrastructure][engine]") {
    SECTION("Constructing with empty model path does not crash") {
        Infrastructure::Engine::LlamaCppTranslationEngine engine("");
        REQUIRE(engine.IsModelLoaded() == false);
    }

    SECTION("Loading non-existent GGUF file handles cleanly and returns false") {
        Infrastructure::Engine::LlamaCppTranslationEngine engine("");
        bool success = engine.LoadModel("non_existent_model_path.gguf");
        REQUIRE(success == false);
        REQUIRE(engine.IsModelLoaded() == false);
    }
}

TEST_CASE("LlamaCppTranslationEngine - Stream Callback Error Handling", "[infrastructure][engine]") {
    SECTION("Stream translation without loaded model triggers error callback") {
        Infrastructure::Engine::LlamaCppTranslationEngine engine("");
        
        Domain::Model::TranslationTask task("Hello world", Domain::Model::LanguageCode::English, Domain::Model::LanguageCode::Chinese);
        
        bool finished = false;
        bool resultSuccess = true;
        std::string resultError = "";

        engine.TranslateStreamAsync(
            task,
            [](const std::string& /*token*/) {},
            [&finished, &resultSuccess, &resultError](bool success, const std::string& /*fullText*/, const std::string& error) {
                resultSuccess = success;
                resultError = error;
                finished = true;
            }
        );

        int timeout = 50;
        while (!finished && timeout-- > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        REQUIRE(finished == true);
        REQUIRE(resultSuccess == false);
        REQUIRE(resultError.find("模型未加载") != std::string::npos);
    }

    SECTION("QuickTranslate returns formatting preview") {
        Infrastructure::Engine::LlamaCppTranslationEngine engine("");
        std::string preview = engine.QuickTranslate("Test text", Domain::Model::LanguageCode::English, Domain::Model::LanguageCode::Chinese);
        REQUIRE(preview.find("Llama.cpp") != std::string::npos);
    }
}
