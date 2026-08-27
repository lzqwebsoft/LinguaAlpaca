#include <wx/wx.h>
#include <memory>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/Logger.hpp"
#include "core/Config.hpp"
#include "core/ModelManager.hpp"
#include "core/SelectionService.hpp"
#include "ui/widgets/SplashScreen.hpp"
#include "ui/widgets/FloatingIconFrame.hpp"
#include "ui/widgets/TranslationBubbleFrame.hpp"
#include "ui/MainFrame.hpp"

using namespace LinguaAlpaca;

class LinguaAlpacaApp : public wxApp {
private:
    std::shared_ptr<ModelManager> m_modelManager;
    std::unique_ptr<SelectionService> m_selectionService;
    UI::SplashScreen* m_splashScreen{nullptr};
    UI::FloatingIconFrame* m_floatingIcon{nullptr};
    UI::TranslationBubbleFrame* m_translationBubble{nullptr};
    UI::MainFrame* m_mainFrame{nullptr};

public:
    SelectionService* GetSelectionService() const { return m_selectionService.get(); }

    bool OnInit() override {
#ifdef _WIN32
        // 启用 Windows 原生 Per-Monitor V2 DPI 感知，确保划词全局坐标与多显示器高分屏绝对一致
        typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            auto setDpiContext = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
            if (setDpiContext) {
                setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            } else {
                SetProcessDPIAware();
            }
        }

        // 如果是从父终端命令行运行，动态挂载控制台以支持直接观察输出
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp = nullptr;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
#endif
        wxInitAllImageHandlers();

        LOG_INFO("App", "LinguaAlpaca application starting...");

        // 1. 初始化配置管理器与统一模型推理调度中枢
        auto configManager = std::make_shared<ConfigManager>();
        m_modelManager = std::make_shared<ModelManager>(configManager);

        // 2. 创建并居中显示现代无边框启动画面
        m_splashScreen = new UI::SplashScreen(nullptr);
        SetTopWindow(m_splashScreen);
        m_splashScreen->Centre();
        m_splashScreen->Show(true);

        // 3. 执行异步初始化流水线 (加载配置、词典、翻译模型等)
        m_splashScreen->StartInitialization(m_modelManager, [this]() {
            LOG_INFO("App", "Initialization completed. Displaying MainFrame...");

            auto configManager = m_modelManager ? m_modelManager->GetConfigManager() : nullptr;

            // 4. 初始化全局划词翻译悬浮组件与全局常驻划词服务
            m_floatingIcon = new UI::FloatingIconFrame(nullptr);
            m_translationBubble = new UI::TranslationBubbleFrame(m_modelManager, nullptr);

            m_floatingIcon->SetClickCallback([this](const wxPoint& pos, const std::string& text) {
                if (m_translationBubble) {
                    m_translationBubble->ShowAndTranslate(pos, text);
                }
            });

            m_selectionService = std::make_unique<SelectionService>(configManager);
            m_selectionService->SetCallback([this](int x, int y, const std::string& text) {
                if (m_floatingIcon) {
                    m_floatingIcon->ShowAt(x, y, text);
                }
            });
            m_selectionService->Start();

            // 5. 创建并居中显示主界面
            m_mainFrame = new UI::MainFrame(m_modelManager);
            SetTopWindow(m_mainFrame);
            m_mainFrame->Centre();
            m_mainFrame->Show(true);

            // 6. 关闭并销毁启动页
            if (m_splashScreen) {
                m_splashScreen->Destroy();
                m_splashScreen = nullptr;
            }

            LOG_INFO("App", "MainFrame and Global Selection Service initialized successfully.");
        });

        return true;
    }

    int OnExit() override {
        LOG_INFO("App", "LinguaAlpaca application stopping...");
        if (m_selectionService) {
            m_selectionService->Stop();
            m_selectionService.reset();
        }
        if (m_modelManager) {
            LOG_INFO("App", "Stopping model manager...");
            m_modelManager->StopModel();
            m_modelManager.reset();
        }
        if (m_floatingIcon) {
            m_floatingIcon->Hide();
            delete m_floatingIcon;
            m_floatingIcon = nullptr;
        }
        if (m_translationBubble) {
            m_translationBubble->Hide();
            delete m_translationBubble;
            m_translationBubble = nullptr;
        }
        if (m_splashScreen) {
            delete m_splashScreen;
            m_splashScreen = nullptr;
        }
        UI::ThemeManager::GetInstance().ClearCallbacks();
        return wxApp::OnExit();
    }
};

wxIMPLEMENT_APP(LinguaAlpacaApp);