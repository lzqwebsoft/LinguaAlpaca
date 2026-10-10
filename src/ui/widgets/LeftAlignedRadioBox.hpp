#pragma once
#include <wx/wx.h>
#include <wx/control.h>
#include <wx/radiobox.h>
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include "../theme/Theme.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/AppIcons.hpp"
#include "../theme/PlatformThemeHelper.hpp"
#include <algorithm>
#include <vector>

namespace LinguaAlpaca::UI {

/**
 * @brief 现代化卡片式单选选项组 (Modern Card-based Radio Option Group)
 *
 * 彻底替换原生 Windows 95/XP 时代的带凹陷边框的 wxRadioBox，采用当今主流操作系统
 * （macOS Sequoia / Windows 11 Fluent 2 / Linear / Raycast）的现代卡片选项组设计风格。
 *
 * 特性亮点：
 *  1. 雅致去框化分组标题：采用加粗的层级标题标签，告别割裂文字的金属凹槽线；
 *  2. 交互式圆角卡片项：单列呈现纵向独立卡片行，多列呈现均分胶囊/芯片卡片网格；
 *  3. 高清抗锯齿单选指示钮：外环主色光圈 + 居中实心圆点，Retina/High-DPI 缩放平滑无锯齿；
 *  4. 智能语义图标增强：自动识别日间 (Sun) / 夜间 (Moon) / 系统 (Settings) 外观图标；
 *  5. 层级排版说明弱化：自动识别中文/英文字符括号内补充描述（如“（按住辅助按键划词触发）”），
 *     分离主标题（强调色）与注释说明（次级灰），避免整行大段文字视觉疲劳；
 *  6. 细腻交互反馈：拥有 Hover 浮动提亮、Hand 光标、Active 点击触觉以及 Tab/方向键无障碍键控；
 *  7. 100% 动态主题切换与 Per-Monitor V2 DPI 纯矢自适应；
 *  8. 100% 向下兼容 wxRadioBox 原生接口与 wxEVT_RADIOBOX 事件。
 */
class LeftAlignedRadioBox : public wxControl {
public:
    LeftAlignedRadioBox() = default;

    LeftAlignedRadioBox(wxWindow* parent,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos = wxDefaultPosition,
                        const wxSize& size = wxDefaultSize,
                        const wxArrayString& choices = wxArrayString(),
                        int majorDimension = 0,
                        long style = wxRA_SPECIFY_COLS,
                        const wxValidator& val = wxDefaultValidator,
                        const wxString& name = wxASCII_STR(wxRadioBoxNameStr))
        : wxControl(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS | wxTAB_TRAVERSAL, val, name)
        , m_title(title)
        , m_majorDimension(majorDimension)
        , m_style(style) {
        for (size_t i = 0; i < choices.size(); ++i) {
            m_items.push_back(choices[i]);
            m_itemEnabled.push_back(true);
            m_itemShown.push_back(true);
        }
        if (!m_items.empty()) {
            m_selectedIndex = 0;
        }
        InitUI();
        SetInitialSize(DoGetBestSize());
    }

    LeftAlignedRadioBox(wxWindow* parent,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos,
                        const wxSize& size,
                        int n,
                        const wxString choices[],
                        int majorDimension = 0,
                        long style = wxRA_SPECIFY_COLS,
                        const wxValidator& val = wxDefaultValidator,
                        const wxString& name = wxASCII_STR(wxRadioBoxNameStr))
        : wxControl(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS | wxTAB_TRAVERSAL, val, name)
        , m_title(title)
        , m_majorDimension(majorDimension)
        , m_style(style) {
        for (int i = 0; i < n; ++i) {
            m_items.push_back(choices[i]);
            m_itemEnabled.push_back(true);
            m_itemShown.push_back(true);
        }
        if (!m_items.empty()) {
            m_selectedIndex = 0;
        }
        InitUI();
        SetInitialSize(DoGetBestSize());
    }

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxArrayString& choices = wxArrayString(),
                int majorDimension = 0,
                long style = wxRA_SPECIFY_COLS,
                const wxValidator& val = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRadioBoxNameStr)) {
        if (!wxControl::Create(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS | wxTAB_TRAVERSAL, val, name))
            return false;
        m_title = title;
        m_majorDimension = majorDimension;
        m_style = style;
        m_items.clear();
        m_itemEnabled.clear();
        m_itemShown.clear();
        for (size_t i = 0; i < choices.size(); ++i) {
            m_items.push_back(choices[i]);
            m_itemEnabled.push_back(true);
            m_itemShown.push_back(true);
        }
        m_selectedIndex = m_items.empty() ? wxNOT_FOUND : 0;
        InitUI();
        SetInitialSize(DoGetBestSize());
        return true;
    }

    // --- wxRadioBox 兼容公共接口 ---

    int GetSelection() const {
        return m_selectedIndex;
    }

    void SetSelection(int n) {
        SetSelectionInternal(n, false);
    }

    wxString GetStringSelection() const {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
            return m_items[m_selectedIndex];
        }
        return wxEmptyString;
    }

    bool SetStringSelection(const wxString& s) {
        int idx = FindString(s);
        if (idx != wxNOT_FOUND) {
            SetSelection(idx);
            return true;
        }
        return false;
    }

    unsigned int GetCount() const {
        return static_cast<unsigned int>(m_items.size());
    }

    int FindString(const wxString& s, bool bCase = false) const {
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (s.IsSameAs(m_items[i], bCase)) {
                return static_cast<int>(i);
            }
        }
        return wxNOT_FOUND;
    }

    wxString GetString(unsigned int n) const {
        if (n < m_items.size()) {
            return m_items[n];
        }
        return wxEmptyString;
    }

    void SetString(unsigned int n, const wxString& text) {
        if (n < m_items.size()) {
            m_items[n] = text;
            InvalidateBestSize();
            RecalculateItemRects();
            Refresh();
        }
    }

    wxString GetItemLabel(unsigned int n) const { return GetString(n); }
    void SetItemLabel(unsigned int n, const wxString& text) { SetString(n, text); }

    void SetItemIcon(unsigned int item, const char* svgIcon) {
        if (item >= m_itemIcons.size()) {
            m_itemIcons.resize(item + 1, nullptr);
        }
        m_itemIcons[item] = svgIcon;
        Refresh();
    }

    const char* GetItemIcon(unsigned int item) const {
        if (item < m_itemIcons.size() && m_itemIcons[item] != nullptr) {
            return m_itemIcons[item];
        }
        if (item < m_items.size()) {
            return GetAutoIconForLabel(m_items[item]);
        }
        return nullptr;
    }

    bool Enable(bool enable = true) override {
        bool res = wxControl::Enable(enable);
        Refresh();
        return res;
    }

    bool Enable(unsigned int item, bool enable = true) {
        if (item < m_itemEnabled.size()) {
            m_itemEnabled[item] = enable;
            Refresh();
            return true;
        }
        return false;
    }

    bool IsItemEnabled(unsigned int item) const {
        if (item < m_itemEnabled.size()) {
            return m_itemEnabled[item] && IsThisEnabled();
        }
        return false;
    }

    bool Show(bool show = true) override {
        bool res = wxControl::Show(show);
        return res;
    }

    bool Show(unsigned int item, bool show = true) {
        if (item < m_itemShown.size()) {
            m_itemShown[item] = show;
            InvalidateBestSize();
            RecalculateItemRects();
            Refresh();
            return true;
        }
        return false;
    }

    bool IsItemShown(unsigned int item) const {
        if (item < m_itemShown.size()) {
            return m_itemShown[item];
        }
        return false;
    }

    int GetColumnCount() const {
        if (m_items.empty()) return 1;
        int count = static_cast<int>(m_items.size());
        if (m_style & wxRA_SPECIFY_ROWS) {
            int rows = m_majorDimension > 0 ? m_majorDimension : 1;
            return (count + rows - 1) / rows;
        } else {
            int cols = m_majorDimension > 0 ? m_majorDimension : 1;
            return std::min(cols, count);
        }
    }

    int GetRowCount() const {
        if (m_items.empty()) return 1;
        int count = static_cast<int>(m_items.size());
        if (m_style & wxRA_SPECIFY_ROWS) {
            int rows = m_majorDimension > 0 ? m_majorDimension : 1;
            return std::min(rows, count);
        } else {
            int cols = m_majorDimension > 0 ? m_majorDimension : 1;
            return (count + cols - 1) / cols;
        }
    }

    wxString GetLabel() const override { return m_title; }
    void SetLabel(const wxString& label) override {
        m_title = label;
        InvalidateBestSize();
        RecalculateItemRects();
        Refresh();
    }

    bool SetFont(const wxFont& font) override {
        m_customFont = font;
        bool res = wxControl::SetFont(font);
        InvalidateBestSize();
        RecalculateItemRects();
        Refresh();
        return res;
    }

    bool SetForegroundColour(const wxColour& colour) override {
        m_titleColour = colour;
        Refresh();
        return true;
    }

    void UpdateTheme() {
        Refresh();
    }

    void AdjustAlignment() {
        RecalculateItemRects();
        Refresh();
    }

    bool AcceptsFocus() const override { return IsThisEnabled(); }
    bool AcceptsFocusFromKeyboard() const override { return IsThisEnabled(); }

protected:
    wxSize DoGetBestSize() const override {
        int titleH = 0;
        if (!m_title.empty()) {
            wxFont titleFont = ThemeFont::MakeFont(10, wxFONTWEIGHT_BOLD);
            int tw = 0, th = 0;
            GetTextExtent(m_title, &tw, &th, nullptr, nullptr, &titleFont);
            titleH = th + 8_dip;
        }
        int numRows = GetRowCount();
        int itemH = 42_dip;
        int rowGap = 8_dip;
        int totalH = titleH + numRows * itemH + std::max(0, numRows - 1) * rowGap;
        return wxSize(100_dip, std::max(20_dip, totalH));
    }

private:
    void InitUI() {
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        Bind(wxEVT_PAINT, &LeftAlignedRadioBox::OnPaint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
        Bind(wxEVT_SIZE, &LeftAlignedRadioBox::OnSize, this);
        Bind(wxEVT_MOTION, &LeftAlignedRadioBox::OnMouseMove, this);
        Bind(wxEVT_LEAVE_WINDOW, &LeftAlignedRadioBox::OnMouseLeave, this);
        Bind(wxEVT_LEFT_DOWN, &LeftAlignedRadioBox::OnLeftDown, this);
        Bind(wxEVT_KEY_DOWN, &LeftAlignedRadioBox::OnKeyDown, this);
        Bind(wxEVT_SET_FOCUS, &LeftAlignedRadioBox::OnSetFocus, this);
        Bind(wxEVT_KILL_FOCUS, &LeftAlignedRadioBox::OnKillFocus, this);
        Bind(wxEVT_MOUSEWHEEL, [](wxMouseEvent& event) {
            event.Skip();
        });
    }

    static const char* GetAutoIconForLabel(const wxString& label) {
        if (label.Find(L"浅色") != wxNOT_FOUND || label.Find(L"日间") != wxNOT_FOUND) {
            return SVG::SUN;
        }
        if (label.Find(L"暗色") != wxNOT_FOUND || label.Find(L"夜间") != wxNOT_FOUND || label.Find(L"深色") != wxNOT_FOUND) {
            return SVG::MOON;
        }
        if (label.Find(L"跟随系统") != wxNOT_FOUND || label.Find(L"系统外观") != wxNOT_FOUND) {
            return SVG::SETTINGS;
        }
        return nullptr;
    }

    int HitTestItem(const wxPoint& pt) const {
        for (size_t i = 0; i < m_itemRects.size(); ++i) {
            if (m_itemRects[i].Contains(pt)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void SetSelectionInternal(int n, bool sendEvent) {
        if (n == wxNOT_FOUND || n < 0) {
            if (m_selectedIndex != wxNOT_FOUND) {
                m_selectedIndex = wxNOT_FOUND;
                Refresh();
            }
            return;
        }
        if (n >= 0 && n < static_cast<int>(m_items.size())) {
            if (m_selectedIndex != n) {
                m_selectedIndex = n;
                Refresh();
                if (sendEvent) {
                    wxCommandEvent event(wxEVT_RADIOBOX, GetId());
                    event.SetEventObject(this);
                    event.SetInt(m_selectedIndex);
                    event.SetString(m_items[m_selectedIndex]);
                    ProcessWindowEvent(event);
                }
            }
        }
    }

    void OnSize(wxSizeEvent& event) {
        RecalculateItemRects();
        Refresh();
        event.Skip();
    }

    void OnMouseMove(wxMouseEvent& event) {
        int hit = HitTestItem(event.GetPosition());
        if (hit != m_hoverIndex) {
            m_hoverIndex = hit;
            SetCursor((hit >= 0 && IsItemEnabled(static_cast<unsigned int>(hit))) ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
            Refresh();
        }
    }

    void OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            SetCursor(wxCursor(wxCURSOR_ARROW));
            Refresh();
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        SetFocus();
        int hit = HitTestItem(event.GetPosition());
        if (hit >= 0 && hit < static_cast<int>(m_items.size()) && IsItemEnabled(static_cast<unsigned int>(hit))) {
            if (m_selectedIndex != hit) {
                SetSelectionInternal(hit, true);
            }
        }
    }

    void OnKeyDown(wxKeyEvent& event) {
        int key = event.GetKeyCode();
        int count = static_cast<int>(m_items.size());
        if (count <= 0) {
            event.Skip();
            return;
        }
        if (key == WXK_UP || key == WXK_LEFT) {
            int next = (m_selectedIndex <= 0) ? count - 1 : m_selectedIndex - 1;
            while (next != m_selectedIndex && !IsItemEnabled(next)) {
                next = (next <= 0) ? count - 1 : next - 1;
            }
            if (next != m_selectedIndex && IsItemEnabled(next)) {
                SetSelectionInternal(next, true);
            }
        } else if (key == WXK_DOWN || key == WXK_RIGHT) {
            int next = (m_selectedIndex + 1) % count;
            while (next != m_selectedIndex && !IsItemEnabled(next)) {
                next = (next + 1) % count;
            }
            if (next != m_selectedIndex && IsItemEnabled(next)) {
                SetSelectionInternal(next, true);
            }
        } else {
            event.Skip();
        }
    }

    void OnSetFocus(wxFocusEvent& event) {
        m_hasFocus = true;
        Refresh();
        event.Skip();
    }

    void OnKillFocus(wxFocusEvent& event) {
        m_hasFocus = false;
        Refresh();
        event.Skip();
    }

    void RecalculateItemRects() {
        wxSize sz = GetClientSize();
        if (sz.x <= 0 || m_items.empty()) {
            m_itemRects.clear();
            return;
        }

        int titleH = 0;
        if (!m_title.empty()) {
            wxFont titleFont = ThemeFont::MakeFont(10, wxFONTWEIGHT_BOLD);
            int tw = 0, th = 0;
            GetTextExtent(m_title, &tw, &th, nullptr, nullptr, &titleFont);
            titleH = th + 8_dip;
        }

        size_t count = m_items.size();
        m_itemRects.resize(count);

        int numCols = GetColumnCount();
        int numRows = GetRowCount();
        int itemH = 42_dip;
        int rowGap = 8_dip;
        int colGap = 10_dip;
        int availW = sz.x;

        if (numCols <= 1) {
            for (size_t i = 0; i < count; ++i) {
                int y = titleH + static_cast<int>(i) * (itemH + rowGap);
                m_itemRects[i] = wxRect(0, y, availW, itemH);
            }
        } else {
            int totalColGap = (numCols - 1) * colGap;
            int colW = std::max(1, (availW - totalColGap) / numCols);

            for (size_t i = 0; i < count; ++i) {
                int col = (m_style & wxRA_SPECIFY_ROWS) ? (static_cast<int>(i) / numRows) : (static_cast<int>(i) % numCols);
                int row = (m_style & wxRA_SPECIFY_ROWS) ? (static_cast<int>(i) % numRows) : (static_cast<int>(i) / numCols);

                int x = col * (colW + colGap);
                int w = (col == numCols - 1) ? (availW - x) : colW;
                int y = titleH + row * (itemH + rowGap);

                m_itemRects[i] = wxRect(x, y, w, itemH);
            }
        }
    }

    void OnPaint(wxPaintEvent& WXUNUSED(event)) {
        wxAutoBufferedPaintDC dc(this);
        wxSize size = GetClientSize();
        if (size.x <= 0 || size.y <= 0)
            return;

        auto palette = ThemeColors::GetCurrentPalette();
        wxColour parentBg = GetParent() ? GetParent()->GetBackgroundColour() : palette.cardBg;
        dc.SetBackground(wxBrush(parentBg));
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc)
            return;

        // 1. 绘制组标题 (去边框化现代分组标签，绝非 Windows 95 压线槽)
        int titleH = 0;
        if (!m_title.empty()) {
            wxFont titleFont = ThemeFont::MakeFont(10, wxFONTWEIGHT_BOLD);
            wxColour titleColour = m_titleColour.IsOk() ? m_titleColour : palette.textPrimary;
            gc->SetFont(titleFont, titleColour);
            gc->DrawText(m_title, 0, 0);
            double tw = 0, th = 0;
            gc->GetTextExtent(m_title, &tw, &th);
            titleH = static_cast<int>(th) + 8_dip;
        }

        if (m_itemRects.size() != m_items.size()) {
            RecalculateItemRects();
        }

        int numCols = GetColumnCount();
        double cardRadius = 8.0_dip;
        wxFont normalFont = m_customFont.IsOk() ? m_customFont : ThemeFont::GetFont(FontRole::Control, false);
        wxFont boldFont = normalFont;
        boldFont.SetWeight(wxFONTWEIGHT_BOLD);

        // 如果外层背景是卡片白/灰，选项默认背景采用窗口底色，形成自然内嵌立体感
        wxColour unselectedCardBg = (parentBg == palette.windowBg) ? palette.cardBg : palette.windowBg;

        for (size_t i = 0; i < m_items.size(); ++i) {
            if (i >= m_itemRects.size() || !m_itemShown[i])
                continue;

            const wxRect& rect = m_itemRects[i];
            if (rect.width <= 0 || rect.height <= 0)
                continue;

            bool isSelected = (m_selectedIndex == static_cast<int>(i));
            bool isHovered = (m_hoverIndex == static_cast<int>(i) && !isSelected);
            bool isEnabled = IsItemEnabled(static_cast<unsigned int>(i)) && IsThisEnabled();

            // 2. 绘制卡片背景与边框
            if (!isEnabled) {
                gc->SetBrush(gc->CreateBrush(wxBrush(parentBg)));
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.cardBorder).Width(1.0)));
            } else if (isSelected) {
                // 选中态：极具现代感的柔和强调色填充 + 醒目的品牌主色描边
                gc->SetBrush(gc->CreateBrush(wxBrush(palette.bannerBg)));
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.accentPrimary).Width(1.8)));
            } else if (isHovered) {
                // 悬停态：微光交互反馈
                gc->SetBrush(gc->CreateBrush(wxBrush(palette.bannerBg)));
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.accentHover).Width(1.2)));
            } else {
                // 常态：内嵌底色 + 极细雅致卡片边框
                gc->SetBrush(gc->CreateBrush(wxBrush(unselectedCardBg)));
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.cardBorder).Width(1.0)));
            }
            gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5, rect.width - 1.0, rect.height - 1.0, cardRadius);

            // 3. 计算单选圆钮 (Radio Indicator) 几何参数
            double centerY = std::floor(rect.y + rect.height / 2.0);
            int outerRadius = 9_dip;
            int innerRadius = 4_dip;
            int radioSz = outerRadius * 2;
            int radioGap = 10_dip;

            const wxString& fullLabel = m_items[i];
            const char* svgIcon = GetItemIcon(static_cast<unsigned int>(i));

            // 解析文字：支持将括号内补充说明作为次要文本弱化渲染
            wxString mainText = fullLabel;
            wxString descText;
            size_t parenPos = fullLabel.find(L"（");
            if (parenPos == wxString::npos) parenPos = fullLabel.find('(');
            if (parenPos != wxString::npos && numCols == 1) {
                descText = fullLabel.substr(parenPos);
                mainText = fullLabel.substr(0, parenPos);
            }

            gc->SetFont(isSelected ? boldFont : normalFont, palette.textPrimary);
            double mainW = 0, mainH = 0;
            gc->GetTextExtent(mainText, &mainW, &mainH);
            double descW = 0, descH = 0;
            if (!descText.empty()) {
                gc->SetFont(normalFont, palette.textSecondary);
                gc->GetTextExtent(descText, &descW, &descH);
            }

            int iconSz = (svgIcon != nullptr) ? 16_dip : 0;
            int iconGap = (svgIcon != nullptr) ? 8_dip : 0;

            double totalContentW = radioSz + radioGap + (iconSz > 0 ? (iconSz + iconGap) : 0) + mainW + descW;

            double startX = 0;
            if (numCols > 1 && rect.width > totalContentW + 20_dip) {
                // 多列水平排列时，内容整体居中对称
                startX = std::floor(rect.x + (rect.width - totalContentW) / 2.0);
            } else {
                // 单列纵向排列时，整齐左对齐
                startX = rect.x + 14_dip;
            }

            // 4. 绘制抗锯齿圆环单选按钮 (Radio Button)
            // 采用精准同心圆 Path (AddCircle) 构造，绝对保证内圆点与外圆环像素级同心居中
            double radioCenterX = startX + outerRadius;
            startX += radioSz + radioGap;

            wxGraphicsPath outerPath = gc->CreatePath();
            outerPath.AddCircle(radioCenterX, centerY, outerRadius);

            if (!isEnabled) {
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.textSecondary).Width(1.2)));
                gc->SetBrush(gc->CreateBrush(wxBrush(parentBg)));
                gc->DrawPath(outerPath);
            } else if (isSelected) {
                // 外圈主色光环
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.accentPrimary).Width(1.8)));
                gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
                gc->DrawPath(outerPath);

                // 内实心圆点 (严格共用 radioCenterX 与 centerY 几何中心)
                wxGraphicsPath innerPath = gc->CreatePath();
                innerPath.AddCircle(radioCenterX, centerY, innerRadius);
                gc->SetBrush(gc->CreateBrush(wxBrush(palette.accentPrimary)));
                gc->FillPath(innerPath);
            } else if (isHovered) {
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.accentPrimary).Width(1.4)));
                gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
                gc->DrawPath(outerPath);
            } else {
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(palette.cardBorder).Width(1.4)));
                gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
                gc->DrawPath(outerPath);
            }

            // 5. 绘制可选 SVG 语义图标 (如日间 Sun / 夜间 Moon / 系统 Settings)
            if (svgIcon != nullptr) {
                wxColour iconCol = isSelected ? palette.accentPrimary : (isHovered ? palette.textPrimary : palette.textSecondary);
                wxBitmapBundle bundle = IconManager::GetIconBundle(svgIcon, wxSize(16, 16), iconCol);
                wxBitmap bmp = bundle.GetBitmap(dip(16, 16));
                if (bmp.IsOk()) {
                    double iconY = centerY - bmp.GetHeight() / 2.0;
                    gc->DrawBitmap(bmp, startX, iconY, bmp.GetWidth(), bmp.GetHeight());
                }
                startX += iconSz + iconGap;
            }

            // 6. 绘制主要文本
            wxColour mainTextCol;
            if (!isEnabled) {
                mainTextCol = palette.textSecondary;
            } else if (isSelected) {
                mainTextCol = palette.textPrimary;
            } else if (isHovered) {
                mainTextCol = palette.textPrimary;
            } else {
                mainTextCol = (numCols > 1) ? palette.textSecondary : palette.textPrimary;
            }

            gc->SetFont(isSelected ? boldFont : normalFont, mainTextCol);
            double textY = centerY - mainH / 2.0;
            gc->DrawText(mainText, startX, textY);
            startX += mainW;

            // 7. 绘制次要说明文本（括号内说明，弱化文字提高排版层级感）
            if (!descText.empty()) {
                gc->SetFont(normalFont, palette.textSecondary);
                double descY = centerY - descH / 2.0;
                gc->DrawText(descText, startX, descY);
            }
        }
    }

    wxString m_title;
    int m_majorDimension{0};
    long m_style{wxRA_SPECIFY_COLS};
    std::vector<wxString> m_items;
    std::vector<const char*> m_itemIcons;
    std::vector<bool> m_itemEnabled;
    std::vector<bool> m_itemShown;
    std::vector<wxRect> m_itemRects;

    int m_selectedIndex{wxNOT_FOUND};
    int m_hoverIndex{-1};
    bool m_hasFocus{false};

    wxFont m_customFont;
    wxColour m_titleColour;
};

} // namespace LinguaAlpaca::UI
