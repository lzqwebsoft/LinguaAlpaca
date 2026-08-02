#include <wx/wx.h>
#include <memory>
#include "domain/model/AppConfig.hpp"
#include "infrastructure/engine/LlamaCppTranslationEngine.hpp"
#include "infrastructure/repository/InMemoryHistoryRepository.hpp"
#include "infrastructure/repository/IniConfigRepository.hpp"
#include "application/service/ConfigurationService.hpp"
#include "application/service/TranslationService.hpp"
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

        // 2. 初始化 LlamaCpp 离线大模型原生引擎与 Services
        auto llamaEngine = std::make_shared<Infrastructure::Engine::LlamaCppTranslationEngine>();
        auto historyRepo = std::make_shared<Infrastructure::Repository::InMemoryHistoryRepository>();
        auto translationService = std::make_shared<Application::Service::TranslationService>(llamaEngine, historyRepo, configService);

        // 3. 自动恢复上次存盘的模型配置
        if (!currentConfig.modelPath.empty()) {
            translationService->LoadModel(currentConfig.modelPath);
        }

        // 4. 创建与居中显示 Presentation 主界面
        Presentation::Views::MainFrame* mainFrame = new Presentation::Views::MainFrame(translationService);
        mainFrame->Centre();
        mainFrame->Show(true);

        // 5. 启动检查：若量化模型未成功加载，弹出原生模态对话弹框提示用户
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