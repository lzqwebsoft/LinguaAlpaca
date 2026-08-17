#include "MainFrame.hpp"
#include "theme/Theme.hpp"
#include "theme/IconManager.hpp"
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
        SetClientSize(dip(1080, 780));
        SetMinClientSize(dip(960, 680));
        InitUI();
        Centre();
    }

    void MainFrame::InitUI() {
        auto palette = ThemeColors::GetCurrentPalette();
        SetBackgroundColour(palette.cardBorder);

        wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

        // 1. 顶部自定义 Titlebar Header Panel
        m_topHeaderPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition,
            wxSize(-1, 54_dip), wxBORDER_NONE);
        m_topHeaderPanel->SetBackgroundColour(palette.sidebarBg);
        m_topHeaderPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);

        wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

        // Logo & App Name
        wxPanel* logoBadge =
            new wxPanel(m_topHeaderPanel, wxID_ANY, wxDefaultPosition, dip(28, 28),
                wxBORDER_NONE);
        logoBadge->SetBackgroundColour(palette.accentPrimary);
        wxBoxSizer* badgeSizer = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* badgeText = new wxStaticText(logoBadge, wxID_ANY, L"译");
        badgeText->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        badgeText->SetForegroundColour(*wxWHITE);
        badgeText->SetBackgroundColour(palette.accentPrimary);
        badgeSizer->Add(badgeText, 0, wxALIGN_CENTER);
        logoBadge->SetSizer(badgeSizer);

        m_appNameText = new wxStaticText(m_topHeaderPanel, wxID_ANY, L"译灵驼 · LinguaAlpaca");
        m_appNameText->SetFont(wxFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        m_appNameText->SetForegroundColour(palette.textPrimary);
        m_appNameText->SetBackgroundColour(palette.sidebarBg);

        // 模式切换按钮 (SVG Moon/Sun)
        bool isLight = ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Light;
        wxBitmapBundle themeBundle = IconManager::GetIconBundle(isLight ? SVG::MOON : SVG::SUN, dip(18, 18), palette.textPrimary);
        m_themeBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, themeBundle, wxDefaultPosition, dip(36, 36), wxBORDER_NONE);
        m_themeBtn->SetBackgroundColour(palette.sidebarBg);

        // 自定义窗口控制按钮 (SVG 最小化, 放大/还原, 关闭)
        wxBitmapBundle minBundle = IconManager::GetIconBundle(SVG::MINIMIZE, dip(14, 14), palette.textSecondary);
        wxBitmapBundle maxBundle = IconManager::GetIconBundle(SVG::MAXIMIZE, dip(14, 14), palette.textSecondary);
        wxBitmapBundle closeBundle = IconManager::GetIconBundle(SVG::CLOSE, dip(14, 14), palette.textSecondary);

        m_minBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, minBundle, wxDefaultPosition, dip(30, 30), wxBORDER_NONE);
        m_minBtn->SetBackgroundColour(palette.sidebarBg);
        m_minBtn->SetToolTip(L"最小化");

        m_maxBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, maxBundle, wxDefaultPosition, dip(30, 30), wxBORDER_NONE);
        m_maxBtn->SetBackgroundColour(palette.sidebarBg);
        m_maxBtn->SetToolTip(L"最大化 / 还原");

        m_closeBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, closeBundle, wxDefaultPosition, dip(30, 30), wxBORDER_NONE);
        m_closeBtn->SetBackgroundColour(palette.sidebarBg);
        m_closeBtn->SetToolTip(L"关闭");

        headerSizer->Add(logoBadge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
        headerSizer->Add(m_appNameText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8_dip);
        headerSizer->AddStretchSpacer(1);
        headerSizer->Add(m_themeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);
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
        m_historyView = new PlaceholderView(m_contentContainer, L"翻译历史记录");
        m_logView = new LogView(m_contentContainer, m_modelManager ? m_modelManager->GetConfigManager() : nullptr);
        m_settingsView = new SettingsView(m_contentContainer, m_modelManager);

        m_ocrView->Hide();
        m_historyView->Hide();
        m_logView->Hide();
        m_settingsView->Hide();

        m_contentSizer->Add(m_textView, 1, wxEXPAND);
        m_contentSizer->Add(m_ocrView, 1, wxEXPAND);
        m_contentSizer->Add(m_historyView, 1, wxEXPAND);
        m_contentSizer->Add(m_logView, 1, wxEXPAND);
        m_contentSizer->Add(m_settingsView, 1, wxEXPAND);

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
        m_minBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Iconize(true); });
        m_maxBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Maximize(!IsMaximized()); });
        m_closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(true); });

        // 绑定自定义 Titlebar 的拖动与双击最大化事件
        m_topHeaderPanel->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
        m_topHeaderPanel->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
        m_topHeaderPanel->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
        m_topHeaderPanel->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);

        logoBadge->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
        logoBadge->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
        logoBadge->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
        logoBadge->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);

        m_appNameText->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
        m_appNameText->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
        m_appNameText->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
        m_appNameText->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);

        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

        // 初始启动时触发当前选中的默认 Tab 0 (文本翻译) 按需加载
        if (m_modelManager) {
            m_modelManager->EnsureModelAsync(TargetModelType::Translation);
        }
    }

    void MainFrame::OnClose(wxCloseEvent& event) {
        if (m_modelManager) {
            m_modelManager->StopModelAsync();
        }
        if (wxTheApp) {
            wxTheApp->ExitMainLoop();
        }
        event.Skip();
    }

    void MainFrame::NavigateToSettings() {
        if (m_sidebar) {
            m_sidebar->SetActiveItem(4);
            wxCommandEvent evt(EVT_SIDEBAR_NAV_CHANGED, m_sidebar->GetId());
            evt.SetInt(4);
            m_sidebar->ProcessWindowEvent(evt);
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
    }

    void MainFrame::OnThemeToggle(wxCommandEvent& WXUNUSED(event)) {
        ThemeManager::GetInstance().ToggleTheme();
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
            isLight ? SVG::MOON : SVG::SUN, dip(18, 18),
            palette.textPrimary);
        m_themeBtn->SetBitmap(themeBundle);
        m_themeBtn->SetBackgroundColour(palette.sidebarBg);

        wxBitmapBundle minBundle = IconManager::GetIconBundle(
            SVG::MINIMIZE, dip(14, 14), palette.textSecondary);
        wxBitmapBundle maxBundle = IconManager::GetIconBundle(
            SVG::MAXIMIZE, dip(14, 14), palette.textSecondary);
        wxBitmapBundle closeBundle = IconManager::GetIconBundle(
            SVG::CLOSE, dip(14, 14), palette.textSecondary);

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
        if (m_logView)
            m_logView->UpdateTheme();
        if (m_settingsView)
            m_settingsView->UpdateTheme();

        Refresh();
        m_contentContainer->Refresh();
    }

    void MainFrame::OnNavChanged(wxCommandEvent& event) {
        int index = event.GetInt();

        m_textView->Hide();
        m_ocrView->Hide();
        m_historyView->Hide();
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
            m_historyView->Show();
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

} // namespace LinguaAlpaca::UI
