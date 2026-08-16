#include "OcrView.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>

namespace LinguaAlpaca::UI {

OcrView::OcrView(
    wxWindow *parent,
    std::shared_ptr<ModelManager> modelManager,
    wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      m_modelManager(std::move(modelManager)) {
  InitUI();

  m_healthTimer.Bind(wxEVT_TIMER,
                     [this](wxTimerEvent &) { UpdateStatusBadge(); });
  m_healthTimer.Start(1500);
}

OcrView::~OcrView() {
  if (m_healthTimer.IsRunning()) {
    m_healthTimer.Stop();
  }
}

void OcrView::InitUI() {
  auto palette = ThemeColors::GetCurrentPalette();
  SetBackgroundColour(palette.windowBg);

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // 1. Header Bar: Icon + Title ("图片 OCR 识别") + Status Badge
  wxBoxSizer *headerSizer = new wxBoxSizer(wxHORIZONTAL);

  wxBitmapBundle titleBundle = IconManager::GetIconBundle(
      SVG::OCR, dip(24, 24), palette.accentPrimary);
  wxStaticBitmap *titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

  m_titleText = new wxStaticText(this, wxID_ANY, L"图片 OCR 识别");
  m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                              wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_titleText->SetForegroundColour(palette.textPrimary);

  m_statusBadge = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 28_dip),
                              wxBORDER_NONE);
  wxBoxSizer *badgeSizer = new wxBoxSizer(wxHORIZONTAL);
  m_badgeText = new wxStaticText(m_statusBadge, wxID_ANY, L"●  OCR模型未配置");
  m_badgeText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                              wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  badgeSizer->Add(m_badgeText, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 10_dip);
  m_statusBadge->SetSizer(badgeSizer);

  headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
  headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
  headerSizer->AddStretchSpacer(1);
  headerSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

  mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20_dip);

  // 2. Middle Content Area (Left: Upload Card, Right: Recognized Text Card)
  wxBoxSizer *contentSizer = new wxBoxSizer(wxHORIZONTAL);

  // Left Column: Upload Bar + Dropzone Card (~45% width)
  wxBoxSizer *leftColSizer = new wxBoxSizer(wxVERTICAL);

  m_leftControlPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                   wxSize(-1, 48_dip), wxBORDER_NONE);
  m_leftControlPanel->SetBackgroundColour(palette.cardBg);

  wxBoxSizer *leftControlSizer = new wxBoxSizer(wxHORIZONTAL);
  m_typeLabel = new wxStaticText(m_leftControlPanel, wxID_ANY, L"🏷 识别类型");
  m_typeLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                              wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
  m_typeLabel->SetForegroundColour(palette.textPrimary);

  m_typeChoice = new wxChoice(m_leftControlPanel, wxID_ANY, wxDefaultPosition,
                              wxSize(-1, 32_dip));
  m_typeChoice->Append(L"通识 OCR");
  m_typeChoice->Append(L"表格识别 (Table)");
  m_typeChoice->Append(L"公式识别 (Formula)");
  m_typeChoice->Append(L"图表识别 (Chart)");
  m_typeChoice->Append(L"文本定位 (Spotting)");
  m_typeChoice->Append(L"印章识别 (Seal)");
  m_typeChoice->SetSelection(0);
  m_typeChoice->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                               wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));

  leftControlSizer->Add(m_typeLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12_dip);
  leftControlSizer->Add(m_typeChoice, 1,
                        wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12_dip);
  m_leftControlPanel->SetSizer(leftControlSizer);

  leftColSizer->Add(m_leftControlPanel, 0, wxEXPAND | wxBOTTOM, 12_dip);

  // Dropzone Card
  m_dropzonePanel = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxBORDER_NONE);
  m_dropzonePanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));

  wxBoxSizer *dropSizer = new wxBoxSizer(wxVERTICAL);

  m_topOverlayBar =
      new wxPanel(m_dropzonePanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 42_dip));
  m_topOverlayBar->SetBackgroundColour(wxColour(0, 0, 0, 128));

  wxBoxSizer *topOverlaySizer = new wxBoxSizer(wxHORIZONTAL);
  m_previewBtn = new CustomButton(
      m_topOverlayBar, wxID_ANY, L"预览", ButtonStyle::Secondary,
      wxDefaultPosition, dip(76, 30));
  m_previewBtn->SetIcon(SVG::EYE, dip(14, 14), palette.textPrimary);

  m_replaceBtn = new CustomButton(
      m_topOverlayBar, wxID_ANY, L"替换", ButtonStyle::Primary,
      wxDefaultPosition, dip(76, 30));
  m_replaceBtn->SetIcon(SVG::REPLACE, dip(14, 14), *wxWHITE);

  topOverlaySizer->AddStretchSpacer(1);
  topOverlaySizer->Add(m_previewBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
  topOverlaySizer->Add(m_replaceBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
  m_topOverlayBar->SetSizer(topOverlaySizer);
  m_topOverlayBar->Hide();

  dropSizer->Add(m_topOverlayBar, 0, wxEXPAND | wxTOP, 0);
  dropSizer->AddStretchSpacer(1);

  m_centerUploadBtn = new CustomButton(
      m_dropzonePanel, wxID_ANY, L" 替换图片", ButtonStyle::Primary,
      wxDefaultPosition, dip(130, 38));
  m_centerUploadBtn->SetIcon(SVG::CLOUD_UPLOAD, dip(16, 16),
                             *wxWHITE);
  m_centerUploadBtn->Hide();
  dropSizer->Add(m_centerUploadBtn, 0, wxALIGN_CENTER_HORIZONTAL);

  wxBitmapBundle uploadBundle = IconManager::GetIconBundle(
      SVG::CLOUD_UPLOAD, dip(44, 44), palette.accentPrimary);
  m_uploadIconBmp = new wxStaticBitmap(m_dropzonePanel, wxID_ANY, uploadBundle);

  m_dropTextPrimary =
      new wxStaticText(m_dropzonePanel, wxID_ANY, L"点击上传 或拖拽图片至此");
  m_dropTextPrimary->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                    wxFONTWEIGHT_BOLD, false,
                                    "Microsoft YaHei"));
  m_dropTextPrimary->SetForegroundColour(palette.textPrimary);

  m_dropTextSecondary =
      new wxStaticText(m_dropzonePanel, wxID_ANY, L"支持 JPG, PNG, BMP, WebP");
  m_dropTextSecondary->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                      wxFONTWEIGHT_NORMAL, false,
                                      "Microsoft YaHei"));
  m_dropTextSecondary->SetForegroundColour(palette.textSecondary);

  dropSizer->Add(m_uploadIconBmp, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8_dip);
  dropSizer->Add(m_dropTextPrimary, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4_dip);
  dropSizer->Add(m_dropTextSecondary, 0, wxALIGN_CENTER_HORIZONTAL);
  dropSizer->AddStretchSpacer(1);

  m_dropzonePanel->SetSizer(dropSizer);
  m_dropzonePanel->SetDropTarget(new OcrFileDropTarget(this));

  m_dropzonePanel->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(m_dropzonePanel);
    wxSize size = m_dropzonePanel->GetClientSize();
    if (size.x <= 0 || size.y <= 0)
      return;

    auto p = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(p.cardBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc)
      return;

    if (m_loadedImage.IsOk() && !m_loadedImagePath.IsEmpty()) {
      int pad = 6_dip;
      int availW = size.x - pad * 2;
      int availH = size.y - pad * 2;
      if (availW > 0 && availH > 0) {
        int imgW = m_loadedImage.GetWidth();
        int imgH = m_loadedImage.GetHeight();
        double scale = std::min(static_cast<double>(availW) / imgW,
                                static_cast<double>(availH) / imgH);
        int drawW = static_cast<int>(imgW * scale);
        int drawH = static_cast<int>(imgH * scale);
        int drawX = pad + (availW - drawW) / 2;
        int drawY = pad + (availH - drawH) / 2;

        wxImage scaledImg = m_loadedImage;
        scaledImg.Rescale(drawW, drawH, wxIMAGE_QUALITY_HIGH);
        wxBitmap bmp(scaledImg);

        gc->Clip(4_dip, 4_dip, size.x - 8_dip, size.y - 8_dip);
        gc->DrawBitmap(bmp, drawX, drawY, drawW, drawH);
        gc->ResetClip();

        wxGraphicsPath borderPath = gc->CreatePath();
        borderPath.AddRoundedRectangle(4_dip, 4_dip, size.x - 8_dip, size.y - 8_dip, 8_dip);
        wxPen pen(p.cardBorder, 1);
        gc->SetPen(pen);
        gc->StrokePath(borderPath);
      }
    } else {
      wxGraphicsPath path = gc->CreatePath();
      path.AddRoundedRectangle(4_dip, 4_dip, size.x - 8_dip, size.y - 8_dip, 8_dip);

      wxPen pen(p.cardBorder, 2, wxPENSTYLE_SHORT_DASH);
      gc->SetPen(pen);
      gc->StrokePath(path);
    }
  });

  leftColSizer->Add(m_dropzonePanel, 1, wxEXPAND);
  contentSizer->Add(leftColSizer, 45, wxEXPAND | wxRIGHT, 12_dip);

  // Right Column: Recognized Text Card (Ratio 55%)
  m_resultCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxBORDER_NONE);
  m_resultCard->SetBackgroundColour(palette.cardBg);

  wxBoxSizer *resultSizer = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer *resultHeader = new wxBoxSizer(wxHORIZONTAL);
  m_resultTitle = new wxStaticText(m_resultCard, wxID_ANY, L"A 识别文本");
  m_resultTitle->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_resultTitle->SetForegroundColour(palette.textPrimary);

  m_copyBtn = new CustomButton(m_resultCard, wxID_ANY, L"",
                               ButtonStyle::Secondary,
                               wxDefaultPosition, dip(34, 34));
  m_copyBtn->SetIcon(SVG::COPY, dip(16, 16), palette.textPrimary);
  m_copyBtn->SetToolTip(L"复制文本");

  m_clearBtn = new CustomButton(m_resultCard, wxID_ANY, L"",
                                ButtonStyle::Secondary,
                                wxDefaultPosition, dip(34, 34));
  m_clearBtn->SetIcon(SVG::CLEAR, dip(16, 16), palette.textPrimary);
  m_clearBtn->SetToolTip(L"清空内容");

  resultHeader->Add(m_resultTitle, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
  resultHeader->AddStretchSpacer(1);
  resultHeader->Add(m_copyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
  resultHeader->Add(m_clearBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);

  resultSizer->Add(resultHeader, 0, wxEXPAND | wxTOP | wxBOTTOM, 10_dip);

  m_resultTextCtrl =
      new wxTextCtrl(m_resultCard, wxID_ANY, L"", wxDefaultPosition,
                     wxDefaultSize, wxTE_MULTILINE | wxBORDER_NONE);
  m_resultTextCtrl->SetHint(L"上传图片后，识别结果将自动显示在这里...");
  m_resultTextCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                   wxFONTWEIGHT_NORMAL, false,
                                   "Microsoft YaHei"));
  m_resultTextCtrl->SetBackgroundColour(palette.windowBg);
  m_resultTextCtrl->SetForegroundColour(palette.textPrimary);

  resultSizer->Add(m_resultTextCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT, 16_dip);

  wxBoxSizer *resultFooter = new wxBoxSizer(wxHORIZONTAL);
  m_charCountText = new wxStaticText(m_resultCard, wxID_ANY, L"0 字符");
  m_charCountText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                  wxFONTWEIGHT_NORMAL, false,
                                  "Microsoft YaHei"));
  m_charCountText->SetForegroundColour(palette.textSecondary);

  resultFooter->AddStretchSpacer(1);
  resultFooter->Add(m_charCountText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);
  resultSizer->Add(resultFooter, 0, wxEXPAND | wxTOP | wxBOTTOM, 10_dip);

  m_resultCard->SetSizer(resultSizer);
  contentSizer->Add(m_resultCard, 55, wxEXPAND);

  mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

  // 3. Bottom Action Bar: Recognize / Stop Buttons
  wxBoxSizer *bottomSizer = new wxBoxSizer(wxHORIZONTAL);

  m_recognizeBtn = new CustomButton(
      this, wxID_ANY, L"识别", ButtonStyle::Primary,
      wxDefaultPosition, dip(145, 42));
  m_recognizeBtn->SetIcon(SVG::OCR, dip(16, 16), *wxWHITE);

  m_stopBtn = new CustomButton(this, wxID_ANY, L"停止",
                               ButtonStyle::Danger,
                               wxDefaultPosition, dip(145, 42));
  m_stopBtn->SetIcon(SVG::STOP, dip(16, 16), *wxWHITE);
  m_stopBtn->Hide();

  bottomSizer->Add(m_recognizeBtn, 0);
  bottomSizer->Add(m_stopBtn, 0);
  bottomSizer->AddStretchSpacer(1);

  mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

  SetSizer(mainSizer);

  UpdateStatusBadge();
  Layout();

  // Event Bindings
  m_dropzonePanel->Bind(wxEVT_LEFT_DOWN,
                        &OcrView::OnSelectImageClicked, this);
  m_uploadIconBmp->Bind(wxEVT_LEFT_DOWN,
                        &OcrView::OnSelectImageClicked, this);
  m_dropTextPrimary->Bind(wxEVT_LEFT_DOWN,
                          &OcrView::OnSelectImageClicked, this);
  m_dropTextSecondary->Bind(wxEVT_LEFT_DOWN,
                            &OcrView::OnSelectImageClicked, this);

  m_dropzonePanel->Bind(wxEVT_ENTER_WINDOW,
                        &OcrView::OnDropzoneMouseEnter, this);
  m_dropzonePanel->Bind(wxEVT_LEAVE_WINDOW,
                        &OcrView::OnDropzoneMouseLeave, this);

  m_topOverlayBar->Bind(wxEVT_ENTER_WINDOW,
                        &OcrView::OnDropzoneMouseEnter, this);
  m_topOverlayBar->Bind(wxEVT_LEAVE_WINDOW,
                        &OcrView::OnDropzoneMouseLeave, this);

  m_previewBtn->Bind(wxEVT_ENTER_WINDOW,
                     &OcrView::OnDropzoneMouseEnter, this);
  m_previewBtn->Bind(wxEVT_LEAVE_WINDOW,
                     &OcrView::OnDropzoneMouseLeave, this);

  m_replaceBtn->Bind(wxEVT_ENTER_WINDOW,
                     &OcrView::OnDropzoneMouseEnter, this);
  m_replaceBtn->Bind(wxEVT_LEAVE_WINDOW,
                     &OcrView::OnDropzoneMouseLeave, this);

  m_centerUploadBtn->Bind(wxEVT_ENTER_WINDOW,
                          &OcrView::OnDropzoneMouseEnter, this);
  m_centerUploadBtn->Bind(wxEVT_LEAVE_WINDOW,
                          &OcrView::OnDropzoneMouseLeave, this);

  m_previewBtn->Bind(wxEVT_BUTTON, &OcrView::OnPreviewClicked, this);
  m_replaceBtn->Bind(wxEVT_BUTTON, &OcrView::OnReplaceClicked, this);
  m_centerUploadBtn->Bind(wxEVT_BUTTON, &OcrView::OnReplaceClicked, this);

  m_recognizeBtn->Bind(wxEVT_BUTTON, &OcrView::OnRecognizeClicked, this);
  m_stopBtn->Bind(wxEVT_BUTTON, &OcrView::OnStopClicked, this);
  m_clearBtn->Bind(wxEVT_BUTTON, &OcrView::OnClearClicked, this);
  m_copyBtn->Bind(wxEVT_BUTTON, &OcrView::OnCopyClicked, this);

  m_resultTextCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
    wxString text = m_resultTextCtrl->GetValue();
    m_charCountText->SetLabel(wxString::Format(L"%zu 字符", text.Length()));
  });
}

void OcrView::UpdateStatusBadge() {
  if (!m_statusBadge || !m_badgeText)
    return;

  ServerStatusInfo info;
  if (m_modelManager) {
    info = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
  }

  switch (info.state) {
  case ServerHealthState::Ready:
    m_statusBadge->SetBackgroundColour(wxColour(240, 253, 244));
    m_badgeText->SetForegroundColour(wxColour(22, 101, 52));
    m_badgeText->SetLabel(L"●  OCR模型已就绪");
    break;
  case ServerHealthState::Loading:
    m_statusBadge->SetBackgroundColour(wxColour(254, 249, 195));
    m_badgeText->SetForegroundColour(wxColour(161, 98, 7));
    m_badgeText->SetLabel(L"●  正在加载OCR模型...");
    break;
  case ServerHealthState::Unconfigured:
    m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
    m_badgeText->SetForegroundColour(wxColour(220, 38, 38));
    m_badgeText->SetLabel(L"●  OCR模型未配置");
    break;
  case ServerHealthState::Offline:
    m_statusBadge->SetBackgroundColour(wxColour(241, 245, 249));
    m_badgeText->SetForegroundColour(wxColour(100, 116, 139));
    m_badgeText->SetLabel(L"●  服务离线");
    break;
  case ServerHealthState::Error:
  default:
    m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
    m_badgeText->SetForegroundColour(wxColour(220, 38, 38));
    m_badgeText->SetLabel(L"●  服务异常");
    break;
  }
  m_statusBadge->Layout();
}

void OcrView::UpdateTheme() {
  auto palette = ThemeColors::GetCurrentPalette();
  SetBackgroundColour(palette.windowBg);

  if (m_titleText)
    m_titleText->SetForegroundColour(palette.textPrimary);
  if (m_leftControlPanel)
    m_leftControlPanel->SetBackgroundColour(palette.cardBg);
  if (m_typeLabel)
    m_typeLabel->SetForegroundColour(palette.textPrimary);
  if (m_dropTextPrimary)
    m_dropTextPrimary->SetForegroundColour(palette.textPrimary);
  if (m_dropTextSecondary)
    m_dropTextSecondary->SetForegroundColour(palette.textSecondary);
  if (m_resultCard)
    m_resultCard->SetBackgroundColour(palette.cardBg);
  if (m_resultTitle)
    m_resultTitle->SetForegroundColour(palette.textPrimary);
  if (m_charCountText)
    m_charCountText->SetForegroundColour(palette.textSecondary);

  if (m_resultTextCtrl) {
    m_resultTextCtrl->SetBackgroundColour(palette.windowBg);
    m_resultTextCtrl->SetForegroundColour(palette.textPrimary);
  }

  if (m_uploadIconBmp) {
    wxBitmapBundle uploadBundle = IconManager::GetIconBundle(
        SVG::CLOUD_UPLOAD, wxSize(44, 44), palette.accentPrimary);
    m_uploadIconBmp->SetBitmap(uploadBundle.GetBitmap(wxSize(44, 44)));
  }

  if (m_recognizeBtn)
    m_recognizeBtn->Refresh();
  if (m_stopBtn)
    m_stopBtn->Refresh();
  if (m_copyBtn)
    m_copyBtn->Refresh();
  if (m_clearBtn)
    m_clearBtn->Refresh();

  if (m_dropzonePanel)
    m_dropzonePanel->Refresh();
  UpdateStatusBadge();
  Refresh();
}

void OcrView::OpenImageDialog() {
  wxFileDialog openFileDialog(this, L"选择待识别图片", "", "",
                              "Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.webp|All Files (*.*)|*.*",
                              wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (openFileDialog.ShowModal() == wxID_OK) {
    LoadImageFile(openFileDialog.GetPath());
  }
}

void OcrView::OnSelectImageClicked(wxMouseEvent &WXUNUSED(event)) {
  if (!m_loadedImage.IsOk()) {
    OpenImageDialog();
  }
}

void OcrView::OnPreviewClicked(wxCommandEvent &WXUNUSED(event)) {
  if (m_loadedImage.IsOk()) {
    ImagePreviewDialog dialog(this, m_loadedImage,
                              L"图片预览 - " + m_imageFileName);
    dialog.ShowModal();
  }
}

void OcrView::OnReplaceClicked(wxCommandEvent &WXUNUSED(event)) {
  if (m_currentState == OcrTaskState::Recognizing)
    return;
  OpenImageDialog();
}

void OcrView::OnDropzoneMouseEnter(wxMouseEvent &event) {
  if (m_currentState == OcrTaskState::Recognizing) {
    event.Skip();
    return;
  }

  if (m_loadedImage.IsOk() && !m_loadedImagePath.IsEmpty()) {
    if (m_topOverlayBar)
      m_topOverlayBar->Show();
    if (m_centerUploadBtn)
      m_centerUploadBtn->Show();
    m_dropzonePanel->Layout();
    m_dropzonePanel->Refresh();
  }
  event.Skip();
}

void OcrView::OnDropzoneMouseLeave(wxMouseEvent &event) {
  if (m_dropzonePanel) {
    wxRect screenRect = m_dropzonePanel->GetScreenRect();
    wxPoint mousePos = wxGetMousePosition();
    if (screenRect.Contains(mousePos)) {
      event.Skip();
      return;
    }
  }

  if (m_loadedImage.IsOk() && !m_loadedImagePath.IsEmpty()) {
    if (m_topOverlayBar)
      m_topOverlayBar->Hide();
    if (m_centerUploadBtn)
      m_centerUploadBtn->Hide();
    if (m_dropzonePanel) {
      m_dropzonePanel->Layout();
      m_dropzonePanel->Refresh();
    }
  }
  event.Skip();
}

void OcrView::OnImageFileDropped(const wxString &filePath) {
  LoadImageFile(filePath);
}

void OcrView::LoadImageFile(const wxString &filePath) {
  if (!wxFileExists(filePath))
    return;

  m_loadedImagePath = filePath;
  m_imageFileName = wxFileName(filePath).GetFullName();
  m_loadedImage.LoadFile(filePath);

  UpdateDropzoneUI();
}

void OcrView::UpdateDropzoneUI() {
  bool hasImage = m_loadedImage.IsOk() && !m_loadedImagePath.IsEmpty();

  if (hasImage) {
    if (m_uploadIconBmp)
      m_uploadIconBmp->Hide();
    if (m_dropTextPrimary)
      m_dropTextPrimary->Hide();
    if (m_dropTextSecondary)
      m_dropTextSecondary->Hide();
    if (m_topOverlayBar)
      m_topOverlayBar->Hide();
    if (m_centerUploadBtn)
      m_centerUploadBtn->Hide();
  } else {
    if (m_uploadIconBmp)
      m_uploadIconBmp->Show();
    if (m_dropTextPrimary)
      m_dropTextPrimary->Show();
    if (m_dropTextSecondary)
      m_dropTextSecondary->Show();
    if (m_topOverlayBar)
      m_topOverlayBar->Hide();
    if (m_centerUploadBtn)
      m_centerUploadBtn->Hide();
  }

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
    if (m_topOverlayBar)
      m_topOverlayBar->Hide();
    if (m_centerUploadBtn)
      m_centerUploadBtn->Hide();
  } else {
    m_recognizeBtn->Show();
    m_stopBtn->Hide();
  }
  GetSizer()->Layout();
}

std::string OcrView::GetSelectedTaskType() const {
  int sel = m_typeChoice ? m_typeChoice->GetSelection() : 0;
  switch (sel) {
  case 1:
    return "table";
  case 2:
    return "formula";
  case 3:
    return "chart";
  case 4:
    return "spotting";
  case 5:
    return "seal";
  default:
    return "ocr";
  }
}

void OcrView::OnRecognizeClicked(wxCommandEvent &WXUNUSED(event)) {
  if (m_currentState != OcrTaskState::Idle)
    return;

  if (m_loadedImagePath.IsEmpty()) {
    wxMessageBox(L"请先上传或拖拽一张待识别的图片！", L"提示",
                 wxOK | wxICON_INFORMATION, this);
    return;
  }

  if (!m_modelManager) {
    wxMessageBox(L"服务管理器未初始化！", L"错误", wxOK | wxICON_ERROR, this);
    return;
  }

  auto info = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
  if (info.state != ServerHealthState::Ready) {
    wxMessageBox(L"OCR 模型尚未就绪或未能成功加载！\n\n请先前往「系统设置」界面配置合法的 OCR 模型与 mmproj 视觉投影器文件路径。",
                 L"OCR 服务提示", wxOK | wxICON_WARNING, this);
    return;
  }

  m_resultTextCtrl->Clear();
  m_charCountText->SetLabel(L"0 字符");
  SetState(OcrTaskState::Recognizing);

  std::string imgPath = m_loadedImagePath.ToUTF8().data();
  std::string taskType = GetSelectedTaskType();

  wxWeakRef<OcrView> weakSelf(this);

  m_modelManager->ExecuteOcrStream(
      imgPath, taskType,
      [weakSelf](const std::string &token) {
        wxString wToken = wxString::FromUTF8(token);
        if (wxTheApp) {
          wxTheApp->CallAfter([weakSelf, wToken]() {
            if (!weakSelf || !weakSelf->m_resultTextCtrl)
              return;
            weakSelf->m_resultTextCtrl->AppendText(wToken);
            wxString current = weakSelf->m_resultTextCtrl->GetValue();
            if (weakSelf->m_charCountText) {
              weakSelf->m_charCountText->SetLabel(
                  wxString::Format(L"%zu 字符", current.Length()));
            }
          });
        }
      },
      [weakSelf](const std::string &fullText, bool success,
                 const std::string &error) {
        if (wxTheApp) {
          wxTheApp->CallAfter([weakSelf, fullText, success, error]() {
            if (!weakSelf)
              return;
            weakSelf->SetState(OcrTaskState::Idle);

            if (!weakSelf->m_resultTextCtrl)
              return;

            if (success) {
              wxString wClean = wxString::FromUTF8(fullText);
              weakSelf->m_resultTextCtrl->SetValue(wClean);
              if (weakSelf->m_charCountText) {
                weakSelf->m_charCountText->SetLabel(
                    wxString::Format(L"%zu 字符", wClean.Length()));
              }
            } else if (!error.empty()) {
              if (error == "已取消") {
                weakSelf->m_resultTextCtrl->AppendText(
                    L"\n\n[⏹ OCR 识别已被用户中断]");
              } else {
                weakSelf->m_resultTextCtrl->SetValue(
                    wxString::FromUTF8("识别出现提示/错误: " + error));
              }
            }
          });
        }
      });
}

void OcrView::OnStopClicked(wxCommandEvent &WXUNUSED(event)) {
  if (m_modelManager) {
    m_modelManager->CancelInference();
  }
  SetState(OcrTaskState::Idle);
}

void OcrView::OnClearClicked(wxCommandEvent &WXUNUSED(event)) {
  m_resultTextCtrl->Clear();
  m_charCountText->SetLabel(L"0 字符");
}

void OcrView::OnCopyClicked(wxCommandEvent &WXUNUSED(event)) {
  wxString text = m_resultTextCtrl->GetValue();
  if (text.IsEmpty())
    return;

  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
    wxMessageBox(L"识别结果已复制到剪贴板！", L"提示",
                 wxOK | wxICON_INFORMATION, this);
  }
}

} // namespace LinguaAlpaca::UI
