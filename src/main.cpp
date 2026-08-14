#include <wx/wx.h>
#include <memory>
#include <iostream>
#include <cstdlib>
#include "common.h"
#include "arg.h"
#include "cli-server.h"
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

        // 2. 启动后台嵌入式 llama-server 单模型模式 (默认装载翻译模型)
        std::string transName = !currentConfig.translationModelName.empty() ? currentConfig.translationModelName : currentConfig.modelPath;
        std::string ocrName = !currentConfig.ocrModelName.empty() ? currentConfig.ocrModelName : currentConfig.ocrModelPath;

        m_llamaServer = std::make_shared<Infrastructure::Server::EmbeddedLlamaServer>();
        Infrastructure::Server::ServerConfig serverConfig;
        serverConfig.modelPath = currentConfig.modelPath;
        m_llamaServer->Start(serverConfig);

        // 3. 初始化基于 HTTP SSE 的通用推理引擎 SseLlamaEngine
        auto sseEngine = std::make_shared<Infrastructure::Engine::SseLlamaEngine>(m_llamaServer);
        if (!transName.empty()) {
            sseEngine->SetTranslationModelName(transName);
        }
        if (!ocrName.empty()) {
            sseEngine->SetOcrModelName(ocrName);
        }
        if (!currentConfig.ocrMmprojPath.empty()) {
            sseEngine->SetOcrMmprojPath(currentConfig.ocrMmprojPath);
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