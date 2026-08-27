#include "OcrView.hpp"
#include "core/ClipboardHelper.hpp"
#include "core/WinTtsHelper.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include <base64.hpp>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/mstream.h>
#include <wx/stdpaths.h>

namespace LinguaAlpaca::UI {

	OcrView::OcrView(
		wxWindow* parent,
		std::shared_ptr<ModelManager> modelManager,
		wxWindowID id)
		: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
		m_modelManager(std::move(modelManager)) {
		InitUI();

		m_healthTimer.Bind(wxEVT_TIMER,
			[this](wxTimerEvent&) { UpdateStatusBadge(); });
		m_healthTimer.Start(1500);
	}

	OcrView::~OcrView() {
		if (m_healthTimer.IsRunning()) {
			m_healthTimer.Stop();
		}
		WinTtsHelper::GetInstance().Stop();
	}

	void OcrView::InitUI() {
		auto palette = ThemeColors::GetCurrentPalette();
		SetBackgroundColour(palette.windowBg);

		wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

		// 1. Header Bar: Icon + Title ("图片 OCR 识别") + Status Badge
		wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

		wxBitmapBundle titleBundle = IconManager::GetIconBundle(
			SVG::OCR, wxSize(24, 24), palette.accentPrimary);
		wxStaticBitmap* titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

		m_titleText = new wxStaticText(this, wxID_ANY, L"图片 OCR 识别");
		m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_titleText->SetForegroundColour(palette.textPrimary);

		m_statusBadge = new StatusBadge(this);

		headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
		headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
		headerSizer->AddStretchSpacer(1);
		headerSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

		mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20_dip);

		// 2. Middle Content Area (Left: Upload Card, Right: Recognized Text Card)
		wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

		// Left Column: Upload Bar + Dropzone Card (~45% width)
		wxBoxSizer* leftColSizer = new wxBoxSizer(wxVERTICAL);

		m_leftControlPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition,
			wxSize(-1, 48_dip), wxBORDER_NONE);
		m_leftControlPanel->SetBackgroundColour(palette.cardBg);

		wxBoxSizer* leftControlSizer = new wxBoxSizer(wxHORIZONTAL);
		m_typeLabel = new wxStaticText(m_leftControlPanel, wxID_ANY, L"识别类型");
		m_typeLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_typeLabel->SetForegroundColour(palette.textSecondary);

		m_typeChoice = new CustomChoice(m_leftControlPanel, wxID_ANY, wxDefaultPosition, dip(180, 32));
		m_typeChoice->Append(L"通识 OCR");
		m_typeChoice->Append(L"表格识别 (Table)");
		m_typeChoice->Append(L"公式识别 (Formula)");
		m_typeChoice->Append(L"图表识别 (Chart)");
		m_typeChoice->Append(L"文本定位 (Spotting)");
		m_typeChoice->Append(L"印章识别 (Seal)");
		m_typeChoice->SetSelection(0);

		leftControlSizer->Add(m_typeLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10_dip);
		leftControlSizer->Add(m_typeChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
		m_leftControlPanel->SetSizer(leftControlSizer);

		leftColSizer->Add(m_leftControlPanel, 0, wxEXPAND | wxBOTTOM, 12_dip);

		// Dropzone Card
		m_dropzonePanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
		m_dropzonePanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
		m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));
		m_dropzonePanel->SetBackgroundColour(palette.cardBg);

		wxBoxSizer* dropSizer = new wxBoxSizer(wxVERTICAL);

		wxBitmapBundle uploadBundle = IconManager::GetIconBundle(SVG::CLOUD_UPLOAD, wxSize(44, 44), palette.accentPrimary);
		m_uploadIconBmp = new wxStaticBitmap(m_dropzonePanel, wxID_ANY, uploadBundle);

		m_dropTextPrimary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"点击上传 或 拖拽/粘贴图片至此");
		m_dropTextPrimary->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_dropTextPrimary->SetForegroundColour(palette.textPrimary);

		m_dropTextSecondary = new wxStaticText(m_dropzonePanel, wxID_ANY, L"支持 JPG, PNG, BMP, WebP 及剪贴板截图 (Ctrl+V)");
		m_dropTextSecondary->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_dropTextSecondary->SetForegroundColour(palette.textSecondary);

		dropSizer->AddStretchSpacer(1);
		dropSizer->Add(m_uploadIconBmp, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8_dip);
		dropSizer->Add(m_dropTextPrimary, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4_dip);
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

					// 悬浮在图片上且非识别中状态时，绘制半透明暗色遮罩与交互按钮
					if (m_isDropzoneHovered && m_currentState != OcrTaskState::Recognizing) {
						// 1. 半透明暗色遮罩
						gc->SetBrush(gc->CreateBrush(wxBrush(wxColour(0, 0, 0, 85))));
						gc->FillPath(path);

						// 2. 右上角「预览」按钮 (次级毛玻璃样式)
						int topBtnW = 76_dip;
						int topBtnH = 30_dip;
						int topBtnX = size.x - topBtnW - 14_dip;
						int topBtnY = 12_dip;
						m_previewBtnRect = wxRect(topBtnX, topBtnY, topBtnW, topBtnH);

						bool isTopHovered = (m_hoveredAction == DropzoneHoverAction::Preview);
						wxColour topBg = isTopHovered ? wxColour(255, 255, 255, 240) : wxColour(255, 255, 255, 190);
						wxColour topText = wxColour(30, 41, 59);

						gc->SetBrush(gc->CreateBrush(wxBrush(topBg)));
						gc->SetPen(*wxTRANSPARENT_PEN);
						gc->DrawRoundedRectangle(topBtnX, topBtnY, topBtnW, topBtnH, 6.0_dip);

						wxSize topIconSz = dip(14, 14);
						wxBitmapBundle eyeBundle = IconManager::GetIconBundle(SVG::EYE, wxSize(14, 14), topText);
						wxBitmap eyeBmp = eyeBundle.GetBitmap(topIconSz);

						wxFont topFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
						gc->SetFont(topFont, topText);
						double eyeTw = 0, eyeTh = 0;
						gc->GetTextExtent(L"预览", &eyeTw, &eyeTh);

						double topContentW = topIconSz.x + 4_dip + eyeTw;
						double topStartX = topBtnX + (topBtnW - topContentW) / 2.0;
						if (eyeBmp.IsOk()) {
							gc->DrawBitmap(eyeBmp, topStartX, topBtnY + (topBtnH - topIconSz.y) / 2.0, topIconSz.x, topIconSz.y);
						}
						gc->DrawText(L"预览", topStartX + topIconSz.x + 4_dip, topBtnY + (topBtnH - eyeTh) / 2.0);

						// 3. 居中「替换图片」按钮 (Primary 强调样式，无任何多余灰色背景)
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

						wxFont centerFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
						gc->SetFont(centerFont, *wxWHITE);
						double cTw = 0, cTh = 0;
						gc->GetTextExtent(L" 替换图片", &cTw, &cTh);

						double centerContentW = centerIconSz.x + 4_dip + cTw;
						double centerStartX = centerBtnX + (centerBtnW - centerContentW) / 2.0;
						if (cloudBmp.IsOk()) {
							gc->DrawBitmap(cloudBmp, centerStartX, centerBtnY + (centerBtnH - centerIconSz.y) / 2.0, centerIconSz.x, centerIconSz.y);
						}
						gc->DrawText(L" 替换图片", centerStartX + centerIconSz.x + 4_dip, centerBtnY + (centerBtnH - cTh) / 2.0);
					}
					else {
						m_previewBtnRect = wxRect();
						m_centerBtnRect = wxRect();
					}

					gc->ResetClip();

					wxPen pen(p.cardBorder, 1);
					gc->SetPen(pen);
					gc->StrokePath(path);
				}
			}
			else {
				m_previewBtnRect = wxRect();
				m_centerBtnRect = wxRect();
				wxPen pen(p.cardBorder, 2, wxPENSTYLE_SHORT_DASH);
				gc->SetPen(pen);
				gc->StrokePath(path);
			}
			});

		leftColSizer->Add(m_dropzonePanel, 1, wxEXPAND);
		contentSizer->Add(leftColSizer, 45, wxEXPAND | wxRIGHT, 12_dip);

		// Right Column: Recognized Text Card (Ratio 55%)
		m_resultCard = new CardPanel(this, L"识别文本", true);
		m_resultCard->GetTextCtrl()->SetHint(L"上传图片后，识别结果将自动显示在这里...");

		m_resultCard->AddToolIcon(1, SVG::SPEAKER, L"朗读内容", [this]() {
			if (!m_resultCard || !m_resultCard->GetTextCtrl())
				return;
			wxString text = m_resultCard->GetTextCtrl()->GetValue();
			if (text.IsEmpty())
				return;
			WinTtsHelper::GetInstance().Speak(text.ToStdWstring(), LanguageCode::AutoDetect);
			});

		m_resultCard->AddToolIcon(2, SVG::COPY, L"复制文本", [this]() {
			if (!m_resultCard)
				return;
			wxString text = m_resultCard->GetTextCtrl()->GetValue();
			if (text.IsEmpty())
				return;

			if (ClipboardHelper::SetClipboardText(text.ToUTF8().data())) {
				wxMessageBox(L"识别结果已复制到剪贴板！", L"提示",
					wxOK | wxICON_INFORMATION, this);
			}
			});

		m_resultCard->AddToolIcon(3, SVG::CLEAR, L"清空内容", [this]() {
			if (!m_resultCard)
				return;
			WinTtsHelper::GetInstance().Stop();
			m_resultCard->GetTextCtrl()->Clear();
			m_resultCard->SetCharacterCount(0);
			});

		contentSizer->Add(m_resultCard, 55, wxEXPAND);

		mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// 3. Bottom Action Bar: Recognize / Stop Buttons
		wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

		m_recognizeBtn = new CustomButton(this, wxID_ANY, L"识别", ButtonStyle::Primary, wxDefaultPosition, dip(145, 42));
		m_recognizeBtn->SetIcon(SVG::OCR, dip(16, 16), *wxWHITE);

		m_stopBtn = new CustomButton(this, wxID_ANY, L"停止", ButtonStyle::Danger, wxDefaultPosition, dip(145, 42));
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
		m_dropzonePanel->Bind(wxEVT_LEFT_DOWN, &OcrView::OnDropzoneLeftDown, this);
		m_dropzonePanel->Bind(wxEVT_ENTER_WINDOW, &OcrView::OnDropzoneMouseEnter, this);
		m_dropzonePanel->Bind(wxEVT_LEAVE_WINDOW, &OcrView::OnDropzoneMouseLeave, this);
		m_dropzonePanel->Bind(wxEVT_MOTION, &OcrView::OnDropzoneMouseMove, this);

		auto showMenuHandler = [this](wxMouseEvent& event) {
			ShowDropzoneContextMenu(event.GetPosition());
		};
		m_dropzonePanel->Bind(wxEVT_RIGHT_DOWN, showMenuHandler);
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

		if (m_resultCard && m_resultCard->GetTextCtrl()) {
			m_resultCard->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
				if (m_resultCard && m_resultCard->GetTextCtrl()) {
					wxString text = m_resultCard->GetTextCtrl()->GetValue();
					m_resultCard->SetCharacterCount(text.Length());
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
		if (m_leftControlPanel)
			m_leftControlPanel->SetBackgroundColour(palette.cardBg);
		if (m_typeLabel)
			m_typeLabel->SetForegroundColour(palette.textSecondary);
		if (m_typeChoice) {
			m_typeChoice->UpdateTheme();
		}
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

		if (m_dropzonePanel) {
			m_dropzonePanel->SetBackgroundColour(palette.cardBg);
			m_dropzonePanel->Refresh();
		}
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

	void OcrView::OpenImagePreview() {
		if (m_loadedImage.IsOk()) {
			ImagePreviewDialog dialog(this, m_loadedImage,
				L"图片预览 - " + m_imageFileName);
			dialog.ShowModal();
		}
	}

	void OcrView::OnSelectImageClicked(wxMouseEvent& WXUNUSED(event)) {
		if (!m_loadedImage.IsOk()) {
			OpenImageDialog();
		}
	}

	void OcrView::OnPreviewClicked(wxCommandEvent& WXUNUSED(event)) {
		OpenImagePreview();
	}

	void OcrView::OnReplaceClicked(wxCommandEvent& WXUNUSED(event)) {
		if (m_currentState == OcrTaskState::Recognizing)
			return;
		OpenImageDialog();
	}

	void OcrView::OnDropzoneMouseEnter(wxMouseEvent& event) {
		if (m_currentState == OcrTaskState::Recognizing) {
			event.Skip();
			return;
		}

		m_isDropzoneHovered = true;
		if (m_dropzonePanel) {
			m_dropzonePanel->Refresh();
		}
		event.Skip();
	}

	void OcrView::OnDropzoneMouseLeave(wxMouseEvent& event) {
		m_isDropzoneHovered = false;
		m_hoveredAction = DropzoneHoverAction::None;
		if (m_dropzonePanel) {
			m_dropzonePanel->SetCursor(wxCursor(wxCURSOR_HAND));
			m_dropzonePanel->Refresh();
		}
		event.Skip();
	}

	void OcrView::OnDropzoneMouseMove(wxMouseEvent& event) {
		if (m_currentState == OcrTaskState::Recognizing || !m_loadedImage.IsOk() || m_loadedImagePath.IsEmpty()) {
			event.Skip();
			return;
		}

		wxPoint pt = event.GetPosition();
		DropzoneHoverAction newAction = DropzoneHoverAction::None;
		if (m_previewBtnRect.Contains(pt)) {
			newAction = DropzoneHoverAction::Preview;
		}
		else if (m_centerBtnRect.Contains(pt)) {
			newAction = DropzoneHoverAction::Replace;
		}

		if (newAction != m_hoveredAction) {
			m_hoveredAction = newAction;
			if (m_dropzonePanel) {
				m_dropzonePanel->Refresh();
			}
		}
		event.Skip();
	}

	void OcrView::OnDropzoneLeftDown(wxMouseEvent& event) {
		if (m_currentState == OcrTaskState::Recognizing)
			return;

		if (m_loadedImage.IsOk()) {
			wxPoint pt = event.GetPosition();
			if (m_previewBtnRect.Contains(pt)) {
				OpenImagePreview();
				return;
			}
		}
		OpenImageDialog();
	}

	void OcrView::OnImageFileDropped(const wxString& filePath) {
		LoadImageFile(filePath);
	}

	void OcrView::LoadImageFile(const wxString& filePath) {
		if (!wxFileExists(filePath))
			return;

		m_loadedImagePath = filePath;
		m_imageFileName = wxFileName(filePath).GetFullName();
		m_loadedImage.LoadFile(filePath);

		UpdateDropzoneUI();
	}

	bool OcrView::PasteImageFromClipboard() {
		if (m_currentState == OcrTaskState::Recognizing)
			return false;

		if (!wxTheClipboard->Open()) {
			return false;
		}

		bool handled = false;

		// 1. 优先检查剪贴板中是否有位图图像 (如系统截图、聊天工具截图、剪切板位图)
		if (wxTheClipboard->IsSupported(wxDF_BITMAP)) {
			wxBitmapDataObject bmpData;
			if (wxTheClipboard->GetData(bmpData)) {
				wxBitmap bmp = bmpData.GetBitmap();
				if (bmp.IsOk()) {
					wxImage img = bmp.ConvertToImage();
					if (img.IsOk()) {
						m_loadedImagePath = L"[剪贴板截图]";
						m_imageFileName = L"剪贴板截图.png";
						m_loadedImage = img;
						UpdateDropzoneUI();
						handled = true;
					}
				}
			}
		}

		// 2. 检查剪贴板中是否复制了文件 (如在文件资源管理器中复制了图片文件)
		if (!handled && wxTheClipboard->IsSupported(wxDF_FILENAME)) {
			wxFileDataObject fileData;
			if (wxTheClipboard->GetData(fileData)) {
				const wxArrayString& files = fileData.GetFilenames();
				for (const auto& file : files) {
					wxString ext = wxFileName(file).GetExt().Lower();
					if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "webp" || ext == "tif" || ext == "tiff") {
						LoadImageFile(file);
						handled = true;
						break;
					}
				}
			}
		}

		wxTheClipboard->Close();
		return handled;
	}

	void OcrView::ShowDropzoneContextMenu(const wxPoint& pos) {
		if (m_currentState == OcrTaskState::Recognizing) return;

		wxMenu menu;
		menu.Append(1001, L"粘贴图片 (Ctrl+V)");
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
					wxMessageBox(L"剪贴板中未找到图像数据或图片文件！\n\n提示：您可以使用系统截图快捷键 (如 Win+Shift+S 或 Alt+A) 截图后直接按 Ctrl+V 粘贴。",
						L"提示", wxOK | wxICON_INFORMATION, this);
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
		}
		else {
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
		}
		else {
			m_recognizeBtn->Show();
			m_stopBtn->Hide();
		}
		if (m_dropzonePanel) {
			m_dropzonePanel->Refresh();
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

	void OcrView::OnRecognizeClicked(wxCommandEvent& WXUNUSED(event)) {
		if (m_currentState != OcrTaskState::Idle)
			return;

		if (!m_loadedImage.IsOk()) {
			wxMessageBox(L"请先上传、拖拽或从剪贴板粘贴一张待识别的图片！", L"提示",
				wxOK | wxICON_INFORMATION, this);
			return;
		}

		if (!m_modelManager) {
			wxMessageBox(L"服务管理器未初始化！", L"错误", wxOK | wxICON_ERROR, this);
			return;
		}

		WinTtsHelper::GetInstance().Stop();

		if (m_resultCard && m_resultCard->GetTextCtrl()) {
			m_resultCard->GetTextCtrl()->Clear();
			m_resultCard->SetCharacterCount(0);
			m_resultCard->GetTextCtrl()->SetValue(L"正在启动/检查 OCR 视觉识别引擎，请稍候...");
		}

		SetState(OcrTaskState::Recognizing);

		// 异步确保 OCR 视觉模型已装载就绪，再执行识别
		m_modelManager->EnsureModelAsync(
			TargetModelType::Ocr,
			BindUi([this](const std::string& statusMsg) {
				if (m_resultCard && m_resultCard->GetTextCtrl()) {
					m_resultCard->GetTextCtrl()->SetValue(L"正在加载 OCR 视觉模型: " + wxString::FromUTF8(statusMsg));
				}
				UpdateStatusBadge();
				}),
			BindUi([this](bool ok, const ServerStatusInfo& info) {
				if (!ok) {
					SetState(OcrTaskState::Idle);
					if (m_resultCard && m_resultCard->GetTextCtrl()) {
						m_resultCard->GetTextCtrl()->SetValue(
							L"OCR 视觉模型未就绪: " + wxString::FromUTF8(info.message) +
							L"\n\n请先前往「系统设置」检查并配置 OCR 模型与 mmproj 视觉投影器文件路径。"
						);
					}
					UpdateStatusBadge();
				}
				else {
					std::string imgPath = m_loadedImagePath.ToUTF8().data();
					std::string taskType = GetSelectedTaskType();
					DoExecuteOcr(imgPath, taskType);
				}
				})
		);
	}

	void OcrView::DoExecuteOcr(const std::string& imgPath, const std::string& taskType) {
		if (!m_modelManager) {
			SetState(OcrTaskState::Idle);
			if (m_resultCard && m_resultCard->GetTextCtrl()) {
				m_resultCard->GetTextCtrl()->SetValue(L"错误: 服务管理器未初始化");
			}
			return;
		}

		if (m_resultCard && m_resultCard->GetTextCtrl()) {
			m_resultCard->GetTextCtrl()->Clear();
			m_resultCard->SetCharacterCount(0);
		}

		// 纯内存智能等比缩放与 JPEG 压缩（完全不写入磁盘缓存）：
		// 1. 若图片尺寸过大 (如长边超过 1536px)，在内存中进行高质量双三次等比缩放；
		// 2. 统一通过 wxMemoryOutputStream 在内存中压缩为质量 88 的 JPEG 数据流；
		// 3. 内存直接 base64 编码构造 data:image/jpeg;base64,... 并送入模型，零磁盘 I/O、零临时文件。
		std::string dataUri;
		if (m_loadedImage.IsOk()) {
			int origW = m_loadedImage.GetWidth();
			int origH = m_loadedImage.GetHeight();
			constexpr int MAX_OCR_DIM = 1536;

			wxImage targetImg;
			if (origW > MAX_OCR_DIM || origH > MAX_OCR_DIM) {
				double scale = static_cast<double>(MAX_OCR_DIM) / std::max(origW, origH);
				int newW = std::max(1, static_cast<int>(origW * scale));
				int newH = std::max(1, static_cast<int>(origH * scale));
				targetImg = m_loadedImage.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
			}
			else {
				targetImg = m_loadedImage.Copy();
			}

			if (targetImg.IsOk()) {
				wxMemoryOutputStream memStream;
				targetImg.SetOption(wxIMAGE_OPTION_QUALITY, 88);
				if (targetImg.SaveFile(memStream, wxBITMAP_TYPE_JPEG)) {
					wxStreamBuffer* buf = memStream.GetOutputStreamBuffer();
					if (buf && buf->GetBufferSize() > 0) {
						std::string rawData(static_cast<const char*>(buf->GetBufferStart()), buf->GetBufferSize());
						std::string b64 = base64::encode(rawData);
						dataUri = "data:image/jpeg;base64," + b64;
					}
				}
			}
		}

		// 回退容错：若内存编码失败则使用原路径
		if (dataUri.empty() && !imgPath.empty()) {
			dataUri = imgPath;
		}

		m_modelManager->ExecuteOcrStream(
			dataUri, taskType,
			BindUi([this](const std::string& token) {
				if (!m_resultCard || !m_resultCard->GetTextCtrl()) return;
				m_resultCard->GetTextCtrl()->AppendText(wxString::FromUTF8(token));
				wxString current = m_resultCard->GetTextCtrl()->GetValue();
				m_resultCard->SetCharacterCount(current.Length());
				}),
			BindUi([this](const std::string& fullText, bool success, const std::string& error) {
				SetState(OcrTaskState::Idle);

				if (!m_resultCard || !m_resultCard->GetTextCtrl()) return;

				if (success) {
					wxString wClean = wxString::FromUTF8(fullText);
					m_resultCard->GetTextCtrl()->SetValue(wClean);
					m_resultCard->SetCharacterCount(wClean.Length());
				}
				else if (!error.empty()) {
					if (error == "已取消") {
						m_resultCard->GetTextCtrl()->AppendText(L"\n\n[⏹ OCR 识别已被用户中断]");
					}
					else {
						m_resultCard->GetTextCtrl()->SetValue(wxString::FromUTF8("识别出现提示/错误: " + error));
					}
				}
				})
		);
	}

	void OcrView::OnStopClicked(wxCommandEvent& WXUNUSED(event)) {
		if (m_modelManager) {
			m_modelManager->CancelInference();
		}
		WinTtsHelper::GetInstance().Stop();
		SetState(OcrTaskState::Idle);
	}


} // namespace LinguaAlpaca::UI
