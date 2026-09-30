#include "OcrView.hpp"
#include "core/ClipboardHelper.hpp"
#include "core/WinTtsHelper.hpp"
#include "core/pdf/PdfHelper.hpp"
#include "core/document/DocumentPipeline.hpp"
#include "engine/DocLayoutEngine.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include "MainFrame.hpp"
#include <base64.hpp>
#include <atomic>
#include <fstream>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/datetime.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/mstream.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace LinguaAlpaca::UI {

OcrView::OcrView(wxWindow* parent, std::shared_ptr<ModelManager> modelManager, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_modelManager(std::move(modelManager)) {
    InitUI();

    m_healthTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateStatusBadge(); });
}

OcrView::~OcrView() {
    if (m_healthTimer.IsRunning()) {
        m_healthTimer.Stop();
    }
    WinTtsHelper::GetInstance().Stop();
}

bool OcrView::Show(bool show) {
    bool res = wxPanel::Show(show);
    if (show) {
        UpdateStatusBadge();
        if (!m_healthTimer.IsRunning()) {
            m_healthTimer.Start(1500);
        }
    } else {
        if (m_healthTimer.IsRunning()) {
            m_healthTimer.Stop();
        }
    }
    return res;
}

void OcrView::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. Header Bar: Icon + Title ("文档与图片 OCR 解析") + Status Badge
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle titleBundle = IconManager::GetIconBundle(SVG::OCR, wxSize(24, 24), palette.accentPrimary);
    wxStaticBitmap* titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

    m_titleText = new wxStaticText(this, wxID_ANY, L"文档与图片 OCR 解析");
    m_titleText->SetFont(ThemeFont::GetFont(FontRole::DisplayTitle));
    m_titleText->SetForegroundColour(palette.textPrimary);

    m_statusBadge = new StatusBadge(this);

    headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->AddStretchSpacer(1);
    headerSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20_dip);

    // 2. Middle Content Area (Left: Dropzone Card ~45%, Right: Recognized Text Card ~55%)
    wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

    // Dropzone Card
    m_dropzonePanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
    m_dropzonePanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));
    m_dropzonePanel->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* dropSizer = new wxBoxSizer(wxVERTICAL);

    wxBitmapBundle uploadBundle = IconManager::GetIconBundle(SVG::CLOUD_UPLOAD, wxSize(44, 44), palette.accentPrimary);
    m_uploadIconBmp = new wxStaticBitmap(m_dropzonePanel, wxID_ANY, uploadBundle);

    m_dropTextPrimary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"点击上传 或 拖拽/粘贴文档与图片");
    m_dropTextPrimary->SetFont(ThemeFont::GetFont(FontRole::Body, true));
    m_dropTextPrimary->SetForegroundColour(palette.textPrimary);

#ifdef __APPLE__
    m_dropTextSecondary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"支持多页 PDF 及常见图片格式 (⌘V 粘贴)");
#else
    m_dropTextSecondary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"支持多页 PDF 及常见图片格式 (Ctrl+V 粘贴)");
#endif
    m_dropTextSecondary->SetFont(ThemeFont::GetFont(FontRole::Caption));
    m_dropTextSecondary->SetForegroundColour(palette.textSecondary);

    dropSizer->AddStretchSpacer(1);
    dropSizer->Add(m_uploadIconBmp, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8_dip);
    dropSizer->Add(m_dropTextPrimary, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 6_dip);
    dropSizer->Add(m_dropTextSecondary, 0, wxALIGN_CENTER_HORIZONTAL);
    dropSizer->AddStretchSpacer(1);

    m_dropzonePanel->SetSizer(dropSizer);
    m_dropzonePanel->SetDropTarget(new OcrFileDropTarget(this));

    m_dropzonePanel->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        m_dropzonePanel->Refresh();
        event.Skip();
    });
    m_dropzonePanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_dropzonePanel);
        wxSize size = m_dropzonePanel->GetClientSize();
        if (size.x <= 0 || size.y <= 0)
            return;

        auto p = ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(p.windowBg));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc)
            return;

        wxGraphicsPath path = gc->CreatePath();
        path.AddRoundedRectangle(4_dip, 4_dip, size.x - 8_dip, size.y - 8_dip, 8_dip);

        gc->SetBrush(gc->CreateBrush(wxBrush(p.cardBg)));
        gc->FillPath(path);

        if (m_loadedImage.IsOk() && !m_loadedImagePath.IsEmpty()) {
            int pad = 6_dip;
            int availW = size.x - pad * 2;
            int availH = size.y - pad * 2;
            if (availW > 0 && availH > 0) {
                int imgW = m_loadedImage.GetWidth();
                int imgH = m_loadedImage.GetHeight();
                double scale = std::min(static_cast<double>(availW) / imgW, static_cast<double>(availH) / imgH);
                int drawW = static_cast<int>(imgW * scale);
                int drawH = static_cast<int>(imgH * scale);
                int drawX = pad + (availW - drawW) / 2;
                int drawY = pad + (availH - drawH) / 2;

                wxImage scaledImg = m_loadedImage;
                scaledImg.Rescale(drawW, drawH, wxIMAGE_QUALITY_HIGH);
                wxBitmap bmp(scaledImg);

                gc->Clip(4_dip, 4_dip, size.x - 8_dip, size.y - 8_dip);
                gc->DrawBitmap(bmp, drawX, drawY, drawW, drawH);

                // 悬浮在图片上时，绘制半透明暗色遮罩与交互按钮 (识别进行中仍支持预览与翻页查看)
                if (m_isDropzoneHovered || m_isDraggingPdfSlider) {
                    // 1. 半透明暗色遮罩 (识别中时使用轻量遮罩，不阻挡校对视图)
                    int maskAlpha = (m_currentState == OcrTaskState::Recognizing) ? 40 : 85;
                    gc->SetBrush(gc->CreateBrush(wxBrush(wxColour(0, 0, 0, maskAlpha))));
                    gc->FillPath(path);

                    wxColour topText = wxColour(30, 41, 59);

                    // 2. 左上角「页数」胶囊 (仅在鼠标进入左侧区域时显示，颜色与右上角预览按钮完全统一)
                    if (m_isPdfDoc && m_pdfTotalPages > 1) {
                        wxString pageStr = wxString::Format(L"%d / %d 页", m_pdfCurrentPage + 1, m_pdfTotalPages);
                        gc->SetFont(ThemeFont::GetFont(FontRole::Control, true), topText);
                        double pTw = 0, pTh = 0;
                        gc->GetTextExtent(pageStr, &pTw, &pTh);

                        int pagePillW = static_cast<int>(pTw) + 20_dip;
                        int pagePillH = 30_dip;
                        int pagePillX = 14_dip;
                        int pagePillY = 12_dip;

                        gc->SetBrush(gc->CreateBrush(wxBrush(wxColour(255, 255, 255, 190))));
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->DrawRoundedRectangle(pagePillX, pagePillY, pagePillW, pagePillH, 6.0_dip);
                        gc->DrawText(pageStr, pagePillX + (pagePillW - pTw) / 2.0, pagePillY + (pagePillH - pTh) / 2.0);
                    }

                    // 3. 右上角「预览」按钮 (无论是否在识别中，均支持点击预览！)
                    int topBtnW = 76_dip;
                    int topBtnH = 30_dip;
                    int topBtnX = size.x - topBtnW - 14_dip;
                    int topBtnY = 12_dip;
                    m_previewBtnRect = wxRect(topBtnX, topBtnY, topBtnW, topBtnH);

                    bool isTopHovered = (m_hoveredAction == DropzoneHoverAction::Preview);
                    wxColour topBg = isTopHovered ? wxColour(255, 255, 255, 240) : wxColour(255, 255, 255, 190);

                    gc->SetBrush(gc->CreateBrush(wxBrush(topBg)));
                    gc->SetPen(*wxTRANSPARENT_PEN);
                    gc->DrawRoundedRectangle(topBtnX, topBtnY, topBtnW, topBtnH, 6.0_dip);

                    wxSize topIconSz = dip(14, 14);
                    wxBitmapBundle eyeBundle = IconManager::GetIconBundle(SVG::EYE, wxSize(14, 14), topText);
                    wxBitmap eyeBmp = eyeBundle.GetBitmap(topIconSz);

                    wxFont topFont = ThemeFont::GetFont(FontRole::Control, true);
                    gc->SetFont(topFont, topText);
                    double eyeTw = 0, eyeTh = 0;
                    gc->GetTextExtent(L"预览", &eyeTw, &eyeTh);

                    double topContentW = topIconSz.x + 4_dip + eyeTw;
                    double topStartX = topBtnX + (topBtnW - topContentW) / 2.0;
                    if (eyeBmp.IsOk()) {
                        gc->DrawBitmap(eyeBmp, topStartX, topBtnY + (topBtnH - topIconSz.y) / 2.0, topIconSz.x, topIconSz.y);
                    }
                    gc->DrawText(L"预览", topStartX + topIconSz.x + 4_dip, topBtnY + (topBtnH - eyeTh) / 2.0);

                    // 4. 居中「替换文档」按钮 (仅在非识别状态下显示)
                    if (m_currentState != OcrTaskState::Recognizing) {
                        int centerBtnW = 130_dip;
                        int centerBtnH = 38_dip;
                        int centerBtnX = (size.x - centerBtnW) / 2;
                        int centerBtnY = (size.y - centerBtnH) / 2;
                        m_centerBtnRect = wxRect(centerBtnX, centerBtnY, centerBtnW, centerBtnH);

                        bool isCenterHovered = (m_hoveredAction == DropzoneHoverAction::Replace);
                        wxColour centerBg = isCenterHovered ? p.accentHover : p.accentPrimary;

                        gc->SetBrush(gc->CreateBrush(wxBrush(centerBg)));
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->DrawRoundedRectangle(centerBtnX, centerBtnY, centerBtnW, centerBtnH, 10.0_dip);

                        wxSize centerIconSz = dip(16, 16);
                        wxBitmapBundle cloudBundle = IconManager::GetIconBundle(SVG::CLOUD_UPLOAD, wxSize(16, 16), *wxWHITE);
                        wxBitmap cloudBmp = cloudBundle.GetBitmap(centerIconSz);

                        wxFont centerFont = ThemeFont::GetFont(FontRole::Control, true);
                        gc->SetFont(centerFont, *wxWHITE);
                        double cTw = 0, cTh = 0;
                        gc->GetTextExtent(L" 替换文档", &cTw, &cTh);

                        double centerContentW = centerIconSz.x + 4_dip + cTw;
                        double centerStartX = centerBtnX + (centerBtnW - centerContentW) / 2.0;
                        if (cloudBmp.IsOk()) {
                            gc->DrawBitmap(cloudBmp, centerStartX, centerBtnY + (centerBtnH - centerIconSz.y) / 2.0, centerIconSz.x, centerIconSz.y);
                        }
                        gc->DrawText(L" 替换文档", centerStartX + centerIconSz.x + 4_dip, centerBtnY + (centerBtnH - cTh) / 2.0);
                    } else {
                        m_centerBtnRect = wxRect();
                    }

                    // 5. 若是多页 PDF，绘制右侧垂直滑动条 (统一采用与预览按钮一致的半透明毛玻璃 / 白底卡片视觉)
                    if (m_isPdfDoc && m_pdfTotalPages > 1) {
                        int sliderTrackW = (m_isHoveringPdfSlider || m_isDraggingPdfSlider) ? 10_dip : 6_dip;
                        int sliderX = size.x - sliderTrackW - 14_dip;
                        int sliderY = 52_dip; // 避开右上角预览按钮 (12_dip + 30_dip = 42_dip)
                        int sliderH = size.y - sliderY - 20_dip;

                        if (sliderH > 40_dip) {
                            m_pdfSliderTrackRect = wxRect(sliderX - 6_dip, sliderY, sliderTrackW + 12_dip, sliderH);

                            // 滑道背景：半透明浅色滑轨
                            wxColour trackBg = (m_isHoveringPdfSlider || m_isDraggingPdfSlider) ? wxColour(255, 255, 255, 90) : wxColour(255, 255, 255, 50);
                            gc->SetBrush(gc->CreateBrush(wxBrush(trackBg)));
                            gc->SetPen(*wxTRANSPARENT_PEN);
                            gc->DrawRoundedRectangle(sliderX, sliderY, sliderTrackW, sliderH, sliderTrackW / 2.0);

                            // 滑块高度与位置
                            int minThumbH = 26_dip;
                            int thumbH = std::clamp(static_cast<int>(sliderH / m_pdfTotalPages), minThumbH, sliderH / 3);
                            int availableH = sliderH - thumbH;
                            int thumbY = sliderY + static_cast<int>(std::round(static_cast<double>(availableH) * m_pdfCurrentPage / (m_pdfTotalPages - 1)));
                            m_pdfSliderThumbRect = wxRect(sliderX - 6_dip, thumbY, sliderTrackW + 12_dip, thumbH);

                            // 滑块颜色：与预览按钮保持完全统一 (普通 190 / 悬停与拖拽 240)
                            wxColour thumbColor = (m_isHoveringPdfSlider || m_isDraggingPdfSlider) ? wxColour(255, 255, 255, 240) : wxColour(255, 255, 255, 190);
                            gc->SetBrush(gc->CreateBrush(wxBrush(thumbColor)));
                            gc->DrawRoundedRectangle(sliderX, thumbY, sliderTrackW, thumbH, sliderTrackW / 2.0);

                            // 悬停或拖拽滑块时，在滑块左侧绘制实时浮动页码指示气泡 (样式与预览按钮完全统一)
                            if (m_isHoveringPdfSlider || m_isDraggingPdfSlider) {
                                int displayPage = m_isDraggingPdfSlider ? (m_pdfCurrentPage + 1) : (m_sliderHoverPage + 1);
                                wxString tipStr = wxString::Format(L"第 %d 页", displayPage);
                                gc->SetFont(ThemeFont::GetFont(FontRole::Control, true), topText);
                                double tTw = 0, tTh = 0;
                                gc->GetTextExtent(tipStr, &tTw, &tTh);
                                int tipW = static_cast<int>(tTw) + 18_dip;
                                int tipH = 30_dip; // 与预览按钮高度完全一致
                                int tipX = sliderX - tipW - 8_dip;
                                int tipTargetY = m_isDraggingPdfSlider ? (thumbY + (thumbH - tipH) / 2) : (m_lastMouseY - tipH / 2);
                                int tipY = std::clamp(tipTargetY, sliderY, sliderY + sliderH - tipH);

                                gc->SetBrush(gc->CreateBrush(wxBrush(wxColour(255, 255, 255, 240))));
                                gc->SetPen(*wxTRANSPARENT_PEN);
                                gc->DrawRoundedRectangle(tipX, tipY, tipW, tipH, 6.0_dip);
                                gc->DrawText(tipStr, tipX + (tipW - tTw) / 2.0, tipY + (tipH - tTh) / 2.0);
                            }
                        }
                    } else {
                        m_pdfSliderTrackRect = wxRect();
                        m_pdfSliderThumbRect = wxRect();
                    }
                } else {
                    m_previewBtnRect = wxRect();
                    m_centerBtnRect = wxRect();
                    m_pdfSliderTrackRect = wxRect();
                    m_pdfSliderThumbRect = wxRect();
                }

                gc->ResetClip();

                wxPen pen(p.cardBorder, 1);
                gc->SetPen(pen);
                gc->StrokePath(path);
            }
        } else {
            m_previewBtnRect = wxRect();
            m_centerBtnRect = wxRect();
            m_pdfSliderTrackRect = wxRect();
            m_pdfSliderThumbRect = wxRect();
            m_isDraggingPdfSlider = false;
            m_isHoveringPdfSlider = false;
            wxPen pen(p.cardBorder, 2, wxPENSTYLE_SHORT_DASH);
            gc->SetPen(pen);
            gc->StrokePath(path);
        }
    });

    contentSizer->Add(m_dropzonePanel, 45, wxEXPAND | wxRIGHT, 12_dip);

    // Right Column: Recognized Text Card (Ratio 55%)
    m_resultCard = new CardPanel(this, L"识别文本与 Markdown", true, true);
    if (m_resultCard->GetTextCtrl()) {
        m_resultCard->GetTextCtrl()->SetHint(L"上传 PDF 或图片后，识别结果与结构化 Markdown 将在此处呈现...");
    }

    if (m_resultCard->GetMarkdownView()) {
        m_resultCard->GetMarkdownView()->SetOnImageClickCallback([this](const wxString& imgSrc) {
            wxString localPath = imgSrc;
            if (localPath.StartsWith("file:///")) {
                localPath = localPath.Mid(8);
            }
#ifdef _WIN32
            localPath.Replace("/", "\\");
#endif
            if (wxFileExists(localPath)) {
                wxImage img;
                if (img.LoadFile(localPath)) {
                    ImagePreviewDialog dialog(this, img, L"插图预览 - " + wxFileName(localPath).GetFullName());
                    dialog.ShowModal();
                }
            }
        });
    }

    m_resultCard->AddToolIcon(1, SVG::SPEAKER, L"朗读内容", [this]() {
        if (!m_resultCard)
            return;
        if (!m_lastMarkdownResult.IsEmpty()) {
            WinTtsHelper::GetInstance().Speak(m_lastMarkdownResult.ToStdWstring(), LanguageCode::AutoDetect);
            return;
        }
        if (m_resultCard->GetTextCtrl()) {
            wxString text = m_resultCard->GetTextCtrl()->GetValue();
            if (!text.IsEmpty()) {
                WinTtsHelper::GetInstance().Speak(text.ToStdWstring(), LanguageCode::AutoDetect);
            }
        }
    });

    m_resultCard->AddToolIcon(2, SVG::COPY, L"复制 Markdown/文本", [this]() {
        if (!m_resultCard)
            return;
        wxString textToCopy = m_lastMarkdownResult;
        if (textToCopy.IsEmpty() && m_resultCard->GetTextCtrl()) {
            textToCopy = m_resultCard->GetTextCtrl()->GetValue();
        }
        if (!textToCopy.IsEmpty() && ClipboardHelper::SetClipboardText(textToCopy.ToUTF8().data())) {
            wxMessageBox(L"Markdown 结果已复制到剪贴板！", L"提示", wxOK | wxICON_INFORMATION, this);
        }
    });

    m_resultCard->AddToolIcon(3, SVG::SAVE, L"导出 Markdown (.md)", [this]() { ExportMarkdown(); });

    m_resultCard->AddToolIcon(4, SVG::CODE, L"导出 JSON (.json)", [this]() { ExportJson(); });

    m_resultCard->AddToolIcon(5, SVG::FOLDER_OPEN, L"打开输出文件夹", [this]() { OpenOutputDir(); });

    m_resultCard->AddToolIcon(6, SVG::CLEAR, L"清空内容", [this]() {
        if (!m_resultCard)
            return;
        WinTtsHelper::GetInstance().Stop();
        m_resultCard->Clear();
        m_lastMarkdownResult.Clear();
        m_lastJsonResult.Clear();
        if (m_openFolderBtn) {
            m_openFolderBtn->Hide();
            if (GetSizer())
                GetSizer()->Layout();
        }
        UpdateTranslateButtonVisibility();
    });

    contentSizer->Add(m_resultCard, 55, wxEXPAND);

    mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

    // 独立的现代化文档解析进度与状态组件
    m_progressPanel = new OcrProgressPanel(this);
    mainSizer->Add(m_progressPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

    // 3. Bottom Action Bar: Recognize / Stop / OpenFolder / Translate Buttons
    wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

    m_recognizeBtn = new CustomButton(this, wxID_ANY, L"开始文档解析", ButtonStyle::Primary, wxDefaultPosition, dip(155, 42));
    m_recognizeBtn->SetIcon(SVG::LAYOUT, dip(16, 16), *wxWHITE);

    m_stopBtn = new CustomButton(this, wxID_ANY, L"停止", ButtonStyle::Danger, wxDefaultPosition, dip(145, 42));
    m_stopBtn->SetIcon(SVG::STOP, dip(16, 16), *wxWHITE);
    m_stopBtn->Hide();

    m_openFolderBtn = new CustomButton(this, wxID_ANY, L"打开结果目录", ButtonStyle::Secondary, wxDefaultPosition, dip(155, 42));
    m_openFolderBtn->SetIcon(SVG::FOLDER_OPEN, dip(16, 16), palette.textPrimary);
    m_openFolderBtn->Hide();

    m_translateBtn = new CustomButton(this, wxID_ANY, L"一键翻译", ButtonStyle::Primary, wxDefaultPosition, dip(145, 42));
    m_translateBtn->SetIcon(SVG::TRANSLATE, dip(16, 16), *wxWHITE);
    m_translateBtn->Hide();

    bottomSizer->Add(m_recognizeBtn, 0);
    bottomSizer->Add(m_stopBtn, 0);
    bottomSizer->Add(m_openFolderBtn, 0, wxLEFT, 10_dip);
    bottomSizer->AddStretchSpacer(1);
    bottomSizer->Add(m_translateBtn, 0);

    mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

    SetSizer(mainSizer);

    UpdateStatusBadge();
    Layout();

    // Event Bindings
    m_recognizeBtn->Bind(wxEVT_BUTTON, &OcrView::OnRecognizeClicked, this);
    m_stopBtn->Bind(wxEVT_BUTTON, &OcrView::OnStopClicked, this);
    m_translateBtn->Bind(wxEVT_BUTTON, &OcrView::OnTranslateClicked, this);

    m_dropzonePanel->Bind(wxEVT_LEFT_DOWN, &OcrView::OnDropzoneLeftDown, this);
    m_dropzonePanel->Bind(wxEVT_LEFT_UP, &OcrView::OnDropzoneLeftUp, this);
    m_dropzonePanel->Bind(wxEVT_MOUSEWHEEL, &OcrView::OnDropzoneMouseWheel, this);
    m_dropzonePanel->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        m_isDraggingPdfSlider = false;
        if (m_dropzonePanel)
            m_dropzonePanel->Refresh();
    });
    m_dropzonePanel->Bind(wxEVT_ENTER_WINDOW, &OcrView::OnDropzoneMouseEnter, this);
    m_dropzonePanel->Bind(wxEVT_LEAVE_WINDOW, &OcrView::OnDropzoneMouseLeave, this);
    m_dropzonePanel->Bind(wxEVT_MOTION, &OcrView::OnDropzoneMouseMove, this);

    auto showMenuHandler = [this](wxMouseEvent& event) { ShowDropzoneContextMenu(event.GetPosition()); };
    m_dropzonePanel->Bind(wxEVT_RIGHT_DOWN, showMenuHandler);

    m_openFolderBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenOutputDir(); });
    m_uploadIconBmp->Bind(wxEVT_RIGHT_DOWN, showMenuHandler);
    m_dropTextPrimary->Bind(wxEVT_RIGHT_DOWN, showMenuHandler);
    m_dropTextSecondary->Bind(wxEVT_RIGHT_DOWN, showMenuHandler);

    m_uploadIconBmp->Bind(wxEVT_LEFT_DOWN, &OcrView::OnSelectImageClicked, this);
    m_dropTextPrimary->Bind(wxEVT_LEFT_DOWN, &OcrView::OnSelectImageClicked, this);
    m_dropTextSecondary->Bind(wxEVT_LEFT_DOWN, &OcrView::OnSelectImageClicked, this);

    // 全局快捷键: Ctrl+V 粘贴剪贴板图片
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if ((event.GetKeyCode() == 'V' || event.GetKeyCode() == 'v') && (event.ControlDown() || event.CmdDown())) {
            wxWindow* focused = wxWindow::FindFocus();
            if (focused && focused->IsKindOf(wxCLASSINFO(wxTextCtrl))) {
                event.Skip();
                return;
            }
            if (PasteImageFromClipboard()) {
                return;
            }
        }
        event.Skip();
    });

    m_recognizeBtn->Bind(wxEVT_BUTTON, &OcrView::OnRecognizeClicked, this);
    m_stopBtn->Bind(wxEVT_BUTTON, &OcrView::OnStopClicked, this);
    m_translateBtn->Bind(wxEVT_BUTTON, &OcrView::OnTranslateClicked, this);

    if (m_resultCard && m_resultCard->GetTextCtrl()) {
        m_resultCard->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            if (m_resultCard && m_resultCard->GetTextCtrl()) {
                wxString text = m_resultCard->GetTextCtrl()->GetValue();
                m_resultCard->SetCharacterCount(text.Length());
                UpdateTranslateButtonVisibility();
            }
        });
    }
}

void OcrView::UpdateStatusBadge() {
    if (!m_statusBadge)
        return;

    ServerStatusInfo info;
    if (m_modelManager) {
        info = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
    }

    wxString label;
    switch (info.state) {
    case ServerHealthState::Ready:
        label = L"●  OCR模型已就绪";
        break;
    case ServerHealthState::Loading:
        label = L"●  正在加载OCR模型...";
        break;
    case ServerHealthState::Unconfigured:
        label = L"●  OCR模型未配置";
        break;
    case ServerHealthState::Offline:
        label = L"●  服务离线";
        break;
    case ServerHealthState::Error:
    default:
        label = L"●  服务异常";
        break;
    }

    m_statusBadge->SetStatus(info.state, label);
}

void OcrView::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    if (m_titleText)
        m_titleText->SetForegroundColour(palette.textPrimary);

    if (m_progressPanel)
        m_progressPanel->UpdateTheme();
    if (m_dropTextPrimary)
        m_dropTextPrimary->SetForegroundColour(palette.textPrimary);
    if (m_dropTextSecondary)
        m_dropTextSecondary->SetForegroundColour(palette.textSecondary);
    if (m_resultCard)
        m_resultCard->UpdateTheme();

    if (m_uploadIconBmp) {
        wxBitmapBundle uploadBundle = IconManager::GetIconBundle(SVG::CLOUD_UPLOAD, wxSize(44, 44), palette.accentPrimary);
        m_uploadIconBmp->SetBitmap(uploadBundle);
    }

    if (m_recognizeBtn)
        m_recognizeBtn->Refresh();
    if (m_stopBtn)
        m_stopBtn->Refresh();
    if (m_translateBtn)
        m_translateBtn->Refresh();

    if (m_dropzonePanel) {
        m_dropzonePanel->SetBackgroundColour(palette.cardBg);
        m_dropzonePanel->Refresh();
    }
    UpdateStatusBadge();
    Refresh();
}

void OcrView::OpenImageDialog() {
    wxFileDialog openFileDialog(this, L"选择待解析文档或图片", "", "",
                                "所有支持文件 (*.pdf;*.png;*.jpg;*.jpeg;*.bmp;*.webp)|*.pdf;*.png;*.jpg;*.jpeg;*.bmp;*.webp|PDF 文档 (*.pdf)|*.pdf|图片文件 "
                                "(*.png;*.jpg;*.jpeg;*.bmp;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.webp|所有文件 (*.*)|*.*",
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        LoadImageFile(openFileDialog.GetPath());
    }
}

void OcrView::OpenImagePreview() {
    if (m_loadedImage.IsOk()) {
        ImagePreviewDialog dialog(this, m_loadedImage, L"图片预览 - " + m_imageFileName);
        dialog.ShowModal();
    }
}

void OcrView::OnSelectImageClicked(wxMouseEvent& WXUNUSED(event)) {
    if (!m_loadedImage.IsOk()) {
        OpenImageDialog();
    }
}

void OcrView::OnDropzoneMouseEnter(wxMouseEvent& event) {
    m_isDropzoneHovered = true;
    if (m_dropzonePanel) {
        m_dropzonePanel->Refresh();
    }
    event.Skip();
}

void OcrView::OnDropzoneMouseLeave(wxMouseEvent& event) {
    m_isDropzoneHovered = false;
    m_hoveredAction = DropzoneHoverAction::None;
    m_isHoveringPdfSlider = false;
    if (!m_isDraggingPdfSlider && m_dropzonePanel) {
        m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));
        m_dropzonePanel->Refresh();
    }
    event.Skip();
}

void OcrView::OnDropzoneMouseMove(wxMouseEvent& event) {
    if (!m_loadedImage.IsOk() || m_loadedImagePath.IsEmpty()) {
        event.Skip();
        return;
    }

    wxPoint pt = event.GetPosition();
    m_lastMouseY = pt.y;

    // 1. 若当前正在拖拽 PDF 垂直滑块
    if (m_isDraggingPdfSlider) {
        int deltaY = pt.y - m_sliderDragStartMouseY;
        int sliderH = m_pdfSliderTrackRect.height;
        int minThumbH = 26_dip;
        int thumbH = std::clamp(static_cast<int>(sliderH / m_pdfTotalPages), minThumbH, sliderH / 3);
        int availableH = sliderH - thumbH;
        if (availableH > 0 && m_pdfTotalPages > 1) {
            double pagePerPixel = static_cast<double>(m_pdfTotalPages - 1) / availableH;
            int newPage = std::clamp(m_sliderDragStartPage + static_cast<int>(std::round(deltaY * pagePerPixel)), 0, m_pdfTotalPages - 1);
            if (newPage != m_pdfCurrentPage) {
                SetPdfPage(newPage);
            }
        }
        if (m_dropzonePanel) {
            m_dropzonePanel->Refresh();
        }
        return;
    }

    DropzoneHoverAction newAction = DropzoneHoverAction::None;
    bool newHoverSlider = false;

    // 2. 检查是否悬停在 PDF 垂直滑动条 (滑道或滑块) 上
    if (m_isPdfDoc && m_pdfTotalPages > 1 && m_pdfSliderTrackRect.Contains(pt)) {
        newAction = DropzoneHoverAction::PdfSlider;
        newHoverSlider = true;
        int relativeY = pt.y - m_pdfSliderTrackRect.y;
        double ratio = std::clamp(static_cast<double>(relativeY) / m_pdfSliderTrackRect.height, 0.0, 1.0);
        m_sliderHoverPage = std::clamp(static_cast<int>(std::round(ratio * (m_pdfTotalPages - 1))), 0, m_pdfTotalPages - 1);
    } else {
        if (m_previewBtnRect.Contains(pt)) {
            newAction = DropzoneHoverAction::Preview;
        } else if (m_centerBtnRect.Contains(pt) && m_currentState != OcrTaskState::Recognizing) {
            newAction = DropzoneHoverAction::Replace;
        }
    }

    bool needRefresh = (newAction != m_hoveredAction || newHoverSlider != m_isHoveringPdfSlider);
    if (newHoverSlider) {
        needRefresh = true; // 随鼠标移动更新提示气泡 Y 坐标
    }

    m_hoveredAction = newAction;
    m_isHoveringPdfSlider = newHoverSlider;

    if (m_dropzonePanel) {
        if (newHoverSlider) {
            m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_SIZENS));
        } else {
            m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));
        }
        if (needRefresh) {
            m_dropzonePanel->Refresh();
        }
    }
    event.Skip();
}

void OcrView::OnDropzoneLeftDown(wxMouseEvent& event) {
    if (m_loadedImage.IsOk()) {
        wxPoint pt = event.GetPosition();

        // 优先处理 PDF 垂直滑块点击与拖拽
        if (m_isPdfDoc && m_pdfTotalPages > 1) {
            if (m_pdfSliderThumbRect.Contains(pt)) {
                m_isDraggingPdfSlider = true;
                m_sliderDragStartMouseY = pt.y;
                m_sliderDragStartPage = m_pdfCurrentPage;
                if (m_dropzonePanel && !m_dropzonePanel->HasCapture()) {
                    m_dropzonePanel->CaptureMouse();
                }
                if (m_dropzonePanel) {
                    m_dropzonePanel->Refresh();
                }
                return;
            }
            if (m_pdfSliderTrackRect.Contains(pt)) {
                // 点击滑道：直接按相对位置跳页并立即开启拖拽
                int relativeY = pt.y - m_pdfSliderTrackRect.y;
                double ratio = std::clamp(static_cast<double>(relativeY) / m_pdfSliderTrackRect.height, 0.0, 1.0);
                int targetPage = std::clamp(static_cast<int>(std::round(ratio * (m_pdfTotalPages - 1))), 0, m_pdfTotalPages - 1);
                SetPdfPage(targetPage);
                m_isDraggingPdfSlider = true;
                m_sliderDragStartMouseY = pt.y;
                m_sliderDragStartPage = targetPage;
                if (m_dropzonePanel && !m_dropzonePanel->HasCapture()) {
                    m_dropzonePanel->CaptureMouse();
                }
                if (m_dropzonePanel) {
                    m_dropzonePanel->Refresh();
                }
                return;
            }
        }

        if (m_previewBtnRect.Contains(pt)) {
            OpenImagePreview();
            return;
        }
    }

    // 识别进行中禁止通过点击背景替换文档
    if (m_currentState == OcrTaskState::Recognizing)
        return;

    OpenImageDialog();
}

void OcrView::OnDropzoneLeftUp(wxMouseEvent& event) {
    if (m_isDraggingPdfSlider) {
        m_isDraggingPdfSlider = false;
        if (m_dropzonePanel && m_dropzonePanel->HasCapture()) {
            m_dropzonePanel->ReleaseMouse();
        }
        if (m_dropzonePanel) {
            m_dropzonePanel->Refresh();
        }
        return;
    }
    event.Skip();
}

void OcrView::OnDropzoneMouseWheel(wxMouseEvent& event) {
    if (m_isPdfDoc && m_pdfTotalPages > 1) {
        int rot = event.GetWheelRotation();
        if (rot > 0) {
            SetPdfPage(m_pdfCurrentPage - 1);
        } else if (rot < 0) {
            SetPdfPage(m_pdfCurrentPage + 1);
        }
        return;
    }
    event.Skip();
}

void OcrView::OnImageFileDropped(const wxString& filePath) {
    LoadImageFile(filePath);
}

void OcrView::LoadImageFile(const wxString& filePath) {
    if (!wxFileExists(filePath))
        return;

    m_loadedImagePath = filePath;
    m_imageFileName = wxFileName(filePath).GetFullName();
    m_isPdfDoc = PdfHelper::IsPdfFile(filePath.ToUTF8().data());

    wxFileName docFn(filePath);
    m_lastOutputDir = docFn.GetPath() + "/" + docFn.GetName();

    if (m_isPdfDoc) {
        m_pdfTotalPages = PdfHelper::GetPageCount(filePath.ToUTF8().data());
        m_pdfCurrentPage = 0;
        PdfHelper::RenderPage(filePath.ToUTF8().data(), 0, m_loadedImage, 1600);
        if (m_progressPanel) {
            m_progressPanel->SetIdle(wxString::Format(L"PDF 文档已就绪 (共 %d 页)，支持垂直滑动条或滚轮滑动翻页", m_pdfTotalPages));
        }
    } else {
        m_pdfTotalPages = 1;
        m_pdfCurrentPage = 0;
        m_loadedImage.LoadFile(filePath);
        if (m_progressPanel) {
            m_progressPanel->SetIdle(L"图片已就绪，两阶段文档解析将自动定位表格、公式并转为 Markdown");
        }
    }

    if (m_recognizeBtn) {
        m_recognizeBtn->SetLabel(L"开始文档解析");
        m_recognizeBtn->SetIcon(SVG::LAYOUT, dip(16, 16), *wxWHITE);
        m_recognizeBtn->Refresh();
    }

    UpdateDropzoneUI();
}

void OcrView::SetPdfPage(int page) {
    if (!m_isPdfDoc || m_pdfTotalPages <= 1)
        return;
    int clampedPage = std::clamp(page, 0, m_pdfTotalPages - 1);
    if (clampedPage == m_pdfCurrentPage && m_loadedImage.IsOk())
        return;
    m_pdfCurrentPage = clampedPage;
    PdfHelper::RenderPage(m_loadedImagePath.ToUTF8().data(), m_pdfCurrentPage, m_loadedImage, 1600);
    if (m_dropzonePanel)
        m_dropzonePanel->Refresh();
}

void OcrView::ExportMarkdown() {
    if (m_lastMarkdownResult.IsEmpty()) {
        if (m_resultCard && m_resultCard->GetTextCtrl()) {
            m_lastMarkdownResult = m_resultCard->GetTextCtrl()->GetValue();
        }
    }
    if (m_lastMarkdownResult.IsEmpty()) {
        wxMessageBox(L"当前暂无识别或解析内容可导出！", L"提示", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxString defaultName = m_imageFileName.IsEmpty() ? L"document.md" : wxFileName(m_imageFileName).GetName() + L".md";
    wxFileDialog saveDialog(this, L"导出 Markdown 文件", "", defaultName, "Markdown Files (*.md)|*.md|All Files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveDialog.ShowModal() == wxID_OK) {
        wxString savePath = saveDialog.GetPath();
        std::ofstream out(savePath.ToStdWstring(), std::ios::binary);
        if (out) {
            std::string utf8 = m_lastMarkdownResult.ToUTF8().data();
            out.write(utf8.data(), utf8.size());
            out.close();
            wxMessageBox(L"Markdown 文档已成功导出！", L"导出成功", wxOK | wxICON_INFORMATION, this);
        }
    }
}

void OcrView::ExportJson() {
    if (m_lastJsonResult.IsEmpty()) {
        wxMessageBox(L"当前暂无结构化 JSON 数据！请先运行「文档解析」流水线。", L"提示", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxString defaultName = m_imageFileName.IsEmpty() ? L"document.json" : wxFileName(m_imageFileName).GetName() + L".json";
    wxFileDialog saveDialog(this, L"导出结构化 JSON 数据", "", defaultName, "JSON Files (*.json)|*.json|All Files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveDialog.ShowModal() == wxID_OK) {
        wxString savePath = saveDialog.GetPath();
        std::ofstream out(savePath.ToStdWstring(), std::ios::binary);
        if (out) {
            std::string utf8 = m_lastJsonResult.ToUTF8().data();
            out.write(utf8.data(), utf8.size());
            out.close();
            wxMessageBox(L"结构化 JSON 数据已成功导出！", L"导出成功", wxOK | wxICON_INFORMATION, this);
        }
    }
}

void OcrView::OpenOutputDir() {
    if (m_lastOutputDir.IsEmpty()) {
        if (!m_loadedImagePath.IsEmpty()) {
            wxFileName fn(m_loadedImagePath);
            m_lastOutputDir = fn.GetPath() + "/" + fn.GetName();
        }
    }
    if (m_lastOutputDir.IsEmpty() || !wxDirExists(m_lastOutputDir)) {
        wxMessageBox(L"输出目录尚未生成或不存在！", L"提示", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxLaunchDefaultApplication(m_lastOutputDir);
}

bool OcrView::PasteImageFromClipboard() {
    if (m_currentState == OcrTaskState::Recognizing)
        return false;

    wxImage img;
    wxString fileName;
    wxString filePath;

    if (ClipboardHelper::GetClipboardImage(img, &fileName, &filePath)) {
        // 防御性校验：如果剪贴板未能成功保存为真实文件路径，兜底落盘为临时图片文件
        if (filePath.IsEmpty() || !wxFileExists(filePath)) {
            wxString tempDir = wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator() + "LinguaAlpaca";
            if (!wxDirExists(tempDir)) {
                wxFileName::Mkdir(tempDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            }
            static std::atomic<uint64_t> s_ocrClipCounter{0};
            wxString timeStr = wxDateTime::Now().Format("%Y%m%d_%H%M%S");
            wxString tempPath = tempDir + wxFileName::GetPathSeparator() + wxString::Format("clipboard_%s_%llu.png", timeStr, ++s_ocrClipCounter);
            if (img.SaveFile(tempPath, wxBITMAP_TYPE_PNG)) {
                filePath = tempPath;
                if (fileName.IsEmpty() || fileName == L"剪贴板截图.png" || fileName == L"[剪贴板截图]") {
                    fileName = wxFileName(tempPath).GetFullName();
                }
            }
        }

        m_loadedImagePath = filePath;
        m_imageFileName = fileName;
        m_loadedImage = img;
        m_isPdfDoc = false;
        m_pdfTotalPages = 1;
        m_pdfCurrentPage = 0;

        if (!filePath.IsEmpty()) {
            wxFileName docFn(filePath);
            m_lastOutputDir = docFn.GetPath() + "/" + docFn.GetName();
        }

        if (m_progressPanel) {
            m_progressPanel->SetIdle(L"图片已就绪，两阶段文档解析将自动定位表格、公式并转为 Markdown");
        }
        if (m_recognizeBtn) {
            m_recognizeBtn->SetLabel(L"开始文档解析");
        }

        UpdateDropzoneUI();
        return true;
    }

    return false;
}

void OcrView::ShowDropzoneContextMenu(const wxPoint& pos) {
    if (m_currentState == OcrTaskState::Recognizing)
        return;

    wxMenu menu;
#ifdef __APPLE__
    menu.Append(1001, L"粘贴图片 (⌘V)");
#else
    menu.Append(1001, L"粘贴图片 (Ctrl+V)");
#endif
    menu.Append(1002, L"选择本地图片...");
    if (m_loadedImage.IsOk()) {
        menu.AppendSeparator();
        menu.Append(1003, L"预览图片");
        menu.Append(1004, L"清除图片");
    }

    menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        switch (e.GetId()) {
        case 1001:
            if (!PasteImageFromClipboard()) {
#ifdef __APPLE__
                wxMessageBox(L"剪贴板中未找到图像数据或图片文件！\n\n提示：您可以使用系统截图快捷键 (Cmd+Shift+Control+4 或第三方截图工具) 截图后直接按 ⌘V 粘贴，也可以在访达 (Finder) "
                             L"中复制图片文件后按 ⌘V 粘贴。",
                             L"提示", wxOK | wxICON_INFORMATION, this);
#else
					wxMessageBox(L"剪贴板中未找到图像数据或图片文件！\n\n提示：您可以使用系统截图快捷键 (如 Win+Shift+S 或 Alt+A) 截图后直接按 Ctrl+V 粘贴，也可以在文件资源管理器中复制图片文件后按 Ctrl+V 粘贴。",
						L"提示", wxOK | wxICON_INFORMATION, this);
#endif
            }
            break;
        case 1002:
            OpenImageDialog();
            break;
        case 1003:
            OpenImagePreview();
            break;
        case 1004:
            m_loadedImagePath.Clear();
            m_imageFileName.Clear();
            m_loadedImage.Destroy();
            m_isPdfDoc = false;
            m_pdfTotalPages = 0;
            m_pdfCurrentPage = 0;
            m_pdfSliderTrackRect = wxRect();
            m_pdfSliderThumbRect = wxRect();
            m_isDraggingPdfSlider = false;
            m_isHoveringPdfSlider = false;
            if (m_progressPanel) {
                m_progressPanel->Reset();
            }
            UpdateDropzoneUI();
            break;
        }
    });

    m_dropzonePanel->PopupMenu(&menu, pos);
}

void OcrView::UpdateDropzoneUI() {
    bool hasImage = m_loadedImage.IsOk();

    if (hasImage) {
        if (m_uploadIconBmp)
            m_uploadIconBmp->Hide();
        if (m_dropTextPrimary)
            m_dropTextPrimary->Hide();
        if (m_dropTextSecondary)
            m_dropTextSecondary->Hide();
    } else {
        if (m_uploadIconBmp)
            m_uploadIconBmp->Show();
        if (m_dropTextPrimary)
            m_dropTextPrimary->Show();
        if (m_dropTextSecondary)
            m_dropTextSecondary->Show();
    }

    m_isDropzoneHovered = false;
    m_hoveredAction = DropzoneHoverAction::None;

    if (m_dropzonePanel) {
        m_dropzonePanel->Layout();
        m_dropzonePanel->Refresh();
    }
}

void OcrView::SetState(OcrTaskState state) {
    m_currentState = state;
    if (state == OcrTaskState::Recognizing) {
        m_recognizeBtn->Hide();
        m_stopBtn->Show();
        if (m_translateBtn) {
            m_translateBtn->Hide();
        }
    } else {
        m_recognizeBtn->Show();
        m_stopBtn->Hide();
        UpdateTranslateButtonVisibility();
    }
    if (m_dropzonePanel) {
        m_dropzonePanel->Refresh();
    }
    GetSizer()->Layout();
}

void OcrView::OnRecognizeClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_currentState != OcrTaskState::Idle)
        return;

    if (!m_loadedImage.IsOk()) {
        wxMessageBox(L"请先上传、拖拽或从剪贴板粘贴一张待识别的 PDF 文档或图片！", L"提示", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!m_modelManager) {
        wxMessageBox(L"服务管理器未初始化！", L"错误", wxOK | wxICON_ERROR, this);
        return;
    }

    // 双重防御保护：确保 m_loadedImagePath 在磁盘上切实存在
    if (m_loadedImagePath.IsEmpty() || !wxFileExists(m_loadedImagePath)) {
        wxString tempDir = wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator() + "LinguaAlpaca";
        if (!wxDirExists(tempDir)) {
            wxFileName::Mkdir(tempDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }
        static std::atomic<uint64_t> s_runClipCounter{0};
        wxString timeStr = wxDateTime::Now().Format("%Y%m%d_%H%M%S");
        wxString tempPath = tempDir + wxFileName::GetPathSeparator() + wxString::Format("clipboard_%s_%llu.png", timeStr, ++s_runClipCounter);
        if (m_loadedImage.SaveFile(tempPath, wxBITMAP_TYPE_PNG)) {
            m_loadedImagePath = tempPath;
            if (m_imageFileName.IsEmpty() || m_imageFileName == L"剪贴板截图.png" || m_imageFileName == L"[剪贴板截图]") {
                m_imageFileName = wxFileName(tempPath).GetFullName();
            }
            wxFileName docFn(tempPath);
            m_lastOutputDir = docFn.GetPath() + "/" + docFn.GetName();
        } else {
            wxMessageBox(L"无法将待识别图像保存至临时文件，请检查系统临时目录读写权限！", L"错误", wxOK | wxICON_ERROR, this);
            return;
        }
    }

    WinTtsHelper::GetInstance().Stop();
    DoExecuteDocumentPipeline(m_loadedImagePath.ToUTF8().data());
}

void OcrView::DoExecuteDocumentPipeline(const std::string& docPath) {
    if (!m_modelManager) {
        SetState(OcrTaskState::Idle);
        return;
    }

    WinTtsHelper::GetInstance().Stop();

    // 清空结果卡片并重置字符计数，准备呈现最新识别内容
    if (m_resultCard) {
        m_resultCard->Clear();
        m_resultCard->SetCharacterCount(0);
    }
    m_lastMarkdownResult.Clear();
    m_lastJsonResult.Clear();

    if (m_openFolderBtn) {
        m_openFolderBtn->Hide();
        if (GetSizer())
            GetSizer()->Layout();
    }
    UpdateTranslateButtonVisibility();

    if (m_progressPanel) {
        m_progressPanel->SetStarting(L"正在启动 PaddleOCR-VL 两阶段文档解析流水线...");
    }

    SetState(OcrTaskState::Recognizing);

    int totalPages = m_pdfTotalPages > 0 ? m_pdfTotalPages : 1;

    // 1. 先确保版面分析模型装载就绪 (若未配置则优雅降级为单阶段全页识别)
    m_modelManager->EnsureModelAsync(TargetModelType::DocLayout, BindUi([this, totalPages](const std::string& statusMsg) {
                                         if (m_progressPanel) {
                                             m_progressPanel->SetProgress(0, totalPages, 3, L"正在准备版面分析引擎: " + wxString::FromUTF8(statusMsg));
                                         }
                                     }),
                                     BindUi([this, docPath, totalPages](bool layoutOk, const ServerStatusInfo& layoutInfo) {
                                         // 2. 继续确保 OCR 视觉模型服务就绪
                                         m_modelManager->EnsureModelAsync(
                                             TargetModelType::Ocr, BindUi([this, layoutOk, totalPages](const std::string& statusMsg) {
                                                 wxString prefix = layoutOk ? L"版面分析引擎已就绪，正在准备视觉模型: " : L"单阶段模式，正在准备视觉模型: ";
                                                 if (m_progressPanel) {
                                                     m_progressPanel->SetProgress(0, totalPages, 8, prefix + wxString::FromUTF8(statusMsg));
                                                 }
                                                 UpdateStatusBadge();
                                             }),
                                             BindUi([this, docPath, layoutOk](bool ok, const ServerStatusInfo& info) {
                                                 if (!ok) {
                                                     SetState(OcrTaskState::Idle);
                                                     if (m_progressPanel) {
                                                         m_progressPanel->SetError(L"OCR 视觉模型未就绪: " + wxString::FromUTF8(info.message) + L" (请前往「系统偏好设置」检查模型配置)");
                                                     }
                                                     UpdateStatusBadge();
                                                     return;
                                                 }

                                                 wxFileName fn(wxString::FromUTF8(docPath));
                                                 std::string outDir = std::string(fn.GetPath().ToUTF8().data()) + "/" + std::string(fn.GetName().ToUTF8().data());
                                                 m_lastOutputDir = wxString::FromUTF8(outDir);

                                                 auto pipeline = m_modelManager->GetDocumentPipeline();
                                                 if (!pipeline) {
                                                     SetState(OcrTaskState::Idle);
                                                     if (m_progressPanel) {
                                                         m_progressPanel->SetError(L"文档解析服务未就绪");
                                                     }
                                                     return;
                                                 }

                                                 pipeline->StartParseAsync(docPath, outDir, false, BindUi([this](int curPage, int totalPages, std::string stageDesc, std::string currentMarkdown) {
                                                                               int displayPage = curPage + 1;
                                                                               int percent = 0;
                                                                               if (totalPages <= 1) {
                                                                                   // 单页图像模式
                                                                                   if (stageDesc.find("光栅化") != std::string::npos)
                                                                                       percent = 15;
                                                                                   else if (stageDesc.find("版面分析") != std::string::npos)
                                                                                       percent = 35;
                                                                                   else if (stageDesc.find("识别") != std::string::npos) {
                                                                                       int elIdx = 0, elTotal = 0;
                                                                                       const char* s = strchr(stageDesc.c_str(), '(');
                                                                                       if (s && sscanf(s, "(%d/%d)", &elIdx, &elTotal) == 2 && elTotal > 0) {
                                                                                           percent = 35 + static_cast<int>(55.0 * elIdx / elTotal);
                                                                                       } else {
                                                                                           percent = 60;
                                                                                       }
                                                                                   } else {
                                                                                       percent = 85;
                                                                                   }
                                                                               } else {
                                                                                   // 多页 PDF 模式
                                                                                   double pageFraction = 0.2;
                                                                                   if (stageDesc.find("光栅化") != std::string::npos)
                                                                                       pageFraction = 0.1;
                                                                                   else if (stageDesc.find("版面分析") != std::string::npos)
                                                                                       pageFraction = 0.35;
                                                                                   else if (stageDesc.find("识别") != std::string::npos) {
                                                                                       int elIdx = 0, elTotal = 0;
                                                                                       const char* s = strchr(stageDesc.c_str(), '(');
                                                                                       if (s && sscanf(s, "(%d/%d)", &elIdx, &elTotal) == 2 && elTotal > 0) {
                                                                                           pageFraction = 0.35 + 0.6 * elIdx / elTotal;
                                                                                       } else {
                                                                                           pageFraction = 0.6;
                                                                                       }
                                                                                   } else {
                                                                                       pageFraction = 0.9;
                                                                                   }
                                                                                   percent = std::clamp(static_cast<int>(((curPage + pageFraction) / totalPages) * 100.0), 1, 99);
                                                                               }

                                                                               // 更新独立进度组件
                                                                               if (m_progressPanel) {
                                                                                   m_progressPanel->SetProgress(displayPage, totalPages, percent, wxString::FromUTF8(stageDesc));
                                                                               }

                                                                               // 接收 Markdown 结果并保持滑动条位置不变 (preserveScroll = true)
                                                                               if (m_resultCard) {
                                                                                   m_resultCard->SetMarkdown(currentMarkdown, std::string(m_lastOutputDir.ToUTF8().data()), true);
                                                                               }
                                                                               m_lastMarkdownResult = wxString::FromUTF8(currentMarkdown);
                                                                           }),
                                                                           BindUi([this](bool success, std::string fullMarkdown, std::string jsonStructured, std::string error) {
                                                                               SetState(OcrTaskState::Idle);
                                                                               if (success) {
                                                                                   m_lastMarkdownResult = wxString::FromUTF8(fullMarkdown);
                                                                                   m_lastJsonResult = wxString::FromUTF8(jsonStructured);
                                                                                   if (m_resultCard) {
                                                                                       m_resultCard->SetMarkdown(fullMarkdown, std::string(m_lastOutputDir.ToUTF8().data()), true);
                                                                                   }
                                                                                   if (m_progressPanel) {
                                                                                       m_progressPanel->SetCompleted(m_pdfTotalPages > 0 ? m_pdfTotalPages : 1, m_lastOutputDir);
                                                                                   }
                                                                                   if (m_openFolderBtn) {
                                                                                       m_openFolderBtn->Show();
                                                                                       if (GetSizer())
                                                                                           GetSizer()->Layout();
                                                                                   }
                                                                                   UpdateTranslateButtonVisibility();
                                                                                   wxMessageBox(L"PaddleOCR-VL 两阶段文档解析完成！\n\n"
                                                                                                L"• 结构化 Markdown 已生成并在右侧渲染\n"
                                                                                                L"• 结果已自动保存至: " +
                                                                                                    m_lastOutputDir +
                                                                                                    L"\n"
                                                                                                    L"• 表格、插图、公式均已按阅读顺序结构化重构",
                                                                                                L"文档解析完成", wxOK | wxICON_INFORMATION, this);
                                                                               } else {
                                                                                   // 中断或失败状态完全由独立进度组件展示，彻底避免冲掉 m_resultCard 识别结果
                                                                                   if (error == "用户已取消解析") {
                                                                                       if (m_progressPanel) {
                                                                                           m_progressPanel->SetCancelled(m_pdfCurrentPage + 1, m_pdfTotalPages,
                                                                                                                         L"用户已取消解析，已保留已生成的 Markdown 结果");
                                                                                       }
                                                                                   } else {
                                                                                       if (m_progressPanel) {
                                                                                           m_progressPanel->SetError(wxString::FromUTF8("文档解析中断或异常: " + error));
                                                                                       }
                                                                                   }
                                                                                   UpdateTranslateButtonVisibility();
                                                                               }
                                                                           }));
                                             }));
                                     }));
}

void OcrView::OnStopClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_modelManager) {
        auto pipeline = m_modelManager->GetDocumentPipeline();
        if (pipeline) {
            pipeline->Cancel();
        }
        m_modelManager->CancelInference();
    }
    WinTtsHelper::GetInstance().Stop();
    SetState(OcrTaskState::Idle);
    if (m_progressPanel) {
        m_progressPanel->SetCancelled(m_pdfCurrentPage + 1, m_pdfTotalPages, L"已停止解析，已保留当前生成的 Markdown 结果");
    }
}

void OcrView::UpdateTranslateButtonVisibility() {
    if (!m_translateBtn)
        return;
    bool hasContent = false;
    if (m_resultCard && m_resultCard->GetTextCtrl()) {
        wxString text = m_resultCard->GetTextCtrl()->GetValue().Trim(true).Trim(false);
        if (m_currentState == OcrTaskState::Idle && !text.IsEmpty() && !text.StartsWith(L"正在") && !text.StartsWith(L"错误:") && !text.StartsWith(L"识别出现提示/错误:")) {
            hasContent = true;
        }
    }
    if (hasContent) {
        if (!m_translateBtn->IsShown()) {
            m_translateBtn->Show();
            if (GetSizer())
                GetSizer()->Layout();
        }
    } else {
        if (m_translateBtn->IsShown()) {
            m_translateBtn->Hide();
            if (GetSizer())
                GetSizer()->Layout();
        }
    }
}

void OcrView::OnTranslateClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_currentState != OcrTaskState::Idle)
        return;

    wxString text;
    if (m_resultCard && m_resultCard->GetTextCtrl()) {
        text = m_resultCard->GetTextCtrl()->GetValue().Trim(true).Trim(false);
    }

    if (text.IsEmpty() || text.StartsWith(L"正在") || text.StartsWith(L"错误:")) {
        return;
    }

    WinTtsHelper::GetInstance().Stop();

    if (m_onTranslateCallback) {
        m_onTranslateCallback(text);
    } else {
        wxWindow* topWin = wxGetTopLevelParent(this);
        MainFrame* mainFrame = dynamic_cast<MainFrame*>(topWin);
        if (mainFrame) {
            mainFrame->NavigateToTextView(text, true);
        }
    }
}

} // namespace LinguaAlpaca::UI
