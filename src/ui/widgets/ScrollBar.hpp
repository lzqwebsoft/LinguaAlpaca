#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/timer.h>

namespace LinguaAlpaca::UI {

class TextCtrl;

/**
 * @brief 自定义现代化细条圆角滑动条控件 (ScrollBar)
 * 
 * 特性：
 * - 仅在鼠标滑动、拖拽或悬停时动态显示，静止后自动隐藏 (Auto-Hide)
 * - 胶囊状圆角滑块，悬停/拖拽时具有强调色高亮反馈
 * - 支持滚轮滚动、拖拽、轨道点击（Page Up / Page Down）
 */
class ScrollBar : public wxWindow {
public:
    using ScrollCallback = std::function<void(int line)>;

    explicit ScrollBar(wxWindow* parent, ScrollCallback onScroll);
    explicit ScrollBar(TextCtrl* parentTextCtrl);
    ~ScrollBar() override;

    void SetScrollParams(int firstVisibleLine, int visibleLines, int totalLines);
    void NotifyActivity();

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnTimer(wxTimerEvent& event);
    void DoScrollToLine(int line);

    TextCtrl* m_parentTextCtrl{nullptr};
    ScrollCallback m_scrollCallback;
    wxTimer m_hideTimer;

    int m_firstVisibleLine{0};
    int m_visibleLines{1};
    int m_totalLines{1};
    bool m_needed{false};
    bool m_isVisible{false};

    bool m_isHovered{false};
    bool m_isDragging{false};
    int m_dragStartMouseY{0};
    int m_dragStartFirstLine{0};
};

} // namespace LinguaAlpaca::UI
