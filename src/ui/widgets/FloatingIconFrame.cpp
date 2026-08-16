#pragma execution_character_set("utf-8")
#include "FloatingIconFrame.hpp"
#include "../theme/AppIcons.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/Theme.hpp"
#include "core/Logger.hpp"

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/graphics.h>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

    FloatingIconFrame::FloatingIconFrame(wxWindow* parent)
        : wxFrame(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
            wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_SHAPED),
        m_autoHideTimer(this) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        int iconSize = 38_dip;
        SetSize(iconSize, iconSize);

        InitUI();

        Bind(wxEVT_PAINT, &FloatingIconFrame::OnPaint, this);
        Bind(wxEVT_ENTER_WINDOW, &FloatingIconFrame::OnMouseEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &FloatingIconFrame::OnMouseLeave, this);
        Bind(wxEVT_LEFT_UP, &FloatingIconFrame::OnLeftUp, this);
        Bind(wxEVT_TIMER, &FloatingIconFrame::OnTimer, this);
    }

    void FloatingIconFrame::InitUI() {
#ifdef _WIN32
        HWND hwnd = (HWND)GetHWND();
        if (hwnd) {
            int iconSize = 38_dip;
            LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                exStyle | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);

            HRGN hRgn = CreateEllipticRgn(0, 0, iconSize, iconSize);
            SetWindowRgn(hwnd, hRgn, TRUE);
        }
#endif
    }

    void FloatingIconFrame::ShowAt(int screenX, int screenY, const std::string& selectedText) {
        m_selectedText = selectedText;
        const int iconSize = 38_dip;
        int targetX = screenX + 8_dip;
        int targetY = screenY + 10_dip;

#ifdef _WIN32
        POINT pt = { targetX, targetY };
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (hMon) {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfo(hMon, &mi)) {
                int workLeft = mi.rcWork.left;
                int workRight = mi.rcWork.right;
                int workTop = mi.rcWork.top;
                int workBottom = mi.rcWork.bottom;

                // 如果右侧超出工作区，则翻转至光标左侧
                if (targetX + iconSize > workRight) {
                    targetX = screenX - iconSize - 8_dip;
                }
                // 如果底部超出工作区，则翻转至光标上方
                if (targetY + iconSize > workBottom) {
                    targetY = screenY - iconSize - 8_dip;
                }

                // 严格钳位在当前屏幕工作区范围内，杜绝任何越界出屏
                if (targetX > workRight - iconSize - 4_dip) {
                    targetX = workRight - iconSize - 4_dip;
                }
                if (targetX < workLeft + 4_dip) {
                    targetX = workLeft + 4_dip;
                }
                if (targetY > workBottom - iconSize - 4_dip) {
                    targetY = workBottom - iconSize - 4_dip;
                }
                if (targetY < workTop + 4_dip) {
                    targetY = workTop + 4_dip;
                }
            }
        }
#else
        wxPoint pos(targetX, targetY);
        int displayIdx = wxDisplay::GetFromPoint(pos);
        if (displayIdx != wxNOT_FOUND) {
            wxDisplay display(displayIdx);
            wxRect geom = display.GetClientArea();
            if (targetX + iconSize > geom.GetRight()) {
                targetX = screenX - iconSize - 8_dip;
            }
            if (targetY + iconSize > geom.GetBottom()) {
                targetY = screenY - iconSize - 8_dip;
            }
            if (targetX > geom.GetRight() - iconSize - 4_dip) targetX = geom.GetRight() - iconSize - 4_dip;
            if (targetX < geom.GetLeft() + 4_dip) targetX = geom.GetLeft() + 4_dip;
            if (targetY > geom.GetBottom() - iconSize - 4_dip) targetY = geom.GetBottom() - iconSize - 4_dip;
            if (targetY < geom.GetTop() + 4_dip) targetY = geom.GetTop() + 4_dip;
        }
#endif

        m_currentPos = wxPoint(targetX, targetY);
        SetPosition(m_currentPos);
        SetSize(iconSize, iconSize);

#ifdef _WIN32
        HWND hwnd = (HWND)GetHWND();
        if (hwnd) {
            HRGN hRgn = CreateEllipticRgn(0, 0, iconSize, iconSize);
            SetWindowRgn(hwnd, hRgn, TRUE);
            SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY, iconSize, iconSize,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
#endif

        LOG_INFO("FloatingIconFrame", "显示浮动图标: (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ")");
        ShowWithoutActivating();
        Refresh();
        Update();

        // 启动 2.5 秒无操作自动隐藏定时器
        m_autoHideTimer.StartOnce(2500);
    }

    void FloatingIconFrame::Dismiss() {
        m_autoHideTimer.Stop();
        Hide();
    }

    void FloatingIconFrame::OnPaint(wxPaintEvent& WXUNUSED(event)) {
        wxAutoBufferedPaintDC dc(this);
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;

        ThemePalette palette = ThemeManager::GetCurrentPalette();

        wxSize sz = GetClientSize();
        double w = sz.x;
        double h = sz.y;
        if (w <= 0 || h <= 0) return;

        // 绘制高对比度圆形背景与高亮边框
        wxColour bgColor = m_isHovered ? palette.accentHover : palette.accentPrimary;
        wxColour borderColor = palette.cardBorderActive;

        gc->SetBrush(wxBrush(bgColor));
        gc->SetPen(wxPen(borderColor, 1.5));
        gc->DrawEllipse(1, 1, w - 2, h - 2);

        // 绘制居中翻译图标 (纯白高亮图标在各种背景下均极清晰)
        wxColour iconColor(255, 255, 255);
        int iconSize = static_cast<int>(w * 0.55);
        if (iconSize < 16) iconSize = 16;
        wxBitmapBundle iconBundle = IconManager::GetIconBundle(SVG::SWAP, wxSize(iconSize, iconSize), iconColor);
        wxBitmap bmp = iconBundle.GetBitmap(wxSize(iconSize, iconSize));
        if (bmp.IsOk()) {
            double ix = (w - bmp.GetScaledWidth()) / 2.0;
            double iy = (h - bmp.GetScaledHeight()) / 2.0;
            gc->DrawBitmap(bmp, ix, iy, bmp.GetScaledWidth(), bmp.GetScaledHeight());
        }
    }

    void FloatingIconFrame::OnMouseEnter(wxMouseEvent& WXUNUSED(event)) {
        m_isHovered = true;
        m_autoHideTimer.Stop();
        SetCursor(wxCursor(wxCURSOR_HAND));
        Refresh();
    }

    void FloatingIconFrame::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
        m_isHovered = false;
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Refresh();
        // 鼠标移出后 2.5 秒淡出
        m_autoHideTimer.StartOnce(2500);
    }

    void FloatingIconFrame::OnLeftUp(wxMouseEvent& WXUNUSED(event)) {
        m_autoHideTimer.Stop();
        Hide();

        if (m_onClickCallback) {
            m_onClickCallback(m_currentPos, m_selectedText);
        }
    }

    void FloatingIconFrame::OnTimer(wxTimerEvent& WXUNUSED(event)) {
        Hide();
    }

} // namespace LinguaAlpaca::UI
