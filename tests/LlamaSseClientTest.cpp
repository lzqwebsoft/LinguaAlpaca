#include <catch2/catch.hpp>
#include "core/llama/LlamaClient.hpp"
#include "core/llama/LlamaServer.hpp"
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

    SECTION("SanitizeOcrToken filters control characters and preserves valid content") {
        // 1. 过滤引起方框 □ 乱码的 \x11 控制字符
        REQUIRE(LlamaClient::SanitizeOcrToken("\x11\x11\x11") == "");

        // 2. 混合控制字符与有效中英文测试
        REQUIRE(LlamaClient::SanitizeOcrToken("\x11浙江省\x11台州医院\x11") == "浙江省台州医院");

        // 3. 保留合法的换行符与制表符
        REQUIRE(LlamaClient::SanitizeOcrToken("第一行\n第二行\r\n第三行\t制表符") == "第一行\n第二行\r\n第三行\t制表符");

        // 4. 过滤 Unicode 替换字符 U+FFFD
        REQUIRE(LlamaClient::SanitizeOcrToken("正常文本\xEF\xBF\xBD测试") == "正常文本测试");

        // 5. 过滤其他不可打印 ASCII 控制字符 (如 0x00, 0x07, 0x1F)
        std::string dirty = std::string("A") + '\x00' + "B" + '\x07' + "C" + '\x1F' + "D";
        REQUIRE(LlamaClient::SanitizeOcrToken(dirty) == "ABCD");
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
