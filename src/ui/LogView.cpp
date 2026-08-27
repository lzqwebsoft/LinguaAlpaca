#include "LogView.hpp"
#include "core/ClipboardHelper.hpp"
#include "theme/Theme.hpp"
#include "theme/IconManager.hpp"
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace LinguaAlpaca::UI {

LogView::LogView(wxWindow* parent,
                 std::shared_ptr<ConfigManager> configManager,
                 wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      m_configManager(std::move(configManager)) {
    InitUI();

    // 订阅 Logger 的实时日志通知 (使用 BindUi 自动跨线程安全调度)
    m_listenerId = Logger::GetInstance().AddListener(BindUi([this](const LogMessage& msg) {
        if (!m_logTextCtrl) return;
        AppendLogMessage(msg);
    }));

    // 加载当前内存中已有的历史日志
    ReloadLogs();
}

LogView::~LogView() {
    if (m_listenerId != 0) {
        Logger::GetInstance().RemoveListener(m_listenerId);
        m_listenerId = 0;
    }
}

void LogView::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 顶部操作工具栏 Panel
    m_headerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_headerPanel->SetBackgroundColour(palette.windowBg);
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    // 标题与图标
    wxBitmapBundle logIconBundle = IconManager::GetIconBundle(SVG::LOG, wxSize(20, 20), palette.accentPrimary);
    m_titleIcon = new wxStaticBitmap(m_headerPanel, wxID_ANY, logIconBundle);

    m_titleText = new wxStaticText(m_headerPanel, wxID_ANY, L"运行与诊断日志");
    m_titleText->SetFont(wxFont(14, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    headerSizer->Add(m_titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->AddStretchSpacer(1);

    // 过滤等级 Choice
    wxArrayString levels;
    levels.Add(L"全部等级");
    levels.Add(L"DEBUG");
    levels.Add(L"INFO");
    levels.Add(L"WARN");
    levels.Add(L"ERROR");

    wxStaticText* filterLabel = new wxStaticText(m_headerPanel, wxID_ANY, L"过滤：");
    filterLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    filterLabel->SetForegroundColour(palette.textSecondary);

    m_filterChoice = new CustomChoice(m_headerPanel, wxID_ANY, wxDefaultPosition, dip(110, 28), levels);
    m_filterChoice->SetSelection(0);

    wxBoxSizer* filterSizer = new wxBoxSizer(wxHORIZONTAL);
    filterSizer->Add(filterLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
    filterSizer->Add(m_filterChoice, 0, wxALIGN_CENTER_VERTICAL);

    // 自动滚动 Checkbox
    m_autoScrollCheck = new wxCheckBox(m_headerPanel, wxID_ANY, L"自动滚动");
    m_autoScrollCheck->SetValue(true);
    m_autoScrollCheck->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_autoScrollCheck->SetForegroundColour(palette.textPrimary);

    // 功能按钮
    m_clearBtn = new CustomButton(m_headerPanel, wxID_ANY, L"清空", ButtonStyle::Secondary, wxDefaultPosition, dip(64, 28));
    m_copyBtn = new CustomButton(m_headerPanel, wxID_ANY, L"复制全部", ButtonStyle::Secondary, wxDefaultPosition, dip(88, 28));
    m_openDirBtn = new CustomButton(m_headerPanel, wxID_ANY, L"打开日志目录", ButtonStyle::Secondary, wxDefaultPosition, dip(110, 28));

    headerSizer->Add(filterSizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    headerSizer->Add(m_autoScrollCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    headerSizer->Add(m_clearBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    headerSizer->Add(m_copyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    headerSizer->Add(m_openDirBtn, 0, wxALIGN_CENTER_VERTICAL);

    m_headerPanel->SetSizer(headerSizer);
    mainSizer->Add(m_headerPanel, 0, wxEXPAND | wxALL, 16_dip);

    // 2. 日志内容卡片 Panel (参考 CardPanel 绘制圆角与边框)
    m_cardContainer = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
    m_cardContainer->SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_cardContainer->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        m_cardContainer->Refresh();
        event.Skip();
    });
    m_cardContainer->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_cardContainer);
        wxSize size = m_cardContainer->GetClientSize();
        if (size.x <= 0 || size.y <= 0)
            return;

        auto palette = ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(palette.windowBg));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc)
            return;

        // 绘制圆角卡片背景与边框
        double radius = 12.0_dip;
        gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
        gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
        gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);
    });

    wxBoxSizer* cardSizer = new wxBoxSizer(wxVERTICAL);

    long textStyle = wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxBORDER_NONE;
    m_logTextCtrl = new TextCtrl(m_cardContainer, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, textStyle);
    
    // 设置等宽控制台字体
    wxFont monoFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas");
    m_logTextCtrl->SetFont(monoFont);
    m_logTextCtrl->SetBackgroundColour(palette.cardBg);
    m_logTextCtrl->SetForegroundColour(palette.textPrimary);

    wxBoxSizer* textHBox = new wxBoxSizer(wxHORIZONTAL);
    textHBox->AddSpacer(8_dip);
    textHBox->Add(m_logTextCtrl, 1, wxEXPAND | wxRIGHT, 4_dip);

    cardSizer->Add(textHBox, 1, wxEXPAND | wxTOP | wxBOTTOM, 8_dip);
    m_cardContainer->SetSizer(cardSizer);

    mainSizer->Add(m_cardContainer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);
    SetSizer(mainSizer);
    Layout();

    // 事件绑定
    m_clearBtn->Bind(wxEVT_BUTTON, &LogView::OnClear, this);
    m_copyBtn->Bind(wxEVT_BUTTON, &LogView::OnCopyAll, this);
    m_openDirBtn->Bind(wxEVT_BUTTON, &LogView::OnOpenLogDir, this);
    m_filterChoice->Bind(wxEVT_CHOICE, &LogView::OnFilterChanged, this);
    m_autoScrollCheck->Bind(wxEVT_CHECKBOX, &LogView::OnAutoScrollToggled, this);
}

void LogView::AppendLogMessage(const LogMessage& msg) {
    if (!m_logTextCtrl) return;

    // 过滤逻辑
    if (m_filterLevel >= 0) {
        if (static_cast<int>(msg.level) != m_filterLevel) {
            return;
        }
    }

    auto palette = ThemeColors::GetCurrentPalette();
    wxColour levelCol = palette.textPrimary;
    switch (msg.level) {
    case LogLevel::Debug:
        levelCol = wxColour(130, 130, 130);
        break;
    case LogLevel::Info:
        levelCol = palette.textPrimary;
        break;
    case LogLevel::Warning:
        levelCol = wxColour(220, 140, 20);
        break;
    case LogLevel::Error:
        levelCol = wxColour(230, 60, 60);
        break;
    }

    wxTextAttr attr(levelCol, palette.cardBg);
    wxFont monoFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,  (msg.level == LogLevel::Error || msg.level == LogLevel::Warning) ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, "Consolas");
    attr.SetFont(monoFont);
    m_logTextCtrl->SetDefaultStyle(attr);

    wxString line = wxString::FromUTF8(msg.FormattedString() + "\n");
    m_logTextCtrl->AppendText(line);

    if (m_autoScroll) {
        m_logTextCtrl->ShowPosition(m_logTextCtrl->GetLastPosition());
    }
}

void LogView::ReloadLogs() {
    if (!m_logTextCtrl) return;

     // 锁定控件重绘，批量处理完后一次性刷新
    m_logTextCtrl->Freeze();
    m_logTextCtrl->Clear();

    auto history = Logger::GetInstance().GetRecentLogs();
    for (const auto& msg : history) {
        AppendLogMessage(msg);
    }
    m_logTextCtrl->Thaw();
}

void LogView::OnClear(wxCommandEvent& WXUNUSED(event)) {
    Logger::GetInstance().ClearLogs();
    if (m_logTextCtrl) {
        m_logTextCtrl->Clear();
    }
}

void LogView::OnCopyAll(wxCommandEvent& WXUNUSED(event)) {
    if (!m_logTextCtrl) return;
    wxString text = m_logTextCtrl->GetValue();
    if (text.IsEmpty()) return;

    ClipboardHelper::SetClipboardText(text.ToUTF8().data());
}

void LogView::OnOpenLogDir(wxCommandEvent& WXUNUSED(event)) {
    std::string logDir = ConfigManager::GetDefaultLogDir();
    wxString dirPath = wxString::FromUTF8(logDir);
    if (!wxDirExists(dirPath)) {
        wxFileName::Mkdir(dirPath, 0777, wxPATH_MKDIR_FULL);
    }
    wxLaunchDefaultApplication(dirPath);
}

void LogView::OnFilterChanged(wxCommandEvent& event) {
    int sel = event.GetSelection();
    if (sel == 0) {
        m_filterLevel = -1; // 全部
    } else {
        m_filterLevel = sel - 1; // 0: Debug, 1: Info, 2: Warning, 3: Error
    }
    ReloadLogs();
}

void LogView::OnAutoScrollToggled(wxCommandEvent& event) {
    m_autoScroll = event.IsChecked();
    if (m_autoScroll && m_logTextCtrl) {
        m_logTextCtrl->ShowPosition(m_logTextCtrl->GetLastPosition());
    }
}

void LogView::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);
    if (m_headerPanel) m_headerPanel->SetBackgroundColour(palette.windowBg);
    if (m_cardContainer) {
        m_cardContainer->Refresh();
    }

    if (m_titleIcon) {
        wxBitmapBundle logIconBundle = IconManager::GetIconBundle(SVG::LOG, wxSize(20, 20), palette.accentPrimary);
        m_titleIcon->SetBitmap(logIconBundle);
    }

    if (m_titleText) {
        m_titleText->SetForegroundColour(palette.textPrimary);
    }
    if (m_filterChoice) {
        m_filterChoice->UpdateTheme();
    }
    if (m_autoScrollCheck) {
        m_autoScrollCheck->SetForegroundColour(palette.textPrimary);
    }

    if (m_clearBtn) m_clearBtn->Refresh();
    if (m_copyBtn) m_copyBtn->Refresh();
    if (m_openDirBtn) m_openDirBtn->Refresh();

    if (m_logTextCtrl) {
        m_logTextCtrl->SetBackgroundColour(palette.cardBg);
        m_logTextCtrl->SetForegroundColour(palette.textPrimary);
    }

    ReloadLogs();
    Refresh();
}

} // namespace LinguaAlpaca::UI
