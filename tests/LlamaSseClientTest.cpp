#include <catch2/catch.hpp>
#include "core/LlamaClient.hpp"
#include "core/LlamaServer.hpp"
#include "core/ModelManager.hpp"
#include "core/Config.hpp"

using namespace LinguaAlpaca;

TEST_CASE("LlamaClient - Basic Unit Tests", "[core][client]") {
    LlamaClient client("http://127.0.0.1:8080");

    SECTION("Base URL setter and getter") {
        REQUIRE(client.GetBaseUrl() == "http://127.0.0.1:8080");
        client.SetBaseUrl("http://127.0.0.1:9999");
        REQUIRE(client.GetBaseUrl() == "http://127.0.0.1:9999");
        REQUIRE(client.IsRunning() == false);
    }
}

TEST_CASE("ModelManager - Health and Config Verification", "[core][model_manager]") {
    auto configManager = std::make_shared<ConfigManager>();
    auto modelManager = std::make_shared<ModelManager>(configManager);

    SECTION("Unconfigured model returns Unconfigured status") {
        auto status = modelManager->GetHealthStatus(TargetModelType::Translation);
        REQUIRE((status.state == ServerHealthState::Unconfigured ||
                 status.state == ServerHealthState::Offline));
    }
}
