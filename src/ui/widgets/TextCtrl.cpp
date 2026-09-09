#pragma execution_character_set("utf-8")
#include "TextCtrl.hpp"
#include "ScrollBar.hpp"
#include "../theme/Theme.hpp"
#include "../../core/ClipboardHelper.hpp"
#include <algorithm>
#include <wx/dcbuffer.h>
#include "core/markdown/MarkdownFormatter.hpp"
#include "../../core/Logger.hpp"

#ifdef _WIN32
#include <windows.h>
#include <richedit.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>

namespace {
NSScrollView* GetMacScrollView(wxTextCtrl* textCtrl) {
    if (!textCtrl)
        return nil;
    NSView* view = (NSView*)textCtrl->GetHandle();
    if (!view)
        return nil;
    if ([view isKindOfClass:[NSScrollView class]]) {
        return (NSScrollView*)view;
    }
    return [view enclosingScrollView];
}
} // namespace
#endif

namespace LinguaAlpaca::UI {

// ----------------------------------------------------------------------------
// TextCtrl 实现
// ----------------------------------------------------------------------------

TextCtrl::TextCtrl(wxWindow* parent, wxWindowID id, const wxString& value, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE) {
    InitUI(value, style);
}

TextCtrl::~TextCtrl() {
    CleanupNativeScrollHandling();
}

void TextCtrl::InitUI(const wxString& value, long style) {
    long textStyle = (style & ~wxTE_READONLY) | wxTE_MULTILINE | wxBORDER_NONE;
    if (style & wxTE_RICH2) {
        textStyle |= wxTE_RICH2;
    }

    m_textCtrl = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, textStyle);
    m_textCtrl->SetFont(ThemeFont::GetFont(FontRole::Body));
    if (style & wxTE_READONLY) {
        m_textCtrl->SetEditable(false);
        m_isEditable = false;
    } else {
        m_textCtrl->SetEditable(true);
        m_isEditable = true;
    }

    SetupNativeScrollHandling();

    m_scrollBar = new ScrollBar(this);

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_textCtrl, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 6_dip);
    sizer->Add(m_scrollBar, 0, wxEXPAND | wxRIGHT, 2_dip);
    SetSizer(sizer);

    // 鼠标滚轮滑动与中键拖拽平移事件监听
    m_textCtrl->Bind(wxEVT_MOUSEWHEEL, &TextCtrl::OnMouseWheel, this);
    Bind(wxEVT_MOUSEWHEEL, &TextCtrl::OnMouseWheel, this);
    m_scrollBar->Bind(wxEVT_MOUSEWHEEL, &TextCtrl::OnMouseWheel, this);

    // 仅在获得焦点与失去焦点时控制滑动条显示，避免鼠标掠过时误唤醒
    m_textCtrl->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        if (m_scrollBar) {
            m_scrollBar->SetFocused(true);
        }
        event.Skip();
    });
    m_textCtrl->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        if (m_scrollBar) {
            m_scrollBar->SetFocused(false);
        }
        event.Skip();
    });

    m_textCtrl->Bind(wxEVT_MIDDLE_DOWN, &TextCtrl::OnMiddleDown, this);
    m_textCtrl->Bind(wxEVT_MIDDLE_UP, &TextCtrl::OnMiddleUp, this);
    m_textCtrl->Bind(wxEVT_MOTION, &TextCtrl::OnMouseMove, this);

    Bind(wxEVT_MIDDLE_DOWN, &TextCtrl::OnMiddleDown, this);
    Bind(wxEVT_MIDDLE_UP, &TextCtrl::OnMiddleUp, this);
    Bind(wxEVT_MOTION, &TextCtrl::OnMouseMove, this);

    // 右键上下文快捷菜单绑定
    m_textCtrl->Bind(wxEVT_CONTEXT_MENU, &TextCtrl::OnContextMenu, this);
    Bind(wxEVT_CONTEXT_MENU, &TextCtrl::OnContextMenu, this);

    Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
        int id = event.GetId();
        if (id == wxID_COPY) {
            Copy();
        } else if (id == wxID_SELECTALL) {
            SelectAll();
        } else if (id == wxID_CUT) {
            Cut();
        } else if (id == wxID_PASTE) {
            Paste();
        } else if (id == wxID_CLEAR) {
            Clear();
        }
    });

    m_textCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        SanitizeNativeTextAttributes();
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

void TextCtrl::OnContextMenu(wxContextMenuEvent& WXUNUSED(event)) {
    wxMenu menu;
    menu.Append(wxID_COPY, L"复制\tCtrl+C");
    menu.Append(wxID_SELECTALL, L"全选\tCtrl+A");

    if (m_isEditable) {
        menu.AppendSeparator();
        menu.Append(wxID_CUT, L"剪切\tCtrl+X");
        menu.Append(wxID_PASTE, L"粘贴\tCtrl+V");
        menu.Append(wxID_CLEAR, L"清空");
    }

    bool hasSel = false;
    if (m_textCtrl) {
        long from = 0, to = 0;
        m_textCtrl->GetSelection(&from, &to);
        hasSel = (from != to);
    }

    menu.Enable(wxID_COPY, hasSel || (m_textCtrl && !m_textCtrl->GetValue().IsEmpty()));
    if (m_isEditable) {
        menu.Enable(wxID_CUT, hasSel);
        menu.Enable(wxID_PASTE, m_textCtrl && m_textCtrl->CanPaste());
        menu.Enable(wxID_CLEAR, m_textCtrl && !m_textCtrl->GetValue().IsEmpty());
    }

    PopupMenu(&menu);
}

void TextCtrl::SetupNativeScrollHandling() {
    if (!m_textCtrl)
        return;

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        ::SendMessage(hwnd, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)FALSE);
    }
#elif defined(__APPLE__)
    NSScrollView* sv = GetMacScrollView(m_textCtrl);
    if (sv) {
        [sv setHasVerticalScroller:NO];
        [sv setHasHorizontalScroller:NO];
        [sv setAutohidesScrollers:YES];

        NSClipView* clipView = [sv contentView];
        if (clipView) {
            [clipView setPostsBoundsChangedNotifications:YES];
            id observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSViewBoundsDidChangeNotification
                                                                            object:clipView
                                                                             queue:[NSOperationQueue mainQueue]
                                                                        usingBlock:[this](NSNotification*) {
                                                                            NSEvent* event = [NSApp currentEvent];
                                                                            if (event && ([event type] == NSEventTypeScrollWheel || [event type] == NSEventTypeLeftMouseDragged)) {
                                                                                if (m_scrollBar) {
                                                                                    m_scrollBar->NotifyActivity();
                                                                                }
                                                                            }
                                                                            UpdateScrollInfo();
                                                                        }];
            m_macScrollObserver = (void*)[observer retain];
        }
    }
#endif
    SanitizeNativeTextAttributes();
}

void TextCtrl::CleanupNativeScrollHandling() {
#ifdef __APPLE__
    if (m_macScrollObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:(id)m_macScrollObserver];
        [(id)m_macScrollObserver release];
        m_macScrollObserver = nullptr;
    }
#endif
}

void TextCtrl::SanitizeNativeTextAttributes(bool stripStorageBg) {
#ifdef _WIN32
    if (m_textCtrl) {
        HWND hwnd = (HWND)m_textCtrl->GetHWND();
        if (hwnd) {
            CHARFORMAT2W cf;
            ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_BACKCOLOR;
            cf.dwEffects = CFE_AUTOBACKCOLOR;
            cf.crBackColor = 0;
            if (stripStorageBg || !m_isMarkdownMode) {
                ::SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
            }
            ::SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
            ::SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        }
    }
#elif defined(__APPLE__)
    if (!m_textCtrl)
        return;

    NSScrollView* sv = GetMacScrollView(m_textCtrl);
    if (sv) {
        [sv setDrawsBackground:NO];
        [sv setBorderType:NSNoBorder];

        NSClipView* clipView = [sv contentView];
        if (clipView) {
            [clipView setDrawsBackground:NO];
        }

        NSTextView* tv = (NSTextView*)[sv documentView];
        if (tv && [tv isKindOfClass:[NSTextView class]]) {
            [tv setDrawsBackground:NO];
            [tv setBackgroundColor:[NSColor clearColor]];
            [tv setFocusRingType:NSFocusRingTypeNone];

            auto palette = ThemeColors::GetCurrentPalette();
            NSColor* cursorColor = [NSColor colorWithSRGBRed:palette.textPrimary.Red() / 255.0
                                                       green:palette.textPrimary.Green() / 255.0
                                                        blue:palette.textPrimary.Blue() / 255.0
                                                       alpha:1.0];
            [tv setInsertionPointColor:cursorColor];

            @autoreleasepool {
                NSDictionary* currentAttrs = [tv typingAttributes];
                if (currentAttrs && [currentAttrs objectForKey:NSBackgroundColorAttributeName]) {
                    NSMutableDictionary* attrs = [currentAttrs mutableCopy];
                    [attrs removeObjectForKey:NSBackgroundColorAttributeName];
                    [tv setTypingAttributes:attrs];
                    [attrs release];
                }

                if (tv.textStorage && tv.textStorage.length > 0) {
                    bool isDark = (ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Dark);
                    if (stripStorageBg || !m_isMarkdownMode) {
                        [tv.textStorage removeAttribute:NSBackgroundColorAttributeName range:NSMakeRange(0, tv.textStorage.length)];
                    } else if (isDark) {
                        // In markdown mode during dark theme, remove any background attribute that has a light/white tint
                        [tv.textStorage enumerateAttribute:NSBackgroundColorAttributeName
                                                   inRange:NSMakeRange(0, tv.textStorage.length)
                                                   options:0
                                                usingBlock:^(id value, NSRange range, BOOL* stop) {
                            if (value) {
                                NSColor* col = (NSColor*)value;
                                NSColor* rgbCol = [col colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
                                if (rgbCol) {
                                    CGFloat r = [rgbCol redComponent];
                                    CGFloat g = [rgbCol greenComponent];
                                    CGFloat b = [rgbCol blueComponent];
                                    if (r > 0.4 || g > 0.4 || b > 0.4) {
                                        [tv.textStorage removeAttribute:NSBackgroundColorAttributeName range:range];
                                    }
                                } else {
                                    [tv.textStorage removeAttribute:NSBackgroundColorAttributeName range:range];
                                }
                            }
                        }];
                    }
                }
            }

            [tv setNeedsDisplay:YES];
            [sv setNeedsDisplay:YES];
        }
    }
#else
    (void)stripStorageBg;
#endif
}

int TextCtrl::GetSafeLineHeight() const {
    if (!m_textCtrl)
        return 16;
    int h = m_textCtrl->GetCharHeight();
    return h > 0 ? h : 16;
}

int TextCtrl::GetFirstVisibleLine() const {
    if (!m_textCtrl)
        return 0;

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        return (int)::SendMessage(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    }
#elif defined(__APPLE__)
    NSScrollView* sv = GetMacScrollView(m_textCtrl);
    if (sv) {
        NSClipView* clipView = [sv contentView];
        NSRect visibleRect = [clipView documentVisibleRect];
        return (int)(visibleRect.origin.y / (double)GetSafeLineHeight());
    }
#endif

    return 0;
}

void TextCtrl::OnMouseWheel(wxMouseEvent& event) {
    int rotation = event.GetWheelRotation();
    if (rotation == 0)
        return;

    if (m_scrollBar) {
        m_scrollBar->NotifyActivity();
    }

    int delta = event.GetWheelDelta();
    if (delta <= 0)
        delta = 120;
    int linesPerAction = event.GetLinesPerAction();
    if (linesPerAction <= 0)
        linesPerAction = 3;

    int steps = rotation / delta;
    if (steps == 0)
        steps = (rotation > 0 ? 1 : -1);

    int linesToScroll = -steps * linesPerAction;
    ScrollToLine(GetFirstVisibleLine() + linesToScroll);
}

void TextCtrl::OnMiddleDown(wxMouseEvent& event) {
    m_isMiddleDragging = true;
    m_middleDragStartY = event.GetPosition().y;
    m_middleDragStartFirstLine = GetFirstVisibleLine();

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
        int deltaLines = deltaY / GetSafeLineHeight();
        ScrollToLine(m_middleDragStartFirstLine + deltaLines);
    } else {
        event.Skip();
    }
}

void TextCtrl::SetValue(const wxString& value) {
    m_isMarkdownMode = false;
    m_rawMarkdown.clear();
    if (m_textCtrl) {
        wxTextAttr emptyAttr;
        m_textCtrl->SetDefaultStyle(emptyAttr);
        wxTextAttr defaultAttr(m_textCtrl->GetForegroundColour(), wxNullColour, m_textCtrl->GetFont());
        m_textCtrl->SetDefaultStyle(defaultAttr);
        m_textCtrl->SetValue(value);
        SanitizeNativeTextAttributes(true);
        UpdateScrollInfo();
    }
}

wxString TextCtrl::GetValue() const {
    return m_textCtrl ? m_textCtrl->GetValue() : wxString();
}

void TextCtrl::AppendText(const wxString& text) {
    if (m_textCtrl) {
        m_textCtrl->AppendText(text);
        SanitizeNativeTextAttributes(true);
        UpdateScrollInfo();
    }
}

void TextCtrl::Clear() {
    m_isMarkdownMode = false;
    m_rawMarkdown.clear();
    if (m_textCtrl) {
        wxTextAttr emptyAttr;
        m_textCtrl->SetDefaultStyle(emptyAttr);
        wxTextAttr defaultAttr(m_textCtrl->GetForegroundColour(), wxNullColour, m_textCtrl->GetFont());
        m_textCtrl->SetDefaultStyle(defaultAttr);
        m_textCtrl->Clear();
        SanitizeNativeTextAttributes(true);
        UpdateScrollInfo();
    }
}

void TextCtrl::WriteText(const wxString& text) {
    if (m_textCtrl) {
        m_textCtrl->WriteText(text);
        SanitizeNativeTextAttributes();
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
        bool res = m_textCtrl->SetDefaultStyle(style);
        SanitizeNativeTextAttributes();
        return res;
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

void TextCtrl::SetMarkdown(const std::string& markdownText) {
    m_isMarkdownMode = true;
    m_rawMarkdown = markdownText;

    if (!m_textCtrl)
        return;

    m_textCtrl->Freeze();
    wxTextAttr emptyAttr;
    m_textCtrl->SetDefaultStyle(emptyAttr);
    m_textCtrl->Clear();
    SanitizeNativeTextAttributes(true);

    if (markdownText.empty()) {
        m_textCtrl->Thaw();
        SanitizeNativeTextAttributes(false);
        UpdateScrollInfo();
        return;
    }

    ThemePalette palette = ThemeManager::GetCurrentPalette();

    // 基础字体与尺寸规范 (高DPI友好，macOS自动校准+2pt)
    wxFont baseFont = m_textCtrl->GetFont();
    if (!baseFont.IsOk()) {
        baseFont = ThemeFont::GetFont(FontRole::Body);
    }
    int basePt = baseFont.GetPointSize();
    if (basePt <= 0)
        basePt = ThemeFont::GetFont(FontRole::Body).GetPointSize();
    wxString faceName = baseFont.GetFaceName();
    if (faceName.IsEmpty())
        faceName = ThemeFont::GetDefaultFamily();

    // 字体层级定义
    wxFont defaultFont(basePt, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, faceName);
    wxFont boldFont(basePt, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont italicFont(basePt, wxFONTFAMILY_SWISS, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL, false, faceName);
    wxFont boldItalicFont(basePt, wxFONTFAMILY_SWISS, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont codeFont(std::max(8, basePt - 1), wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, ThemeFont::GetDefaultMonoFamily());
    wxFont codeBlockFont(std::max(8, basePt - 1), wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, ThemeFont::GetDefaultMonoFamily());

    // 标题逐级字号
    wxFont h1Font(basePt + 4, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont h2Font(basePt + 3, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont h3Font(basePt + 2, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont h4Font(basePt + 1, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont h5Font(basePt, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, faceName);
    wxFont h6Font(std::max(8, basePt - 1), wxFONTFAMILY_SWISS, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_BOLD, false, faceName);

    wxColour baseFg = m_textCtrl->GetForegroundColour();
    if (!baseFg.IsOk()) {
        baseFg = palette.textPrimary;
    }
    wxColour baseBg = wxNullColour; // 普通文本不加字符底色，保持透明融入背景

    // 样式属性定义 (严格继承控件的 ForegroundColour，确保如译文蓝色不被重置为默认黑色)
    wxTextAttr defaultAttr(baseFg, baseBg, defaultFont);
    wxTextAttr h1Attr(palette.accentPrimary, baseBg, h1Font);
    wxTextAttr h2Attr(palette.accentPrimary, baseBg, h2Font);
    wxTextAttr h3Attr(baseFg, baseBg, h3Font);
    wxTextAttr h4Attr(baseFg, baseBg, h4Font);
    wxTextAttr h5Attr(palette.textSecondary, baseBg, h5Font);
    wxTextAttr h6Attr(palette.textSecondary, baseBg, h6Font);

    wxTextAttr boldAttr(baseFg, baseBg, boldFont);
    wxTextAttr italicAttr(palette.textSecondary, baseBg, italicFont);
    wxTextAttr boldItalicAttr(baseFg, baseBg, boldItalicFont);

    // 行内代码与代码块
    wxTextAttr inlineCodeAttr(palette.bannerText, palette.bannerBg, codeFont);
    wxTextAttr codeBlockAttr(palette.textPrimary, palette.windowBg, codeBlockFont);

    // 引用
    wxTextAttr blockquoteBarAttr(palette.accentPrimary, baseBg, boldFont);
    wxTextAttr blockquoteAttr(palette.textSecondary, baseBg, italicFont);

    // 列表与分割线
    wxTextAttr listBulletAttr(palette.accentPrimary, baseBg, boldFont);
    wxTextAttr listNumAttr(palette.accentPrimary, baseBg, boldFont);
    wxTextAttr dividerAttr(palette.cardBorderActive, baseBg, defaultFont);

    // 链接与删除线
    wxTextAttr linkAttr(palette.accentPrimary, baseBg, defaultFont);
    linkAttr.SetFontUnderlined(true);
    wxTextAttr strikeAttr(palette.textSecondary, baseBg, defaultFont);
    strikeAttr.SetFontStrikethrough(true);

    auto getStyleAttr = [&](MarkdownStyle style) -> const wxTextAttr& {
        switch (style) {
        case MarkdownStyle::Heading1:
            return h1Attr;
        case MarkdownStyle::Heading2:
            return h2Attr;
        case MarkdownStyle::Heading3:
            return h3Attr;
        case MarkdownStyle::Heading4:
            return h4Attr;
        case MarkdownStyle::Heading5:
            return h5Attr;
        case MarkdownStyle::Heading6:
            return h6Attr;
        case MarkdownStyle::Bold:
            return boldAttr;
        case MarkdownStyle::Italic:
            return italicAttr;
        case MarkdownStyle::BoldItalic:
            return boldItalicAttr;
        case MarkdownStyle::InlineCode:
            return inlineCodeAttr;
        case MarkdownStyle::CodeBlock:
            return codeBlockAttr;
        case MarkdownStyle::Blockquote:
            return blockquoteAttr;
        case MarkdownStyle::BlockquoteBar:
            return blockquoteBarAttr;
        case MarkdownStyle::ListBullet:
            return listBulletAttr;
        case MarkdownStyle::ListNumber:
            return listNumAttr;
        case MarkdownStyle::Divider:
            return dividerAttr;
        case MarkdownStyle::LinkText:
            return linkAttr;
        case MarkdownStyle::Strikethrough:
            return strikeAttr;
        case MarkdownStyle::Default:
        default:
            return defaultAttr;
        }
    };

    auto segments = MarkdownFormatter::Parse(markdownText);
    for (const auto& seg : segments) {
        m_textCtrl->SetDefaultStyle(getStyleAttr(seg.style));
        m_textCtrl->AppendText(wxString::FromUTF8(seg.text));
    }
    m_textCtrl->SetDefaultStyle(emptyAttr);

    m_textCtrl->Thaw();
    SanitizeNativeTextAttributes(false);
    ScrollToLine(0);
    UpdateScrollInfo();
}

void TextCtrl::SetMarkdown(const wxString& markdownText) {
    SetMarkdown(std::string(markdownText.ToUTF8().data()));
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

wxString TextCtrl::GetStringSelection() const {
    return m_textCtrl ? m_textCtrl->GetStringSelection() : wxString();
}

void TextCtrl::GetSelection(long* from, long* to) const {
    if (m_textCtrl) {
        m_textCtrl->GetSelection(from, to);
    }
}

void TextCtrl::SetSelection(long from, long to) {
    if (m_textCtrl) {
        m_textCtrl->SetSelection(from, to);
    }
}

void TextCtrl::Copy() {
    if (!m_textCtrl)
        return;
    wxString sel = m_textCtrl->GetStringSelection();
    if (!sel.IsEmpty()) {
        ClipboardHelper::SetClipboardText(sel.ToUTF8().data());
    } else {
        wxString val = m_textCtrl->GetValue();
        if (!val.IsEmpty()) {
            ClipboardHelper::SetClipboardText(val.ToUTF8().data());
        }
    }
}

void TextCtrl::Cut() {
    if (m_textCtrl && m_isEditable) {
        m_textCtrl->Cut();
        UpdateScrollInfo();
    }
}

void TextCtrl::Paste() {
    if (m_textCtrl && m_isEditable) {
        m_textCtrl->Paste();
        UpdateScrollInfo();
    }
}

bool TextCtrl::CanCopy() const {
    if (!m_textCtrl)
        return false;
    return m_textCtrl->CanCopy() || !m_textCtrl->GetValue().IsEmpty();
}

bool TextCtrl::CanCut() const {
    return m_textCtrl && m_isEditable && m_textCtrl->CanCut();
}

bool TextCtrl::CanPaste() const {
    return m_textCtrl && m_isEditable && m_textCtrl->CanPaste();
}

bool TextCtrl::SetFont(const wxFont& font) {
    bool res = wxPanel::SetFont(font);
    if (m_textCtrl) {
        m_textCtrl->SetFont(font);
    }
    if (m_isMarkdownMode && !m_rawMarkdown.empty()) {
        SetMarkdown(m_rawMarkdown);
    } else {
        UpdateScrollInfo();
    }
    return res;
}

bool TextCtrl::SetBackgroundColour(const wxColour& colour) {
    bool res = wxPanel::SetBackgroundColour(colour);
    if (m_textCtrl) {
        m_textCtrl->SetBackgroundColour(colour);
        wxTextAttr emptyAttr;
        m_textCtrl->SetDefaultStyle(emptyAttr);
        wxTextAttr attr(m_textCtrl->GetForegroundColour(), wxNullColour, m_textCtrl->GetFont());
        m_textCtrl->SetDefaultStyle(attr);
        if (!m_isMarkdownMode) {
            long lastPos = m_textCtrl->GetLastPosition();
            if (lastPos > 0) {
                m_textCtrl->SetStyle(0, lastPos, attr);
            }
        }
    }
    SanitizeNativeTextAttributes(true);
    if (m_scrollBar) {
        m_scrollBar->SetBackgroundColour(colour);
        m_scrollBar->Refresh();
    }
    if (m_isMarkdownMode && !m_rawMarkdown.empty()) {
        SetMarkdown(m_rawMarkdown);
    }
    return res;
}

bool TextCtrl::SetForegroundColour(const wxColour& colour) {
    bool res = wxPanel::SetForegroundColour(colour);
    if (m_textCtrl) {
        m_textCtrl->SetForegroundColour(colour);
        wxTextAttr emptyAttr;
        m_textCtrl->SetDefaultStyle(emptyAttr);
        wxTextAttr attr(colour, wxNullColour, m_textCtrl->GetFont());
        m_textCtrl->SetDefaultStyle(attr);
        if (!m_isMarkdownMode) {
            long lastPos = m_textCtrl->GetLastPosition();
            if (lastPos > 0) {
                m_textCtrl->SetStyle(0, lastPos, attr);
            }
        }
    }
    SanitizeNativeTextAttributes(true);
    if (m_isMarkdownMode && !m_rawMarkdown.empty()) {
        SetMarkdown(m_rawMarkdown);
    }
    return res;
}

void TextCtrl::ScrollToLine(int targetLine) {
    if (!m_textCtrl)
        return;

    bool handled = false;

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        int totalLines = (int)::SendMessage(hwnd, EM_GETLINECOUNT, 0, 0);
        targetLine = std::clamp(targetLine, 0, std::max(0, totalLines - 1));

        int delta = targetLine - GetFirstVisibleLine();
        if (delta != 0) {
            ::SendMessage(hwnd, EM_LINESCROLL, 0, (LPARAM)delta);
        }
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        ::SendMessage(hwnd, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)FALSE);
        handled = true;
    }
#elif defined(__APPLE__)
    NSScrollView* sv = GetMacScrollView(m_textCtrl);
    if (sv) {
        [sv setHasVerticalScroller:NO];
        [sv setHasHorizontalScroller:NO];

        NSClipView* clipView = [sv contentView];
        NSView* docView = [sv documentView];
        NSRect docRect = docView ? [docView frame] : NSZeroRect;
        NSRect visibleRect = [clipView documentVisibleRect];

        int lineHeight = GetSafeLineHeight();
        double maxScrollY = std::max(0.0, (double)(docRect.size.height - visibleRect.size.height));
        double targetY = std::clamp((double)targetLine * lineHeight, 0.0, maxScrollY);

        NSPoint newOrigin = NSMakePoint(visibleRect.origin.x, targetY);
        [clipView scrollToPoint:newOrigin];
        [sv reflectScrolledClipView:clipView];
        handled = true;
    }
#endif

    if (!handled) {
        int totalLines = std::max(1, m_textCtrl->GetNumberOfLines());
        targetLine = std::clamp(targetLine, 0, std::max(0, totalLines - 1));
        long charPos = m_textCtrl->XYToPosition(0, targetLine);
        if (charPos != -1) {
            m_textCtrl->ShowPosition(charPos);
        }
    }

    UpdateScrollInfo();
}

void TextCtrl::UpdateScrollInfo() {
    if (!m_textCtrl || !m_scrollBar)
        return;

    int lineHeight = GetSafeLineHeight();
    int totalLines = std::max(1, m_textCtrl->GetNumberOfLines());
    int visibleLines = std::max(1, m_textCtrl->GetClientSize().GetHeight() / lineHeight);
    int firstVisibleLine = GetFirstVisibleLine();

#ifdef _WIN32
    HWND hwnd = (HWND)m_textCtrl->GetHWND();
    if (hwnd) {
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        ::SendMessage(hwnd, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)FALSE);

        totalLines = (int)::SendMessage(hwnd, EM_GETLINECOUNT, 0, 0);

        RECT rc;
        ::SendMessage(hwnd, EM_GETRECT, 0, (LPARAM)&rc);
        int clientH = rc.bottom - rc.top;
        if (clientH <= 0) {
            clientH = m_textCtrl->GetClientSize().GetHeight();
        }
        visibleLines = std::max(1, clientH / lineHeight);
    }
#elif defined(__APPLE__)
    NSScrollView* sv = GetMacScrollView(m_textCtrl);
    if (sv) {
        [sv setHasVerticalScroller:NO];
        [sv setHasHorizontalScroller:NO];

        NSClipView* clipView = [sv contentView];
        NSRect visibleRect = [clipView documentVisibleRect];
        NSView* docView = [sv documentView];
        NSRect docRect = docView ? [docView frame] : NSZeroRect;

        totalLines = std::max(1, (int)std::ceil(docRect.size.height / (double)lineHeight));
        visibleLines = std::max(1, (int)(visibleRect.size.height / (double)lineHeight));
    }
#endif

    totalLines = std::max(1, totalLines);
    visibleLines = std::max(1, visibleLines);
    firstVisibleLine = std::clamp(firstVisibleLine, 0, std::max(0, totalLines - visibleLines));

    m_scrollBar->SetScrollParams(firstVisibleLine, visibleLines, totalLines);
}

} // namespace LinguaAlpaca::UI
