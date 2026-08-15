#include <catch2/catch.hpp>
#include <wx/wx.h>
#include <thread>
#include <chrono>
#include "engine/LlamaCppTranslationEngine.hpp"
#include "core/Types.hpp"

using namespace LinguaAlpaca;

TEST_CASE("LlamaCppTranslationEngine - Initialization & Invalid Path Test", "[engine]") {
    SECTION("Constructing with empty model path does not crash") {
        Engine::LlamaCppTranslationEngine engine("");
        REQUIRE(engine.IsModelLoaded() == false);
    }

    SECTION("Loading non-existent GGUF file handles cleanly and returns false") {
        Engine::LlamaCppTranslationEngine engine("");
        bool success = engine.LoadModel("non_existent_model_path.gguf");
        REQUIRE(success == false);
        REQUIRE(engine.IsModelLoaded() == false);
    }
}

TEST_CASE("LlamaCppTranslationEngine - Stream Callback Error Handling", "[engine]") {
    SECTION("Stream translation without loaded model triggers error callback") {
        Engine::LlamaCppTranslationEngine engine("");
        
        TranslationTask task("Hello world", LanguageCode::English, LanguageCode::Chinese);
        
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
            if (wxTheApp) {
                wxTheApp->ProcessPendingEvents();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        REQUIRE(finished == true);
        REQUIRE(resultSuccess == false);
        REQUIRE(resultError.find("模型未加载") != std::string::npos);
    }
}
