#include <catch2/catch.hpp>
#include "infrastructure/engine/SseLlamaEngine.hpp"
#include "infrastructure/server/EmbeddedLlamaServer.hpp"

using namespace LinguaAlpaca;

TEST_CASE("SseLlamaEngine - Basic Unit Tests", "[infrastructure][engine]") {
    Infrastructure::Engine::SseLlamaEngine engine("http://127.0.0.1:8080");

    SECTION("Model name setters and getters") {
        engine.SetTranslationModelName("Hy-MT2-1.8B-GGUF");
        engine.SetOcrModelName("PaddleOCR-VL-1.6.gguf");

        REQUIRE(engine.GetTranslationModelName() == "Hy-MT2-1.8B-GGUF");
        REQUIRE(engine.GetOcrModelName() == "PaddleOCR-VL-1.6.gguf");
        REQUIRE(engine.IsModelLoaded() == true);
    }
}
