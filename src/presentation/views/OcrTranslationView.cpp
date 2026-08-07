#include "OcrTranslationView.hpp"
#include "../theme/ThemeColors.hpp"
#include "../theme/IconManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/clipbrd.h>

namespace LinguaAlpaca::Presentation::Views {

OcrTranslationView::OcrTranslationView(
    wxWindow* parent,
    std::shared_ptr<Application::Service::TranslationService> translationService,
    std::shared_ptr<Application::Service::OcrService> ocrService,
    wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_translationService(std::move(translationService))
    , m_ocrService(std::move(ocrService)) {
    InitUI();
}

OcrTranslationView::~OcrTranslationView() {}

void OcrTranslationView::InitUI() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // =========================================================================
    // 1. Header Bar: Icon + Title ("图片 OCR 识别") + Status Badge
    // =========================================================================
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle titleBundle = Theme::IconManager::GetIconBundle(Theme::SVG::OCR, wxSize(24, 24), palette.accentPrimary);
    wxStaticBitmap* titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

    m_titleText = new wxStaticText(this, wxID_ANY, L"图片 OCR 识别");
    m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    m_statusBadge = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 28), wxBORDER_NONE);
    wxBoxSizer* badgeSizer = new wxBoxSizer(wxHORIZONTAL);
    m_badgeText = new wxStaticText(m_statusBadge, wxID_ANY, L"● OCR模型未配置");
    m_badgeText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    badgeSizer->Add(m_badgeText, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 10);
    m_statusBadge->SetSizer(badgeSizer);

    headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->AddStretchSpacer(1);
    headerSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20);

    // =========================================================================
    // 2. Middle Content Area (Left: Upload Card, Right: Recognized Text Card)
    // =========================================================================
    wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

    // -------------------------------------------------------------------------
    // Left Column: Upload Bar + Dropzone Card (~45% width)
    // -------------------------------------------------------------------------
    wxBoxSizer* leftColSizer = new wxBoxSizer(wxVERTICAL);

    // Top Control Bar in Left Panel
    m_leftControlPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 48), wxBORDER_NONE);
    m_leftControlPanel->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* leftControlSizer = new wxBoxSizer(wxHORIZONTAL);
    m_typeLabel = new wxStaticText(m_leftControlPanel, wxID_ANY, L"🏷 识别类型");
    m_typeLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_typeLabel->SetForegroundColour(palette.textPrimary);

    m_typeChoice = new wxChoice(m_leftControlPanel, wxID_ANY, wxDefaultPosition, wxSize(150, 32));
    m_typeChoice->Append(L"通识 OCR");
    m_typeChoice->Append(L"表格识别 (Table)");
    m_typeChoice->Append(L"公式识别 (Formula)");
    m_typeChoice->Append(L"图表识别 (Chart)");
    m_typeChoice->Append(L"文本定位 (Spotting)");
    m_typeChoice->Append(L"印章识别 (Seal)");
    m_typeChoice->SetSelection(0);
    m_typeChoice->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));

    m_uploadBtn = new Components::CustomButton(m_leftControlPanel, wxID_ANY, L"上传", Components::ButtonStyle::Primary, wxDefaultPosition, wxSize(90, 34));
    m_uploadBtn->SetIcon(Theme::SVG::CLOUD_UPLOAD, wxSize(16, 16), *wxWHITE);

    leftControlSizer->Add(m_typeLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    leftControlSizer->Add(m_typeChoice, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    leftControlSizer->AddStretchSpacer(1);
    leftControlSizer->Add(m_uploadBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    m_leftControlPanel->SetSizer(leftControlSizer);

    leftColSizer->Add(m_leftControlPanel, 0, wxEXPAND | wxBOTTOM, 12);

    // Dropzone Card
    m_dropzonePanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_dropzonePanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));

    wxBoxSizer* dropSizer = new wxBoxSizer(wxVERTICAL);
    dropSizer->AddStretchSpacer(1);

    wxBitmapBundle uploadBundle = Theme::IconManager::GetIconBundle(Theme::SVG::CLOUD_UPLOAD, wxSize(44, 44), palette.accentPrimary);
    m_uploadIconBmp = new wxStaticBitmap(m_dropzonePanel, wxID_ANY, uploadBundle);

    m_dropTextPrimary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"点击上传 或拖拽图片至此");
    m_dropTextPrimary->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_dropTextPrimary->SetForegroundColour(palette.textPrimary);

    m_dropTextSecondary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"支持 JPG, PNG, BMP, WebP");
    m_dropTextSecondary->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_dropTextSecondary->SetForegroundColour(palette.textSecondary);

    dropSizer->Add(m_uploadIconBmp, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    dropSizer->Add(m_dropTextPrimary, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    dropSizer->Add(m_dropTextSecondary, 0, wxALIGN_CENTER_HORIZONTAL);
    dropSizer->AddStretchSpacer(1);

    m_dropzonePanel->SetSizer(dropSizer);
    m_dropzonePanel->SetDropTarget(new OcrFileDropTarget(this));

    // Custom Paint Dashed Card
    m_dropzonePanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_dropzonePanel);
        wxSize size = m_dropzonePanel->GetClientSize();
        if (size.x <= 0 || size.y <= 0) return;

        auto p = Theme::ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(p.cardBg));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            wxGraphicsPath path = gc->CreatePath();
            path.AddRoundedRectangle(4, 4, size.x - 8, size.y - 8, 8);

            wxPen pen(p.cardBorder, 2, wxPENSTYLE_SHORT_DASH);
            gc->SetPen(pen);
            gc->StrokePath(path);
        }
    });

    leftColSizer->Add(m_dropzonePanel, 1, wxEXPAND);
    contentSizer->Add(leftColSizer, 45, wxEXPAND | wxRIGHT, 12);

    // -------------------------------------------------------------------------
    // Right Column: Recognized Text Card (Ratio 55%)
    // -------------------------------------------------------------------------
    m_resultCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_resultCard->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* resultSizer = new wxBoxSizer(wxVERTICAL);

    // Card Header
    wxBoxSizer* resultHeader = new wxBoxSizer(wxHORIZONTAL);
    m_resultTitle = new wxStaticText(m_resultCard, wxID_ANY, L"A 识别文本");
    m_resultTitle->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_resultTitle->SetForegroundColour(palette.textPrimary);

    m_copyBtn = new Components::CustomButton(m_resultCard, wxID_ANY, L"", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(34, 34));
    m_copyBtn->SetIcon(Theme::SVG::COPY, wxSize(16, 16), palette.textPrimary);
    m_copyBtn->SetToolTip(L"复制文本");

    m_clearBtn = new Components::CustomButton(m_resultCard, wxID_ANY, L"", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(34, 34));
    m_clearBtn->SetIcon(Theme::SVG::CLEAR, wxSize(16, 16), palette.textPrimary);
    m_clearBtn->SetToolTip(L"清空内容");

    resultHeader->Add(m_resultTitle, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    resultHeader->AddStretchSpacer(1);
    resultHeader->Add(m_copyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    resultHeader->Add(m_clearBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    resultSizer->Add(resultHeader, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);

    // Text Ctrl Area
    m_resultTextCtrl = new wxTextCtrl(m_resultCard, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxBORDER_NONE);
    m_resultTextCtrl->SetHint(L"上传图片后，识别结果将自动显示在这里...");
    m_resultTextCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_resultTextCtrl->SetBackgroundColour(palette.windowBg);
    m_resultTextCtrl->SetForegroundColour(palette.textPrimary);

    resultSizer->Add(m_resultTextCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT, 16);

    // Card Footer (Character Count)
    wxBoxSizer* resultFooter = new wxBoxSizer(wxHORIZONTAL);
    m_charCountText = new wxStaticText(m_resultCard, wxID_ANY, L"0 字符");
    m_charCountText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_charCountText->SetForegroundColour(palette.textSecondary);

    resultFooter->AddStretchSpacer(1);
    resultFooter->Add(m_charCountText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
    resultSizer->Add(resultFooter, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);

    m_resultCard->SetSizer(resultSizer);
    contentSizer->Add(m_resultCard, 55, wxEXPAND);

    mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    // =========================================================================
    // 3. Bottom Action Bar: Recognize / Stop Buttons
    // =========================================================================
    wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

    m_recognizeBtn = new Components::CustomButton(this, wxID_ANY, L"识别", Components::ButtonStyle::Primary, wxDefaultPosition, wxSize(145, 42));
    m_recognizeBtn->SetIcon(Theme::SVG::OCR, wxSize(16, 16), *wxWHITE);

    m_stopBtn = new Components::CustomButton(this, wxID_ANY, L"停止", Components::ButtonStyle::Danger, wxDefaultPosition, wxSize(145, 42));
    m_stopBtn->SetIcon(Theme::SVG::STOP, wxSize(16, 16), *wxWHITE);
    m_stopBtn->Hide();

    bottomSizer->Add(m_recognizeBtn, 0);
    bottomSizer->Add(m_stopBtn, 0);
    bottomSizer->AddStretchSpacer(1);

    mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    SetSizer(mainSizer);

    // Update Status Badge
    UpdateStatusBadge();

    // Event Bindings
    m_uploadBtn->Bind(wxEVT_BUTTON, &OcrTranslationView::OnUploadClicked, this);
    m_dropzonePanel->Bind(wxEVT_LEFT_DOWN, &OcrTranslationView::OnSelectImageClicked, this);
    m_uploadIconBmp->Bind(wxEVT_LEFT_DOWN, &OcrTranslationView::OnSelectImageClicked, this);
    m_dropTextPrimary->Bind(wxEVT_LEFT_DOWN, &OcrTranslationView::OnSelectImageClicked, this);
    m_dropTextSecondary->Bind(wxEVT_LEFT_DOWN, &OcrTranslationView::OnSelectImageClicked, this);

    m_recognizeBtn->Bind(wxEVT_BUTTON, &OcrTranslationView::OnRecognizeClicked, this);
    m_stopBtn->Bind(wxEVT_BUTTON, &OcrTranslationView::OnStopClicked, this);
    m_clearBtn->Bind(wxEVT_BUTTON, &OcrTranslationView::OnClearClicked, this);
    m_copyBtn->Bind(wxEVT_BUTTON, &OcrTranslationView::OnCopyClicked, this);

    m_resultTextCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        wxString text = m_resultTextCtrl->GetValue();
        m_charCountText->SetLabel(wxString::Format(L"%zu 字符", text.Length()));
    });
}

void OcrTranslationView::UpdateStatusBadge() {
    if (!m_statusBadge || !m_badgeText) return;

    bool loaded = m_ocrService ? m_ocrService->IsModelLoaded() : false;
    if (loaded) {
        m_statusBadge->SetBackgroundColour(wxColour(240, 253, 244));
        m_badgeText->SetForegroundColour(wxColour(22, 101, 52));
        m_badgeText->SetLabel(L"● OCR模型已就绪");
    } else {
        m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
        m_badgeText->SetForegroundColour(wxColour(220, 38, 38));
        m_badgeText->SetLabel(L"● OCR模型未配置");
    }
    m_statusBadge->Layout();
}

void OcrTranslationView::UpdateTheme() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    if (m_titleText) m_titleText->SetForegroundColour(palette.textPrimary);
    if (m_leftControlPanel) m_leftControlPanel->SetBackgroundColour(palette.cardBg);
    if (m_typeLabel) m_typeLabel->SetForegroundColour(palette.textPrimary);
    if (m_dropTextPrimary) m_dropTextPrimary->SetForegroundColour(palette.textPrimary);
    if (m_dropTextSecondary) m_dropTextSecondary->SetForegroundColour(palette.textSecondary);
    if (m_resultCard) m_resultCard->SetBackgroundColour(palette.cardBg);
    if (m_resultTitle) m_resultTitle->SetForegroundColour(palette.textPrimary);
    if (m_charCountText) m_charCountText->SetForegroundColour(palette.textSecondary);

    if (m_resultTextCtrl) {
        m_resultTextCtrl->SetBackgroundColour(palette.windowBg);
        m_resultTextCtrl->SetForegroundColour(palette.textPrimary);
    }

    if (m_uploadIconBmp) {
        wxBitmapBundle uploadBundle = Theme::IconManager::GetIconBundle(Theme::SVG::CLOUD_UPLOAD, wxSize(44, 44), palette.accentPrimary);
        m_uploadIconBmp->SetBitmap(uploadBundle.GetBitmap(wxSize(44, 44)));
    }

    if (m_uploadBtn) m_uploadBtn->Refresh();
    if (m_recognizeBtn) m_recognizeBtn->Refresh();
    if (m_stopBtn) m_stopBtn->Refresh();
    if (m_copyBtn) m_copyBtn->Refresh();
    if (m_clearBtn) m_clearBtn->Refresh();

    if (m_dropzonePanel) m_dropzonePanel->Refresh();
    UpdateStatusBadge();
    Refresh();
}

void OcrTranslationView::OpenImageDialog() {
    wxFileDialog openFileDialog(this, L"选择待识别图片", "", "",
        "Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.webp|All Files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        LoadImageFile(openFileDialog.GetPath());
    }
}

void OcrTranslationView::OnUploadClicked(wxCommandEvent& WXUNUSED(event)) {
    OpenImageDialog();
}

void OcrTranslationView::OnSelectImageClicked(wxMouseEvent& WXUNUSED(event)) {
    OpenImageDialog();
}

void OcrTranslationView::OnImageFileDropped(const wxString& filePath) {
    LoadImageFile(filePath);
}

void OcrTranslationView::LoadImageFile(const wxString& filePath) {
    if (!wxFileExists(filePath)) return;

    m_loadedImagePath = filePath;
    m_imageFileName = wxFileName(filePath).GetFullName();

    m_dropTextPrimary->SetLabel(L"已选择图片: " + m_imageFileName);
    m_dropTextSecondary->SetLabel(filePath);
    m_dropzonePanel->Layout();
}

void OcrTranslationView::SetState(OcrTaskState state) {
    m_currentState = state;
    if (state == OcrTaskState::Recognizing) {
        m_recognizeBtn->Hide();
        m_stopBtn->Show();
    } else {
        m_recognizeBtn->Show();
        m_stopBtn->Hide();
    }
    GetSizer()->Layout();
}

std::string OcrTranslationView::GetSelectedTaskType() const {
    int sel = m_typeChoice ? m_typeChoice->GetSelection() : 0;
    switch (sel) {
        case 1: return "table";
        case 2: return "formula";
        case 3: return "chart";
        case 4: return "spotting";
        case 5: return "seal";
        default: return "ocr";
    }
}

void OcrTranslationView::OnRecognizeClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_currentState != OcrTaskState::Idle) return;

    if (m_loadedImagePath.IsEmpty()) {
        wxMessageBox(L"请先上传或拖拽一张待识别的图片！", L"提示", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!m_ocrService || !m_ocrService->IsModelLoaded()) {
        wxMessageBox(
            L"OCR 模型尚未配置或未能成功加载！\n\n请先前往「系统设置」界面，配置合法的 PaddleOCR-VL 主模型与 mmproj 视觉投影器文件路径。",
            L"OCR 服务未配置提示",
            wxOK | wxICON_WARNING,
            this
        );
        return;
    }

    m_resultTextCtrl->Clear();
    m_charCountText->SetLabel(L"0 字符");
    SetState(OcrTaskState::Recognizing);

    std::string imgPath = m_loadedImagePath.ToUTF8().data();
    std::string taskType = GetSelectedTaskType();
    std::string modelPath = m_ocrService->GetModelPath();
    std::string mmprojPath = m_ocrService->GetMmprojPath();

    m_ocrService->RecognizeImageStream(
        imgPath, taskType, modelPath, mmprojPath,
        [this](const std::string& token) {
            wxString wToken = wxString::FromUTF8(token);
            wxTheApp->CallAfter([this, wToken]() {
                m_resultTextCtrl->AppendText(wToken);
                wxString current = m_resultTextCtrl->GetValue();
                m_charCountText->SetLabel(wxString::Format(L"%zu 字符", current.Length()));
            });
        },
        [this](const std::string& fullText, bool success, const std::string& error) {
            wxTheApp->CallAfter([this, fullText, success, error]() {
                SetState(OcrTaskState::Idle);

                if (success) {
                    wxString wClean = wxString::FromUTF8(fullText);
                    m_resultTextCtrl->SetValue(wClean);
                    m_charCountText->SetLabel(wxString::Format(L"%zu 字符", wClean.Length()));
                } else if (!error.empty()) {
                    if (error == "已取消") {
                        m_resultTextCtrl->AppendText(L"\n\n[⏹ OCR 识别已被用户中断]");
                    } else {
                        m_resultTextCtrl->SetValue(wxString::FromUTF8("识别出现提示/错误: " + error));
                    }
                }
            });
        }
    );
}

void OcrTranslationView::OnStopClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_ocrService) {
        m_ocrService->CancelOcr();
    }
    SetState(OcrTaskState::Idle);
}

void OcrTranslationView::OnClearClicked(wxCommandEvent& WXUNUSED(event)) {
    m_resultTextCtrl->Clear();
    m_charCountText->SetLabel(L"0 字符");
}

void OcrTranslationView::OnCopyClicked(wxCommandEvent& WXUNUSED(event)) {
    wxString text = m_resultTextCtrl->GetValue();
    if (text.IsEmpty()) return;

    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
        wxMessageBox(L"识别结果已复制到剪贴板！", L"提示", wxOK | wxICON_INFORMATION, this);
    }
}

} // namespace LinguaAlpaca::Presentation::Views
