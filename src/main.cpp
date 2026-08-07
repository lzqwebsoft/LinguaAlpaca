#include <wx/wx.h>
#include <memory>
#include "domain/model/AppConfig.hpp"
#include "infrastructure/engine/LlamaCppTranslationEngine.hpp"
#include "infrastructure/engine/LlamaCppOcrEngine.hpp"
#include "infrastructure/repository/InMemoryHistoryRepository.hpp"
#include "infrastructure/repository/IniConfigRepository.hpp"
#include "application/service/ConfigurationService.hpp"
#include "application/service/TranslationService.hpp"
#include "application/service/OcrService.hpp"
#include "presentation/views/MainFrame.hpp"
#include "presentation/components/WelcomeModelDialog.hpp"

using namespace LinguaAlpaca;

class LinguaAlpacaApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();

        // 1. 初始化 ConfigurationService (读取 config.ini 持久化文件)
        auto configRepo = std::make_shared<Infrastructure::Repository::IniConfigRepository>();
        auto configService = std::make_shared<Application::Service::ConfigurationService>(configRepo);
        auto currentConfig = configService->GetConfig();

        // 2. 初始化历史服务
        auto historyRepo = std::make_shared<Infrastructure::Repository::InMemoryHistoryRepository>();

        // 3. 初始化 翻译 原生引擎与 Services
        auto llamaEngine = std::make_shared<Infrastructure::Engine::LlamaCppTranslationEngine>();
        auto translationService = std::make_shared<Application::Service::TranslationService>(llamaEngine, historyRepo, configService);

        // 4 初始化 OCR 原生引擎与 Service
        auto ocrEngine = std::make_shared<Infrastructure::Engine::LlamaCppOcrEngine>();
        auto ocrService = std::make_shared<Application::Service::OcrService>(ocrEngine);

        // 5. 自动恢复上次存盘的翻译模型配置
        if (!currentConfig.modelPath.empty()) {
            translationService->LoadModel(currentConfig.modelPath);
        }

        // 6. 自动恢复上次存盘的 OCR 模型配置
        if (!currentConfig.ocrModelPath.empty() && !currentConfig.ocrMmprojPath.empty()) {
            ocrService->LoadModel(currentConfig.ocrModelPath, currentConfig.ocrMmprojPath);
        }

        // 7. 创建与居中显示 Presentation 主界面
        Presentation::Views::MainFrame* mainFrame = new Presentation::Views::MainFrame(translationService, ocrService);
        mainFrame->Centre();
        mainFrame->Show(true);

        // 8. 启动检查：若文本翻译模型未成功加载，弹出原生模态对话弹框提示用户
        if (!translationService->IsModelLoaded()) {
            Presentation::Components::WelcomeModelDialog dialog(mainFrame);
            int res = dialog.ShowModal();

            if (res == wxID_OK && dialog.ShouldNavigateToSettings()) {
                mainFrame->NavigateToSettings();
            }
        }

        return true;
    }
};

wxIMPLEMENT_APP(LinguaAlpacaApp);