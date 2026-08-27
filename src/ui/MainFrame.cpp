#include "MainFrame.hpp"
#include "theme/Theme.hpp"
#include "theme/IconManager.hpp"
#include "widgets/WelcomeModelDialog.hpp"
#include "widgets/AppTaskBarIcon.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

    MainFrame::MainFrame(std::shared_ptr<ModelManager> modelManager)
        : wxFrame(nullptr, wxID_ANY, L"译灵驼 · LinguaAlpaca", wxDefaultPosition,
            wxDefaultSize, wxBORDER_NONE),
        m_modelManager(std::move(modelManager)) {
        SetIcons(IconManager::GetAppIconBundle());
#ifdef __WXMSW__
        HWND hwnd = (HWND)GetHWND();
        if (hwnd) {
            // 1. 设置 WS_EX_APPWINDOW 样式，确保无边框窗体在 Windows 任务栏正常常驻与显示
            LONG_PTR exStyle = ::GetWindowLongPtr(hwnd, GWL_EXSTYLE);
            ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, (exStyle | WS_EX_APPWINDOW) & ~WS_EX_TOOLWINDOW);

            // 2. 显式发送 Win32 原生 WM_SETICON 消息 (ICON_BIG / ICON_SMALL)
            HICON hIconBig = (HICON)::LoadImageW(
                ::GetModuleHandleW(NULL),
                MAKEINTRESOURCEW(1),
                IMAGE_ICON,
                ::GetSystemMetrics(SM_CXICON),
                ::GetSystemMetrics(SM_CYICON),
                LR_DEFAULTCOLOR
            );
            HICON hIconSmall = (HICON)::LoadImageW(
                ::GetModuleHandleW(NULL),
                MAKEINTRESOURCEW(1),
                IMAGE_ICON,
                ::GetSystemMetrics(SM_CXSMICON),
                ::GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR
            );
            if (!hIconBig) {
                wxIcon wxIco = IconManager::GetAppIcon(wxSize(::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)));
                if (wxIco.IsOk()) hIconBig = (HICON)wxIco.GetHICON();
            }
            if (!hIconSmall) {
                wxIcon wxIco = IconManager::GetAppIcon(wxSize(::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)));
                if (wxIco.IsOk()) hIconSmall = (HICON)wxIco.GetHICON();
            }
            if (hIconBig) {
                ::SetClassLongPtrW(hwnd, GCLP_HICON, (LONG_PTR)hIconBig);
                ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
            }
            if (hIconSmall) {
                ::SetClassLongPtrW(hwnd, GCLP_HICONSM, (LONG_PTR)hIconSmall);
                ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
            }
        }
#endif
        SetClientSize(dip(1080, 780));
        SetMinClientSize(dip(960, 680));
        InitUI();
        Centre();

        // 创建系统托盘图标
        m_taskBarIcon = std::make_unique<AppTaskBarIcon>(this);

        // 注册全局主题变更回调
        ThemeManager::GetInstance().RegisterCallback([this](ThemeMode) {
            ApplyTheme();
        });

        // 根据已保存配置初始化主题偏好
        if (m_modelManager && m_modelManager->GetConfigManager()) {
            auto cfg = m_modelManager->GetConfigManager()->GetConfig();
            ThemeManager::GetInstance().SetPreferenceByString(cfg.themeMode);
        }

        // 启动完全后，如果没有配置翻译模型则弹出欢迎引导
        CallAfter([this]() {
            CheckAndShowWelcomeDialog();
        });
    }

    MainFrame::~MainFrame() {
        if (m_taskBarIcon) {
            m_taskBarIcon->RemoveIcon();
            m_taskBarIcon.reset();
        }
    }

    void MainFrame::InitUI() {
        auto palette = ThemeColors::GetCurrentPalette();
        SetBackgroundColour(palette.cardBorder);

        wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

        // 1. 顶部自定义 Titlebar Header Panel
        m_topHeaderPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition,
            wxSize(-1, 52_dip), wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
        m_topHeaderPanel->SetBackgroundColour(palette.sidebarBg);
        m_topHeaderPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);

        wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

        // Logo & App Name (以标准逻辑 DIP 尺寸传入 Bundle，避免多重 DPI 缩放溢出裁剪)
        wxBitmapBundle logoBundle = IconManager::GetAppLogoBundle(wxSize(30, 30));
        m_logoIcon = new wxStaticBitmap(m_topHeaderPanel, wxID_ANY, logoBundle);

        m_appNameText = new wxStaticText(m_topHeaderPanel, wxID_ANY, L"译灵驼 · LinguaAlpaca");
        m_appNameText->SetFont(wxFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        m_appNameText->SetForegroundColour(palette.textPrimary);
        m_appNameText->SetBackgroundColour(palette.sidebarBg);

        // 模式切换按钮 (SVG Moon/Sun)
        bool isLight = ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Light;
        wxBitmapBundle themeBundle = IconManager::GetIconBundle(isLight ? SVG::MOON : SVG::SUN, wxSize(18, 18), palette.textPrimary);
        m_themeBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, themeBundle, wxDefaultPosition, dip(34, 34), wxBORDER_NONE);
        m_themeBtn->SetBackgroundColour(palette.sidebarBg);
        m_themeBtn->SetToolTip(L"切换明暗主题");

        // 自定义窗口控制按钮 (SVG 最小化, 放大/还原, 关闭)
        wxBitmapBundle minBundle = IconManager::GetIconBundle(SVG::MINIMIZE, wxSize(15, 15), palette.textSecondary);
        wxBitmapBundle maxBundle = IconManager::GetIconBundle(SVG::MAXIMIZE, wxSize(15, 15), palette.textSecondary);
        wxBitmapBundle closeBundle = IconManager::GetIconBundle(SVG::CLOSE, wxSize(15, 15), palette.textSecondary);

        m_minBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, minBundle, wxDefaultPosition, dip(32, 32), wxBORDER_NONE);
        m_minBtn->SetBackgroundColour(palette.sidebarBg);
        m_minBtn->SetToolTip(L"最小化到系统托盘");

        m_maxBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, maxBundle, wxDefaultPosition, dip(32, 32), wxBORDER_NONE);
        m_maxBtn->SetBackgroundColour(palette.sidebarBg);
        m_maxBtn->SetToolTip(L"最大化 / 还原");

        m_closeBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, closeBundle, wxDefaultPosition, dip(32, 32), wxBORDER_NONE);
        m_closeBtn->SetBackgroundColour(palette.sidebarBg);
        m_closeBtn->SetToolTip(L"关闭");

        headerSizer->Add(m_logoIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
        headerSizer->Add(m_appNameText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10_dip);
        headerSizer->AddStretchSpacer(1);
        headerSizer->Add(m_themeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
        headerSizer->Add(m_minBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
        headerSizer->Add(m_maxBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
        headerSizer->Add(m_closeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);

        m_topHeaderPanel->SetSizer(headerSizer);
        rootSizer->Add(m_topHeaderPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 1);

        // 2. 主区 (侧边栏 + 视图容器)
        wxBoxSizer* bodySizer = new wxBoxSizer(wxHORIZONTAL);
        m_sidebar = new SidebarNav(this);

        m_contentContainer = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_contentContainer->SetBackgroundColour(palette.windowBg);

        m_contentSizer = new wxBoxSizer(wxVERTICAL);
        m_contentContainer->SetSizer(m_contentSizer);

        // 实例化各子视图并注入 ModelManager
        m_textView = new TextView(m_contentContainer, m_modelManager);
        m_ocrView = new OcrView(m_contentContainer, m_modelManager);
        m_dictView = new DictView(m_contentContainer, m_modelManager);
        m_logView = new LogView(m_contentContainer, m_modelManager ? m_modelManager->GetConfigManager() : nullptr);
        m_settingsView = new SettingsView(m_contentContainer, m_modelManager);

        m_contentSizer->Add(m_textView, 1, wxEXPAND);
        m_contentSizer->Add(m_ocrView, 1, wxEXPAND);
        m_contentSizer->Add(m_dictView, 1, wxEXPAND);
        m_contentSizer->Add(m_logView, 1, wxEXPAND);
        m_contentSizer->Add(m_settingsView, 1, wxEXPAND);

        // 默认显示文本翻译 (Tab 0)
        m_ocrView->Hide();
        m_dictView->Hide();
        m_logView->Hide();
        m_settingsView->Hide();

        bodySizer->Add(m_sidebar, 0, wxEXPAND);
        bodySizer->Add(m_contentContainer, 1, wxEXPAND);
        rootSizer->Add(bodySizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 1);

        SetSizer(rootSizer);
        Layout();

        // 绘制 Header Panel 底部的精细分隔线
        m_topHeaderPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(m_topHeaderPanel);
            wxSize size = m_topHeaderPanel->GetClientSize();
            auto p = ThemeColors::GetCurrentPalette();

            dc.SetBackground(wxBrush(p.sidebarBg));
            dc.Clear();

            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc) {
                gc->SetPen(gc->CreatePen(wxPen(p.cardBorder, 1)));
                gc->StrokeLine(0, size.y - 1, size.x, size.y - 1);
            }
            });

        // 绑定主题与侧边栏事件
        m_themeBtn->Bind(wxEVT_BUTTON, &MainFrame::OnThemeToggle, this);
        m_sidebar->Bind(EVT_SIDEBAR_NAV_CHANGED, &MainFrame::OnNavChanged, this);

        // 绑定窗口控制按钮事件
        m_minBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
        m_maxBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            Maximize(!IsMaximized());
            UpdateMaxButtonState();
        });
        m_closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(true); });

        // 绑定自定义 Titlebar 的拖动与双击最大化事件
        m_topHeaderPanel->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
        m_topHeaderPanel->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
        m_topHeaderPanel->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
        m_topHeaderPanel->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);

        m_logoIcon->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
        m_logoIcon->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
        m_logoIcon->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
        m_logoIcon->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);

        m_appNameText->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
        m_appNameText->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
        m_appNameText->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
        m_appNameText->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);

        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
        Bind(wxEVT_ICONIZE, [this](wxIconizeEvent& event) {
            if (event.IsIconized()) {
                Hide();
            }
            event.Skip();
        });
        Bind(wxEVT_MAXIMIZE, [this](wxMaximizeEvent& event) {
            UpdateMaxButtonState();
            event.Skip();
        });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            UpdateMaxButtonState();
            event.Skip();
        });

        // 初始启动时触发当前选中的默认 Tab 0 (文本翻译) 按需加载
        if (m_modelManager) {
            m_modelManager->EnsureModelAsync(TargetModelType::Translation);
        }
    }

    void MainFrame::OnClose(wxCloseEvent& WXUNUSED(event)) {
        if (m_taskBarIcon) {
            m_taskBarIcon->RemoveIcon();
        }
        if (m_modelManager) {
            m_modelManager->StopModel();
        }
        if (wxTheApp) {
            wxTheApp->ExitMainLoop();
        }
        Destroy();
    }

    void MainFrame::RestoreAndFocus() {
        if (!IsShown()) {
            Show(true);
        }
        if (IsIconized()) {
            Iconize(false);
        }
        Raise();
#ifdef __WXMSW__
        HWND hwnd = (HWND)GetHWND();
        if (hwnd) {
            ::SetForegroundWindow(hwnd);
        }
#endif
    }

    void MainFrame::QuitApplication() {
        if (wxTheApp) {
            wxTheApp->CallAfter([this]() {
                Close(true);
            });
        } else {
            Close(true);
        }
    }

    void MainFrame::NavigateToSettings() {
        if (m_sidebar) {
            m_sidebar->SetActiveItem(4);
            wxCommandEvent evt(EVT_SIDEBAR_NAV_CHANGED, m_sidebar->GetId());
            evt.SetInt(4);
            m_sidebar->ProcessWindowEvent(evt);
        }
    }

    void MainFrame::CheckAndShowWelcomeDialog() {
        if (!m_modelManager || !m_modelManager->GetConfigManager()) return;

        auto config = m_modelManager->GetConfigManager()->GetConfig();
        bool hasModel = !config.modelPath.empty() && wxFileExists(wxString::FromUTF8(config.modelPath));

        if (!hasModel) {
            WelcomeModelDialog dlg(this);
            if (dlg.ShowModal() == wxID_OK && dlg.ShouldNavigateToSettings()) {
                NavigateToSettings();
            }
        }
    }

    void MainFrame::OnHeaderLeftDown(wxMouseEvent& event) {
#ifdef __WXMSW__
        ReleaseCapture();
        ::SendMessage((HWND)GetHWND(), WM_NCLBUTTONDOWN, HTCAPTION, 0);
#else
        m_isDragging = true;
        m_dragStartPos = wxGetMousePosition() - GetPosition();
        if (!m_topHeaderPanel->HasCapture()) {
            m_topHeaderPanel->CaptureMouse();
        }
#endif
        event.Skip();
    }

    void MainFrame::OnHeaderLeftUp(wxMouseEvent& event) {
        if (m_isDragging) {
            m_isDragging = false;
            if (m_topHeaderPanel->HasCapture()) {
                m_topHeaderPanel->ReleaseMouse();
            }
        }
        event.Skip();
    }

    void MainFrame::OnHeaderMouseMove(wxMouseEvent& event) {
        if (m_isDragging && event.Dragging() && event.LeftIsDown()) {
            wxPoint currentMouseScreen = wxGetMousePosition();
            SetPosition(currentMouseScreen - m_dragStartPos);
        }
        event.Skip();
    }

    void MainFrame::OnHeaderDoubleClick(wxMouseEvent& WXUNUSED(event)) {
        Maximize(!IsMaximized());
        UpdateMaxButtonState();
    }

    void MainFrame::UpdateMaxButtonState() {
        if (!m_maxBtn) return;
        auto palette = ThemeColors::GetCurrentPalette();
        bool max = IsMaximized();
        wxBitmapBundle bundle = IconManager::GetIconBundle(
            max ? SVG::RESTORE : SVG::MAXIMIZE, wxSize(15, 15), palette.textSecondary);
        m_maxBtn->SetBitmap(bundle);
        m_maxBtn->SetToolTip(max ? L"还原" : L"最大化");
        m_maxBtn->Refresh();
    }

    void MainFrame::ApplyTheme() {
        auto palette = ThemeColors::GetCurrentPalette();

        SetBackgroundColour(palette.cardBorder);
        m_topHeaderPanel->SetBackgroundColour(palette.sidebarBg);

        if (m_appNameText) {
            m_appNameText->SetForegroundColour(palette.textPrimary);
            m_appNameText->SetBackgroundColour(palette.sidebarBg);
            m_appNameText->Refresh();
        }

        bool isLight = ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Light;
        wxBitmapBundle themeBundle = IconManager::GetIconBundle(
            isLight ? SVG::MOON : SVG::SUN, wxSize(18, 18),
            palette.textPrimary);
        m_themeBtn->SetBitmap(themeBundle);
        m_themeBtn->SetBackgroundColour(palette.sidebarBg);

        wxBitmapBundle minBundle = IconManager::GetIconBundle(
            SVG::MINIMIZE, wxSize(15, 15), palette.textSecondary);
        wxBitmapBundle maxBundle = IconManager::GetIconBundle(
            IsMaximized() ? SVG::RESTORE : SVG::MAXIMIZE, wxSize(15, 15), palette.textSecondary);
        wxBitmapBundle closeBundle = IconManager::GetIconBundle(
            SVG::CLOSE, wxSize(15, 15), palette.textSecondary);

        m_minBtn->SetBitmap(minBundle);
        m_minBtn->SetBackgroundColour(palette.sidebarBg);

        m_maxBtn->SetBitmap(maxBundle);
        m_maxBtn->SetBackgroundColour(palette.sidebarBg);

        m_closeBtn->SetBitmap(closeBundle);
        m_closeBtn->SetBackgroundColour(palette.sidebarBg);

        m_contentContainer->SetBackgroundColour(palette.windowBg);
        m_sidebar->Refresh();

        if (m_textView)
            m_textView->UpdateTheme();
        if (m_ocrView)
            m_ocrView->UpdateTheme();
        if (m_dictView)
            m_dictView->UpdateTheme();
        if (m_logView)
            m_logView->UpdateTheme();
        if (m_settingsView)
            m_settingsView->UpdateTheme();

        Refresh();
        m_contentContainer->Refresh();
    }

    void MainFrame::OnThemeToggle(wxCommandEvent& WXUNUSED(event)) {
        ThemeManager::GetInstance().ToggleTheme();
        if (m_modelManager && m_modelManager->GetConfigManager()) {
            m_modelManager->GetConfigManager()->SaveThemeMode(ThemeManager::GetInstance().GetPreferenceString());
        }
    }

    void MainFrame::OnNavChanged(wxCommandEvent& event) {
        int index = event.GetInt();

        m_textView->Hide();
        m_ocrView->Hide();
        m_dictView->Hide();
        m_logView->Hide();
        m_settingsView->Hide();

        switch (index) {
        case 0:
            m_textView->Show();
            if (m_modelManager) {
                m_modelManager->EnsureModelAsync(TargetModelType::Translation);
            }
            break;
        case 1:
            m_ocrView->Show();
            if (m_modelManager) {
                m_modelManager->EnsureModelAsync(TargetModelType::Ocr);
            }
            break;
        case 2:
            m_dictView->Show();
            m_dictView->RefreshDictList();
            break;
        case 3:
            m_logView->Show();
            break;
        case 4:
            m_settingsView->Show();
            break;
        default:
            m_textView->Show();
            if (m_modelManager) {
                m_modelManager->EnsureModelAsync(TargetModelType::Translation);
            }
            break;
        }

        m_contentContainer->Layout();
    }

#ifdef __WXMSW__
    WXLRESULT MainFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
        if (nMsg == WM_QUERYENDSESSION) {
            return TRUE;
        }
        if (nMsg == WM_ENDSESSION) {
            if (wParam) {
                if (m_taskBarIcon) {
                    m_taskBarIcon->RemoveIcon();
                }
                if (m_modelManager) {
                    m_modelManager->StopModel();
                }
            }
            return 0;
        }

        WXLRESULT rc = wxFrame::MSWWindowProc(nMsg, wParam, lParam);
        if (nMsg == WM_GETMINMAXINFO) {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            HWND hwnd = (HWND)GetHWND();
            if (hwnd && mmi) {
                HMONITOR hMonitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                if (hMonitor) {
                    MONITORINFO mi;
                    mi.cbSize = sizeof(MONITORINFO);
                    if (::GetMonitorInfoW(hMonitor, &mi)) {
                        mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                        mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                        mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                        mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                        if (mmi->ptMaxTrackSize.x < mmi->ptMaxSize.x) {
                            mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x;
                        }
                        if (mmi->ptMaxTrackSize.y < mmi->ptMaxSize.y) {
                            mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y;
                        }
                    }
                }
            }
            return 0;
        }
        return rc;
    }
#endif

} // namespace LinguaAlpaca::UI
