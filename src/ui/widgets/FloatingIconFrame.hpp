#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/timer.h>
#include <functional>
#include <string>

namespace LinguaAlpaca::UI {

using FloatingIconClickCallback = std::function<void(const wxPoint& pos, const std::string& selectedText)>;

class FloatingIconFrame : public wxFrame {
public:
    explicit FloatingIconFrame(wxWindow* parent = nullptr);
    ~FloatingIconFrame() override = default;

    // 在指定屏幕坐标展示悬浮图标
    void ShowAt(int screenX, int screenY, const std::string& selectedText);

    // 隐藏悬浮图标
    void Dismiss();

    // 设置点击回调
    void SetClickCallback(FloatingIconClickCallback callback) {
        m_onClickCallback = std::move(callback);
    }

private:
    void InitUI();
    void RenderLayeredWindow(int screenX, int screenY);
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnTimer(wxTimerEvent& event);

    wxPoint m_currentPos;
    std::string m_selectedText;
    bool m_isHovered{false};
    bool m_isDragging{false};
    wxPoint m_dragStartMousePos;
    wxPoint m_dragStartFramePos;

    wxTimer m_autoHideTimer;
    FloatingIconClickCallback m_onClickCallback;

#ifdef _WIN32
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};

} // namespace LinguaAlpaca::UI
