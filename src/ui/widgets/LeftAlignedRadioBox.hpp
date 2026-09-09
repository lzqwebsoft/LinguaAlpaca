#pragma once
#include <wx/wx.h>
#include <wx/radiobox.h>
#include "../theme/Theme.hpp"
#include "../theme/PlatformThemeHelper.hpp"
#include <algorithm>
#include <vector>

namespace LinguaAlpaca::UI {

/**
 * @brief wxRadioBox with left-aligned choices and unclipped labels.
 *
 * On macOS Cocoa, wxWidgets hardcodes (width - sz.x) / 2 centering of radio buttons
 * and calculates insufficient widths based on fallback font metrics, which causes
 * labels to be truncated with ellipses (e.g. "浅...").
 * LeftAlignedRadioBox intercepts sizing to align radio buttons to the left margin,
 * propagates fonts to child buttons, and expands their widths according to native
 * intrinsic content size so text is never omitted or truncated.
 */
class LeftAlignedRadioBox : public wxRadioBox {
public:
    using wxRadioBox::wxRadioBox;

    bool SetFont(const wxFont& font) override {
        bool res = wxRadioBox::SetFont(font);
        for (wxWindowList::compatibility_iterator node = GetChildren().GetFirst(); node; node = node->GetNext()) {
            wxWindow* child = node->GetData();
            if (child) {
                child->SetFont(font);
            }
        }
#ifdef __WXOSX__
        AlignChildrenLeft();
#endif
        return res;
    }

    void UpdateTheme() {
        PlatformThemeHelper::ApplyControlTheme(this, ThemeColors::GetCurrentPalette());
    }

    void AdjustAlignment() {
#ifdef __WXOSX__
        AlignChildrenLeft();
#endif
    }

protected:
    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override {
        wxRadioBox::DoSetSize(x, y, width, height, sizeFlags);
#ifdef __WXOSX__
        AlignChildrenLeft();
#endif
    }

private:
#ifdef __WXOSX__
    void AlignChildrenLeft() {
        const wxWindowList& children = GetChildren();
        if (children.empty())
            return;

        int numCols = GetColumnCount();
        if (numCols <= 0)
            numCols = 1;
        int numRows = GetRowCount();
        if (numRows <= 0)
            numRows = (int)children.size();

        int leftMargin = 16_dip;
        int gapX = 20_dip; // 选项之间的横向舒适间距

        // 1. 计算每一列所需的最大宽度（使用子控件原生 GetBestSize 彻底防止省略截断）
        std::vector<int> colWidths(numCols, 0);
        int itemIndex = 0;
        for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext(), ++itemIndex) {
            wxWindow* child = node->GetData();
            if (!child)
                continue;

            int col = (GetWindowStyle() & wxRA_SPECIFY_ROWS) ? (itemIndex / numRows) : (itemIndex % numCols);
            wxSize best = child->GetBestSize();
            // 额外增加 16_dip 裕量，确保任何中文字体或缩放比下绝不出现省略号截断
            int neededW = best.x + 16_dip;
            if (col >= 0 && col < numCols) {
                colWidths[col] = std::max(colWidths[col], neededW);
            }
        }

        // 2. 计算各列在横向上的起始 X 坐标
        std::vector<int> colStartX(numCols, leftMargin);
        for (int c = 1; c < numCols; ++c) {
            colStartX[c] = colStartX[c - 1] + colWidths[c - 1] + gapX;
        }

        // 3. 应用尺寸与坐标至每个子单选按钮
        itemIndex = 0;
        for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext(), ++itemIndex) {
            wxWindow* child = node->GetData();
            if (!child)
                continue;

            int col = (GetWindowStyle() & wxRA_SPECIFY_ROWS) ? (itemIndex / numRows) : (itemIndex % numCols);
            int posX = (col >= 0 && col < numCols) ? colStartX[col] : leftMargin;
            int width = (col >= 0 && col < numCols) ? colWidths[col] : (child->GetBestSize().x + 16_dip);

            wxPoint currentPos = child->GetPosition();
            wxSize currentSize = child->GetSize();
            int height = std::max(currentSize.y, child->GetBestSize().y);

            child->SetSize(posX, currentPos.y, width, height);
        }
    }
#endif
};

} // namespace LinguaAlpaca::UI
