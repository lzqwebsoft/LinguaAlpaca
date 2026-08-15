#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <wx/wx.h>
#include <thread>
#include <chrono>

#include "core/ModelManager.hpp"
#include "core/Config.hpp"

using namespace LinguaAlpaca;

int main(int argc, char* argv[]) {
    wxInitializer initializer;
    int result = Catch::Session().run(argc, argv);
    return result;
}

TEST_CASE("ModelManager - Basic Stream Translation Test", "[core][model_manager]") {
    auto configManager = std::make_shared<ConfigManager>();
    ModelManager manager(configManager);

    SECTION("Empty text task completes immediately with empty result") {
        TranslationTask task("", LanguageCode::English, LanguageCode::Chinese);

        bool done = false;
        manager.ExecuteTranslationStream(
            task,
            [](const std::string& /*token*/) {},
            [&done](bool success, const std::string& fullText, const std::string& /*error*/) {
                REQUIRE(success == true);
                REQUIRE(fullText == "");
                done = true;
            }
        );
        REQUIRE(done == true);
    }
}
