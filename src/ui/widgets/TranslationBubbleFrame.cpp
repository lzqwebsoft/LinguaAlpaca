#pragma execution_character_set("utf-8")
#include "TranslationBubbleFrame.hpp"
#include "../theme/Theme.hpp"
#include "../theme/AppIcons.hpp"
#include "../theme/IconManager.hpp"
#include "../../core/ClipboardHelper.hpp"
#include "../../core/WinTtsHelper.hpp"

#include <wx/display.h>
#include <wx/dcbuffer.h>
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

    TranslationBubbleFrame::~TranslationBubbleFrame() {
        WinTtsHelper::GetInstance().Stop();
    }

    void TranslationBubbleFrame::InitUI() {
        ThemePalette palette = ThemeManager::GetCurrentPalette();
        SetBackgroundColour(palette.cardBorder);

        m_mainPanel = new wxPanel(this, wxID_ANY);
        m_mainPanel->SetBackgroundColour(palette.cardBg);

        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

        // 1. 顶部拖拽标题栏 (参考 MainFrame 美化风格)
        m_headerPanel = new wxPanel(m_mainPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 40_dip), wxBORDER_NONE);
        m_headerPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

        wxBitmapBundle logoBundle = IconManager::GetIconBundle(SVG::TRANSLATE, wxSize(18, 18), palette.accentPrimary);
        wxStaticBitmap* logoIcon = new wxStaticBitmap(m_headerPanel, wxID_ANY, logoBundle);

        m_titleText = new wxStaticText(m_headerPanel, wxID_ANY, L"译灵驼");
        m_titleText->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        m_titleText->SetForegroundColour(palette.textPrimary);

        m_langBadge = new StatusBadge(m_headerPanel, wxID_ANY);
        UpdateLanguageBadge();

        // 读取配置中的字号大小
        if (m_modelManager && m_modelManager->GetConfigManager()) {
            m_currentFontSize = m_modelManager->GetConfigManager()->GetConfig().bubbleFontSize;
        }
        m_currentFontSize = std::clamp(m_currentFontSize, 8, 22);

        // 辅助 lambda: 统一创建扁平无边框操作按钮，消除几十行重复 new/SetToolTip 代码
        auto createHeaderBtn = [this](const wxString& tooltip) -> wxBitmapButton* {
            auto* btn = new wxBitmapButton(m_headerPanel, wxID_ANY, wxBitmapBundle(), wxDefaultPosition, dip(30, 30), wxBORDER_NONE);
            btn->SetToolTip(tooltip);
            return btn;
        };

        m_fontDecreaseBtn = createHeaderBtn(L"缩小字体 (A-)");
        m_fontIncreaseBtn = createHeaderBtn(L"放大字体 (A+)");
        m_pinBtn = createHeaderBtn(L"固定窗口位置");
        m_retryBtn = createHeaderBtn(L"重新翻译 (再次请求模型)");
        m_copyBtn = createHeaderBtn(L"复制译文");
        m_closeBtn = createHeaderBtn(L"关闭");

        headerSizer->Add(logoIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10_dip);
        headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6_dip);
        headerSizer->Add(m_langBadge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8_dip);
        headerSizer->AddStretchSpacer(1);
        headerSizer->Add(m_fontDecreaseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2_dip);
        headerSizer->Add(m_fontIncreaseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
        headerSizer->Add(m_pinBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
        headerSizer->Add(m_retryBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
        headerSizer->Add(m_copyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);
        headerSizer->Add(m_closeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
        m_headerPanel->SetSizer(headerSizer);

        // 1.5 原文折叠条 (用于收起/展开原文，保持界面聚焦)
        m_sourceToggleBar = new wxPanel(m_mainPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 26_dip), wxBORDER_NONE);
        m_sourceToggleBar->SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_sourceToggleBar->SetCursor(wxCursor(wxCURSOR_HAND));
        m_sourceToggleBar->SetToolTip(L"点击展开/折叠原文");

        wxBoxSizer* toggleSizer = new wxBoxSizer(wxHORIZONTAL);
        m_sourceToggleIcon = new wxStaticBitmap(m_sourceToggleBar, wxID_ANY, wxBitmapBundle());
        m_sourceToggleIcon->SetCursor(wxCursor(wxCURSOR_HAND));

        m_sourceToggleLabel = new wxStaticText(m_sourceToggleBar, wxID_ANY, L"显示原文");
        m_sourceToggleLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        m_sourceToggleLabel->SetCursor(wxCursor(wxCURSOR_HAND));

        m_sourcePreviewText = new wxStaticText(m_sourceToggleBar, wxID_ANY, "");
        m_sourcePreviewText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        m_sourcePreviewText->SetCursor(wxCursor(wxCURSOR_HAND));

        toggleSizer->Add(m_sourceToggleIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10_dip);
        toggleSizer->Add(m_sourceToggleLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4_dip);
        toggleSizer->Add(m_sourcePreviewText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6_dip);
        m_sourceToggleBar->SetSizer(toggleSizer);

        // 高性能底层绘制: 直接使用原生 GDI DC 绘制 1px 分隔线，避免每次 Paint 创建重型 wxGraphicsContext COM 实例
        m_headerPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(m_headerPanel);
            wxSize sz = m_headerPanel->GetClientSize();
            ThemePalette p = ThemeManager::GetCurrentPalette();
            dc.SetBackground(wxBrush(p.sidebarBg));
            dc.Clear();
            dc.SetPen(wxPen(p.cardBorder, 1));
            dc.DrawLine(0, sz.y - 1, sz.x, sz.y - 1);
        });

        m_sourceToggleBar->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(m_sourceToggleBar);
            wxSize sz = m_sourceToggleBar->GetClientSize();
            ThemePalette p = ThemeManager::GetCurrentPalette();
            dc.SetBackground(wxBrush(p.sidebarBg));
            dc.Clear();
            dc.SetPen(wxPen(p.cardBorder, 1));
            dc.DrawLine(0, sz.y - 1, sz.x, sz.y - 1);
        });

        // 折叠栏点击
        auto onToggleClick = [this](wxMouseEvent&) {
            SetSourcePanelExpanded(!m_isSourceExpanded);
        };
        m_sourceToggleBar->Bind(wxEVT_LEFT_DOWN, onToggleClick);
        m_sourceToggleIcon->Bind(wxEVT_LEFT_DOWN, onToggleClick);
        m_sourceToggleLabel->Bind(wxEVT_LEFT_DOWN, onToggleClick);
        m_sourcePreviewText->Bind(wxEVT_LEFT_DOWN, onToggleClick);

        // 悬停高亮仅调整文本色，避免在鼠标移动时重新生成 SVG Bundle
        auto onToggleEnter = [this](wxMouseEvent& evt) {
            evt.Skip();
            if (m_sourceToggleLabel) {
                m_sourceToggleLabel->SetForegroundColour(ThemeManager::GetCurrentPalette().accentPrimary);
                m_sourceToggleLabel->Refresh();
            }
        };
        auto onToggleLeave = [this](wxMouseEvent& evt) {
            evt.Skip();
            if (m_sourceToggleLabel) {
                m_sourceToggleLabel->SetForegroundColour(ThemeManager::GetCurrentPalette().textSecondary);
                m_sourceToggleLabel->Refresh();
            }
        };
        m_sourceToggleBar->Bind(wxEVT_ENTER_WINDOW, onToggleEnter);
        m_sourceToggleBar->Bind(wxEVT_LEAVE_WINDOW, onToggleLeave);

        // 2. 原文与译文区域 (Splitter 上下平分)
        m_splitter = new SplitterWindow(m_mainPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_NOBORDER);

        wxBitmapBundle speakBundle = IconManager::GetIconBundle(SVG::SPEAKER, wxSize(14, 14), palette.textSecondary);

        // 原文展示与编辑区
        m_sourcePanel = new wxPanel(m_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);
        m_sourcePanel->SetBackgroundColour(palette.windowBg);
        wxBoxSizer* sourceSizer = new wxBoxSizer(wxVERTICAL);

        m_sourceCtrl = new TextCtrl(m_sourcePanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxBORDER_NONE | wxTE_RICH2);
        m_sourceCtrl->SetFont(wxFont(std::max(8, m_currentFontSize - 1), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        m_sourceCtrl->SetBackgroundColour(palette.windowBg);
        m_sourceCtrl->SetForegroundColour(palette.textPrimary);
        m_sourceCtrl->SetHint(L"输入或编辑待翻译文本 (Ctrl+Enter 翻译)...");

        if (m_sourceCtrl->GetInnerCtrl()) {
            m_sourceCtrl->GetInnerCtrl()->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& evt) {
                if (evt.GetKeyCode() == WXK_RETURN && evt.ControlDown()) {
                    wxCommandEvent dummy;
                    OnRetry(dummy);
                } else {
                    evt.Skip();
                }
            });
        }

        sourceSizer->Add(m_sourceCtrl, 1, wxEXPAND);
        m_sourcePanel->SetSizer(sourceSizer);

        m_sourceSpeakBtn = new wxBitmapButton(m_sourcePanel, wxID_ANY, speakBundle, wxDefaultPosition, dip(24, 24), wxBORDER_NONE);
        m_sourceSpeakBtn->SetBackgroundColour(palette.windowBg);
        m_sourceSpeakBtn->SetToolTip(L"朗读原文");
        m_sourceSpeakBtn->SetCursor(wxCursor(wxCURSOR_HAND));

        // 译文输出区
        m_targetPanel = new wxPanel(m_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);
        m_targetPanel->SetBackgroundColour(palette.cardBg);
        wxBoxSizer* targetSizer = new wxBoxSizer(wxVERTICAL);

        m_targetCtrl = new TextCtrl(m_targetPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE | wxTE_RICH2);
        m_targetCtrl->SetFont(wxFont(m_currentFontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        m_targetCtrl->SetBackgroundColour(palette.cardBg);
        m_targetCtrl->SetForegroundColour(palette.textPrimary);
        targetSizer->Add(m_targetCtrl, 1, wxEXPAND);
        m_targetPanel->SetSizer(targetSizer);

        m_targetSpeakBtn = new wxBitmapButton(m_targetPanel, wxID_ANY, speakBundle, wxDefaultPosition, dip(24, 24), wxBORDER_NONE);
        m_targetSpeakBtn->SetBackgroundColour(palette.cardBg);
        m_targetSpeakBtn->SetToolTip(L"朗读译文");
        m_targetSpeakBtn->SetCursor(wxCursor(wxCURSOR_HAND));

        // 悬浮播放按钮自适应定位与置顶 (确保悬浮于 TextCtrl 之上)
        m_sourcePanel->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            if (m_sourceSpeakBtn && m_sourcePanel) {
                wxSize sz = m_sourcePanel->GetClientSize();
                wxSize btnSz = m_sourceSpeakBtn->GetSize();
                m_sourceSpeakBtn->Move(sz.x - btnSz.x - 6_dip, 4_dip);
                m_sourceSpeakBtn->Raise();
            }
        });

        m_targetPanel->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            if (m_targetSpeakBtn && m_targetPanel) {
                wxSize sz = m_targetPanel->GetClientSize();
                wxSize btnSz = m_targetSpeakBtn->GetSize();
                m_targetSpeakBtn->Move(sz.x - btnSz.x - 6_dip, 4_dip);
                m_targetSpeakBtn->Raise();
            }
        });

        m_targetPanel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            event.Skip();
            if (m_targetCtrl && m_targetCtrl->GetInnerCtrl()) {
                m_targetCtrl->GetInnerCtrl()->SetFocus();
            }
        });

        m_sourceSpeakBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!m_sourceCtrl) return;
            wxString text = m_sourceCtrl->GetValue();
            if (text.IsEmpty()) return;
            LanguageCode srcCode = LanguageCode::AutoDetect;
            if (m_modelManager && m_modelManager->GetConfigManager()) {
                srcCode = LanguageHelper::FromCodeName(m_modelManager->GetConfigManager()->GetConfig().sourceLang);
            }
            WinTtsHelper::GetInstance().Speak(text.ToStdWstring(), srcCode);
        });

        m_targetSpeakBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!m_targetCtrl) return;
            wxString text = m_targetCtrl->GetValue();
            if (text.IsEmpty()) return;
            LanguageCode tgtCode = LanguageCode::Chinese;
            if (m_modelManager && m_modelManager->GetConfigManager()) {
                tgtCode = LanguageHelper::FromCodeName(m_modelManager->GetConfigManager()->GetConfig().targetLang);
            }
            WinTtsHelper::GetInstance().Speak(text.ToStdWstring(), tgtCode);
        });

        m_splitter->SetMinimumPaneSize(35_dip);
        m_splitter->SetSashGravity(0.5);
        m_isSourceExpanded = false;
        m_sourcePanel->Hide();
        m_splitter->Initialize(m_targetPanel);

        // 3. 底部状态栏与 Resize 手柄
        m_footerPanel = new wxPanel(m_mainPanel, wxID_ANY);
        wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);

        m_statusText = new wxStaticText(m_footerPanel, wxID_ANY, L"就绪");
        m_statusText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));

        m_resizeGrip = new wxPanel(m_footerPanel, wxID_ANY, wxDefaultPosition, dip(16, 16));
        m_resizeGrip->SetCursor(wxCursor(wxCURSOR_SIZENWSE));

        footerSizer->Add(m_statusText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10_dip);
        footerSizer->AddStretchSpacer(1);
        footerSizer->Add(m_resizeGrip, 0, wxALIGN_BOTTOM | wxRIGHT | wxBOTTOM, 2_dip);
        m_footerPanel->SetSizer(footerSizer);

        mainSizer->Add(m_headerPanel, 0, wxEXPAND);
        mainSizer->Add(m_sourceToggleBar, 0, wxEXPAND);
        mainSizer->Add(m_splitter, 1, wxEXPAND);
        mainSizer->Add(m_footerPanel, 0, wxEXPAND);
        m_mainPanel->SetSizer(mainSizer);

        wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
        frameSizer->Add(m_mainPanel, 1, wxEXPAND | wxALL, 1);
        SetSizer(frameSizer);
        Layout();

        // 统一绑定边缘调整与标题栏拖拽事件 (彻底去除 16 个冗余中转函数)
        auto bindEdgeEvents = [this](wxWindow* win, bool isHeader) {
            win->Bind(wxEVT_LEFT_DOWN, [this, win, isHeader](wxMouseEvent& e) { HandleEdgeLeftDown(e, win, isHeader); });
            win->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) { HandleEdgeLeftUp(e); });
            win->Bind(wxEVT_MOTION, [this, win, isHeader](wxMouseEvent& e) { HandleEdgeMouseMove(e, win, isHeader); });
            win->Bind(wxEVT_LEAVE_WINDOW, [this, win](wxMouseEvent& e) { HandleEdgeMouseLeave(e, win); });
        };

        bindEdgeEvents(m_headerPanel, true);
        bindEdgeEvents(m_titleText, true);
        bindEdgeEvents(m_mainPanel, false);
        bindEdgeEvents(m_footerPanel, false);

        // 右下角 Grip 手柄事件
        m_resizeGrip->Bind(wxEVT_PAINT, &TranslationBubbleFrame::OnGripPaint, this);
        m_resizeGrip->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            StartResize(ResizeDirection::BottomRight, wxGetMousePosition(), m_resizeGrip);
        });
        m_resizeGrip->Bind(wxEVT_MOTION, [this](wxMouseEvent&) {
            if (m_isResizing) ProcessResizeDrag(wxGetMousePosition());
         });
        m_resizeGrip->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
            if (m_isResizing) EndResize();
        });

        // 按钮操作事件
        m_fontDecreaseBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnDecreaseFontSize, this);
        m_fontIncreaseBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnIncreaseFontSize, this);
        m_copyBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnCopyResult, this);
        m_pinBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnTogglePin, this);
        m_retryBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnRetry, this);
        m_closeBtn->Bind(wxEVT_BUTTON, &TranslationBubbleFrame::OnCloseBtn, this);
    }

    void TranslationBubbleFrame::ShowAndTranslate(const wxPoint& spawnPos, const std::string& sourceText) {
        WinTtsHelper::GetInstance().Stop();
        m_lastSourceText = sourceText;
        UpdateLanguageBadge();

        // 同步加载最新字号设置
        if (m_modelManager && m_modelManager->GetConfigManager()) {
            int cfgFontSize = m_modelManager->GetConfigManager()->GetConfig().bubbleFontSize;
            if (cfgFontSize >= 8 && cfgFontSize <= 22 && cfgFontSize != m_currentFontSize) {
                ApplyFontSize(cfgFontSize, false);
            }
        }

        m_sourceCtrl->SetValue(wxString::FromUTF8(sourceText));
        SetSourcePanelExpanded(false);
        m_targetCtrl->SetValue(L"正在启动翻译引擎...");
        m_statusText->SetLabel(L"正在翻译...");
        m_currentFullText.clear();

        const int bubbleWidth = m_bubbleSize.GetWidth();
        const int bubbleHeight = m_bubbleSize.GetHeight();
        int posX = (m_isPinned && m_hasPinnedPos) ? m_pinnedPos.x : spawnPos.x;
        int posY = (m_isPinned && m_hasPinnedPos) ? m_pinnedPos.y : spawnPos.y;

#ifdef _WIN32
        POINT pt = { posX, posY };
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (hMon) {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfo(hMon, &mi)) {
                if (posX + bubbleWidth > mi.rcWork.right) {
                    posX = mi.rcWork.right - bubbleWidth - 10_dip;
                }
                if (posY + bubbleHeight > mi.rcWork.bottom) {
                    posY = mi.rcWork.bottom - bubbleHeight - 10_dip;
                }
                if (posX < mi.rcWork.left + 10_dip) posX = mi.rcWork.left + 10_dip;
                if (posY < mi.rcWork.top + 10_dip) posY = mi.rcWork.top + 10_dip;
            }
        }
#else
        int displayIdx = wxDisplay::GetFromPoint(wxPoint(posX, posY));
        if (displayIdx != wxNOT_FOUND) {
            wxDisplay display(displayIdx);
            wxRect geom = display.GetClientArea();
            if (posX + bubbleWidth > geom.GetRight()) posX = geom.GetRight() - bubbleWidth - 10;
            if (posY + bubbleHeight > geom.GetBottom()) posY = geom.GetBottom() - bubbleHeight - 10;
            if (posX < geom.GetLeft() + 10) posX = geom.GetLeft() + 10;
            if (posY < geom.GetTop() + 10) posY = geom.GetTop() + 10;
        }
#endif

        // 单次调用 SetSize 完成位置与尺寸设定，减少重复 Windows 窗口移动消息风暴
        SetSize(posX, posY, bubbleWidth, bubbleHeight);
        if (m_isPinned) {
            m_pinnedPos = wxPoint(posX, posY);
            m_hasPinnedPos = true;
        }
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(GetHWND());
        if (hwnd) {
            ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
#endif
        Show(true);
        Raise();

        if (!m_modelManager) {
            m_targetCtrl->SetValue(L"错误: ModelManager 未初始化");
            m_statusText->SetLabel(L"异常");
            return;
        }

        // 确保翻译模型已装载就绪，再执行流式翻译
        m_modelManager->EnsureModelAsync(
            TargetModelType::Translation,
            BindUi([this](const std::string& statusMsg) {
                if (m_statusText) {
                    m_statusText->SetLabel(wxString::FromUTF8(statusMsg));
                }
                }),
            BindUi([this, sourceText](bool ok, const ServerStatusInfo& info) {
                if (!ok) {
                    if (m_targetCtrl) {
                        m_targetCtrl->SetValue(L"模型未就绪: " + wxString::FromUTF8(info.message) + L"\n请点击右上角「重新翻译」按钮重试。");
                    }
                    if (m_statusText) {
                        m_statusText->SetLabel(L"模型未就绪 (点击重试)");
                    }
                } else {
                    DoExecuteTranslation(sourceText);
                }
            })
        );
    }

    void TranslationBubbleFrame::DoExecuteTranslation(const std::string& sourceText) {
        if (!m_modelManager) {
            if (m_targetCtrl) m_targetCtrl->SetValue(L"错误: ModelManager 未初始化");
            if (m_statusText) m_statusText->SetLabel(L"异常");
            return;
        }

        LanguageCode srcCode = LanguageCode::AutoDetect;
        LanguageCode tgtCode = LanguageCode::Chinese;
        if (m_modelManager && m_modelManager->GetConfigManager()) {
            auto cfg = m_modelManager->GetConfigManager()->GetConfig();
            srcCode = LanguageHelper::FromCodeName(cfg.sourceLang);
            tgtCode = LanguageHelper::FromCodeName(cfg.targetLang);
        }

        TranslationTask task(sourceText, srcCode, tgtCode);

        if (m_targetCtrl) m_targetCtrl->Clear();
        m_currentFullText.clear();
        if (m_statusText) m_statusText->SetLabel(L"正在翻译...");

        m_modelManager->ExecuteTranslationStream(
            task,
            // Token 流式接收
            BindUi([this](const std::string& token) {
                if (m_targetCtrl) {
                    m_targetCtrl->AppendText(wxString::FromUTF8(token));
                }
            }),
            // 完成回调
            BindUi([this](bool success, const std::string& fullText, const std::string& error) {
                if (!m_targetCtrl) return;
                if (success) {
                    m_currentFullText = fullText;
                    m_targetCtrl->SetMarkdown(fullText);
                    if (m_statusText) {
                        m_statusText->SetLabel(L"翻译完成 (Hy-MT2)");
                    }
                } else {
                    m_targetCtrl->SetValue(L"翻译失败: " + wxString::FromUTF8(error) + L"\n\n请点击右上角「重新翻译」按钮重试。");
                    if (m_statusText) {
                        m_statusText->SetLabel(L"推理失败 (点击重试)");
                    }
                }
            })
        );
    }

    void TranslationBubbleFrame::SetSourcePanelExpanded(bool expanded) {
        m_isSourceExpanded = expanded;
        ThemePalette p = ThemeManager::GetCurrentPalette();

        if (m_isSourceExpanded) {
            if (m_sourceToggleIcon) {
                m_sourceToggleIcon->SetBitmap(IconManager::GetIconBundle(SVG::CHEVRON_DOWN, wxSize(14, 14), p.accentPrimary));
            }
            if (m_sourceToggleLabel) {
                m_sourceToggleLabel->SetLabel(L"收起原文");
                m_sourceToggleLabel->SetToolTip(L"折叠并隐藏原文");
            }
            if (m_sourcePreviewText) {
                m_sourcePreviewText->SetLabel(L"(Ctrl+Enter 重新翻译)");
            }

            int totalH = m_splitter->GetClientSize().y;
            int defaultSash = std::max(40_dip, totalH * 4 / 10);
            int sashPos = (m_savedSashPos >= 35_dip) ? m_savedSashPos : defaultSash;

            if (!m_splitter->IsSplit()) {
                m_sourcePanel->Show(true);
                m_splitter->SplitHorizontally(m_sourcePanel, m_targetPanel, sashPos);
            } else {
                m_splitter->SetSashPosition(sashPos);
            }
        } else {
            if (m_sourceToggleIcon) {
                m_sourceToggleIcon->SetBitmap(IconManager::GetIconBundle(SVG::CHEVRON_RIGHT, wxSize(14, 14), p.textSecondary));
            }
            if (m_sourceToggleLabel) {
                m_sourceToggleLabel->SetLabel(L"显示原文");
                m_sourceToggleLabel->SetToolTip(L"展开并显示原文");
            }
            UpdateSourcePreview();

            if (m_splitter->IsSplit()) {
                m_savedSashPos = m_splitter->GetSashPosition();
                m_splitter->Unsplit(m_sourcePanel);
            }
        }

        if (m_mainPanel) m_mainPanel->Layout();

        // 展开或折叠后重新对齐并置顶悬浮朗读按钮
        if (m_isSourceExpanded && m_sourceSpeakBtn && m_sourcePanel) {
            wxSize sz = m_sourcePanel->GetClientSize();
            wxSize btnSz = m_sourceSpeakBtn->GetSize();
            m_sourceSpeakBtn->Move(sz.x - btnSz.x - 6_dip, 4_dip);
            m_sourceSpeakBtn->Raise();
        }
        if (m_targetSpeakBtn && m_targetPanel) {
            wxSize sz = m_targetPanel->GetClientSize();
            wxSize btnSz = m_targetSpeakBtn->GetSize();
            m_targetSpeakBtn->Move(sz.x - btnSz.x - 6_dip, 4_dip);
            m_targetSpeakBtn->Raise();
        }
    }

    void TranslationBubbleFrame::UpdateSourcePreview() {
        if (!m_sourcePreviewText || m_isSourceExpanded) return;

        // 高效截取前 120 字节进行预览，避免在长文本上执行全文拷贝与分配 (复杂度 O(1))
        wxString raw;
        if (!m_lastSourceText.empty()) {
            size_t len = std::min<size_t>(m_lastSourceText.size(), 120);
            // 规避 UTF-8 截断导致的多字节字符损坏
            while (len > 0 && (static_cast<unsigned char>(m_lastSourceText[len - 1]) & 0xC0) == 0x80) {
                --len;
            }
            raw = wxString::FromUTF8(m_lastSourceText.data(), len);
        } else if (m_sourceCtrl) {
            raw = m_sourceCtrl->GetValue().Left(60);
        }

        // 过滤与清洗：去除控制字符、换行符、Object Replacement Character (U+FFFC 等)，并合并多余空格
        wxString cleaned;
        cleaned.Alloc(raw.Length());
        bool lastWasSpace = false;

        for (wxUniChar ch : raw) {
            // U+FFFC (Object Replacement Character), U+FFFD, 零宽字符
            if (ch == 0xFFFC || ch == 0xFFFD || ch == 0xFEFF ||
                ch == 0x200B || ch == 0x200C || ch == 0x200D) {
                if (!lastWasSpace && !cleaned.IsEmpty()) {
                    cleaned += ' ';
                    lastWasSpace = true;
                }
                continue;
            }

            // 换行、制表符、空白字符
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '\v' || ch == '\f' ||
                ch == ' ' || ch == 0x00A0 || ch == 0x2028 || ch == 0x2029) {
                if (!lastWasSpace && !cleaned.IsEmpty()) {
                    cleaned += ' ';
                    lastWasSpace = true;
                }
                continue;
            }

            // 过滤不可见控制字符 (ASCII < 32)
            if (ch < 32) {
                continue;
            }

            cleaned += ch;
            lastWasSpace = false;
        }

        cleaned.Trim(true).Trim(false);
        if (cleaned.Length() > 36) {
            cleaned = cleaned.Left(33) + "...";
        }

        if (!cleaned.IsEmpty()) {
            m_sourcePreviewText->SetLabel(L"- " + cleaned);
        } else {
            m_sourcePreviewText->SetLabel("");
        }
    }

    void TranslationBubbleFrame::Dismiss() {
        WinTtsHelper::GetInstance().Stop();
        Hide();
    }

    void TranslationBubbleFrame::UpdateTheme() {
        ThemePalette palette = ThemeManager::GetCurrentPalette();
        SetBackgroundColour(palette.cardBorder);

        if (m_mainPanel) m_mainPanel->SetBackgroundColour(palette.cardBg);
        if (m_headerPanel) m_headerPanel->SetBackgroundColour(palette.sidebarBg);
        if (m_titleText) m_titleText->SetForegroundColour(palette.textPrimary);
        UpdateLanguageBadge();

        if (m_splitter) m_splitter->SetBackgroundColour(palette.cardBg);
        if (m_sourcePanel) m_sourcePanel->SetBackgroundColour(palette.windowBg);
        if (m_targetPanel) m_targetPanel->SetBackgroundColour(palette.cardBg);
        if (m_sourceCtrl) {
            m_sourceCtrl->SetBackgroundColour(palette.windowBg);
            m_sourceCtrl->SetForegroundColour(palette.textPrimary);
        }
        if (m_targetCtrl) {
            m_targetCtrl->SetBackgroundColour(palette.cardBg);
            m_targetCtrl->SetForegroundColour(palette.textPrimary);
        }

        wxBitmapBundle speakBundle = IconManager::GetIconBundle(SVG::SPEAKER, wxSize(14, 14), palette.textSecondary);
        if (m_sourceSpeakBtn) {
            m_sourceSpeakBtn->SetBackgroundColour(palette.windowBg);
            m_sourceSpeakBtn->SetBitmap(speakBundle);
            m_sourceSpeakBtn->Raise();
        }
        if (m_targetSpeakBtn) {
            m_targetSpeakBtn->SetBackgroundColour(palette.cardBg);
            m_targetSpeakBtn->SetBitmap(speakBundle);
            m_targetSpeakBtn->Raise();
        }

        if (m_footerPanel) m_footerPanel->SetBackgroundColour(palette.sidebarBg);
        if (m_statusText) m_statusText->SetForegroundColour(palette.textSecondary);
        if (m_sourceToggleBar) m_sourceToggleBar->SetBackgroundColour(palette.sidebarBg);

        if (m_sourceToggleIcon) {
            m_sourceToggleIcon->SetBitmap(IconManager::GetIconBundle(
                m_isSourceExpanded ? SVG::CHEVRON_DOWN : SVG::CHEVRON_RIGHT,
                wxSize(14, 14),
                m_isSourceExpanded ? palette.accentPrimary : palette.textSecondary
            ));
        }
        if (m_sourceToggleLabel) {
            m_sourceToggleLabel->SetForegroundColour(palette.textSecondary);
        }
        if (m_sourcePreviewText) {
            m_sourcePreviewText->SetForegroundColour(palette.textSecondary);
        }

        // 辅助更新操作按钮颜色与图标
        auto updateBtn = [&palette](wxBitmapButton* btn, const char* svg, const wxColour& tint) {
            if (!btn) return;
            btn->SetBackgroundColour(palette.sidebarBg);
            btn->SetBitmap(IconManager::GetIconBundle(svg, wxSize(15, 15), tint));
            };

        updateBtn(m_fontDecreaseBtn, SVG::ZOOM_OUT, palette.textSecondary);
        updateBtn(m_fontIncreaseBtn, SVG::ZOOM_IN, palette.textSecondary);
        updateBtn(m_pinBtn, SVG::PIN, m_isPinned ? palette.accentPrimary : palette.textSecondary);
        updateBtn(m_retryBtn, SVG::REPLACE, palette.textSecondary);
        updateBtn(m_copyBtn, SVG::COPY, palette.textSecondary);
        updateBtn(m_closeBtn, SVG::CLOSE, palette.textSecondary);

        if (m_resizeGrip) m_resizeGrip->SetBackgroundColour(palette.sidebarBg);

        Refresh();
    }

    TranslationBubbleFrame::ResizeDirection TranslationBubbleFrame::HitTest(const wxPoint& ptInFrame, const wxSize& frameSize) const {
        const int margin = 6_dip;
        bool onLeft = (ptInFrame.x >= 0 && ptInFrame.x <= margin);
        bool onRight = (ptInFrame.x >= frameSize.x - margin && ptInFrame.x <= frameSize.x);
        bool onTop = (ptInFrame.y >= 0 && ptInFrame.y <= margin);
        bool onBottom = (ptInFrame.y >= frameSize.y - margin && ptInFrame.y <= frameSize.y);

        if (onTop && onLeft)     return ResizeDirection::TopLeft;
        if (onTop && onRight)    return ResizeDirection::TopRight;
        if (onBottom && onLeft)  return ResizeDirection::BottomLeft;
        if (onBottom && onRight) return ResizeDirection::BottomRight;
        if (onLeft)              return ResizeDirection::Left;
        if (onRight)             return ResizeDirection::Right;
        if (onTop)               return ResizeDirection::Top;
        if (onBottom)            return ResizeDirection::Bottom;

        return ResizeDirection::None;
    }

    void TranslationBubbleFrame::UpdateCursorForDir(ResizeDirection dir, wxWindow* targetWin) {
        if (!targetWin) return;
        // 静态缓存系统游标实例，避免每次鼠标微移时反复构造与释放 GDI Cursor 句柄
        static const wxCursor cursorWE(wxCURSOR_SIZEWE);
        static const wxCursor cursorNS(wxCURSOR_SIZENS);
        static const wxCursor cursorNWSE(wxCURSOR_SIZENWSE);
        static const wxCursor cursorNESW(wxCURSOR_SIZENESW);

        switch (dir) {
        case ResizeDirection::Left:
        case ResizeDirection::Right:
            targetWin->SetCursor(cursorWE);
            break;
        case ResizeDirection::Top:
        case ResizeDirection::Bottom:
            targetWin->SetCursor(cursorNS);
            break;
        case ResizeDirection::TopLeft:
        case ResizeDirection::BottomRight:
            targetWin->SetCursor(cursorNWSE);
            break;
        case ResizeDirection::TopRight:
        case ResizeDirection::BottomLeft:
            targetWin->SetCursor(cursorNESW);
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

        const int deltaX = screenPos.x - m_resizeStartMousePos.x;
        const int deltaY = screenPos.y - m_resizeStartMousePos.y;

        int newX = m_resizeStartFramePos.x;
        int newY = m_resizeStartFramePos.y;
        int newW = m_resizeStartFrameSize.x;
        int newH = m_resizeStartFrameSize.y;

        const int minW = 320_dip;
        const int minH = 200_dip;

        // 正交水平维度计算 (Left / Right)
        const bool isLeft = (m_resizeDir == ResizeDirection::Left ||
            m_resizeDir == ResizeDirection::TopLeft ||
            m_resizeDir == ResizeDirection::BottomLeft);
        const bool isRight = (m_resizeDir == ResizeDirection::Right ||
            m_resizeDir == ResizeDirection::TopRight ||
            m_resizeDir == ResizeDirection::BottomRight);

        if (isLeft) {
            int calculatedW = m_resizeStartFrameSize.x - deltaX;
            if (calculatedW < minW) {
                newX = m_resizeStartFramePos.x + (m_resizeStartFrameSize.x - minW);
                newW = minW;
            } else {
                newX = m_resizeStartFramePos.x + deltaX;
                newW = calculatedW;
            }
        } else if (isRight) {
            newW = std::max(minW, m_resizeStartFrameSize.x + deltaX);
        }

        // 正交垂直维度计算 (Top / Bottom)
        const bool isTop = (m_resizeDir == ResizeDirection::Top ||
            m_resizeDir == ResizeDirection::TopLeft ||
            m_resizeDir == ResizeDirection::TopRight);
        const bool isBottom = (m_resizeDir == ResizeDirection::Bottom ||
            m_resizeDir == ResizeDirection::BottomLeft ||
            m_resizeDir == ResizeDirection::BottomRight);

        if (isTop) {
            int calculatedH = m_resizeStartFrameSize.y - deltaY;
            if (calculatedH < minH) {
                newY = m_resizeStartFramePos.y + (m_resizeStartFrameSize.y - minH);
                newH = minH;
            } else {
                newY = m_resizeStartFramePos.y + deltaY;
                newH = calculatedH;
            }
        } else if (isBottom) {
            newH = std::max(minH, m_resizeStartFrameSize.y + deltaY);
        }

        // 仅在坐标或尺寸真正发生变化时才调用 SetSize，消除不必要的 Layout 与 WM_PAINT 刷新
        wxPoint curPos = GetPosition();
        wxSize curSize = GetSize();
        if (curPos.x != newX || curPos.y != newY || curSize.x != newW || curSize.y != newH) {
            SetSize(newX, newY, newW, newH);
            m_bubbleSize = wxSize(newW, newH);
            if (m_isPinned) {
                m_pinnedPos = wxPoint(newX, newY);
                m_hasPinnedPos = true;
            }
        }
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

    void TranslationBubbleFrame::HandleEdgeLeftDown(wxMouseEvent&, wxWindow* sourceWin, bool isHeader) {
        wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
        ResizeDirection dir = HitTest(ptInFrame, GetSize());
        if (dir != ResizeDirection::None && (!isHeader || dir != ResizeDirection::Bottom)) {
            StartResize(dir, wxGetMousePosition(), sourceWin);
        } else if (isHeader) {
            m_isDragging = true;
            // 计算屏幕绝对坐标相对窗口左上角的偏移量，彻底修复因点击子控件 (如 m_titleText) 导致的跳跃 Bug
            m_dragOffset = wxGetMousePosition() - GetPosition();
            if (sourceWin && !sourceWin->HasCapture()) {
                sourceWin->CaptureMouse();
            }
        }
    }

    void TranslationBubbleFrame::HandleEdgeMouseMove(wxMouseEvent& event, wxWindow* sourceWin, bool isHeader) {
        if (m_isResizing) {
            ProcessResizeDrag(wxGetMousePosition());
            return;
        }
        if (m_isDragging && event.Dragging() && event.LeftIsDown()) {
            wxPoint newPos = wxGetMousePosition() - m_dragOffset;
            SetPosition(newPos);
            if (m_isPinned) {
                m_pinnedPos = newPos;
                m_hasPinnedPos = true;
            }
            return;
        }
        wxPoint ptInFrame = ScreenToClient(wxGetMousePosition());
        ResizeDirection dir = HitTest(ptInFrame, GetSize());
        if (isHeader && dir == ResizeDirection::Bottom) {
            dir = ResizeDirection::None;
        }
        UpdateCursorForDir(dir, sourceWin);
    }

    void TranslationBubbleFrame::HandleEdgeLeftUp(wxMouseEvent&) {
        if (m_isResizing) {
            EndResize();
        }
        if (m_isDragging) {
            m_isDragging = false;
            if (m_headerPanel && m_headerPanel->HasCapture()) {
                m_headerPanel->ReleaseMouse();
            }
            if (m_titleText && m_titleText->HasCapture()) {
                m_titleText->ReleaseMouse();
            }
        }
    }

    void TranslationBubbleFrame::HandleEdgeMouseLeave(wxMouseEvent&, wxWindow* sourceWin) {
        if (!m_isResizing && !m_isDragging && sourceWin) {
            sourceWin->SetCursor(wxNullCursor);
        }
    }

    void TranslationBubbleFrame::OnGripPaint(wxPaintEvent&) {
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

    void TranslationBubbleFrame::OnCopyResult(wxCommandEvent&) {
        if (m_targetCtrl) {
            wxString sel = m_targetCtrl->GetStringSelection();
            if (!sel.IsEmpty()) {
                ClipboardHelper::SetClipboardText(sel.ToUTF8().data());
                if (m_statusText) {
                    m_statusText->SetLabel(L"已复制选中文本！");
                }
                return;
            }
        }
        if (!m_currentFullText.empty()) {
            ClipboardHelper::SetClipboardText(m_currentFullText);
            if (m_statusText) {
                m_statusText->SetLabel(L"已复制译文到剪贴板！");
            }
        } else if (m_targetCtrl && !m_targetCtrl->GetValue().IsEmpty()) {
            ClipboardHelper::SetClipboardText(m_targetCtrl->GetValue().ToUTF8().data());
            if (m_statusText) {
                m_statusText->SetLabel(L"已复制译文到剪贴板！");
            }
        }
    }

    void TranslationBubbleFrame::OnTogglePin(wxCommandEvent&) {
        m_isPinned = !m_isPinned;
        if (m_isPinned) {
            m_pinnedPos = GetPosition();
            m_hasPinnedPos = true;
        }
        ThemePalette palette = ThemeManager::GetCurrentPalette();
        wxColour iconColor = m_isPinned ? palette.accentPrimary : palette.textSecondary;
        m_pinBtn->SetBitmap(IconManager::GetIconBundle(SVG::PIN, wxSize(15, 15), iconColor));
        m_pinBtn->SetToolTip(m_isPinned ? L"已固定窗口位置 (再次点击取消固定)" : L"固定窗口位置");
    }

    void TranslationBubbleFrame::OnRetry(wxCommandEvent&) {
        WinTtsHelper::GetInstance().Stop();
        UpdateLanguageBadge();

        std::string textToTranslate;
        if (m_sourceCtrl) {
            textToTranslate = m_sourceCtrl->GetValue().ToUTF8().data();
        }
        if (textToTranslate.empty()) {
            textToTranslate = m_lastSourceText;
        }
        if (textToTranslate.empty()) {
            return;
        }
        m_lastSourceText = textToTranslate;
        m_currentFullText.clear();

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

        m_modelManager->EnsureModelAsync(
            TargetModelType::Translation,
            BindUi([this](const std::string& statusMsg) {
                if (m_statusText) {
                    m_statusText->SetLabel(wxString::FromUTF8(statusMsg));
                }
                }),
            BindUi([this, textToTranslate](bool ok, const ServerStatusInfo& info) {
                if (!ok) {
                    if (m_targetCtrl) {
                        m_targetCtrl->SetValue(L"模型启动失败: " + wxString::FromUTF8(info.message) + L"\n请检查模型路径或点击重试。");
                    }
                    if (m_statusText) {
                        m_statusText->SetLabel(L"模型未就绪 (点击重试)");
                    }
                } else {
                    DoExecuteTranslation(textToTranslate);
                }
            })
        );
    }

    void TranslationBubbleFrame::UpdateLanguageBadge() {
        LanguageCode srcCode = LanguageCode::AutoDetect;
        LanguageCode tgtCode = LanguageCode::Chinese;

        if (m_modelManager && m_modelManager->GetConfigManager()) {
            auto cfg = m_modelManager->GetConfigManager()->GetConfig();
            srcCode = LanguageHelper::FromCodeName(cfg.sourceLang);
            tgtCode = LanguageHelper::FromCodeName(cfg.targetLang);
        }

        std::string srcName = LanguageHelper::GetDisplayName(srcCode);
        std::string tgtName = LanguageHelper::GetDisplayName(tgtCode);
        wxString badgeText = wxString::Format(L"%s → %s", wxString::FromUTF8(srcName), wxString::FromUTF8(tgtName));

        ThemePalette palette = ThemeManager::GetCurrentPalette();
        if (m_langBadge) {
            m_langBadge->SetStatus(badgeText, palette.accentPrimary, palette.bannerBg, palette.bannerBorder);
        }
    }

    void TranslationBubbleFrame::OnCloseBtn(wxCommandEvent&) {
        Dismiss();
    }

    void TranslationBubbleFrame::OnIncreaseFontSize(wxCommandEvent&) {
        if (m_currentFontSize < 22) {
            m_currentFontSize++;
            ApplyFontSize(m_currentFontSize, true);
        }
    }

    void TranslationBubbleFrame::OnDecreaseFontSize(wxCommandEvent&) {
        if (m_currentFontSize > 8) {
            m_currentFontSize--;
            ApplyFontSize(m_currentFontSize, true);
        }
    }

    void TranslationBubbleFrame::ApplyFontSize(int fontSize, bool saveToConfig) {
        int clamped = std::clamp(fontSize, 8, 22);
        if (clamped == m_currentFontSize && !saveToConfig) return;
        m_currentFontSize = clamped;

        if (m_sourceCtrl) {
            m_sourceCtrl->SetFont(wxFont(std::max(8, m_currentFontSize - 1), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        }
        if (m_targetCtrl) {
            m_targetCtrl->SetFont(wxFont(m_currentFontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        }
        if (saveToConfig && m_modelManager && m_modelManager->GetConfigManager()) {
            m_modelManager->GetConfigManager()->SaveBubbleFontSize(m_currentFontSize);
        }
    }

} // namespace LinguaAlpaca::UI
