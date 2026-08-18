#pragma execution_character_set("utf-8")
#include "TranslationBubbleFrame.hpp"
#include "../theme/Theme.hpp"
#include "../theme/AppIcons.hpp"
#include "../theme/IconManager.hpp"
#include "../../core/ClipboardHelper.hpp"

#include <wx/display.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>

namespace LinguaAlpaca::UI {

TranslationBubbleFrame::TranslationBubbleFrame(std::shared_ptr<ModelManager> modelManager, wxWindow* parent)
    : wxFrame(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
              wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE),
      m_modelManager(std::move(modelManager)),
      m_bubbleSize(dip(420, 290)) {
    SetClientSize(m_bubbleSize);
    SetMinClientSize(dip(320, 200));
    InitUI();
    UpdateTheme();

    ThemeManager::GetInstance().RegisterCallback([this](ThemeMode) {
        UpdateTheme();
    });
}

void TranslationBubbleFrame::InitUI() {
    ThemePalette palette = ThemeManager::GetCurrentPalette();
    SetBackgroundColour(palette.cardBorder);

    m_mainPanel = new wxPanel(this, wxID_ANY);
    m_mainPanel->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 顶部拖拽标题栏 (参考 MainFrame 美化风格)
    m_headerPanel = new wxPanel(m_mainPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 38_dip), wxBORDER_NONE);
    m_headerPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle logoBundle = IconManager::GetIconBundle(SVG::TRANSLATE, dip(16, 16), palette.accentPrimary);
    wxStaticBitmap* logoIcon = new wxStaticBitmap(m_headerPanel, wxID_ANY, logoBundle);

    m_titleText = new wxStaticText(m_headerPanel, wxID_ANY, L"译灵驼");
    m_titleText->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    m_langBadge = new wxStaticText(m_headerPanel, wxID_ANY, L" 自动 -> 中文 ");
    m_langBadge->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_langBadge->SetForegroundColour(palette.accentPrimary);
    m_langBadge->SetBackgroundColour(palette.bannerBg);

    // 按钮：Pin、重试、复制、关闭 (统一使用 wxBitmapButton / wxBORDER_NONE 扁平无边框风格，贴合 MainFrame)
    wxBitmapBundle pinBundle = IconManager::GetIconBundle(SVG::PIN, dip(14, 14), palette.textSecondary);
    m_pinBtn = new wxBitmapButton(m_headerPanel, wxID_ANY, pinBundle, wxDefaultPosition, dip(28, 28), wxBORDER_NONE);
    m_pinBtn->SetBackgroundColour(palette.sidebarBg);
    m_pinBtn->SetToolTip(L"固定窗口位置");

    wxBitmapBundle retryBundle = IconManager::GetIconBundle(SVG::REPLACE, dip(14, 14), palette.textSecondary);
    m_retryBtn = new wxBitmapButton(m_headerPanel, wxID_ANY, retryBundle, wxDefaultPosition, dip(28, 28), wxBORDER_NONE);
    m_retryBtn->SetBackgroundColour(palette.sidebarBg);
    m_retryBtn->SetToolTip(L"重新翻译 (再次请求模型)");

    wxBitmapBundle copyBundle = IconManager::GetIconBundle(SVG::COPY, dip(14, 14), palette.textSecondary);
    m_copyBtn = new wxBitmapButton(m_headerPanel, wxID_ANY, copyBundle, wxDefaultPosition, dip(28, 28), wxBORDER_NONE);
    m_copyBtn->SetBackgroundColour(palette.sidebarBg);
    m_copyBtn->SetToolTip(L"复制译文");

    wxBitmapBundle closeBundle = IconManager::GetIconBundle(SVG::CLOSE, dip(14, 14), palette.textSecondary);
    m_closeBtn = new wxBitmapButton(m_headerPanel, wxID_ANY, closeBundle, wxDefaultPosition, dip(28, 28), wxBORDER_NONE);
    m_closeBtn->SetBackgroundColour(palette.sidebarBg);
    m_closeBtn->SetToolTip(L"关闭");

    headerSizer->Add(logoIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10_dip);
    headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6_dip);
    headerSizer->Add(m_langBadge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8_dip);
    headerSizer->AddStretchSpacer(1);
    headerSizer->Add(m_pinBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
    headerSizer->Add(m_retryBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
    headerSizer->Add(m_copyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
    headerSizer->Add(m_closeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    m_headerPanel->SetSizer(headerSizer);

    // 2. 原文与译文区域 (通过可拖动的 Splitter 进行上下平分与弹性调整)
    m_splitter = new SplitterWindow(m_mainPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxSP_LIVE_UPDATE | wxSP_NOBORDER);
    m_splitter->SetBackgroundColour(palette.cardBg);

    // 原文展示区
    m_sourcePanel = new wxPanel(m_splitter, wxID_ANY);
    m_sourcePanel->SetBackgroundColour(palette.windowBg);
    wxBoxSizer* sourceSizer = new wxBoxSizer(wxVERTICAL);

    m_sourceCtrl = new TextCtrl(m_sourcePanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
    m_sourceCtrl->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_sourceCtrl->SetBackgroundColour(palette.windowBg);
    m_sourceCtrl->SetForegroundColour(palette.textSecondary);
    sourceSizer->Add(m_sourceCtrl, 1, wxEXPAND);
    m_sourcePanel->SetSizer(sourceSizer);

    // 译文输出区
    m_targetPanel = new wxPanel(m_splitter, wxID_ANY);
    m_targetPanel->SetBackgroundColour(palette.cardBg);
    wxBoxSizer* targetSizer = new wxBoxSizer(wxVERTICAL);

    m_targetCtrl = new TextCtrl(m_targetPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE | wxTE_RICH2);
    m_targetCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_targetCtrl->SetBackgroundColour(palette.cardBg);
    m_targetCtrl->SetForegroundColour(palette.textPrimary);
    targetSizer->Add(m_targetCtrl, 1, wxEXPAND);
    m_targetPanel->SetSizer(targetSizer);

    // 平分窗体高度并设置最小 Pane 限制与等比缩放
    m_splitter->SetMinimumPaneSize(35_dip);
    m_splitter->SetSashGravity(0.5);
    m_splitter->SplitHorizontally(m_sourcePanel, m_targetPanel, 0);

    // 3. 底部状态栏与 Resize 手柄
    m_footerPanel = new wxPanel(m_mainPanel, wxID_ANY);
    m_footerPanel->SetBackgroundColour(palette.sidebarBg);
    wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);

    m_statusText = new wxStaticText(m_footerPanel, wxID_ANY, L"就绪");
    m_statusText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_statusText->SetForegroundColour(palette.textSecondary);

    m_resizeGrip = new wxPanel(m_footerPanel, wxID_ANY, wxDefaultPosition, dip(16, 16));
    m_resizeGrip->SetBackgroundColour(palette.sidebarBg);
    m_resizeGrip->SetCursor(wxCursor(wxCURSOR_SIZENWSE));

    footerSizer->Add(m_statusText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10_dip);
    footerSizer->AddStretchSpacer(1);
    footerSizer->Add(m_resizeGrip, 0, wxALIGN_BOTTOM | wxRIGHT | wxBOTTOM, 2_dip);
    m_footerPanel->SetSizer(footerSizer);

    mainSizer->Add(m_headerPanel, 0, wxEXPAND);
    mainSizer->Add(m_splitter, 1, wxEXPAND | wxLEFT | wxRIGHT, 6_dip);
    mainSizer->Add(m_footerPanel, 0, wxEXPAND);

    m_mainPanel->SetSizer(mainSizer);

    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_mainPanel, 1, wxEXPAND | wxALL, 1);
    SetSizer(frameSizer);
    Layout();

    // 确保初始化时原文面板与译文面板精确各占 50% 高度
    int splitterH = m_splitter->GetClientSize().y;
    if (splitterH > 0) {
        int sashSize = m_splitter->GetSashSize();
        m_splitter->SetSashPosition((splitterH - sashSize) / 2);
    }

    // 监听用户手动调节分割条
    m_splitter->Bind(wxEVT_SPLITTER_SASH_POS_CHANGED, [this](wxSplitterEvent& event) {
        m_hasUserAdjustedSash = true;
        event.Skip();
    });

    // 绘制 Header Panel 底部的精细分隔线 (参考 MainFrame 样式)
    m_headerPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_headerPanel);
        wxSize size = m_headerPanel->GetClientSize();
        ThemePalette p = ThemeManager::GetCurrentPalette();

        dc.SetBackground(wxBrush(p.sidebarBg));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetPen(gc->CreatePen(wxPen(p.cardBorder, 1)));
            gc->StrokeLine(0, size.y - 1, size.x, size.y - 1);
        }
    });

    // 标题栏拖拽与边缘检测事件
    m_headerPanel->Bind(wxEVT_LEFT_DOWN, &TranslationBubbleFrame::OnHeaderLeftDown, this);
    m_headerPanel->Bind(wxEVT_LEFT_UP, &TranslationBubbleFrame::OnHeaderLeftUp, this);
    m_headerPanel->Bind(wxEVT_MOTION, &TranslationBubbleFrame::OnHeaderMouseMove, this);
    m_headerPanel->Bind(wxEVT_LEAVE_WINDOW, &TranslationBubbleFrame::OnHeaderMouseLeave, this);

    m_titleText->Bind(wxEVT_LEFT_DOWN, &TranslationBubbleFrame::OnHeaderLeftDown, this);
    m_titleText->Bind(wxEVT_LEFT_UP, &TranslationBubbleFrame::OnHeaderLeftUp, this);
    m_titleText->Bind(wxEVT_MOTION, &TranslationBubbleFrame::OnHeaderMouseMove, this);
    m_titleText->Bind(wxEVT_LEAVE_WINDOW, &TranslationBubbleFrame::OnHeaderMouseLeave, this);

    // 主面板与底栏边缘检测
    m_mainPanel->Bind(wxEVT_LEFT_DOWN, &TranslationBubbleFrame::OnMainPanelLeftDown, this);
    m_mainPanel->Bind(wxEVT_LEFT_UP, &TranslationBubbleFrame::OnMainPanelLeftUp, this);
    m_mainPanel->Bind(wxEVT_MOTION, &TranslationBubbleFrame::OnMainPanelMouseMove, this);
    m_mainPanel->Bind(wxEVT_LEAVE_WINDOW, &TranslationBubbleFrame::OnMainPanelMouseLeave, this);

    m_footerPanel->Bind(wxEVT_LEFT_DOWN, &TranslationBubbleFrame::OnFooterLeftDown, this);
    m_footerPanel->Bind(wxEVT_LEFT_UP, &TranslationBubbleFrame::OnFooterLeftUp, this);
    m_footerPanel->Bind(wxEVT_MOTION, &TranslationBubbleFrame::OnFooterMouseMove, this);
    m_footerPanel->Bind(wxEVT_LEAVE_WINDOW, &TranslationBubbleFrame::OnFooterMouseLeave, this);

    // 右下角 Grip 手柄事件
    m_resizeGrip->Bind(wxEVT_PAINT, &TranslationBubbleFrame::OnGripPaint, this);
    m_resizeGrip->Bind(wxEVT_LEFT_DOWN, &TranslationBubbleFrame::OnGripLeftDown, this);
    m_resizeGrip->Bind(wxEVT_MOTION, &TranslationBubbleFrame::OnGripMouseMove, this);
    m_resizeGrip->Bind(wxEVT_LEFT_UP, &TranslationBubbleFrame::OnGripLeftUp, this);

    // 按钮操作事件
    m_copyBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnCopyResult, this);
    m_pinBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnTogglePin, this);
    m_retryBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnRetry, this);
    m_closeBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnCloseBtn, this);
}

void TranslationBubbleFrame::ShowAndTranslate(const wxPoint& spawnPos, const std::string& sourceText) {
    m_lastSourceText = sourceText;
    m_sourceCtrl->SetValue(wxString::FromUTF8(sourceText));
    m_targetCtrl->SetValue(L"正在启动翻译引擎...");
    m_statusText->SetLabel(L"正在翻译...");
    m_currentFullText.clear();

    const int bubbleWidth = m_bubbleSize.GetWidth();
    const int bubbleHeight = m_bubbleSize.GetHeight();
    int posX = 0;
    int posY = 0;

    if (m_isPinned && m_hasPinnedPos) {
        posX = m_pinnedPos.x;
        posY = m_pinnedPos.y;
    } else {
        posX = spawnPos.x;
        posY = spawnPos.y;
    }

#ifdef _WIN32
    POINT pt = { posX, posY };
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (hMon) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMon, &mi)) {
            int workLeft = mi.rcWork.left;
            int workRight = mi.rcWork.right;
            int workTop = mi.rcWork.top;
            int workBottom = mi.rcWork.bottom;

            if (posX + bubbleWidth > workRight) {
                posX = workRight - bubbleWidth - 10_dip;
            }
            if (posY + bubbleHeight > workBottom) {
                posY = workBottom - bubbleHeight - 10_dip;
            }
            if (posX < workLeft + 10_dip) posX = workLeft + 10_dip;
            if (posY < workTop + 10_dip) posY = workTop + 10_dip;
        }
    }
#else
    int displayIdx = wxDisplay::GetFromPoint(wxPoint(posX, posY));
    if (displayIdx != wxNOT_FOUND) {
        wxDisplay display(displayIdx);
        wxRect geom = display.GetClientArea();
        if (posX + bubbleWidth > geom.GetRight()) {
            posX = geom.GetRight() - bubbleWidth - 10;
        }
        if (posY + bubbleHeight > geom.GetBottom()) {
            posY = geom.GetBottom() - bubbleHeight - 10;
        }
        if (posX < geom.GetLeft() + 10) posX = geom.GetLeft() + 10;
        if (posY < geom.GetTop() + 10) posY = geom.GetTop() + 10;
    }
#endif

    SetPosition(wxPoint(posX, posY));
    SetSize(bubbleWidth, bubbleHeight);
    if (m_isPinned) {
        m_pinnedPos = wxPoint(posX, posY);
        m_hasPinnedPos = true;
    }
#ifdef _WIN32
    HWND hwnd = (HWND)GetHWND();
    if (hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, posX, posY, bubbleWidth, bubbleHeight,
                     SWP_SHOWWINDOW);
    }
#endif
    Show(true);
    Raise();
    Layout();

    // 每次显示时若用户未手动调节过分割条，确保原文与译文面板精确各占 50% 高度
    if (!m_hasUserAdjustedSash) {
        int splitterH = m_splitter->GetClientSize().y;
        if (splitterH > 0) {
            int sashSize = m_splitter->GetSashSize();
            m_splitter->SetSashPosition((splitterH - sashSize) / 2);
        }
    }

    if (!m_modelManager) {
        m_targetCtrl->SetValue(L"错误: ModelManager 未初始化");
        m_statusText->SetLabel(L"异常");
        return;
    }

    wxWeakRef<TranslationBubbleFrame> weakSelf(this);

    // 确保翻译模型已装载就绪，再执行流式翻译
    m_modelManager->EnsureModelAsync(
        TargetModelType::Translation,
        [weakSelf](const std::string& statusMsg) {
            if (wxTheApp) {
                wxTheApp->CallAfter([weakSelf, statusMsg]() {
                    if (!weakSelf || !weakSelf->m_statusText) return;
                    weakSelf->m_statusText->SetLabel(wxString::FromUTF8(statusMsg));
                });
            }
        },
        [weakSelf, sourceText](bool ok, const ServerStatusInfo& info) {
            if (wxTheApp) {
                wxTheApp->CallAfter([weakSelf, ok, info, sourceText]() {
                    if (!weakSelf) return;
                    if (!ok) {
                        if (weakSelf->m_targetCtrl) {
                            weakSelf->m_targetCtrl->SetValue(L"模型未就绪: " + wxString::FromUTF8(info.message) + L"\n请点击右上角「重新翻译」按钮重试。");
                        }
                        if (weakSelf->m_statusText) {
                            weakSelf->m_statusText->SetLabel(L"模型未就绪 (点击重试)");
                        }
                    } else {
                        weakSelf->DoExecuteTranslation(sourceText);
                    }
                });
            }
        }
    );
}

void TranslationBubbleFrame::DoExecuteTranslation(const std::string& sourceText) {
    if (!m_modelManager) {
        if (m_targetCtrl) m_targetCtrl->SetValue(L"错误: ModelManager 未初始化");
        if (m_statusText) m_statusText->SetLabel(L"异常");
        return;
    }

    TranslationTask task(
        sourceText,
        LanguageCode::AutoDetect,
        LanguageCode::Chinese
    );

    if (m_targetCtrl) m_targetCtrl->Clear();
    m_currentFullText.clear();
    if (m_statusText) m_statusText->SetLabel(L"正在翻译...");

    wxWeakRef<TranslationBubbleFrame> weakSelf(this);

    m_modelManager->ExecuteTranslationStream(
        task,
        // Token 流式接收
        [weakSelf](const std::string& token) {
            wxString wToken = wxString::FromUTF8(token);
            if (wxTheApp) {
                wxTheApp->CallAfter([weakSelf, wToken]() {
                    if (!weakSelf || !weakSelf->m_targetCtrl) return;
                    weakSelf->m_targetCtrl->AppendText(wToken);
                });
            }
        },
        // 完成回调
        [weakSelf](bool success, const std::string& fullText, const std::string& error) {
            if (wxTheApp) {
                wxTheApp->CallAfter([weakSelf, success, fullText, error]() {
                    if (!weakSelf || !weakSelf->m_targetCtrl) return;
                    if (success) {
                        weakSelf->m_targetCtrl->SetValue(wxString::FromUTF8(fullText));
                        weakSelf->m_currentFullText = fullText;
                        if (weakSelf->m_statusText) {
                            weakSelf->m_statusText->SetLabel(L"翻译完成 (Hy-MT2)");
                        }
                    } else {
                        weakSelf->m_targetCtrl->SetValue(L"翻译失败: " + wxString::FromUTF8(error) + L"\n\n请点击右上角「重新翻译」按钮重试。");
                        if (weakSelf->m_statusText) {
                            weakSelf->m_statusText->SetLabel(L"推理失败 (点击重试)");
                        }
                    }
                });
            }
        }
    );
}

void TranslationBubbleFrame::Dismiss() {
    Hide();
}

void TranslationBubbleFrame::UpdateTheme() {
    ThemePalette palette = ThemeManager::GetCurrentPalette();
    SetBackgroundColour(palette.cardBorder);

    if (m_mainPanel) m_mainPanel->SetBackgroundColour(palette.cardBg);
    if (m_headerPanel) m_headerPanel->SetBackgroundColour(palette.sidebarBg);
    if (m_titleText) m_titleText->SetForegroundColour(palette.textPrimary);
    if (m_langBadge) {
        m_langBadge->SetForegroundColour(palette.accentPrimary);
        m_langBadge->SetBackgroundColour(palette.bannerBg);
    }
    if (m_splitter) m_splitter->SetBackgroundColour(palette.cardBg);
    if (m_sourcePanel) m_sourcePanel->SetBackgroundColour(palette.windowBg);
    if (m_targetPanel) m_targetPanel->SetBackgroundColour(palette.cardBg);
    if (m_sourceCtrl) {
        m_sourceCtrl->SetBackgroundColour(palette.windowBg);
        m_sourceCtrl->SetForegroundColour(palette.textSecondary);
    }
    if (m_targetCtrl) {
        m_targetCtrl->SetBackgroundColour(palette.cardBg);
        m_targetCtrl->SetForegroundColour(palette.textPrimary);
    }
    if (m_footerPanel) {
        m_footerPanel->SetBackgroundColour(palette.sidebarBg);
        m_footerPanel->Refresh();
    }
    if (m_statusText) m_statusText->SetForegroundColour(palette.textSecondary);
    if (m_pinBtn) {
        m_pinBtn->SetBackgroundColour(palette.sidebarBg);
        wxColour pinColor = m_isPinned ? palette.accentPrimary : palette.textSecondary;
        wxBitmapBundle pinBundle = IconManager::GetIconBundle(SVG::PIN, dip(14, 14), pinColor);
        m_pinBtn->SetBitmap(pinBundle);
    }
    if (m_retryBtn) {
        m_retryBtn->SetBackgroundColour(palette.sidebarBg);
        wxBitmapBundle retryBundle = IconManager::GetIconBundle(SVG::REPLACE, dip(14, 14), palette.textSecondary);
        m_retryBtn->SetBitmap(retryBundle);
    }
    if (m_copyBtn) {
        m_copyBtn->SetBackgroundColour(palette.sidebarBg);
        wxBitmapBundle copyBundle = IconManager::GetIconBundle(SVG::COPY, dip(14, 14), palette.textSecondary);
        m_copyBtn->SetBitmap(copyBundle);
    }
    if (m_closeBtn) {
        m_closeBtn->SetBackgroundColour(palette.sidebarBg);
        wxBitmapBundle closeBundle = IconManager::GetIconBundle(SVG::CLOSE, dip(14, 14), palette.textSecondary);
        m_closeBtn->SetBitmap(closeBundle);
    }
    if (m_resizeGrip) {
        m_resizeGrip->SetBackgroundColour(palette.sidebarBg);
        m_resizeGrip->Refresh();
    }
    if (m_splitter) {
        m_splitter->Refresh();
    }

    Refresh();
}

TranslationBubbleFrame::ResizeDirection TranslationBubbleFrame::HitTest(const wxPoint& ptInFrame, const wxSize& frameSize) const {
    const int margin = 6_dip;
    bool onLeft = (ptInFrame.x >= 0 && ptInFrame.x <= margin);
    bool onRight = (ptInFrame.x >= frameSize.x - margin && ptInFrame.x <= frameSize.x);
    bool onTop = (ptInFrame.y >= 0 && ptInFrame.y <= margin);
    bool onBottom = (ptInFrame.y >= frameSize.y - margin && ptInFrame.y <= frameSize.y);

    if (onTop && onLeft) return ResizeDirection::TopLeft;
    if (onTop && onRight) return ResizeDirection::TopRight;
    if (onBottom && onLeft) return ResizeDirection::BottomLeft;
    if (onBottom && onRight) return ResizeDirection::BottomRight;
    if (onLeft) return ResizeDirection::Left;
    if (onRight) return ResizeDirection::Right;
    if (onTop) return ResizeDirection::Top;
    if (onBottom) return ResizeDirection::Bottom;

    return ResizeDirection::None;
}

void TranslationBubbleFrame::UpdateCursorForDir(ResizeDirection dir, wxWindow* targetWin) {
    if (!targetWin) return;
    switch (dir) {
        case ResizeDirection::Left:
        case ResizeDirection::Right:
            targetWin->SetCursor(wxCursor(wxCURSOR_SIZEWE));
            break;
        case ResizeDirection::Top:
        case ResizeDirection::Bottom:
            targetWin->SetCursor(wxCursor(wxCURSOR_SIZENS));
            break;
        case ResizeDirection::TopLeft:
        case ResizeDirection::BottomRight:
            targetWin->SetCursor(wxCursor(wxCURSOR_SIZENWSE));
            break;
        case ResizeDirection::TopRight:
        case ResizeDirection::BottomLeft:
            targetWin->SetCursor(wxCursor(wxCURSOR_SIZENESW));
            break;
        default:
            targetWin->SetCursor(wxNullCursor);
            break;
    }
}

void TranslationBubbleFrame::StartResize(ResizeDirection dir, const wxPoint& screenPos, wxWindow* captureWin) {
    if (dir == ResizeDirection::None) return;
    m_isResizing = true;
    m_resizeDir = dir;
    m_resizeStartMousePos = screenPos;
    m_resizeStartFramePos = GetPosition();
    m_resizeStartFrameSize = GetSize();
    m_resizeCaptureWin = captureWin;
    if (m_resizeCaptureWin && !m_resizeCaptureWin->HasCapture()) {
        m_resizeCaptureWin->CaptureMouse();
    }
}

void TranslationBubbleFrame::ProcessResizeDrag(const wxPoint& screenPos) {
    if (!m_isResizing || m_resizeDir == ResizeDirection::None) return;

    int deltaX = screenPos.x - m_resizeStartMousePos.x;
    int deltaY = screenPos.y - m_resizeStartMousePos.y;

    int newX = m_resizeStartFramePos.x;
    int newY = m_resizeStartFramePos.y;
    int newW = m_resizeStartFrameSize.x;
    int newH = m_resizeStartFrameSize.y;

    const int minW = 320_dip;
    const int minH = 200_dip;

    switch (m_resizeDir) {
        case ResizeDirection::Right:
            newW = std::max(minW, m_resizeStartFrameSize.x + deltaX);
            break;
        case ResizeDirection::Bottom:
            newH = std::max(minH, m_resizeStartFrameSize.y + deltaY);
            break;
        case ResizeDirection::BottomRight:
            newW = std::max(minW, m_resizeStartFrameSize.x + deltaX);
            newH = std::max(minH, m_resizeStartFrameSize.y + deltaY);
            break;
        case ResizeDirection::Left: {
            int calculatedW = m_resizeStartFrameSize.x - deltaX;
            if (calculatedW < minW) {
                newX = m_resizeStartFramePos.x + (m_resizeStartFrameSize.x - minW);
                newW = minW;
            } else {
                newX = m_resizeStartFramePos.x + deltaX;
                newW = calculatedW;
            }
            break;
        }
        case ResizeDirection::Top: {
            int calculatedH = m_resizeStartFrameSize.y - deltaY;
            if (calculatedH < minH) {
                newY = m_resizeStartFramePos.y + (m_resizeStartFrameSize.y - minH);
                newH = minH;
            } else {
                newY = m_resizeStartFramePos.y + deltaY;
                newH = calculatedH;
            }
            break;
        }
        case ResizeDirection::TopLeft: {
            int calculatedW = m_resizeStartFrameSize.x - deltaX;
            if (calculatedW < minW) {
                newX = m_resizeStartFramePos.x + (m_resizeStartFrameSize.x - minW);
                newW = minW;
            } else {
                newX = m_resizeStartFramePos.x + deltaX;
                newW = calculatedW;
            }
            int calculatedH = m_resizeStartFrameSize.y - deltaY;
            if (calculatedH < minH) {
                newY = m_resizeStartFramePos.y + (m_resizeStartFrameSize.y - minH);
                newH = minH;
            } else {
                newY = m_resizeStartFramePos.y + deltaY;
                newH = calculatedH;
            }
            break;
        }
        case ResizeDirection::TopRight: {
            newW = std::max(minW, m_resizeStartFrameSize.x + deltaX);
            int calculatedH = m_resizeStartFrameSize.y - deltaY;
            if (calculatedH < minH) {
                newY = m_resizeStartFramePos.y + (m_resizeStartFrameSize.y - minH);
                newH = minH;
            } else {
                newY = m_resizeStartFramePos.y + deltaY;
                newH = calculatedH;
            }
            break;
        }
        case ResizeDirection::BottomLeft: {
            int calculatedW = m_resizeStartFrameSize.x - deltaX;
            if (calculatedW < minW) {
                newX = m_resizeStartFramePos.x + (m_resizeStartFrameSize.x - minW);
                newW = minW;
            } else {
                newX = m_resizeStartFramePos.x + deltaX;
                newW = calculatedW;
            }
            newH = std::max(minH, m_resizeStartFrameSize.y + deltaY);
            break;
        }
        default:
            break;
    }

    SetSize(newX, newY, newW, newH);
    m_bubbleSize = wxSize(newW, newH);
    if (m_isPinned) {
        m_pinnedPos = wxPoint(newX, newY);
        m_hasPinnedPos = true;
    }
    Layout();
}

void TranslationBubbleFrame::EndResize() {
    if (m_isResizing) {
        m_isResizing = false;
        m_resizeDir = ResizeDirection::None;
        if (m_resizeCaptureWin && m_resizeCaptureWin->HasCapture()) {
            m_resizeCaptureWin->ReleaseMouse();
        }
        m_resizeCaptureWin = nullptr;
    }
}

void TranslationBubbleFrame::OnHeaderLeftDown(wxMouseEvent& event) {
    wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
    ResizeDirection dir = HitTest(ptInFrame, GetSize());
    if (dir != ResizeDirection::None && dir != ResizeDirection::Bottom) {
        StartResize(dir, wxGetMousePosition(), m_headerPanel);
    } else {
        m_isDragging = true;
        m_dragStartPos = event.GetPosition();
        m_headerPanel->CaptureMouse();
    }
}

void TranslationBubbleFrame::OnHeaderLeftUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        EndResize();
    }
    if (m_isDragging) {
        m_isDragging = false;
        if (m_headerPanel->HasCapture()) {
            m_headerPanel->ReleaseMouse();
        }
    }
}

void TranslationBubbleFrame::OnHeaderMouseMove(wxMouseEvent& event) {
    if (m_isResizing) {
        ProcessResizeDrag(wxGetMousePosition());
        return;
    }
    if (m_isDragging && event.Dragging() && event.LeftIsDown()) {
        wxPoint mouseOnScreen = wxGetMousePosition();
        wxPoint newPos = mouseOnScreen - m_dragStartPos;
        SetPosition(newPos);
        if (m_isPinned) {
            m_pinnedPos = newPos;
            m_hasPinnedPos = true;
        }
        return;
    }
    wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
    ResizeDirection dir = HitTest(ptInFrame, GetSize());
    if (dir == ResizeDirection::Top || dir == ResizeDirection::TopLeft ||
        dir == ResizeDirection::TopRight || dir == ResizeDirection::Left ||
        dir == ResizeDirection::Right) {
        UpdateCursorForDir(dir, m_headerPanel);
    } else {
        m_headerPanel->SetCursor(wxNullCursor);
    }
}

void TranslationBubbleFrame::OnHeaderMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (!m_isResizing && !m_isDragging) {
        m_headerPanel->SetCursor(wxNullCursor);
    }
}

void TranslationBubbleFrame::OnMainPanelMouseMove(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        ProcessResizeDrag(wxGetMousePosition());
        return;
    }
    wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
    ResizeDirection dir = HitTest(ptInFrame, GetSize());
    UpdateCursorForDir(dir, m_mainPanel);
}

void TranslationBubbleFrame::OnMainPanelLeftDown(wxMouseEvent& WXUNUSED(event)) {
    wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
    ResizeDirection dir = HitTest(ptInFrame, GetSize());
    if (dir != ResizeDirection::None) {
        StartResize(dir, wxGetMousePosition(), m_mainPanel);
    }
}

void TranslationBubbleFrame::OnMainPanelLeftUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        EndResize();
    }
}

void TranslationBubbleFrame::OnMainPanelMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (!m_isResizing) {
        m_mainPanel->SetCursor(wxNullCursor);
    }
}

void TranslationBubbleFrame::OnFooterMouseMove(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        ProcessResizeDrag(wxGetMousePosition());
        return;
    }
    wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
    ResizeDirection dir = HitTest(ptInFrame, GetSize());
    UpdateCursorForDir(dir, m_footerPanel);
}

void TranslationBubbleFrame::OnFooterLeftDown(wxMouseEvent& WXUNUSED(event)) {
    wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
    ResizeDirection dir = HitTest(ptInFrame, GetSize());
    if (dir != ResizeDirection::None) {
        StartResize(dir, wxGetMousePosition(), m_footerPanel);
    }
}

void TranslationBubbleFrame::OnFooterLeftUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        EndResize();
    }
}

void TranslationBubbleFrame::OnFooterMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (!m_isResizing) {
        m_footerPanel->SetCursor(wxNullCursor);
    }
}

void TranslationBubbleFrame::OnGripPaint(wxPaintEvent& WXUNUSED(event)) {
    wxPaintDC dc(m_resizeGrip);
    wxSize sz = m_resizeGrip->GetClientSize();
    ThemePalette palette = ThemeManager::GetCurrentPalette();
    dc.SetPen(wxPen(palette.textSecondary, 1));

    int x = sz.GetWidth() - 3_dip;
    int y = sz.GetHeight() - 3_dip;

    for (int i = 0; i < 3; ++i) {
        int offset = (i + 1) * 3_dip;
        dc.DrawLine(x - offset, y, x, y - offset);
    }
}

void TranslationBubbleFrame::OnGripLeftDown(wxMouseEvent& WXUNUSED(event)) {
    StartResize(ResizeDirection::BottomRight, wxGetMousePosition(), m_resizeGrip);
}

void TranslationBubbleFrame::OnGripMouseMove(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        ProcessResizeDrag(wxGetMousePosition());
    }
}

void TranslationBubbleFrame::OnGripLeftUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isResizing) {
        EndResize();
    }
}

void TranslationBubbleFrame::OnCopyResult(wxCommandEvent& WXUNUSED(event)) {
    if (!m_currentFullText.empty()) {
        ClipboardHelper::SetClipboardText(m_currentFullText);
        if (m_statusText) {
            m_statusText->SetLabel(L"已复制到剪贴板！");
        }
    }
}

void TranslationBubbleFrame::OnTogglePin(wxCommandEvent& WXUNUSED(event)) {
    m_isPinned = !m_isPinned;
    if (m_isPinned) {
        m_pinnedPos = GetPosition();
        m_hasPinnedPos = true;
    }
    ThemePalette palette = ThemeManager::GetCurrentPalette();
    wxColour iconColor = m_isPinned ? palette.accentPrimary : palette.textSecondary;
    wxBitmapBundle pinBundle = IconManager::GetIconBundle(SVG::PIN, dip(14, 14), iconColor);
    m_pinBtn->SetBitmap(pinBundle);
    m_pinBtn->SetToolTip(m_isPinned ? L"已固定窗口位置 (再次点击取消固定)" : L"固定窗口位置");
}

void TranslationBubbleFrame::OnRetry(wxCommandEvent& WXUNUSED(event)) {
    std::string textToTranslate = m_lastSourceText;
    if (textToTranslate.empty() && m_sourceCtrl) {
        textToTranslate = m_sourceCtrl->GetValue().ToStdString();
    }
    if (textToTranslate.empty()) {
        return;
    }

    if (m_targetCtrl) {
        m_targetCtrl->SetValue(L"正在重新连接模型并翻译...");
    }
    if (m_statusText) {
        m_statusText->SetLabel(L"正在重试...");
    }

    if (!m_modelManager) {
        if (m_targetCtrl) m_targetCtrl->SetValue(L"错误: ModelManager 未初始化");
        if (m_statusText) m_statusText->SetLabel(L"异常");
        return;
    }

    wxWeakRef<TranslationBubbleFrame> weakSelf(this);
    m_modelManager->EnsureModelAsync(
        TargetModelType::Translation,
        [weakSelf](const std::string& statusMsg) {
            if (wxTheApp) {
                wxTheApp->CallAfter([weakSelf, statusMsg]() {
                    if (!weakSelf || !weakSelf->m_statusText) return;
                    weakSelf->m_statusText->SetLabel(wxString::FromUTF8(statusMsg));
                });
            }
        },
        [weakSelf, textToTranslate](bool ok, const ServerStatusInfo& info) {
            if (wxTheApp) {
                wxTheApp->CallAfter([weakSelf, ok, info, textToTranslate]() {
                    if (!weakSelf) return;
                    if (!ok) {
                        if (weakSelf->m_targetCtrl) {
                            weakSelf->m_targetCtrl->SetValue(L"模型启动失败: " + wxString::FromUTF8(info.message) + L"\n请检查模型路径或点击重试。");
                        }
                        if (weakSelf->m_statusText) {
                            weakSelf->m_statusText->SetLabel(L"模型未就绪 (点击重试)");
                        }
                    } else {
                        weakSelf->DoExecuteTranslation(textToTranslate);
                    }
                });
            }
        }
    );
}

void TranslationBubbleFrame::OnCloseBtn(wxCommandEvent& WXUNUSED(event)) {
    Dismiss();
}

} // namespace LinguaAlpaca::UI
