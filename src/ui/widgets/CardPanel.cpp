#include "CardPanel.hpp"
#include "../theme/IconManager.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

namespace LinguaAlpaca::UI {

	CardPanel::CardPanel(wxWindow* parent, const wxString& title, bool isActiveBorder, wxWindowID id)
		: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
		m_title(title), m_isActiveBorder(isActiveBorder) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		InitUI();
	}

	void CardPanel::InitUI() {
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		auto palette = ThemeColors::GetCurrentPalette();

		m_titleFont = wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
		m_tabFont = wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
		m_countFont = wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");

		sizer->AddSpacer(42_dip);

		long textStyle = wxTE_MULTILINE | wxBORDER_NONE;
		if (m_isActiveBorder) {
			textStyle |= wxTE_READONLY;
		}

		m_textCtrl = new TextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, textStyle);
		m_textCtrl->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_textCtrl->SetBackgroundColour(palette.cardBg);
		m_textCtrl->SetForegroundColour(m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);

		m_tableView = new CustomTableView(this, wxID_ANY);
		m_tableView->Hide();

		m_contentContainerSizer = new wxBoxSizer(wxVERTICAL);
		m_contentContainerSizer->Add(m_textCtrl, 1, wxEXPAND);
		m_contentContainerSizer->Add(m_tableView, 1, wxEXPAND);

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

	void CardPanel::UpdateTheme() {
		auto palette = ThemeColors::GetCurrentPalette();
		if (m_textCtrl) {
			m_textCtrl->SetBackgroundColour(palette.cardBg);
			m_textCtrl->SetForegroundColour(m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);
			m_textCtrl->Refresh();
		}
		if (m_tableView) {
			m_tableView->UpdateTheme();
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

	void CardPanel::SetContent(const std::string& text) {
		if (text.empty()) {
			Clear();
			return;
		}

		wxString wText = wxString::FromUTF8(text);
		if (m_textCtrl) {
			m_textCtrl->SetValue(wText);
		}
		SetCharacterCount(wText.Length());

		if (TableParser::IsTableFormat(text)) {
			TableData table = TableParser::Parse(text);
			if (!table.IsEmpty() && (table.RowCount() > 0 || !table.headers.empty())) {
				std::string md = TableParser::ToMarkdown(table);
				if (m_textCtrl) {
					wxString wMd = wxString::FromUTF8(md);
					m_textCtrl->SetValue(wMd);
					SetCharacterCount(wMd.Length());
				}
				SetTableData(table);
				SetViewMode(CardViewMode::Table);
				return;
			}
		}

		m_hasTableData = false;
		m_cachedTableData.Clear();
		SetViewMode(CardViewMode::Text);
	}

	void CardPanel::SetTableData(const TableData& table) {
		m_cachedTableData = table;
		m_hasTableData = !table.IsEmpty();
		if (m_tableView) {
			m_tableView->SetTableData(table);
		}
		if (m_hasTableData) {
			if (m_textCtrl && m_textCtrl->GetValue().IsEmpty()) {
				std::string md = TableParser::ToMarkdown(table);
				wxString wMd = wxString::FromUTF8(md);
				m_textCtrl->SetValue(wMd);
				SetCharacterCount(wMd.Length());
			}
		}
		Refresh();
	}

	void CardPanel::SetViewMode(CardViewMode mode) {
		m_currentMode = mode;
		if (m_currentMode == CardViewMode::Table && m_hasTableData) {
			if (m_textCtrl) m_textCtrl->Hide();
			if (m_tableView) m_tableView->Show();
		} else {
			m_currentMode = CardViewMode::Text;
			if (m_tableView) m_tableView->Hide();
			if (m_textCtrl) m_textCtrl->Show();
		}
		Layout();
		Refresh();
	}

	void CardPanel::Clear() {
		if (m_textCtrl) m_textCtrl->Clear();
		if (m_tableView) m_tableView->Clear();
		m_hasTableData = false;
		m_cachedTableData.Clear();
		SetCharacterCount(0);
		SetViewMode(CardViewMode::Text);
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

		// 3. 当存在表格数据时，绘制多视图切换 Tab 按钮
		if (m_hasTableData) {
			double tabX = 16_dip + tw + 16_dip;
			int tabY = 8_dip;
			int tabW = 72_dip;
			int tabH = 24_dip;
			double tabRadius = 5.0_dip;

			m_tableTabRect = wxRect(static_cast<int>(tabX), tabY, tabW, tabH);
			m_textTabRect = wxRect(static_cast<int>(tabX + tabW + 6_dip), tabY, tabW, tabH);

			wxSize tabIconSz = dip(13, 13);

			// 绘制表格 Tab
			bool isTableActive = (m_currentMode == CardViewMode::Table);
			wxColour tableBg = isTableActive ? palette.bannerBg : ((m_hoverTab == 0) ? palette.bannerBg : palette.cardBg);
			wxColour tableBorder = isTableActive ? palette.cardBorderActive : palette.cardBorder;
			wxColour tableText = isTableActive ? palette.accentPrimary : palette.textSecondary;

			gc->SetBrush(gc->CreateBrush(wxBrush(tableBg)));
			gc->SetPen(gc->CreatePen(wxPen(tableBorder, 1.0)));
			gc->DrawRoundedRectangle(m_tableTabRect.x, m_tableTabRect.y, m_tableTabRect.width, m_tableTabRect.height, tabRadius);

			gc->SetFont(m_tabFont, tableText);
			wxString tableLabel = L"表格";
			double ltw = 0, lth = 0;
			gc->GetTextExtent(tableLabel, &ltw, &lth);

			wxBitmapBundle tableBundle = IconManager::GetIconBundle(SVG::TABLE, wxSize(13, 13), tableText);
			wxBitmap tableBmp = tableBundle.GetBitmap(tabIconSz);

			double totalTableW = tabIconSz.x + 4_dip + ltw;
			double tableStartX = m_tableTabRect.x + (tabW - totalTableW) / 2.0;

			if (tableBmp.IsOk()) {
				gc->DrawBitmap(tableBmp, tableStartX, m_tableTabRect.y + (tabH - tabIconSz.y) / 2.0, tabIconSz.x, tabIconSz.y);
			}
			gc->DrawText(tableLabel, tableStartX + tabIconSz.x + 4_dip, m_tableTabRect.y + (tabH - lth) / 2.0);

			// 绘制文本 Tab
			bool isTextActive = (m_currentMode == CardViewMode::Text);
			wxColour textBg = isTextActive ? palette.bannerBg : ((m_hoverTab == 1) ? palette.bannerBg : palette.cardBg);
			wxColour textBorder = isTextActive ? palette.cardBorderActive : palette.cardBorder;
			wxColour textText = isTextActive ? palette.accentPrimary : palette.textSecondary;

			gc->SetBrush(gc->CreateBrush(wxBrush(textBg)));
			gc->SetPen(gc->CreatePen(wxPen(textBorder, 1.0)));
			gc->DrawRoundedRectangle(m_textTabRect.x, m_textTabRect.y, m_textTabRect.width, m_textTabRect.height, tabRadius);

			gc->SetFont(m_tabFont, textText);
			wxString textLabel = L"文本";
			gc->GetTextExtent(textLabel, &ltw, &lth);

			wxBitmapBundle textBundle = IconManager::GetIconBundle(SVG::TEXT, wxSize(13, 13), textText);
			wxBitmap textBmp = textBundle.GetBitmap(tabIconSz);

			double totalTextW = tabIconSz.x + 4_dip + ltw;
			double textStartX = m_textTabRect.x + (tabW - totalTextW) / 2.0;

			if (textBmp.IsOk()) {
				gc->DrawBitmap(textBmp, textStartX, m_textTabRect.y + (tabH - tabIconSz.y) / 2.0, tabIconSz.x, tabIconSz.y);
			}
			gc->DrawText(textLabel, textStartX + tabIconSz.x + 4_dip, m_textTabRect.y + (tabH - lth) / 2.0);
		} else {
			m_tableTabRect = wxRect();
			m_textTabRect = wxRect();
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

		// 5. 绘制 Footer 字符数与行列表格统计
		gc->SetFont(m_countFont, palette.textSecondary);

		wxString countText;
		if (m_currentMode == CardViewMode::Table && m_hasTableData) {
			countText = wxString::Format(L"%zu 行 × %zu 列 (%zu 字符)",
				m_cachedTableData.RowCount(), m_cachedTableData.ColCount(), m_charCount);
		} else {
			countText = wxString::Format(L"%zu 字符", m_charCount);
		}

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
		if (m_hasTableData) {
			if (m_tableTabRect.Contains(x, y)) {
				m_hoverTab = 0;
			} else if (m_textTabRect.Contains(x, y)) {
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

		if (oldHoverTool != m_hoverToolIndex || oldHoverTab != m_hoverTab) {
			if (m_hoverToolIndex != -1 || m_hoverTab != -1) {
				SetCursor(wxCursor(wxCURSOR_HAND));
			} else {
				SetCursor(wxCursor(wxCURSOR_DEFAULT));
			}

			if (m_hoverToolIndex >= 0 && m_hoverToolIndex < (int)m_tools.size()) {
				SetToolTip(m_tools[m_hoverToolIndex].tooltip);
			} else {
				UnsetToolTip();
			}
			Refresh();
		}
	}

	void CardPanel::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
		if (m_hoverToolIndex != -1 || m_hoverTab != -1) {
			m_hoverToolIndex = -1;
			m_hoverTab = -1;
			SetCursor(wxCursor(wxCURSOR_DEFAULT));
			UnsetToolTip();
			Refresh();
		}
	}

	void CardPanel::OnLeftDown(wxMouseEvent& event) {
		int x = event.GetX();
		int y = event.GetY();

		if (m_hasTableData) {
			if (m_tableTabRect.Contains(x, y)) {
				SetViewMode(CardViewMode::Table);
				return;
			} else if (m_textTabRect.Contains(x, y)) {
				SetViewMode(CardViewMode::Text);
				return;
			}
		}

		if (m_hoverToolIndex >= 0 && m_hoverToolIndex < (int)m_tools.size()) {
			if (m_tools[m_hoverToolIndex].onClick) {
				m_tools[m_hoverToolIndex].onClick();
			}
		}
	}

} // namespace LinguaAlpaca::UI
