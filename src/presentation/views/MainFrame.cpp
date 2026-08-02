#include "MainFrame.hpp"
#include "../theme/ThemeColors.hpp"
#include "../../application/service/ThemeManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace LinguaAlpaca::Presentation::Views {

MainFrame::MainFrame(std::shared_ptr<Application::Service::TranslationService> translationService)
    : wxFrame(nullptr, wxID_ANY, L"轻译 · Lingo", wxDefaultPosition, wxSize(1080, 780), wxBORDER_NONE)
    , m_translationService(std::move(translationService)) {
    InitUI();
    Centre();
}

void MainFrame::InitUI() {
    SetMinSize(wxSize(960, 680));

#ifdef __WXMSW__
    HWND hwnd = (HWND)GetHWND();
    LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_APPWINDOW;
    ::SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME;  // 保留四周拖拽改变窗口大小功能
    style &= ~WS_CAPTION;    // 移除 Windows 原生标题栏
    ::SetWindowLong(hwnd, GWL_STYLE, style);

    ::SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#endif

    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 顶部自定义 Titlebar Header Panel
    m_topHeaderPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 54), wxBORDER_NONE);
    m_topHeaderPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);

    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Logo & App Name ("译  轻译 · Lingo")
    wxPanel* logoBadge = new wxPanel(m_topHeaderPanel, wxID_ANY, wxDefaultPosition, wxSize(28, 28), wxBORDER_NONE);
    logoBadge->SetBackgroundColour(palette.accentPrimary);
    wxBoxSizer* badgeSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* badgeText = new wxStaticText(logoBadge, wxID_ANY, L"译");
    badgeText->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    badgeText->SetForegroundColour(*wxWHITE);
    badgeSizer->Add(badgeText, 0, wxALIGN_CENTER);
    logoBadge->SetSizer(badgeSizer);

    wxStaticText* appNameText = new wxStaticText(m_topHeaderPanel, wxID_ANY, L"轻译 · Lingo");
    appNameText->SetFont(wxFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    appNameText->SetForegroundColour(palette.textPrimary);

    // 模式切换按钮 (🌙)
    m_themeBtn = new wxButton(m_topHeaderPanel, wxID_ANY, L"🌙", wxDefaultPosition, wxSize(36, 36), wxBORDER_NONE);
    m_themeBtn->SetFont(wxFont(14, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji"));
    m_themeBtn->SetBackgroundColour(palette.windowBg);

    // 自定义窗口控制按钮 (🟡 最小化, 🟢 放大/还原, 🔴 关闭)
    m_minBtn = new wxButton(m_topHeaderPanel, wxID_ANY, L"🟡", wxDefaultPosition, wxSize(30, 30), wxBORDER_NONE);
    m_minBtn->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji"));
    m_minBtn->SetBackgroundColour(palette.sidebarBg);
    m_minBtn->SetToolTip(L"最小化");

    m_maxBtn = new wxButton(m_topHeaderPanel, wxID_ANY, L"🟢", wxDefaultPosition, wxSize(30, 30), wxBORDER_NONE);
    m_maxBtn->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji"));
    m_maxBtn->SetBackgroundColour(palette.sidebarBg);
    m_maxBtn->SetToolTip(L"最大化 / 还原");

    m_closeBtn = new wxButton(m_topHeaderPanel, wxID_ANY, L"🔴", wxDefaultPosition, wxSize(30, 30), wxBORDER_NONE);
    m_closeBtn->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji"));
    m_closeBtn->SetBackgroundColour(palette.sidebarBg);
    m_closeBtn->SetToolTip(L"关闭");

    headerSizer->Add(logoBadge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    headerSizer->Add(appNameText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    headerSizer->AddStretchSpacer(1);
    headerSizer->Add(m_themeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
    headerSizer->Add(m_minBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    headerSizer->Add(m_maxBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    headerSizer->Add(m_closeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    m_topHeaderPanel->SetSizer(headerSizer);
    rootSizer->Add(m_topHeaderPanel, 0, wxEXPAND);

    // 2. 主区 (侧边栏 + 视图容器)
    wxBoxSizer* bodySizer = new wxBoxSizer(wxHORIZONTAL);
    m_sidebar = new Components::SidebarNav(this);

    m_contentContainer = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_contentContainer->SetBackgroundColour(palette.windowBg);

    m_contentSizer = new wxBoxSizer(wxVERTICAL);
    m_contentContainer->SetSizer(m_contentSizer);

    // 实例化各子视图
    m_selectionView = new TextTranslationView(m_contentContainer, m_translationService);
    m_textView = new PlaceholderView(m_contentContainer, L"文本翻译");
    m_ocrView = new PlaceholderView(m_contentContainer, L"OCR 截图识图");
    m_historyView = new PlaceholderView(m_contentContainer, L"翻译历史记录");
    m_settingsView = new SettingsView(m_contentContainer, m_translationService);

    m_textView->Hide();
    m_ocrView->Hide();
    m_historyView->Hide();
    m_settingsView->Hide();

    m_contentSizer->Add(m_selectionView, 1, wxEXPAND);
    m_contentSizer->Add(m_textView, 1, wxEXPAND);
    m_contentSizer->Add(m_ocrView, 1, wxEXPAND);
    m_contentSizer->Add(m_historyView, 1, wxEXPAND);
    m_contentSizer->Add(m_settingsView, 1, wxEXPAND);

    bodySizer->Add(m_sidebar, 0, wxEXPAND);
    bodySizer->Add(m_contentContainer, 1, wxEXPAND);

    rootSizer->Add(bodySizer, 1, wxEXPAND);

    SetSizer(rootSizer);
    Layout();

    // 绘制 Header Panel 底部的精细分隔线
    m_topHeaderPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_topHeaderPanel);
        wxSize size = m_topHeaderPanel->GetClientSize();
        auto p = Theme::ThemeColors::GetCurrentPalette();

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
    m_sidebar->Bind(Components::EVT_SIDEBAR_NAV_CHANGED, &MainFrame::OnNavChanged, this);

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

    appNameText->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
    appNameText->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
    appNameText->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
    appNameText->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);
}

void MainFrame::NavigateToSettings() {
    if (m_sidebar) {
        m_sidebar->SetActiveItem(4);
        wxCommandEvent evt(Components::EVT_SIDEBAR_NAV_CHANGED, m_sidebar->GetId());
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
    Application::Service::ThemeManager::GetInstance().ToggleTheme();
    auto palette = Theme::ThemeColors::GetCurrentPalette();

    SetBackgroundColour(palette.windowBg);
    m_topHeaderPanel->SetBackgroundColour(palette.sidebarBg);
    m_themeBtn->SetBackgroundColour(palette.windowBg);
    m_themeBtn->SetLabel(Application::Service::ThemeManager::GetInstance().GetCurrentTheme() == 
        Domain::Model::AppThemeMode::Light ? L"🌙" : L"☀️");

    m_minBtn->SetBackgroundColour(palette.sidebarBg);
    m_maxBtn->SetBackgroundColour(palette.sidebarBg);
    m_closeBtn->SetBackgroundColour(palette.sidebarBg);

    m_contentContainer->SetBackgroundColour(palette.windowBg);
    m_sidebar->Refresh();

    if (m_selectionView) m_selectionView->UpdateTheme();
    if (m_settingsView) m_settingsView->UpdateTheme();

    Refresh();
    m_contentContainer->Refresh();
}

void MainFrame::OnNavChanged(wxCommandEvent& event) {
    int index = event.GetInt();

    m_selectionView->Hide();
    m_textView->Hide();
    m_ocrView->Hide();
    m_historyView->Hide();
    m_settingsView->Hide();

    switch (index) {
    case 0: m_selectionView->Show(); break;
    case 1: m_textView->Show(); break;
    case 2: m_ocrView->Show(); break;
    case 3: m_historyView->Show(); break;
    case 4: m_settingsView->Show(); break;
    default: m_selectionView->Show(); break;
    }

    m_contentContainer->Layout();
}

} // namespace LinguaAlpaca::Presentation::Views
