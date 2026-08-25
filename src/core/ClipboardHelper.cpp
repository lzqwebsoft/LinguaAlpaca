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

struct ClipboardFormatData {
    UINT format{0};
    std::vector<uint8_t> buffer;
};

struct ClipboardBackup {
    std::vector<ClipboardFormatData> items;
    bool isValid{false};
};

static ClipboardBackup BackupEntireClipboard() {
    ClipboardBackup backup;
    if (!OpenClipboardWithRetry(nullptr, 4, 8)) {
        return backup;
    }

    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        // GDI 句柄（如 CF_BITMAP）不能直接当作内存块复制，CF_DIB / CF_DIBV5 包含完整的位图数据，支持直接全局内存复制
        if (format == CF_BITMAP || format == CF_PALETTE || format == CF_METAFILEPICT || format == CF_ENHMETAFILE) {
            continue;
        }

        HANDLE hData = GetClipboardData(format);
        if (hData) {
            SIZE_T size = GlobalSize(hData);
            if (size > 0 && size <= 32 * 1024 * 1024) { // 32MB 安全上限
                void* pData = GlobalLock(hData);
                if (pData) {
                    ClipboardFormatData item;
                    item.format = format;
                    item.buffer.resize(size);
                    memcpy(item.buffer.data(), pData, size);
                    GlobalUnlock(hData);
                    backup.items.push_back(std::move(item));
                }
            }
        }
    }

    CloseClipboard();
    backup.isValid = !backup.items.empty();
    return backup;
}

static bool RestoreEntireClipboard(const ClipboardBackup& backup) {
    if (!backup.isValid || backup.items.empty()) {
        return false;
    }

    if (!OpenClipboardWithRetry(nullptr, 5, 10)) {
        return false;
    }

    EmptyClipboard();

    for (const auto& item : backup.items) {
        if (item.buffer.empty()) continue;
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, item.buffer.size());
        if (hGlob) {
            void* pBuf = GlobalLock(hGlob);
            if (pBuf) {
                memcpy(pBuf, item.buffer.data(), item.buffer.size());
                GlobalUnlock(hGlob);
                SetClipboardData(item.format, hGlob);
            } else {
                GlobalFree(hGlob);
            }
        }
    }

    CloseClipboard();
    return true;
}

std::string ClipboardHelper::GetSelectedTextViaSendInput(bool preserveClipboard) {
    ClipboardBackup backup;
    DWORD origSeq = GetClipboardSequenceNumber();

    // 1. 如果需要保护剪贴板，完整备份当前剪贴板中所有格式的数据（包括文字、图片/截图 CF_DIB、文件、富文本等）
    if (preserveClipboard) {
        backup = BackupEntireClipboard();
    }

    // 2. 发送 Ctrl+C 模拟按键
    SendCtrlC();

    // 3. 等待目标程序响应复制并更新剪贴板（必须检测到剪贴板序列号变化，证明有真实选中的文本被复制）
    std::string selectedText;
    bool newContentCopied = false;

    for (int i = 0; i < 15; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        DWORD currentSeq = GetClipboardSequenceNumber();
        if (currentSeq != origSeq) {
            newContentCopied = true;
            // 尝试读取新复制到剪贴板的内容
            if (OpenClipboardWithRetry(nullptr, 4, 6)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
                    if (pText && wcslen(pText) > 0) {
                        selectedText = WideToUtf8(std::wstring(pText));
                    }
                    GlobalUnlock(hData);
                }
                CloseClipboard();
            }
            break;
        }
    }

    // 如果未成功复制到新文本（例如用户在截图、拖拽窗口或未选中任何文本）：
    // 绝对不触碰剪贴板，绝对不执行 EmptyClipboard()，确保用户现有的截图/图片/文件完全不受污染！
    if (!newContentCopied) {
        return "";
    }

    // 4. 只有在确实成功复制了新文本且开启了剪贴板保护时，才恢复原本的所有格式数据
    if (preserveClipboard && backup.isValid) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        RestoreEntireClipboard(backup);
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
