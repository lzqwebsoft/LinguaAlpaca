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
#include <wx/msw/wrapgdip.h>
#endif

namespace LinguaAlpaca::UI {

    FloatingIconFrame::FloatingIconFrame(wxWindow* parent)
        : wxFrame(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
            wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE),
        m_autoHideTimer(this) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        int iconSize = 38_dip;
        SetSize(iconSize, iconSize);

        InitUI();

        Bind(wxEVT_PAINT, &FloatingIconFrame::OnPaint, this);
        Bind(wxEVT_ENTER_WINDOW, &FloatingIconFrame::OnMouseEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &FloatingIconFrame::OnMouseLeave, this);
        Bind(wxEVT_LEFT_DOWN, &FloatingIconFrame::OnLeftDown, this);
        Bind(wxEVT_MOTION, &FloatingIconFrame::OnMouseMove, this);
        Bind(wxEVT_LEFT_UP, &FloatingIconFrame::OnLeftUp, this);
        Bind(wxEVT_TIMER, &FloatingIconFrame::OnTimer, this);
    }

    void FloatingIconFrame::InitUI() {
#ifdef _WIN32
        HWND hwnd = (HWND)GetHWND();
        if (hwnd) {
            LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                exStyle | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }
#endif
    }

    void FloatingIconFrame::RenderLayeredWindow(int screenX, int screenY) {
#ifdef _WIN32
        HWND hwnd = (HWND)GetHWND();
        if (!hwnd) return;

        const int iconSize = 38_dip;
        int w = iconSize;
        int h = iconSize;
        if (w <= 0 || h <= 0) return;

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // Top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvBits = nullptr;
        HDC hdcScreen = ::GetDC(NULL);
        HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
        HBITMAP hBmp = ::CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        HGDIOBJ hOldBmp = ::SelectObject(hdcMem, hBmp);

        if (pvBits) {
            memset(pvBits, 0, w * h * 4);

            {
                Gdiplus::Bitmap memBmp(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)pvBits);
                Gdiplus::Graphics g(&memBmp);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

                ThemePalette palette = ThemeManager::GetCurrentPalette();

                float pad = 1.5f;
                float diam = (float)w - 2.0f * pad;
                Gdiplus::RectF circleRect(pad, pad, diam, diam);

                // 1. 绘制平滑抗锯齿基底圆角卡片背景 (适配亮暗主题)
                wxColour bgCol = palette.cardBg;
                Gdiplus::SolidBrush bgBrush(Gdiplus::Color(252, bgCol.Red(), bgCol.Green(), bgCol.Blue()));
                g.FillEllipse(&bgBrush, circleRect);

                // 2. 加载并高质量抗锯齿绘制应用 Logo 图标 (logo.png)
                wxImage logoImg = IconManager::GetAppLogoImage();
                if (logoImg.IsOk()) {
                    int imgW = logoImg.GetWidth();
                    int imgH = logoImg.GetHeight();
                    Gdiplus::Bitmap srcBmp(imgW, imgH, PixelFormat32bppARGB);
                    Gdiplus::BitmapData bmpData;
                    Gdiplus::Rect r(0, 0, imgW, imgH);
                    if (srcBmp.LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData) == Gdiplus::Ok) {
                        const unsigned char* rgb = logoImg.GetData();
                        const unsigned char* alpha = logoImg.HasAlpha() ? logoImg.GetAlpha() : nullptr;
                        unsigned char* dst = (unsigned char*)bmpData.Scan0;
                        for (int y = 0; y < imgH; ++y) {
                            unsigned char* row = dst + y * bmpData.Stride;
                            for (int x = 0; x < imgW; ++x) {
                                int srcIdx = (y * imgW + x);
                                unsigned char a = alpha ? alpha[srcIdx] : 255;
                                unsigned char red = rgb[srcIdx * 3];
                                unsigned char green = rgb[srcIdx * 3 + 1];
                                unsigned char blue = rgb[srcIdx * 3 + 2];
                                row[x * 4 + 0] = blue;
                                row[x * 4 + 1] = green;
                                row[x * 4 + 2] = red;
                                row[x * 4 + 3] = a;
                            }
                        }
                        srcBmp.UnlockBits(&bmpData);

                        float imgPad = m_isHovered ? 2.5_dip : 3.5_dip;
                        Gdiplus::RectF imgRect(imgPad, imgPad, (float)w - 2.0f * imgPad, (float)h - 2.0f * imgPad);
                        g.DrawImage(&srcBmp, imgRect);
                    }
                }

                // 3. 绘制平滑抗锯齿边缘光晕与边框
                if (m_isHovered) {
                    wxColour accent = palette.accentPrimary;
                    Gdiplus::SolidBrush hoverGlow(Gdiplus::Color(30, accent.Red(), accent.Green(), accent.Blue()));
                    g.FillEllipse(&hoverGlow, circleRect);

                    Gdiplus::Pen hoverPen(Gdiplus::Color(230, accent.Red(), accent.Green(), accent.Blue()), 1.8f);
                    g.DrawEllipse(&hoverPen, circleRect);
                } else {
                    wxColour borderCol = palette.cardBorder;
                    Gdiplus::Pen borderPen(Gdiplus::Color(160, borderCol.Red(), borderCol.Green(), borderCol.Blue()), 1.2f);
                    g.DrawEllipse(&borderPen, circleRect);
                }
            }

            POINT ptSrc = { 0, 0 };
            POINT ptDst = { screenX, screenY };
            SIZE size = { w, h };
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        }

        ::SelectObject(hdcMem, hOldBmp);
        ::DeleteObject(hBmp);
        ::DeleteDC(hdcMem);
        ::ReleaseDC(NULL, hdcScreen);
#else
        Refresh();
#endif
    }

    void FloatingIconFrame::ShowAt(int screenX, int screenY, const std::string& selectedText) {
        m_selectedText = selectedText;
        m_isHovered = false;
        m_isDragging = false;
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
        RenderLayeredWindow(targetX, targetY);
        HWND hwnd = (HWND)GetHWND();
        if (hwnd) {
            ::SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY, iconSize, iconSize,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
#endif

        LOG_INFO("FloatingIconFrame", "显示浮动图标: (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ")");
        ShowWithoutActivating();
#ifndef _WIN32
        Refresh();
        Update();
#endif

        // 启动 2.5 秒无操作自动隐藏定时器
        m_autoHideTimer.StartOnce(2500);
    }

    void FloatingIconFrame::Dismiss() {
        if (HasCapture()) {
            ReleaseMouse();
        }
        m_isDragging = false;
        m_autoHideTimer.Stop();
        Hide();
    }

    void FloatingIconFrame::OnPaint(wxPaintEvent& WXUNUSED(event)) {
#ifndef _WIN32
        wxAutoBufferedPaintDC dc(this);
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;

        wxSize sz = GetClientSize();
        double w = sz.x;
        double h = sz.y;
        if (w <= 0 || h <= 0) return;

        wxBitmapBundle logoBundle = IconManager::GetAppLogoBundle(sz);
        wxBitmap bmp = logoBundle.GetBitmap(sz);
        if (bmp.IsOk()) {
            if (m_isHovered) {
                ThemePalette palette = ThemeManager::GetCurrentPalette();
                gc->SetBrush(wxBrush(wxColour(palette.accentPrimary.Red(), palette.accentPrimary.Green(), palette.accentPrimary.Blue(), 40)));
                gc->SetPen(wxPen(palette.accentPrimary, 1.5));
                gc->DrawEllipse(1, 1, w - 2, h - 2);

                double pad = 1.0_dip;
                gc->DrawBitmap(bmp, pad, pad, w - 2 * pad, h - 2 * pad);
            } else {
                gc->DrawBitmap(bmp, 0, 0, w, h);
            }
        }
#endif
    }

    void FloatingIconFrame::OnMouseEnter(wxMouseEvent& WXUNUSED(event)) {
        if (m_isDragging) return;
        m_isHovered = true;
        m_autoHideTimer.Stop();
        SetCursor(wxCursor(wxCURSOR_HAND));
#ifdef _WIN32
        RenderLayeredWindow(m_currentPos.x, m_currentPos.y);
#else
        Refresh();
#endif
    }

    void FloatingIconFrame::OnMouseLeave(wxMouseEvent& event) {
        if (m_isDragging || (HasCapture() && event.LeftIsDown())) {
            return;
        }
        m_isHovered = false;
        SetCursor(wxCursor(wxCURSOR_ARROW));
#ifdef _WIN32
        RenderLayeredWindow(m_currentPos.x, m_currentPos.y);
#else
        Refresh();
#endif
        // 鼠标移出后 2.5 秒淡出
        m_autoHideTimer.StartOnce(2500);
    }

    void FloatingIconFrame::OnLeftDown(wxMouseEvent& WXUNUSED(event)) {
        m_autoHideTimer.Stop();
        m_isDragging = false;
        m_dragStartMousePos = wxGetMousePosition();
        m_dragStartFramePos = m_currentPos;
        if (!HasCapture()) {
            CaptureMouse();
        }
    }

    void FloatingIconFrame::OnMouseMove(wxMouseEvent& event) {
        if (event.LeftIsDown() || HasCapture()) {
            wxPoint mousePos = wxGetMousePosition();
            int dx = mousePos.x - m_dragStartMousePos.x;
            int dy = mousePos.y - m_dragStartMousePos.y;

            const int dragThreshold = 3_dip;
            if (!m_isDragging) {
                if (std::abs(dx) > dragThreshold || std::abs(dy) > dragThreshold) {
                    m_isDragging = true;
                    SetCursor(wxCursor(wxCURSOR_SIZING));
                }
            }

            if (m_isDragging) {
                int targetX = m_dragStartFramePos.x + dx;
                int targetY = m_dragStartFramePos.y + dy;
                const int iconSize = 38_dip;

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
                    if (targetX > geom.GetRight() - iconSize - 4_dip) targetX = geom.GetRight() - iconSize - 4_dip;
                    if (targetX < geom.GetLeft() + 4_dip) targetX = geom.GetLeft() + 4_dip;
                    if (targetY > geom.GetBottom() - iconSize - 4_dip) targetY = geom.GetBottom() - iconSize - 4_dip;
                    if (targetY < geom.GetTop() + 4_dip) targetY = geom.GetTop() + 4_dip;
                }
#endif

                m_currentPos = wxPoint(targetX, targetY);
                SetPosition(m_currentPos);
#ifdef _WIN32
                RenderLayeredWindow(targetX, targetY);
#else
                Refresh();
#endif
            }
        }
    }

    void FloatingIconFrame::OnLeftUp(wxMouseEvent& WXUNUSED(event)) {
        if (HasCapture()) {
            ReleaseMouse();
        }

        if (m_isDragging) {
            m_isDragging = false;
            SetCursor(wxCursor(wxCURSOR_HAND));
            // 拖动释放后，重新开启 3 秒倒计时自动隐藏
            m_autoHideTimer.StartOnce(3000);
            return;
        }

        // 点击操作：触发点击回调并关闭自身
        m_autoHideTimer.Stop();
        Hide();

        if (m_onClickCallback) {
            m_onClickCallback(m_currentPos, m_selectedText);
        }
    }

    void FloatingIconFrame::OnTimer(wxTimerEvent& WXUNUSED(event)) {
        if (!m_isDragging) {
            Hide();
        }
    }

} // namespace LinguaAlpaca::UI


