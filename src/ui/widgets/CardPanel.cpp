#include "CardPanel.hpp"
#include "../theme/IconManager.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

namespace LinguaAlpaca::UI {

	CardPanel::CardPanel(wxWindow* parent, const wxString& title, bool isActiveBorder, bool enableMarkdown, wxWindowID id)
		: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
		m_title(title), m_isActiveBorder(isActiveBorder), m_isMarkdownEnabled(enableMarkdown) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		if (m_isMarkdownEnabled) {
			m_currentMode = CardViewMode::Rendered;
		}
		InitUI();
	}

	void CardPanel::InitUI() {
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		auto palette = ThemeColors::GetCurrentPalette();

		m_titleFont = ThemeFont::GetFont(FontRole::CardTitle);
		m_tabFont = ThemeFont::GetFont(FontRole::Control, true);
		m_countFont = ThemeFont::GetFont(FontRole::Control);

		sizer->AddSpacer(42_dip);

		m_contentContainerSizer = new wxBoxSizer(wxVERTICAL);

		if (m_isMarkdownEnabled) {
			m_markdownView = new MarkdownView(this, wxID_ANY);
			m_markdownView->SetOnContentChangedCallback([this](const wxString& text) {
				SetCharacterCount(text.Length());
			});
			m_contentContainerSizer->Add(m_markdownView, 1, wxEXPAND);
		} else {
			long textStyle = wxTE_MULTILINE | wxBORDER_NONE;
			if (m_isActiveBorder) {
				textStyle |= wxTE_READONLY;
			}

			m_textCtrl = new TextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, textStyle);
			m_textCtrl->SetFont(ThemeFont::GetFont(FontRole::Body));
			m_textCtrl->SetBackgroundColour(palette.cardBg);
			m_textCtrl->SetForegroundColour(m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);
			m_contentContainerSizer->Add(m_textCtrl, 1, wxEXPAND);
		}

		wxBoxSizer* contentHBox = new wxBoxSizer(wxHORIZONTAL);
		contentHBox->AddSpacer(14_dip);
		contentHBox->Add(m_contentContainerSizer, 1, wxEXPAND | wxRIGHT, 4_dip);

		sizer->Add(contentHBox, 1, wxEXPAND);
		sizer->AddSpacer(32_dip);

		SetSizer(sizer);

		Bind(wxEVT_PAINT, &CardPanel::OnPaint, this);
		Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
			Refresh();
			event.Skip();
		});
		Bind(wxEVT_MOTION, &CardPanel::OnMouseMove, this);
		Bind(wxEVT_LEAVE_WINDOW, &CardPanel::OnMouseLeave, this);
		Bind(wxEVT_LEFT_DOWN, &CardPanel::OnLeftDown, this);
	}

	TextCtrl* CardPanel::GetTextCtrl() const {
		if (m_isMarkdownEnabled && m_markdownView) {
			return m_markdownView->GetTextCtrl();
		}
		return m_textCtrl;
	}

	void CardPanel::UpdateTheme() {
		auto palette = ThemeColors::GetCurrentPalette();
		if (m_markdownView) {
			m_markdownView->UpdateTheme();
		}
		if (m_textCtrl) {
			m_textCtrl->SetBackgroundColour(palette.cardBg);
			m_textCtrl->SetForegroundColour(m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);
			m_textCtrl->Refresh();
		}
		Refresh();
	}

	void CardPanel::AddToolIcon(int id, const char* svgContent,
		const wxString& tooltip,
		std::function<void()> onClick) {
		m_tools.push_back({ id, svgContent, tooltip, onClick });
		Refresh();
	}

	void CardPanel::SetCharacterCount(size_t count) {
		if (m_charCount != count) {
			m_charCount = count;
			Refresh();
		}
	}

	void CardPanel::SetMarkdown(const std::string& markdown, const std::string& baseDir, bool preserveScroll) {
		if (m_isMarkdownEnabled && m_markdownView) {
			m_markdownView->SetMarkdown(markdown, baseDir, preserveScroll);
			SetCharacterCount(markdown.size());
			return;
		}
		if (m_textCtrl) {
			m_textCtrl->SetMarkdown(markdown, preserveScroll);
			SetCharacterCount(markdown.size());
		}
	}

	void CardPanel::SetMarkdown(const wxString& markdown, const wxString& baseDir, bool preserveScroll) {
		SetMarkdown(std::string(markdown.ToUTF8().data()),
		            std::string(baseDir.ToUTF8().data()),
		            preserveScroll);
	}

	void CardPanel::SetContent(const std::string& text, bool preserveScroll) {
		if (text.empty()) {
			Clear();
			return;
		}

		if (m_isMarkdownEnabled) {
			SetMarkdown(text, "", preserveScroll);
			return;
		}

		wxString wText = wxString::FromUTF8(text);
		if (m_textCtrl) {
			m_textCtrl->SetValue(wText, preserveScroll);
		}
		SetCharacterCount(wText.Length());
	}

	void CardPanel::SetViewMode(CardViewMode mode) {
		m_currentMode = mode;

		if (m_isMarkdownEnabled) {
			if (m_currentMode == CardViewMode::Rendered) {
				if (m_markdownView) {
					m_markdownView->SetViewMode(MarkdownViewMode::Rendered);
					m_markdownView->Show();
				}
			} else if (m_currentMode == CardViewMode::Source || m_currentMode == CardViewMode::Text) {
				if (m_markdownView) {
					m_markdownView->SetViewMode(MarkdownViewMode::Source);
					m_markdownView->Show();
				}
			}
		} else {
			m_currentMode = CardViewMode::Text;
			if (m_textCtrl) m_textCtrl->Show();
		}

		Layout();
		Refresh();
	}

	void CardPanel::Clear() {
		if (m_isMarkdownEnabled && m_markdownView) {
			m_markdownView->Clear();
		}
		if (m_textCtrl) m_textCtrl->Clear();
		SetCharacterCount(0);
		SetViewMode(m_isMarkdownEnabled ? CardViewMode::Rendered : CardViewMode::Text);
	}

	void CardPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
		wxAutoBufferedPaintDC dc(this);
		wxSize size = GetClientSize();
		if (size.x <= 0 || size.y <= 0)
			return;

		auto palette = ThemeColors::GetCurrentPalette();
		dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
		dc.Clear();

		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
		if (!gc)
			return;

		// 1. 绘制圆角卡片背景与边框
		double radius = 12.0_dip;
		gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));

		wxColour borderColor = m_isActiveBorder ? palette.cardBorderActive : palette.cardBorder;
		double borderWidth = m_isActiveBorder ? 1.5 : 1.0;
		gc->SetPen(gc->CreatePen(wxPen(borderColor, borderWidth)));
		gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

		// 2. 绘制 Card Header Title
		gc->SetFont(m_titleFont, m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);
		gc->DrawText(m_title, 16_dip, 12_dip);

		double tw = 0, th = 0;
		gc->GetTextExtent(m_title, &tw, &th);

		// 3. 绘制顶部视图切换 Tab 按钮
		if (m_isMarkdownEnabled) {
			double tabX = 16_dip + tw + 16_dip;
			int tabY = 8_dip;
			int tabW = 68_dip;
			int tabH = 24_dip;
			double tabRadius = 5.0_dip;

			m_renderedTabRect = wxRect(static_cast<int>(tabX), tabY, tabW, tabH);
			m_sourceTabRect = wxRect(static_cast<int>(tabX + tabW + 6_dip), tabY, tabW, tabH);

			wxSize tabIconSz = dip(13, 13);

			// 绘制 [排版] Tab
			bool isRenderedActive = (m_currentMode == CardViewMode::Rendered);
			wxColour rendBg = isRenderedActive ? palette.bannerBg : ((m_hoverTab == 0) ? palette.bannerBg : palette.cardBg);
			wxColour rendBorder = isRenderedActive ? palette.cardBorderActive : palette.cardBorder;
			wxColour rendText = isRenderedActive ? palette.accentPrimary : palette.textSecondary;

			gc->SetBrush(gc->CreateBrush(wxBrush(rendBg)));
			gc->SetPen(gc->CreatePen(wxPen(rendBorder, 1.0)));
			gc->DrawRoundedRectangle(m_renderedTabRect.x, m_renderedTabRect.y, m_renderedTabRect.width, m_renderedTabRect.height, tabRadius);

			gc->SetFont(m_tabFont, rendText);
			wxString rendLabel = L"排版";
			double ltw = 0, lth = 0;
			gc->GetTextExtent(rendLabel, &ltw, &lth);

			wxBitmapBundle rendBundle = IconManager::GetIconBundle(SVG::LAYOUT, wxSize(13, 13), rendText);
			wxBitmap rendBmp = rendBundle.GetBitmap(tabIconSz);

			double totalRendW = tabIconSz.x + 4_dip + ltw;
			double rendStartX = m_renderedTabRect.x + (tabW - totalRendW) / 2.0;

			if (rendBmp.IsOk()) {
				gc->DrawBitmap(rendBmp, rendStartX, m_renderedTabRect.y + (tabH - tabIconSz.y) / 2.0, tabIconSz.x, tabIconSz.y);
			}
			gc->DrawText(rendLabel, rendStartX + tabIconSz.x + 4_dip, m_renderedTabRect.y + (tabH - lth) / 2.0);

			// 绘制 [源码] Tab
			bool isSourceActive = (m_currentMode == CardViewMode::Source || m_currentMode == CardViewMode::Text);
			wxColour srcBg = isSourceActive ? palette.bannerBg : ((m_hoverTab == 1) ? palette.bannerBg : palette.cardBg);
			wxColour srcBorder = isSourceActive ? palette.cardBorderActive : palette.cardBorder;
			wxColour srcText = isSourceActive ? palette.accentPrimary : palette.textSecondary;

			gc->SetBrush(gc->CreateBrush(wxBrush(srcBg)));
			gc->SetPen(gc->CreatePen(wxPen(srcBorder, 1.0)));
			gc->DrawRoundedRectangle(m_sourceTabRect.x, m_sourceTabRect.y, m_sourceTabRect.width, m_sourceTabRect.height, tabRadius);

			gc->SetFont(m_tabFont, srcText);
			wxString srcLabel = L"源码";
			gc->GetTextExtent(srcLabel, &ltw, &lth);

			wxBitmapBundle srcBundle = IconManager::GetIconBundle(SVG::TEXT, wxSize(13, 13), srcText);
			wxBitmap srcBmp = srcBundle.GetBitmap(tabIconSz);

			double totalSrcW = tabIconSz.x + 4_dip + ltw;
			double srcStartX = m_sourceTabRect.x + (tabW - totalSrcW) / 2.0;

			if (srcBmp.IsOk()) {
				gc->DrawBitmap(srcBmp, srcStartX, m_sourceTabRect.y + (tabH - tabIconSz.y) / 2.0, tabIconSz.x, tabIconSz.y);
			}
			gc->DrawText(srcLabel, srcStartX + tabIconSz.x + 4_dip, m_sourceTabRect.y + (tabH - lth) / 2.0);
		} else {
			m_renderedTabRect = wxRect();
			m_sourceTabRect = wxRect();
		}

		// 4. 绘制右侧 SVG 工具图标
		int toolX = size.x - 24_dip;
		wxSize toolIconSz = dip(16, 16);

		for (int i = (int)m_tools.size() - 1; i >= 0; --i) {
			toolX -= toolIconSz.x;
			wxColour toolColor = (m_hoverToolIndex == i) ? palette.accentPrimary : palette.textSecondary;
			wxBitmapBundle bundle = IconManager::GetIconBundle(m_tools[i].svgContent, wxSize(16, 16), toolColor);
			wxBitmap bmp = bundle.GetBitmap(toolIconSz);
			if (bmp.IsOk()) {
				gc->DrawBitmap(bmp, toolX, 12_dip, toolIconSz.x, toolIconSz.y);
			}
			toolX -= 12_dip;
		}

		// 5. 绘制 Footer 字符数统计
		gc->SetFont(m_countFont, palette.textSecondary);
		wxString countText = wxString::Format(L"%zu 字符", m_charCount);

		double cw, ch;
		gc->GetTextExtent(countText, &cw, &ch);
		gc->DrawText(countText, size.x - cw - 16_dip, size.y - ch - 10_dip);
	}

	void CardPanel::OnMouseMove(wxMouseEvent& event) {
		int x = event.GetX();
		int y = event.GetY();
		int sizeX = GetClientSize().x;

		int oldHoverTool = m_hoverToolIndex;
		int oldHoverTab = m_hoverTab;

		m_hoverToolIndex = -1;
		m_hoverTab = -1;

		// 检查顶部 Tab 悬浮
		if (m_isMarkdownEnabled) {
			if (m_renderedTabRect.Contains(x, y)) {
				m_hoverTab = 0;
			} else if (m_sourceTabRect.Contains(x, y)) {
				m_hoverTab = 1;
			}
		}

		// 检查右侧工具图标悬浮
		if (y >= 8_dip && y <= 32_dip) {
			int toolX = sizeX - 24_dip;
			int iconW = 16_dip;
			for (int i = (int)m_tools.size() - 1; i >= 0; --i) {
				toolX -= iconW;
				if (x >= toolX - 4_dip && x <= toolX + iconW + 4_dip) {
					m_hoverToolIndex = i;
					break;
				}
				toolX -= 12_dip;
			}
		}

		if (m_hoverToolIndex != -1) {
			SetCursor(wxCursor(wxCURSOR_HAND));
			SetToolTip(m_tools[m_hoverToolIndex].tooltip);
		} else if (m_hoverTab != -1) {
			SetCursor(wxCursor(wxCURSOR_HAND));
			UnsetToolTip();
		} else {
			SetCursor(wxCursor(wxCURSOR_ARROW));
			UnsetToolTip();
		}

		if (oldHoverTool != m_hoverToolIndex || oldHoverTab != m_hoverTab) {
			Refresh();
		}

		event.Skip();
	}

	void CardPanel::OnMouseLeave(wxMouseEvent& event) {
		if (m_hoverToolIndex != -1 || m_hoverTab != -1) {
			m_hoverToolIndex = -1;
			m_hoverTab = -1;
			SetCursor(wxCursor(wxCURSOR_ARROW));
			UnsetToolTip();
			Refresh();
		}
		event.Skip();
	}

	void CardPanel::OnLeftDown(wxMouseEvent& event) {
		int x = event.GetX();
		int y = event.GetY();

		if (m_isMarkdownEnabled) {
			if (m_renderedTabRect.Contains(x, y)) {
				SetViewMode(CardViewMode::Rendered);
				return;
			}
			if (m_sourceTabRect.Contains(x, y)) {
				SetViewMode(CardViewMode::Source);
				return;
			}
		}

		if (m_hoverToolIndex >= 0 && m_hoverToolIndex < (int)m_tools.size()) {
			if (m_tools[m_hoverToolIndex].onClick) {
				m_tools[m_hoverToolIndex].onClick();
			}
		}

		event.Skip();
	}

} // namespace LinguaAlpaca::UI
