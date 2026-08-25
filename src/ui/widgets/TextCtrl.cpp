#pragma execution_character_set("utf-8")
#include "TextCtrl.hpp"
#include "ScrollBar.hpp"
#include "../theme/Theme.hpp"
#include <algorithm>
#include <wx/dcbuffer.h>

#ifdef _WIN32
#include <windows.h>
#include <richedit.h>
#endif

namespace LinguaAlpaca::UI {

// ----------------------------------------------------------------------------
// TextCtrl 实现
// ----------------------------------------------------------------------------

TextCtrl::TextCtrl(wxWindow* parent, wxWindowID id,
                   const wxString& value,
                   const wxPoint& pos,
                   const wxSize& size,
                   long style)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE) {
    InitUI(value, style);
}

void TextCtrl::InitUI(const wxString& value, long style) {
    long textStyle = (style & ~wxTE_READONLY) | wxTE_MULTILINE | wxBORDER_NONE;
    if (style & wxTE_RICH2) {
        textStyle |= wxTE_RICH2;
    }

    m_textCtrl = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, textStyle);
    if (style & wxTE_READONLY) {
        m_textCtrl->SetEditable(false);
        m_isEditable = false;
    } else {
        m_textCtrl->SetEditable(true);
        m_isEditable = true;
    }

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        ::SendMessage(hwnd, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)FALSE);
    }
#endif

    m_scrollBar = new ScrollBar(this);

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_textCtrl, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 6_dip);
    sizer->Add(m_scrollBar, 0, wxEXPAND | wxRIGHT, 2_dip);
    SetSizer(sizer);

    // 鼠标中键滚轮滑动与中键拖拽平移事件监听
    m_textCtrl->Bind(wxEVT_MOUSEWHEEL, &TextCtrl::OnMouseWheel, this);
    Bind(wxEVT_MOUSEWHEEL, &TextCtrl::OnMouseWheel, this);
    m_scrollBar->Bind(wxEVT_MOUSEWHEEL, &TextCtrl::OnMouseWheel, this);

    m_textCtrl->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) {
        if (m_scrollBar) m_scrollBar->NotifyActivity();
        event.Skip();
    });

    m_textCtrl->Bind(wxEVT_MIDDLE_DOWN, &TextCtrl::OnMiddleDown, this);
    m_textCtrl->Bind(wxEVT_MIDDLE_UP, &TextCtrl::OnMiddleUp, this);
    m_textCtrl->Bind(wxEVT_MOTION, &TextCtrl::OnMouseMove, this);

    Bind(wxEVT_MIDDLE_DOWN, &TextCtrl::OnMiddleDown, this);
    Bind(wxEVT_MIDDLE_UP, &TextCtrl::OnMiddleUp, this);
    Bind(wxEVT_MOTION, &TextCtrl::OnMouseMove, this);

    m_textCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        event.Skip();
        UpdateScrollInfo();
    });

    m_textCtrl->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
        event.Skip();
        if (wxTheApp) {
            wxTheApp->CallAfter([this]() { UpdateScrollInfo(); });
        }
    });

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (wxTheApp) {
            wxTheApp->CallAfter([this]() { UpdateScrollInfo(); });
        }
    });

    m_textCtrl->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (wxTheApp) {
            wxTheApp->CallAfter([this]() { UpdateScrollInfo(); });
        }
    });
}

void TextCtrl::OnMouseWheel(wxMouseEvent& event) {
    int rotation = event.GetWheelRotation();
    if (rotation == 0) return;

    if (m_scrollBar) {
        m_scrollBar->NotifyActivity();
    }

    int delta = event.GetWheelDelta();
    if (delta <= 0) delta = 120;
    int linesPerAction = event.GetLinesPerAction();
    if (linesPerAction <= 0) linesPerAction = 3;

    int steps = rotation / delta;
    if (steps == 0) steps = (rotation > 0 ? 1 : -1);

    int linesToScroll = -steps * linesPerAction;

    int currentFirst = 0;
#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        currentFirst = (int)::SendMessage(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    }
#endif
    ScrollToLine(currentFirst + linesToScroll);
}

void TextCtrl::OnMiddleDown(wxMouseEvent& event) {
    m_isMiddleDragging = true;
    m_middleDragStartY = event.GetPosition().y;
#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        m_middleDragStartFirstLine = (int)::SendMessage(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    }
#endif
    if (!HasCapture()) {
        CaptureMouse();
    }
    SetCursor(wxCursor(wxCURSOR_SIZENS));
    if (m_scrollBar) {
        m_scrollBar->NotifyActivity();
    }
}

void TextCtrl::OnMiddleUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isMiddleDragging) {
        m_isMiddleDragging = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
        SetCursor(wxNullCursor);
    }
}

void TextCtrl::OnMouseMove(wxMouseEvent& event) {
    if (m_isMiddleDragging) {
        if (m_scrollBar) {
            m_scrollBar->NotifyActivity();
        }
        int deltaY = event.GetPosition().y - m_middleDragStartY;
        int lineHeight = m_textCtrl->GetCharHeight();
        if (lineHeight <= 0) lineHeight = 16;
        int deltaLines = deltaY / lineHeight;
        ScrollToLine(m_middleDragStartFirstLine + deltaLines);
    } else {
        event.Skip();
    }
}

void TextCtrl::SetValue(const wxString& value) {
    if (m_textCtrl) {
        m_textCtrl->SetValue(value);
        UpdateScrollInfo();
    }
}

wxString TextCtrl::GetValue() const {
    return m_textCtrl ? m_textCtrl->GetValue() : wxString();
}

void TextCtrl::AppendText(const wxString& text) {
    if (m_textCtrl) {
        m_textCtrl->AppendText(text);
        UpdateScrollInfo();
    }
}

void TextCtrl::Clear() {
    if (m_textCtrl) {
        m_textCtrl->Clear();
        UpdateScrollInfo();
    }
}

void TextCtrl::WriteText(const wxString& text) {
    if (m_textCtrl) {
        m_textCtrl->WriteText(text);
        UpdateScrollInfo();
    }
}

void TextCtrl::SetHint(const wxString& hint) {
    if (m_textCtrl) {
        m_textCtrl->SetHint(hint);
    }
}

bool TextCtrl::SetDefaultStyle(const wxTextAttr& style) {
    if (m_textCtrl) {
        return m_textCtrl->SetDefaultStyle(style);
    }
    return false;
}

void TextCtrl::ShowPosition(long pos) {
    if (m_textCtrl) {
        m_textCtrl->ShowPosition(pos);
        UpdateScrollInfo();
    }
}

long TextCtrl::GetLastPosition() const {
    return m_textCtrl ? m_textCtrl->GetLastPosition() : 0;
}

void TextCtrl::SetEditable(bool editable) {
    m_isEditable = editable;
    if (m_textCtrl) {
        m_textCtrl->SetEditable(editable);
    }
}

bool TextCtrl::IsEditable() const {
    return m_isEditable;
}

void TextCtrl::SetInsertionPoint(long pos) {
    if (m_textCtrl) {
        m_textCtrl->SetInsertionPoint(pos);
        UpdateScrollInfo();
    }
}

void TextCtrl::SetInsertionPointEnd() {
    if (m_textCtrl) {
        m_textCtrl->SetInsertionPointEnd();
        UpdateScrollInfo();
    }
}

long TextCtrl::GetInsertionPoint() const {
    return m_textCtrl ? m_textCtrl->GetInsertionPoint() : 0;
}

void TextCtrl::SelectAll() {
    if (m_textCtrl) {
        m_textCtrl->SelectAll();
    }
}

bool TextCtrl::SetFont(const wxFont& font) {
    bool res = wxPanel::SetFont(font);
    if (m_textCtrl) {
        m_textCtrl->SetFont(font);
    }
    UpdateScrollInfo();
    return res;
}

bool TextCtrl::SetBackgroundColour(const wxColour& colour) {
    bool res = wxPanel::SetBackgroundColour(colour);
    if (m_textCtrl) {
        m_textCtrl->SetBackgroundColour(colour);
    }
    if (m_scrollBar) {
        m_scrollBar->SetBackgroundColour(colour);
        m_scrollBar->Refresh();
    }
    return res;
}

bool TextCtrl::SetForegroundColour(const wxColour& colour) {
    bool res = wxPanel::SetForegroundColour(colour);
    if (m_textCtrl) {
        m_textCtrl->SetForegroundColour(colour);
    }
    return res;
}

void TextCtrl::ScrollToLine(int targetLine) {
    if (!m_textCtrl) return;

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        int totalLines = (int)::SendMessage(hwnd, EM_GETLINECOUNT, 0, 0);
        targetLine = std::clamp(targetLine, 0, std::max(0, totalLines - 1));

        int currentFirst = (int)::SendMessage(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
        int delta = targetLine - currentFirst;
        if (delta != 0) {
            ::SendMessage(hwnd, EM_LINESCROLL, 0, (LPARAM)delta);
        }
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        ::SendMessage(hwnd, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)FALSE);
    }
#else
    int totalLines = std::max(1, m_textCtrl->GetNumberOfLines());
    targetLine = std::clamp(targetLine, 0, std::max(0, totalLines - 1));
    long charPos = m_textCtrl->XYToPosition(0, targetLine);
    if (charPos != -1) {
        m_textCtrl->ShowPosition(charPos);
    }
#endif

    UpdateScrollInfo();
}

void TextCtrl::UpdateScrollInfo() {
    if (!m_textCtrl || !m_scrollBar) return;

    int totalLines = 1;
    int firstVisibleLine = 0;
    int visibleLines = 1;

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        ::SendMessage(hwnd, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)FALSE);

        totalLines = (int)::SendMessage(hwnd, EM_GETLINECOUNT, 0, 0);
        firstVisibleLine = (int)::SendMessage(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);

        RECT rc;
        ::SendMessage(hwnd, EM_GETRECT, 0, (LPARAM)&rc);
        int clientH = rc.bottom - rc.top;
        if (clientH <= 0) {
            clientH = m_textCtrl->GetClientSize().GetHeight();
        }

        int lineHeight = m_textCtrl->GetCharHeight();
        if (lineHeight <= 0) lineHeight = 16;
        visibleLines = std::max(1, clientH / lineHeight);
    }
#else
    totalLines = std::max(1, m_textCtrl->GetNumberOfLines());
    int lineHeight = m_textCtrl->GetCharHeight();
    if (lineHeight <= 0) lineHeight = 16;
    visibleLines = std::max(1, m_textCtrl->GetClientSize().GetHeight() / lineHeight);
    firstVisibleLine = 0;
#endif

    totalLines = std::max(1, totalLines);
    visibleLines = std::max(1, visibleLines);
    firstVisibleLine = std::clamp(firstVisibleLine, 0, std::max(0, totalLines - visibleLines));

    m_scrollBar->SetScrollParams(firstVisibleLine, visibleLines, totalLines);
}

} // namespace LinguaAlpaca::UI
