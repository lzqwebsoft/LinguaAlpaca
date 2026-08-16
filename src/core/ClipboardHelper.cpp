#pragma execution_character_set("utf-8")
#include "ClipboardHelper.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <thread>
#include <chrono>
#include <vector>

namespace LinguaAlpaca {

namespace {

// UTF-8 -> std::wstring
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
    return wstr;
}

// std::wstring -> UTF-8
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;
}

bool OpenClipboardWithRetry(HWND hwnd, int maxRetries = 6, int delayMs = 10) {
    for (int i = 0; i < maxRetries; ++i) {
        if (OpenClipboard(hwnd)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return false;
}

} // namespace

std::string ClipboardHelper::GetClipboardText() {
    if (!OpenClipboardWithRetry(nullptr)) {
        return "";
    }

    std::string result;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
        if (pText) {
            result = WideToUtf8(std::wstring(pText));
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

bool ClipboardHelper::SetClipboardText(const std::string& text) {
    if (!OpenClipboardWithRetry(nullptr)) {
        return false;
    }

    EmptyClipboard();
    std::wstring wText = Utf8ToWide(text);
    size_t byteSize = (wText.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, byteSize);
    if (!hGlob) {
        CloseClipboard();
        return false;
    }

    void* pBuf = GlobalLock(hGlob);
    if (pBuf) {
        memcpy(pBuf, wText.c_str(), byteSize);
        GlobalUnlock(hGlob);
        SetClipboardData(CF_UNICODETEXT, hGlob);
    } else {
        GlobalFree(hGlob);
    }

    CloseClipboard();
    return true;
}

bool ClipboardHelper::HasText() {
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
}

bool ClipboardHelper::SendCtrlC() {
    // 构造 Ctrl+C 击键序列
    INPUT inputs[4] = {};

    // 1. Ctrl Down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    // 2. 'C' Down
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';

    // 3. 'C' Up
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    // 4. Ctrl Up
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    UINT sent = SendInput(4, inputs, sizeof(INPUT));
    return sent == 4;
}

std::string ClipboardHelper::GetSelectedTextViaSendInput(bool preserveClipboard) {
    std::wstring originalText;
    bool hasOriginalText = false;
    DWORD origSeq = GetClipboardSequenceNumber();

    // 1. 如果需要保护剪贴板，先备份原剪贴板内容
    if (preserveClipboard) {
        if (OpenClipboardWithRetry(nullptr, 4, 8)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
                if (pText) {
                    originalText = pText;
                    hasOriginalText = true;
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
    }

    // 2. 发送 Ctrl+C 模拟按键
    SendCtrlC();

    // 3. 等待目标程序响应复制并更新剪贴板（最多等待 120ms）
    std::string selectedText;
    for (int i = 0; i < 12; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        DWORD currentSeq = GetClipboardSequenceNumber();
        if (currentSeq != origSeq || i >= 4) {
            // 尝试读取剪贴板
            if (OpenClipboardWithRetry(nullptr, 3, 5)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
                    if (pText && wcslen(pText) > 0) {
                        selectedText = WideToUtf8(std::wstring(pText));
                        GlobalUnlock(hData);
                        CloseClipboard();
                        break;
                    }
                    GlobalUnlock(hData);
                }
                CloseClipboard();
            }
        }
    }

    // 4. 如果开启保护剪贴板，恢复原始剪贴板数据
    if (preserveClipboard) {
        // 短暂延迟确保当前读取完成
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        if (OpenClipboardWithRetry(nullptr, 5, 10)) {
            EmptyClipboard();
            if (hasOriginalText && !originalText.empty()) {
                size_t byteSize = (originalText.size() + 1) * sizeof(wchar_t);
                HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, byteSize);
                if (hGlob) {
                    void* pBuf = GlobalLock(hGlob);
                    if (pBuf) {
                        memcpy(pBuf, originalText.c_str(), byteSize);
                        GlobalUnlock(hGlob);
                        SetClipboardData(CF_UNICODETEXT, hGlob);
                    } else {
                        GlobalFree(hGlob);
                    }
                }
            }
            CloseClipboard();
        }
    }

    // 去除首尾空白字符
    if (!selectedText.empty()) {
        size_t start = selectedText.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = selectedText.find_last_not_of(" \t\r\n");
        selectedText = selectedText.substr(start, end - start + 1);
    }

    return selectedText;
}

} // namespace LinguaAlpaca

#else // Non-Windows fallback

namespace LinguaAlpaca {
std::string ClipboardHelper::GetClipboardText() { return ""; }
bool ClipboardHelper::SetClipboardText(const std::string&) { return false; }
std::string ClipboardHelper::GetSelectedTextViaSendInput(bool) { return ""; }
bool ClipboardHelper::HasText() { return false; }
bool ClipboardHelper::SendCtrlC() { return false; }
} // namespace LinguaAlpaca

#endif
