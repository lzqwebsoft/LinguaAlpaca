#include "MainFrame.hpp"
#include "../theme/ThemeColors.hpp"
#include "../theme/IconManager.hpp"
#include "../../application/service/ThemeManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace LinguaAlpaca::Presentation::Views {

MainFrame::MainFrame(std::shared_ptr<Application::Service::TranslationService> translationService)
    : wxFrame(nullptr, wxID_ANY, L"灵驼译 · LinguaAlpaca", wxDefaultPosition, wxSize(1080, 780), wxBORDER_NONE)
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

    m_appNameText = new wxStaticText(m_topHeaderPanel, wxID_ANY, L"灵驼译 · LinguaAlpaca");
    m_appNameText->SetFont(wxFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_appNameText->SetForegroundColour(palette.textPrimary);

    // 模式切换按钮 (SVG Moon/Sun)
    bool isLight = Application::Service::ThemeManager::GetInstance().GetCurrentTheme() == Domain::Model::AppThemeMode::Light;
    wxBitmapBundle themeBundle = Theme::IconManager::GetIconBundle(isLight ? Theme::SVG::MOON : Theme::SVG::SUN, wxSize(18, 18), palette.textPrimary);
    m_themeBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, themeBundle, wxDefaultPosition, wxSize(36, 36), wxBORDER_NONE);
    m_themeBtn->SetBackgroundColour(palette.sidebarBg);

    // 自定义窗口控制按钮 (SVG 最小化, 放大/还原, 关闭)
    wxBitmapBundle minBundle = Theme::IconManager::GetIconBundle(Theme::SVG::MINIMIZE, wxSize(14, 14), palette.textSecondary);
    wxBitmapBundle maxBundle = Theme::IconManager::GetIconBundle(Theme::SVG::MAXIMIZE, wxSize(14, 14), palette.textSecondary);
    wxBitmapBundle closeBundle = Theme::IconManager::GetIconBundle(Theme::SVG::CLOSE, wxSize(14, 14), palette.textSecondary);

    m_minBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, minBundle, wxDefaultPosition, wxSize(30, 30), wxBORDER_NONE);
    m_minBtn->SetBackgroundColour(palette.sidebarBg);
    m_minBtn->SetToolTip(L"最小化");

    m_maxBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, maxBundle, wxDefaultPosition, wxSize(30, 30), wxBORDER_NONE);
    m_maxBtn->SetBackgroundColour(palette.sidebarBg);
    m_maxBtn->SetToolTip(L"最大化 / 还原");

    m_closeBtn = new wxBitmapButton(m_topHeaderPanel, wxID_ANY, closeBundle, wxDefaultPosition, wxSize(30, 30), wxBORDER_NONE);
    m_closeBtn->SetBackgroundColour(palette.sidebarBg);
    m_closeBtn->SetToolTip(L"关闭");

    headerSizer->Add(logoBadge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    headerSizer->Add(m_appNameText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
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
    m_textView = new TextTranslationView(m_contentContainer, m_translationService);
    m_ocrView = new PlaceholderView(m_contentContainer, L"OCR 截图识图");
    m_historyView = new PlaceholderView(m_contentContainer, L"翻译历史记录");
    m_settingsView = new SettingsView(m_contentContainer, m_translationService);

    m_ocrView->Hide();
    m_historyView->Hide();
    m_settingsView->Hide();

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

    m_appNameText->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnHeaderLeftDown, this);
    m_appNameText->Bind(wxEVT_LEFT_UP, &MainFrame::OnHeaderLeftUp, this);
    m_appNameText->Bind(wxEVT_MOTION, &MainFrame::OnHeaderMouseMove, this);
    m_appNameText->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnHeaderDoubleClick, this);
}

void MainFrame::NavigateToSettings() {
    if (m_sidebar) {
        m_sidebar->SetActiveItem(2);
        wxCommandEvent evt(Components::EVT_SIDEBAR_NAV_CHANGED, m_sidebar->GetId());
        evt.SetInt(2);
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

    if (m_appNameText) {
        m_appNameText->SetForegroundColour(palette.textPrimary);
        m_appNameText->Refresh();
    }

    bool isLight = Application::Service::ThemeManager::GetInstance().GetCurrentTheme() == Domain::Model::AppThemeMode::Light;
    wxBitmapBundle themeBundle = Theme::IconManager::GetIconBundle(isLight ? Theme::SVG::MOON : Theme::SVG::SUN, wxSize(18, 18), palette.textPrimary);
    m_themeBtn->SetBitmap(themeBundle);
    m_themeBtn->SetBackgroundColour(palette.sidebarBg);

    wxBitmapBundle minBundle = Theme::IconManager::GetIconBundle(Theme::SVG::MINIMIZE, wxSize(14, 14), palette.textSecondary);
    wxBitmapBundle maxBundle = Theme::IconManager::GetIconBundle(Theme::SVG::MAXIMIZE, wxSize(14, 14), palette.textSecondary);
    wxBitmapBundle closeBundle = Theme::IconManager::GetIconBundle(Theme::SVG::CLOSE, wxSize(14, 14), palette.textSecondary);

    m_minBtn->SetBitmap(minBundle);
    m_minBtn->SetBackgroundColour(palette.sidebarBg);

    m_maxBtn->SetBitmap(maxBundle);
    m_maxBtn->SetBackgroundColour(palette.sidebarBg);

    m_closeBtn->SetBitmap(closeBundle);
    m_closeBtn->SetBackgroundColour(palette.sidebarBg);

    m_contentContainer->SetBackgroundColour(palette.windowBg);
    m_sidebar->Refresh();

    if (m_textView) m_textView->UpdateTheme();
    if (m_settingsView) m_settingsView->UpdateTheme();

    Refresh();
    m_contentContainer->Refresh();
}

void MainFrame::OnNavChanged(wxCommandEvent& event) {
    int index = event.GetInt();

    m_textView->Hide();
    m_ocrView->Hide();
    m_historyView->Hide();
    m_settingsView->Hide();

    switch (index) {
    case 0: m_textView->Show(); break;
    case 1: m_historyView->Show(); break;
    case 2: m_settingsView->Show(); break;
    default: m_textView->Show(); break;
    }

    m_contentContainer->Layout();
}

} // namespace LinguaAlpaca::Presentation::Views
