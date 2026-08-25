#include "TextView.hpp"
#include "core/WinTtsHelper.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include <wx/clipbrd.h>

namespace LinguaAlpaca::UI {

	TextView::TextView(
		wxWindow* parent,
		std::shared_ptr<ModelManager> modelManager,
		wxWindowID id)
		: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
		m_modelManager(std::move(modelManager)) {
		InitUI();

		m_healthTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
			UpdateStatusBadge();
			});
		m_healthTimer.Start(1500);
	}

	TextView::~TextView() {
		if (m_healthTimer.IsRunning()) {
			m_healthTimer.Stop();
		}
		WinTtsHelper::GetInstance().Stop();
	}

	void TextView::InitUI() {
		auto palette = ThemeColors::GetCurrentPalette();
		SetBackgroundColour(palette.windowBg);

		wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

		// 1. 顶栏：SVG 图标 + 标题 (文本翻译) + 状态标签 (● 监听中)
		wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

		wxBitmapBundle titleBundle = IconManager::GetIconBundle(SVG::TEXT, dip(22, 22), palette.accentPrimary);
		wxStaticBitmap* titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

		m_titleText = new wxStaticText(this, wxID_ANY, L"文本翻译");
		m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_titleText->SetForegroundColour(palette.textPrimary);

		m_statusBadge = new StatusBadge(this);

		headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
		headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
		headerSizer->AddStretchSpacer(1);
		headerSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

		mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20_dip);

		// 2. 语言选择条
		m_langPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 52_dip), wxBORDER_NONE);
		m_langPanel->SetBackgroundColour(palette.cardBg);

		wxBoxSizer* langSizer = new wxBoxSizer(wxHORIZONTAL);
		m_langSelector = new LanguageBar(m_langPanel);
		langSizer->Add(m_langSelector, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12_dip);
		m_langPanel->SetSizer(langSizer);

		mainSizer->Add(m_langPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// 3. 原文与译文卡片区
		wxBoxSizer* cardsSizer = new wxBoxSizer(wxHORIZONTAL);
		m_sourceCard = new CardPanel(this, L"原文", false);
		m_sourceCard->AddToolIcon(1, SVG::SPEAKER, L"朗读原文", [this]() {
			if (!m_sourceCard || !m_sourceCard->GetTextCtrl()) return;
			wxString text = m_sourceCard->GetTextCtrl()->GetValue();
			if (text.IsEmpty()) return;
			LanguageCode srcLang = m_langSelector ? m_langSelector->GetSourceLanguage() : LanguageCode::AutoDetect;
			WinTtsHelper::GetInstance().Speak(text.ToStdWstring(), srcLang);
		});
		m_sourceCard->AddToolIcon(2, SVG::PASTE, L"粘贴文本", [this]() {
			if (wxTheClipboard->Open()) {
				if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
					wxTextDataObject data;
					wxTheClipboard->GetData(data);
					m_sourceCard->GetTextCtrl()->SetValue(data.GetText());
				}
				wxTheClipboard->Close();
			}
		});
		m_sourceCard->AddToolIcon(3, SVG::CLEAR, L"清空原文", [this]() {
			WinTtsHelper::GetInstance().Stop();
			m_sourceCard->GetTextCtrl()->Clear();
			m_sourceCard->SetCharacterCount(0);
		});

		m_sourceCard->GetTextCtrl()->SetValue(L"Hello, welcome to LinguaAlpaca Translator!");
		m_sourceCard->SetCharacterCount(43);

		m_targetCard = new CardPanel(this, L"译文", true);
		m_targetCard->AddToolIcon(1, SVG::SPEAKER, L"朗读译文", [this]() {
			if (!m_targetCard || !m_targetCard->GetTextCtrl()) return;
			wxString text = m_targetCard->GetTextCtrl()->GetValue();
			if (text.IsEmpty()) return;
			LanguageCode tgtLang = m_langSelector ? m_langSelector->GetTargetLanguage() : LanguageCode::Chinese;
			WinTtsHelper::GetInstance().Speak(text.ToStdWstring(), tgtLang);
		});
		m_targetCard->AddToolIcon(2, SVG::COPY, L"复制译文", [this]() {
			wxString text = m_targetCard->GetTextCtrl()->GetValue();
			if (!text.IsEmpty() && wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(text));
				wxTheClipboard->Close();
			}
		});

		m_targetCard->GetTextCtrl()->SetValue(L"你好，欢迎使用灵驼译！");
		m_targetCard->SetCharacterCount(11);

		cardsSizer->Add(m_sourceCard, 1, wxEXPAND | wxRIGHT, 12_dip);
		cardsSizer->Add(m_targetCard, 1, wxEXPAND | wxLEFT, 12_dip);

		mainSizer->Add(cardsSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// 4. 底部操作按钮栏
		wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

		m_translateBtn = new CustomButton(this, wxID_ANY, L"翻译", ButtonStyle::Primary, wxDefaultPosition, dip(110, 42));
		m_translateBtn->SetIcon(SVG::TRANSLATE, dip(16, 16), *wxWHITE);

		m_stopBtn = new CustomButton(this, wxID_ANY, L"中断翻译", ButtonStyle::Danger, wxDefaultPosition, dip(130, 42));
		m_stopBtn->SetIcon(SVG::STOP, dip(16, 16), *wxWHITE);

		m_clearBtn = new CustomButton(this, wxID_ANY, L"清空", ButtonStyle::Secondary, wxDefaultPosition, dip(100, 42));
		m_clearBtn->SetIcon(SVG::CLEAR, dip(16, 16));

		m_swapBtn = new CustomButton(this, wxID_ANY, L"交换", ButtonStyle::Secondary, wxDefaultPosition, dip(100, 42));
		m_swapBtn->SetIcon(SVG::SWAP, dip(16, 16));

		m_copyBtn = new CustomButton(this, wxID_ANY, L"复制译文", ButtonStyle::Green, wxDefaultPosition, dip(120, 42));
		m_copyBtn->SetIcon(SVG::COPY, dip(16, 16), *wxWHITE);

		m_stopBtn->Hide();

		bottomSizer->Add(m_translateBtn, 0, wxRIGHT, 12_dip);
		bottomSizer->Add(m_stopBtn, 0, wxRIGHT, 12_dip);
		bottomSizer->Add(m_clearBtn, 0);
		bottomSizer->AddStretchSpacer(1);
		bottomSizer->Add(m_swapBtn, 0, wxRIGHT, 12_dip);
		bottomSizer->Add(m_copyBtn, 0);

		mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		SetSizer(mainSizer);
		Layout();

		// 事件绑定
		m_translateBtn->Bind(wxEVT_BUTTON, &TextView::OnTranslateClicked, this);
		m_stopBtn->Bind(wxEVT_BUTTON, &TextView::OnStopClicked, this);
		m_clearBtn->Bind(wxEVT_BUTTON, &TextView::OnClearClicked, this);
		m_swapBtn->Bind(wxEVT_BUTTON, &TextView::OnSwapClicked, this);
		m_copyBtn->Bind(wxEVT_BUTTON, &TextView::OnCopyTargetClicked, this);
		m_sourceCard->GetTextCtrl()->Bind(wxEVT_TEXT, &TextView::OnSourceTextChanged, this);

		UpdateStatusBadge();
	}

	void TextView::UpdateStatusBadge() {
		if (!m_statusBadge)
			return;

		ServerStatusInfo info;
		if (m_modelManager) {
			info = m_modelManager->GetHealthStatus(TargetModelType::Translation);
		}

		wxString label;
		switch (info.state) {
		case ServerHealthState::Ready:
			label = L"●  监听中";
			break;
		case ServerHealthState::Loading:
			label = L"●  正在加载模型...";
			break;
		case ServerHealthState::Unconfigured:
			label = L"●  翻译模型未配置";
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

	void TextView::UpdateTheme() {
		auto palette = ThemeColors::GetCurrentPalette();
		SetBackgroundColour(palette.windowBg);

		if (m_titleText)
			m_titleText->SetForegroundColour(palette.textPrimary);

		if (m_langPanel)
			m_langPanel->SetBackgroundColour(palette.cardBg);

		if (m_langSelector)
			m_langSelector->UpdateTheme();
		if (m_sourceCard)
			m_sourceCard->UpdateTheme();
		if (m_targetCard)
			m_targetCard->UpdateTheme();

		if (m_translateBtn)
			m_translateBtn->Refresh();
		if (m_stopBtn)
			m_stopBtn->Refresh();
		if (m_clearBtn)
			m_clearBtn->Refresh();
		if (m_swapBtn)
			m_swapBtn->Refresh();
		if (m_copyBtn)
			m_copyBtn->Refresh();

		UpdateStatusBadge();
		Refresh();
	}

	void TextView::OnTranslateClicked(wxCommandEvent& WXUNUSED(event)) {
		std::string text = m_sourceCard->GetTextCtrl()->GetValue().ToUTF8().data();
		LanguageCode srcLang = m_langSelector->GetSourceLanguage();
		LanguageCode tgtLang = m_langSelector->GetTargetLanguage();

		if (text.empty()) {
			m_targetCard->GetTextCtrl()->Clear();
			m_targetCard->SetCharacterCount(0);
			return;
		}

		// 1. 清空译文框准备流式追加
		m_targetCard->GetTextCtrl()->Clear();
		m_targetCard->SetCharacterCount(0);

		// 2. 切换 UI 状态：显示红色的 [⏹ 中断翻译] 按钮，禁用 [▶ 翻译] 按钮
		m_stopBtn->Show();
		m_translateBtn->Disable();
		Layout();

		TranslationTask task(text, srcLang, tgtLang);

		// 3. 直接通过 ModelManager 发起异步流式翻译
		if (!m_modelManager) {
			m_stopBtn->Hide();
			m_translateBtn->Enable();
			Layout();
			m_targetCard->GetTextCtrl()->SetValue(L"模型服务管理器未初始化");
			return;
		}

		m_modelManager->ExecuteTranslationStream(
			task,
			// Token 流式接收回调 (打字机效果)
			BindUi([this](const std::string& token) {
				if (!m_targetCard || !m_targetCard->GetTextCtrl()) return;
				m_targetCard->GetTextCtrl()->AppendText(wxString::FromUTF8(token));
				wxString current = m_targetCard->GetTextCtrl()->GetValue();
				m_targetCard->SetCharacterCount(current.Length());
			}),
			// 翻译完成/被中断回调
			BindUi([this](bool success, const std::string& fullText, const std::string& error) {
				if (m_stopBtn) m_stopBtn->Hide();
				if (m_translateBtn) m_translateBtn->Enable();
				Layout();

				if (!m_targetCard || !m_targetCard->GetTextCtrl()) return;

				if (success) {
					wxString wClean = wxString::FromUTF8(fullText);
					m_targetCard->GetTextCtrl()->SetValue(wClean);
					m_targetCard->SetCharacterCount(wClean.Length());
				}
				else if (!error.empty()) {
					if (error == "已取消") {
						m_targetCard->GetTextCtrl()->AppendText(L"\n\n[⏹ 翻译已被用户中断]");
					}
					else {
						m_targetCard->GetTextCtrl()->SetValue(wxString::FromUTF8("翻译出错: " + error));
					}
				}
			})
		);
	}

	void TextView::OnStopClicked(wxCommandEvent& WXUNUSED(event)) {
		if (m_modelManager) {
			m_modelManager->CancelInference();
		}
		WinTtsHelper::GetInstance().Stop();
		m_stopBtn->Hide();
		m_translateBtn->Enable();
		Layout();
	}

	void TextView::OnClearClicked(wxCommandEvent& WXUNUSED(event)) {
		if (m_modelManager) {
			m_modelManager->CancelInference();
		}
		WinTtsHelper::GetInstance().Stop();
		m_stopBtn->Hide();
		m_translateBtn->Enable();
		Layout();

		m_sourceCard->GetTextCtrl()->Clear();
		m_targetCard->GetTextCtrl()->Clear();
		m_sourceCard->SetCharacterCount(0);
		m_targetCard->SetCharacterCount(0);
	}

	void TextView::OnCopyTargetClicked(wxCommandEvent& WXUNUSED(event)) {
		wxString text = m_targetCard->GetTextCtrl()->GetValue();
		if (text.IsEmpty())
			return;

		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(text));
			wxTheClipboard->Close();
			wxMessageBox(L"译文已成功复制到剪贴板！", L"提示",
				wxOK | wxICON_INFORMATION, this);
		}
	}

	void TextView::OnSwapClicked(wxCommandEvent& WXUNUSED(event)) {
		WinTtsHelper::GetInstance().Stop();
		if (m_langSelector) {
			m_langSelector->SwapLanguages();
		}

		wxString srcText = m_sourceCard->GetTextCtrl()->GetValue();
		wxString targetText = m_targetCard->GetTextCtrl()->GetValue();

		m_sourceCard->GetTextCtrl()->SetValue(targetText);
		m_targetCard->GetTextCtrl()->SetValue(srcText);

		m_sourceCard->SetCharacterCount(targetText.Length());
		m_targetCard->SetCharacterCount(srcText.Length());
	}

	void TextView::OnSourceTextChanged(wxCommandEvent& WXUNUSED(event)) {
		wxString text = m_sourceCard->GetTextCtrl()->GetValue();
		m_sourceCard->SetCharacterCount(text.Length());
	}

} // namespace LinguaAlpaca::UI
