#pragma once
#pragma execution_character_set("utf-8")

#include <wx/gdicmn.h>
#include <wx/window.h>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

inline double GetSystemDpiScale() {
#ifdef _WIN32
    typedef UINT (WINAPI *GetDpiForSystemFn)();
    static auto pGetDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    if (pGetDpiForSystem) {
        return pGetDpiForSystem() / 96.0;
    }
#endif
    int ppi = wxWindow::FromDIP(96, nullptr);
    return static_cast<double>(ppi) / 96.0;
}

inline int dip_val(int v) {
    if (v == -1) return -1;
    return static_cast<int>(std::round(v * GetSystemDpiScale()));
}

inline double dip_val(double v) {
    return v * GetSystemDpiScale();
}

inline wxSize dip(int w, int h) {
    return wxSize(w == -1 ? -1 : dip_val(w), h == -1 ? -1 : dip_val(h));
}

inline wxSize dip(const wxSize& sz) {
    return dip(sz.x, sz.y);
}

} // namespace LinguaAlpaca::UI

// Global user-defined literals
inline int operator"" _dip(unsigned long long val) {
    return LinguaAlpaca::UI::dip_val(static_cast<int>(val));
}

inline double operator"" _dip(long double val) {
    return LinguaAlpaca::UI::dip_val(static_cast<double>(val));
}
