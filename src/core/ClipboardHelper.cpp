#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif
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

  auto addKey = [&](WORD vk, DWORD flags) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = flags;
    inputs.push_back(in);
  };

  // 2. 如果用户当前按住了 Shift / Alt / Win，先临时发送释放，避免快捷键冲突
  if (shiftPhysicallyDown) addKey(VK_SHIFT, KEYEVENTF_KEYUP);
  if (altPhysicallyDown) addKey(VK_MENU, KEYEVENTF_KEYUP);
  if (winPhysicallyDown) addKey(VK_LWIN, KEYEVENTF_KEYUP);

  // 3. 确保 Ctrl 处于按下状态
  if (!ctrlPhysicallyDown) {
    addKey(VK_CONTROL, 0);
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    inputs.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // 4. 模拟按下并释放目标键 (携带真实硬件扫描码，确保 Adobe Acrobat / 沙箱应用完整接收)
  addKey(vkKey, 0);
  addKey(vkKey, KEYEVENTF_KEYUP);
  SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  inputs.clear();

  // 5. 释放 Ctrl (仅在用户物理上未按 Ctrl 时)
  if (!ctrlPhysicallyDown) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    addKey(VK_CONTROL, KEYEVENTF_KEYUP);
  }

  // 6. 恢复用户原本按住的 Shift / Alt / Win 物理按键状态
  if (shiftPhysicallyDown) addKey(VK_SHIFT, 0);
  if (altPhysicallyDown) addKey(VK_MENU, 0);
  if (winPhysicallyDown) addKey(VK_LWIN, 0);

  if (!inputs.empty()) {
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  }
  return true;
}

bool ClipboardHelper::IsPdfReaderWindow(HWND hwnd) {
  if (!hwnd) return false;

  // 1. 类名检测 (自身类名与顶级祖先类名)
  wchar_t className[128] = { 0 };
  if (GetClassNameW(hwnd, className, 128)) {
    if (_wcsicmp(className, L"AcrobatSDIWindow") == 0 ||
        _wcsicmp(className, L"AVL_AVView") == 0 ||
        _wcsicmp(className, L"AdobeAcrobat") == 0 ||
        _wcsicmp(className, L"FoxitReader") == 0 ||
        _wcsicmp(className, L"SumatraPDF") == 0 ||
        _wcsicmp(className, L"PDFXEdit") == 0 ||
        _wcsicmp(className, L"CAJViewerClass") == 0) {
      return true;
    }
  }

  HWND hRoot = GetAncestor(hwnd, GA_ROOT);
  if (hRoot && hRoot != hwnd && GetClassNameW(hRoot, className, 128)) {
    if (_wcsicmp(className, L"AcrobatSDIWindow") == 0 ||
        _wcsicmp(className, L"AdobeAcrobat") == 0 ||
        _wcsicmp(className, L"FoxitReader") == 0 ||
        _wcsicmp(className, L"SumatraPDF") == 0 ||
        _wcsicmp(className, L"PDFXEdit") == 0) {
      return true;
    }
  }

  // 2. 进程路径与可执行文件名检测
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != 0) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
      wchar_t fullPath[MAX_PATH] = { 0 };
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
        std::wstring path(fullPath);
        for (auto& c : path) c = towlower(c);
        if (path.find(L"acrobat") != std::wstring::npos ||
            path.find(L"acrord") != std::wstring::npos ||
            path.find(L"adobe") != std::wstring::npos ||
            path.find(L"foxit") != std::wstring::npos ||
            path.find(L"sumatra") != std::wstring::npos ||
            path.find(L"pdfxedit") != std::wstring::npos ||
            path.find(L"cajviewer") != std::wstring::npos ||
            path.find(L"wpspdf") != std::wstring::npos) {
          CloseHandle(hProc);
          return true;
        }
      }
      CloseHandle(hProc);
    }
  }
  return false;
}

bool ClipboardHelper::IsNonAxTargetWindow(HWND hwnd) {
  if (!hwnd) return false;

  // 1. 首先检测是否属于 PDF 阅读器 (Adobe Acrobat/Reader, Foxit, Sumatra 等)
  if (IsPdfReaderWindow(hwnd)) {
    return true;
  }

  // 2. 类名检测 (常见终端、特定自绘容器)
  wchar_t className[128] = { 0 };
  if (GetClassNameW(hwnd, className, 128)) {
    if (_wcsicmp(className, L"ConsoleWindowClass") == 0 ||
        _wcsicmp(className, L"mintty") == 0 ||
        _wcsicmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0) {
      return true;
    }
  }

  // 3. 进程可执行文件名称检测 (VS Code, Cursor, Antigravity, WPS, Sublime, JetBrains, 各类终端等)
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != 0) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
      wchar_t fullPath[MAX_PATH] = { 0 };
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
        std::wstring path(fullPath);
        for (auto& c : path) c = towlower(c);
        if (path.find(L"code.exe") != std::wstring::npos ||
            path.find(L"cursor.exe") != std::wstring::npos ||
            path.find(L"antigravity.exe") != std::wstring::npos ||
            path.find(L"wps.exe") != std::wstring::npos ||
            path.find(L"wpp.exe") != std::wstring::npos ||
            path.find(L"et.exe") != std::wstring::npos ||
            path.find(L"wpspdf.exe") != std::wstring::npos ||
            path.find(L"sublime_text.exe") != std::wstring::npos ||
            path.find(L"notepad++.exe") != std::wstring::npos ||
            path.find(L"windowsterminal.exe") != std::wstring::npos ||
            path.find(L"mintty.exe") != std::wstring::npos ||
            path.find(L"mobaxterm.exe") != std::wstring::npos ||
            path.find(L"xshell.exe") != std::wstring::npos ||
            path.find(L"securecrt.exe") != std::wstring::npos ||
            path.find(L"conemu.exe") != std::wstring::npos ||
            path.find(L"conemu64.exe") != std::wstring::npos ||
            path.find(L"wezterm.exe") != std::wstring::npos ||
            path.find(L"alacritty.exe") != std::wstring::npos ||
            path.find(L"putty.exe") != std::wstring::npos ||
            path.find(L"idea") != std::wstring::npos ||
            path.find(L"clion") != std::wstring::npos ||
            path.find(L"pycharm") != std::wstring::npos ||
            path.find(L"webstorm") != std::wstring::npos ||
            path.find(L"goland") != std::wstring::npos ||
            path.find(L"rider") != std::wstring::npos ||
            path.find(L"rustrover") != std::wstring::npos ||
            path.find(L"studio64") != std::wstring::npos ||
            path.find(L"datagrip") != std::wstring::npos ||
            path.find(L"phpstorm") != std::wstring::npos ||
            path.find(L"rubymine") != std::wstring::npos) {
          CloseHandle(hProc);
          return true;
        }
      }
      CloseHandle(hProc);
    }
  }

  return false;
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

#ifdef _WIN32
  HWND fgWnd = GetForegroundWindow();
  bool targetIsPdf = IsPdfReaderWindow(fgWnd);

  // 2. 阶段 1：除专有 PDF 阅读器以外的全部软件（VS Code 终端、编辑器、浏览器、Office 等），
  // 优先发送无破坏性的 Ctrl+Insert（极大拓宽无破坏性复制的适用面，绝不误清除终端或网页选区）
  if (!targetIsPdf) {
    SendCopyKey(VK_INSERT);
    for (int i = 0; i < 8; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      DWORD currentSeq = GetClipboardSequenceNumber();
      if (currentSeq != origSeq) {
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
        if (!selectedText.empty()) {
          copySeq = currentSeq;
          break;
        }
      }
    }
  }

  // 3. 阶段 2：针对 PDF 阅读器（如 Adobe Acrobat/Reader、Foxit 等）或者非 PDF 软件未响应 Ctrl+Insert 时：
  // 发送标准硬件扫描码级 Ctrl+C！
  // 针对 Adobe Acrobat 弹出黑色快捷工具栏丢键或多格式延迟写入的超高敏保障：
  // ① 坚决不发送会误导致 Acrobat 取消选区的 VK_INSERT。
  // ② 轮询期间若剪贴板正在被 Acrobat 锁定写入，不提前 break，持续等待读取。
  // ③ 若前 70ms 剪贴板未更新（Acrobat 忙于生成快捷栏丢键），自动重发一次 Ctrl+C 兜底！
  if (selectedText.empty()) {
    SendCopyKey('C');
    for (int i = 0; i < 24; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      DWORD currentSeq = GetClipboardSequenceNumber();
      if (currentSeq != origSeq) {
        if (OpenClipboardWithRetry(nullptr, 5, 8)) {
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
        // 关键点：只有确实拿到非空文本才完成退出，避免在宿主写入多格式过程中提早退出
        if (!selectedText.empty()) {
          copySeq = currentSeq;
          break;
        }
      }

      // 如果前 70ms 剪贴板无响应，说明初次击键可能被刚弹出的黑条工具栏吞掉，自动重发一次
      if (i == 7 && currentSeq == origSeq) {
        SendCopyKey('C');
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

#else // Non-Windows fallback using wxTheClipboard and macOS native pasteboard / CGEvent

#include <wx/clipbrd.h>
#include <wx/dataobj.h>

#include "Logger.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#include <chrono>
#include <thread>
#endif

namespace LinguaAlpaca {

std::string ClipboardHelper::GetClipboardText() {
    if (wxTheClipboard && wxTheClipboard->Open()) {
        if (wxTheClipboard->IsSupported(wxDF_TEXT) || wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) {
            wxTextDataObject data;
            wxTheClipboard->GetData(data);
            wxTheClipboard->Close();
            return data.GetText().ToUTF8().data();
        }
        wxTheClipboard->Close();
    }
    return "";
}

bool ClipboardHelper::SetClipboardText(const std::string &text) {
    if (wxTheClipboard && wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text)));
        wxTheClipboard->Flush();
        wxTheClipboard->Close();
        return true;
    }
    return false;
}

bool ClipboardHelper::HasText() {
    if (wxTheClipboard && wxTheClipboard->Open()) {
        bool has = wxTheClipboard->IsSupported(wxDF_TEXT) || wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
        wxTheClipboard->Close();
        return has;
    }
    return false;
}

#if defined(__APPLE__)

bool ClipboardHelper::SendCtrlC() {
    @autoreleasepool {
        CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
        if (!source) {
            source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
        }
        if (!source) return false;

        CGKeyCode cCode = static_cast<CGKeyCode>(kVK_ANSI_C);
        CGEventRef keyDown = CGEventCreateKeyboardEvent(source, cCode, true);
        CGEventRef keyUp = CGEventCreateKeyboardEvent(source, cCode, false);

        if (!keyDown || !keyUp) {
            if (keyDown) CFRelease(keyDown);
            if (keyUp) CFRelease(keyUp);
            CFRelease(source);
            return false;
        }

        CGEventSetFlags(keyDown, kCGEventFlagMaskCommand);
        CGEventSetFlags(keyUp, kCGEventFlagMaskCommand);

        // 1. 优先投递到 kCGSessionEventTap（当前用户登录会话事件流，确保目标应用可靠接收 Cmd+C）
        CGEventPost(kCGSessionEventTap, keyDown);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        CGEventPost(kCGSessionEventTap, keyUp);

        // 2. 同时投递到 kCGHIDEventTap（系统 HID 层）提供全面双重保障
        CGEventPost(kCGHIDEventTap, keyDown);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CGEventPost(kCGHIDEventTap, keyUp);

        CFRelease(keyDown);
        CFRelease(keyUp);
        CFRelease(source);
        return true;
    }
}

std::string ClipboardHelper::GetSelectedTextViaSendInput(bool preserveClipboard) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        if (!pb) return "";

        NSInteger initialChangeCount = [pb changeCount];

        // 1. 如果开启了剪贴板保护，完整备份当前剪贴板中所有项及各类型数据
        NSMutableArray<NSDictionary<NSPasteboardType, NSData*>*> *savedItems = nil;
        if (preserveClipboard) {
            NSArray<NSPasteboardItem *> *items = [pb pasteboardItems];
            if (items && items.count > 0) {
                savedItems = [NSMutableArray arrayWithCapacity:items.count];
                for (NSPasteboardItem *item in items) {
                    NSMutableDictionary<NSPasteboardType, NSData*> *dict = [NSMutableDictionary dictionary];
                    for (NSPasteboardType type in [item types]) {
                        NSData *data = [item dataForType:type];
                        if (data) {
                            [dict setObject:data forKey:type];
                        }
                    }
                    if (dict.count > 0) {
                        [savedItems addObject:dict];
                    }
                }
            }
        }

        // 2. 模拟发送 Cmd+C
        if (!SendCtrlC()) {
            LOG_ERROR("ClipboardHelper", "SendCtrlC failed to post keyboard events");
            return "";
        }

        // 3. 轮询等待系统剪贴板更新并成功解析出文本（最长约 350ms）
        std::string selectedText;
        NSInteger copyChangeCount = 0;

        for (int i = 0; i < 35; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            NSInteger currentChangeCount = [pb changeCount];
            if (currentChangeCount != initialChangeCount) {
                // 优先通过 readObjectsForClasses 读取（自动兼容纯文本、富文本 RTF、HTML 及延迟加载的 Promised Text）
                NSArray *classes = @[[NSString class], [NSAttributedString class]];
                NSArray *objects = [pb readObjectsForClasses:classes options:nil];
                NSString *str = nil;
                if (objects && objects.count > 0) {
                    id obj = objects[0];
                    if ([obj isKindOfClass:[NSString class]]) {
                        str = (NSString*)obj;
                    } else if ([obj isKindOfClass:[NSAttributedString class]]) {
                        str = [(NSAttributedString*)obj string];
                    }
                }
                if (!str || str.length == 0) {
                    str = [pb stringForType:NSPasteboardTypeString];
                }
                if (!str || str.length == 0) {
                    str = [pb stringForType:@"public.utf8-plain-text"];
                }
                if (!str || str.length == 0) {
                    str = [pb stringForType:@"NSStringPboardType"];
                }

                if (str && str.length > 0) {
                    NSString *trimmed = [str stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    if (trimmed && trimmed.length > 0) {
                        selectedText = [trimmed UTF8String];
                        copyChangeCount = currentChangeCount;
                        LOG_INFO("ClipboardHelper", "GetSelectedTextViaSendInput: copied successfully at " + std::to_string((i + 1) * 10) + "ms, len=" + std::to_string(selectedText.size()));
                        break; // 成功解析到有效文本才退出轮询
                    }
                }
            }

            // 若前 70ms / 170ms 宿主应用未响应（如 WPS/浏览器/Acrobat 刚弹出工具条丢键），自动重发一次 Cmd+C 兜底
            if ((i == 7 || i == 17) && currentChangeCount == initialChangeCount) {
                SendCtrlC();
            }
        }

        if (selectedText.empty()) {
            LOG_INFO("ClipboardHelper", "GetSelectedTextViaSendInput: timeout, clipboard unchanged (count=" + std::to_string([pb changeCount]) + ")");
            return "";
        }

        // 4. 若开启剪贴板保护且备份有效，恢复原本的剪贴板全部内容
        if (preserveClipboard && savedItems && savedItems.count > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            // 确保在恢复前，用户没有进行新的复制操作
            if ([pb changeCount] == copyChangeCount) {
                [pb clearContents];
                NSMutableArray<NSPasteboardItem*> *restoreItems = [NSMutableArray arrayWithCapacity:savedItems.count];
                for (NSDictionary<NSPasteboardType, NSData*> *dict in savedItems) {
                    NSPasteboardItem *newItem = [[NSPasteboardItem alloc] init];
                    [dict enumerateKeysAndObjectsUsingBlock:^(NSPasteboardType type, NSData *data, BOOL *stop) {
                        [newItem setData:data forType:type];
                    }];
                    [restoreItems addObject:newItem];
                }
                [pb writeObjects:restoreItems];
            }
        }

        return selectedText;
    }
}

#else

std::string ClipboardHelper::GetSelectedTextViaSendInput(bool) { return ""; }
bool ClipboardHelper::SendCtrlC() { return false; }

#endif

} // namespace LinguaAlpaca

#endif
