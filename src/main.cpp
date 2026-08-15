#include <wx/wx.h>
#include <memory>
#include <iostream>

#include "core/Config.hpp"
#include "core/ModelManager.hpp"
#include "ui/MainFrame.hpp"

using namespace LinguaAlpaca;

class LinguaAlpacaApp : public wxApp {
private:
    std::shared_ptr<ModelManager> m_modelManager;

public:
    bool OnInit() override {
        wxInitAllImageHandlers();

        // 1. 初始化配置管理器与统一模型推理调度中枢
        auto configManager = std::make_shared<ConfigManager>();
        m_modelManager = std::make_shared<ModelManager>(configManager);

        // 2. 创建并居中显示主界面
        UI::MainFrame* mainFrame = new UI::MainFrame(m_modelManager);
        mainFrame->Centre();
        mainFrame->Show(true);

        return true;
    }

    int OnExit() override {
        if (m_modelManager) {
            std::cout << "[LinguaAlpacaApp] Stopping model manager..." << std::endl;
        }
        return wxApp::OnExit();
    }
};

wxIMPLEMENT_APP(LinguaAlpacaApp);