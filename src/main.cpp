#include <wx/wx.h>
#include <memory>
#include <iostream>
#include "domain/model/AppConfig.hpp"
#include "infrastructure/server/EmbeddedLlamaServer.hpp"
#include "infrastructure/engine/SseLlamaEngine.hpp"
#include "infrastructure/repository/InMemoryHistoryRepository.hpp"
#include "infrastructure/repository/IniConfigRepository.hpp"
#include "application/service/ConfigurationService.hpp"
#include "application/service/TranslationService.hpp"
#include "application/service/OcrService.hpp"
#include "presentation/views/MainFrame.hpp"
#include "presentation/components/WelcomeModelDialog.hpp"

using namespace LinguaAlpaca;

class LinguaAlpacaApp : public wxApp {
private:
    std::shared_ptr<Infrastructure::Server::EmbeddedLlamaServer> m_llamaServer;

public:
    bool OnInit() override {
        wxInitAllImageHandlers();

        // 1. 初始化 ConfigurationService (读取 config.ini 持久化文件)
        auto configRepo = std::make_shared<Infrastructure::Repository::IniConfigRepository>();
        auto configService = std::make_shared<Application::Service::ConfigurationService>(configRepo);
        auto currentConfig = configService->GetConfig();

        // 2. 动态生成 models_preset.ini 映射配置文件并启动后台嵌入式 llama-server (Router Mode)
        std::string presetPath = Infrastructure::Server::GenerateModelsPresetFile(currentConfig, "models_preset.ini");

        m_llamaServer = std::make_shared<Infrastructure::Server::EmbeddedLlamaServer>();
        Infrastructure::Server::ServerConfig serverConfig;
        serverConfig.modelsDir = currentConfig.modelsDir.empty() ? "./models" : currentConfig.modelsDir;
        serverConfig.modelsPreset = presetPath;
        serverConfig.maxLoadedModels = 1;
        m_llamaServer->Start(serverConfig);

        // 3. 初始化基于 HTTP SSE 的通用推理引擎 SseLlamaEngine
        auto sseEngine = std::make_shared<Infrastructure::Engine::SseLlamaEngine>(m_llamaServer);
        if (!currentConfig.translationModelName.empty()) {
            sseEngine->SetTranslationModelName(currentConfig.translationModelName);
        } else if (!currentConfig.modelPath.empty()) {
            sseEngine->SetTranslationModelName(currentConfig.modelPath);
        }

        if (!currentConfig.ocrModelName.empty()) {
            sseEngine->SetOcrModelName(currentConfig.ocrModelName);
        } else if (!currentConfig.ocrModelPath.empty()) {
            sseEngine->SetOcrModelName(currentConfig.ocrModelPath);
        }

        // 4. 初始化历史服务与依赖注入 Services
        auto historyRepo = std::make_shared<Infrastructure::Repository::InMemoryHistoryRepository>();
        auto translationService = std::make_shared<Application::Service::TranslationService>(sseEngine, historyRepo, configService);
        auto ocrService = std::make_shared<Application::Service::OcrService>(sseEngine);

        // 5. 创建与居中显示 Presentation 主界面
        Presentation::Views::MainFrame* mainFrame = new Presentation::Views::MainFrame(translationService, ocrService);
        mainFrame->Centre();
        mainFrame->Show(true);

        return true;
    }

    int OnExit() override {
        if (m_llamaServer) {
            std::cout << "[LinguaAlpacaApp] Stopping embedded llama-server..." << std::endl;
            m_llamaServer->Stop();
        }
        return wxApp::OnExit();
    }
};

wxIMPLEMENT_APP(LinguaAlpacaApp);