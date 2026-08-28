#pragma execution_character_set("utf-8")
#include "ClipboardHelper.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>


namespace LinguaAlpaca {

namespace {

// UTF-8 -> std::wstring
std::wstring Utf8ToWide(const std::string &str) {
  if (str.empty())
    return L"";
  int size =
      MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
  if (size <= 0)
    return L"";
  std::wstring wstr(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
  return wstr;
}

// std::wstring -> UTF-8
std::string WideToUtf8(const std::wstring &wstr) {
  if (wstr.empty())
    return "";
  int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(),
                                 nullptr, 0, nullptr, nullptr);
  if (size <= 0)
    return "";
  std::string str(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size,
                      nullptr, nullptr);
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

bool ClipboardHelper::SetClipboardText(const std::string &text) {
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

  void *pBuf = GlobalLock(hGlob);
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

#ifdef _WIN32
static bool SendCopyKey(WORD vkKey) {
  // 1. 严格检测当前物理按键状态 (Ctrl, Shift, Alt, Win)
  bool ctrlPhysicallyDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
                            (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
                            (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
  bool shiftPhysicallyDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
  bool altPhysicallyDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
  bool winPhysicallyDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

  std::vector<INPUT> inputs;

  // 2. 如果用户当前按住了 Shift / Alt / Win，先临时发送释放，避免冲突
  if (shiftPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_SHIFT;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(in);
  }
  if (altPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_MENU;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(in);
  }
  if (winPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_LWIN;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(in);
  }

  // 3. 确保 Ctrl 处于按下状态
  if (!ctrlPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_CONTROL;
    inputs.push_back(in);
  }

  // 4. 模拟按下并释放目标键 (如 VK_INSERT 或 'C')
  {
    INPUT inDown = {};
    inDown.type = INPUT_KEYBOARD;
    inDown.ki.wVk = vkKey;
    inputs.push_back(inDown);

    INPUT inUp = {};
    inUp.type = INPUT_KEYBOARD;
    inUp.ki.wVk = vkKey;
    inUp.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(inUp);
  }

  // 5. 释放 Ctrl (仅在用户物理上未按 Ctrl 时)
  if (!ctrlPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_CONTROL;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(in);
  }

  // 6. 恢复用户原本按住的 Shift / Alt / Win 物理按键状态
  if (shiftPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_SHIFT;
    inputs.push_back(in);
  }
  if (altPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_MENU;
    inputs.push_back(in);
  }
  if (winPhysicallyDown) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_LWIN;
    inputs.push_back(in);
  }

  if (inputs.empty())
    return false;
  UINT sent =
      SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  return sent == inputs.size();
}
#endif

bool ClipboardHelper::SendCtrlC() {
#ifdef _WIN32
  return SendCopyKey('C');
#else
  return false;
#endif
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
    if (format == CF_BITMAP || format == CF_PALETTE ||
        format == CF_METAFILEPICT || format == CF_ENHMETAFILE) {
      continue;
    }

    HANDLE hData = GetClipboardData(format);
    if (hData) {
      SIZE_T size = GlobalSize(hData);
      if (size > 0 && size <= 32 * 1024 * 1024) { // 32MB 安全上限
        void *pData = GlobalLock(hData);
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

static bool RestoreEntireClipboard(const ClipboardBackup &backup) {
  if (!backup.isValid || backup.items.empty()) {
    return false;
  }

  if (!OpenClipboardWithRetry(nullptr, 5, 10)) {
    return false;
  }

  EmptyClipboard();

  for (const auto &item : backup.items) {
    if (item.buffer.empty())
      continue;
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, item.buffer.size());
    if (hGlob) {
      void *pBuf = GlobalLock(hGlob);
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

std::string
ClipboardHelper::GetSelectedTextViaSendInput(bool preserveClipboard) {
  ClipboardBackup backup;
  DWORD origSeq = GetClipboardSequenceNumber();

  // 1. 如果需要保护剪贴板，完整备份当前剪贴板中所有格式的数据
  if (preserveClipboard) {
    backup = BackupEntireClipboard();
  }

  std::string selectedText;
  DWORD copySeq = 0;

  // 2. 阶段 A：首选发送无破坏性的 Ctrl+Insert (Windows 全局工业标准复制快捷键)
  // 关键优势：在 VS Code 终端 (xterm.js)、各类终端及主流文本编辑器中，
  // Ctrl+Insert 会直接由浏览器/系统层执行复制，而绝不触发 xterm.js 绑定的
  // Ctrl+C 取消选区 (clearSelection) 行为！
#ifdef _WIN32
  SendCopyKey(VK_INSERT);

  // 快速检测剪贴板是否更新 (最长 60ms)
  for (int i = 0; i < 6; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    DWORD currentSeq = GetClipboardSequenceNumber();
    if (currentSeq != origSeq) {
      copySeq = currentSeq;
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

  // 阶段 B：如果目标程序不响应 Ctrl+Insert，回退发送传统 Ctrl+C
  if (selectedText.empty()) {
    SendCopyKey('C');
    for (int i = 0; i < 12; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      DWORD currentSeq = GetClipboardSequenceNumber();
      if (currentSeq != origSeq) {
        copySeq = currentSeq;
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
  }
#endif

  // 去除首尾空白字符
  if (!selectedText.empty()) {
    size_t start = selectedText.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      selectedText = "";
    } else {
      size_t end = selectedText.find_last_not_of(" \t\r\n");
      selectedText = selectedText.substr(start, end - start + 1);
    }
  }

  // 如果未成功复制到有效的纯文本，绝对不触碰剪贴板
  if (selectedText.empty()) {
    return "";
  }

  // 3.
  // 只有在确实成功复制了有效纯文本且开启了剪贴板保护时，才恢复原本的所有格式数据
  if (preserveClipboard && backup.isValid) {
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    bool userIsCopying = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) &&
                         (((GetAsyncKeyState('C') & 0x8000) != 0) ||
                          ((GetAsyncKeyState(VK_INSERT) & 0x8000) != 0));
    DWORD latestSeq = GetClipboardSequenceNumber();

    if (!userIsCopying && (latestSeq == copySeq)) {
      RestoreEntireClipboard(backup);
    }
  }

  return selectedText;
}

} // namespace LinguaAlpaca

#else // Non-Windows fallback

namespace LinguaAlpaca {
std::string ClipboardHelper::GetClipboardText() { return ""; }
bool ClipboardHelper::SetClipboardText(const std::string &) { return false; }
std::string ClipboardHelper::GetSelectedTextViaSendInput(bool) { return ""; }
bool ClipboardHelper::HasText() { return false; }
bool ClipboardHelper::SendCtrlC() { return false; }
} // namespace LinguaAlpaca

#endif
