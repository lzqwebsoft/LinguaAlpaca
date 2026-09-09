#include "PlatformThemeHelper.hpp"
#include <wx/radiobox.h>
#include <wx/checkbox.h>
#include <wx/radiobut.h>
#include <unordered_set>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

void PlatformThemeHelper::ApplyAppAppearance(ThemeMode mode) {
#ifdef __APPLE__
    if (NSApp) {
        if (@available(macOS 10.14, *)) {
            NSAppearanceName name = (mode == ThemeMode::Dark) ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
            [NSApp setAppearance:[NSAppearance appearanceNamed:name]];
        }
    }
#else
    (void)mode;
#endif
}

void PlatformThemeHelper::ApplyWindowAppearance(wxWindow* window, ThemeMode mode) {
    if (!window)
        return;
#ifdef __APPLE__
    NSView* view = (NSView*)window->GetHandle();
    if (view) {
        NSWindow* win = [view window];
        if (win) {
            if (@available(macOS 10.14, *)) {
                NSAppearanceName name = (mode == ThemeMode::Dark) ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
                [win setAppearance:[NSAppearance appearanceNamed:name]];
            }
        }
    }
#elif defined(_WIN32)
    HWND hwnd = (HWND)window->GetHWND();
    if (hwnd) {
        BOOL useDarkMode = (mode == ThemeMode::Dark);
        typedef HRESULT(WINAPI* DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
        HMODULE dwm = ::GetModuleHandleW(L"dwmapi.dll");
        if (dwm) {
            DwmSetWindowAttributeFunc setAttr = (DwmSetWindowAttributeFunc)::GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (setAttr) {
                setAttr(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
            }
        }
    }
#else
    (void)window;
    (void)mode;
#endif
}

void PlatformThemeHelper::ApplyControlTheme(wxWindow* control, const ThemePalette& palette) {
    if (!control)
        return;

    // Re-entrancy guard to prevent recursive layout/paint loops
    static thread_local std::unordered_set<wxWindow*> s_activeControls;
    if (s_activeControls.count(control)) {
        return;
    }
    s_activeControls.insert(control);
    struct ReentryGuard {
        wxWindow* target;
        ~ReentryGuard() { s_activeControls.erase(target); }
    } guard{control};

    control->SetForegroundColour(palette.textPrimary);
    control->SetBackgroundColour(palette.cardBg);

#ifdef __APPLE__
    @autoreleasepool {
        bool isDark = (ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Dark);
        NSAppearance* app = nil;
        if (@available(macOS 10.14, *)) {
            app = [NSAppearance appearanceNamed:(isDark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua)];
        }

        NSView* view = (NSView*)control->GetHandle();
        if (view) {
            if (app && [view respondsToSelector:@selector(setAppearance:)]) {
                [view setAppearance:app];
            }

            if ([view isKindOfClass:[NSButton class]]) {
                NSButton* btn = (NSButton*)view;
                NSString* title = [btn title];
                if (title && [title length] > 0) {
                    NSColor* col = [NSColor colorWithSRGBRed:palette.textPrimary.Red() / 255.0
                                                       green:palette.textPrimary.Green() / 255.0
                                                        blue:palette.textPrimary.Blue() / 255.0
                                                       alpha:1.0];
                    NSFont* font = [btn font] ? [btn font] : [NSFont systemFontOfSize:[NSFont systemFontSize]];
                    NSMutableParagraphStyle* paragraphStyle = [[[NSMutableParagraphStyle alloc] init] autorelease];
                    [paragraphStyle setAlignment:[btn alignment]];

                    NSDictionary* attrs = @{
                        NSForegroundColorAttributeName: col,
                        NSFontAttributeName: font,
                        NSParagraphStyleAttributeName: paragraphStyle
                    };
                    NSAttributedString* attrTitle = [[NSAttributedString alloc] initWithString:title attributes:attrs];
                    [btn setAttributedTitle:attrTitle];
                    [attrTitle release];
                }
                [btn setNeedsDisplay:YES];
            }
        }

        for (wxWindowList::compatibility_iterator node = control->GetChildren().GetFirst(); node; node = node->GetNext()) {
            wxWindow* child = node->GetData();
            if (child) {
                ApplyControlTheme(child, palette);
            }
        }
    }

#elif defined(_WIN32)
    HWND hwnd = (HWND)control->GetHWND();
    if (hwnd) {
        bool isDark = (ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Dark);
        typedef HRESULT(WINAPI* SetWindowThemeFunc)(HWND, LPCWSTR, LPCWSTR);
        HMODULE uxtheme = ::GetModuleHandleW(L"uxtheme.dll");
        if (uxtheme) {
            SetWindowThemeFunc setThm = (SetWindowThemeFunc)::GetProcAddress(uxtheme, "SetWindowTheme");
            if (setThm) {
                setThm(hwnd, isDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
            }
        }
        ::InvalidateRect(hwnd, NULL, TRUE);
    }
    for (wxWindowList::compatibility_iterator node = control->GetChildren().GetFirst(); node; node = node->GetNext()) {
        wxWindow* child = node->GetData();
        if (child) {
            ApplyControlTheme(child, palette);
        }
    }
#else
    for (wxWindowList::compatibility_iterator node = control->GetChildren().GetFirst(); node; node = node->GetNext()) {
        wxWindow* child = node->GetData();
        if (child) {
            ApplyControlTheme(child, palette);
        }
    }
#endif
}

} // namespace LinguaAlpaca::UI
