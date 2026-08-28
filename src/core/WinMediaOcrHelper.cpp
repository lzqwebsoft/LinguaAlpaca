#pragma execution_character_set("utf-8")
#include "WinMediaOcrHelper.hpp"
#include "Logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// C++/WinRT for native Windows 10/11 Media OCR
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Globalization.h>

#include <algorithm>
#include <vector>

#pragma comment(lib, "windowsapp.lib")

namespace LinguaAlpaca {

namespace {

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;
}

std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace

bool WinMediaOcrHelper::TryExtract(int startX, int startY, int endX, int endY, std::string& outText, int& outAnchorX, int& outAnchorY) {
    try {
        // 计算截取矩形 (带安全 padding)
        int minX = (std::min)(startX, endX) - 10;
        int maxX = (std::max)(startX, endX) + 10;
        int minY = (std::min)(startY, endY) - 8;
        int maxY = (std::max)(startY, endY) + 8;

        int screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int screenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        // 如果是多行划选 (高度超过单行常规高度 ~20px)，自动对齐目标窗口/终端的完整可见水平宽度，杜绝切断单行文本
        if (maxY - minY > 22) {
            POINT ptEnd = { endX, endY };
            HWND hwndTarget = WindowFromPoint(ptEnd);
            if (hwndTarget) {
                RECT rcWnd = { 0 };
                if (GetWindowRect(hwndTarget, &rcWnd) && (rcWnd.right - rcWnd.left > 80)) {
                    minX = (std::max)(screenLeft, (int)rcWnd.left + 2);
                    maxX = (std::min)(screenLeft + screenW, (int)rcWnd.right - 2);
                } else {
                    minX -= 200;
                    maxX += 200;
                }
            } else {
                minX -= 200;
                maxX += 200;
            }
        } else {
            // 单行划词小位移自适应填充
            if (maxX - minX < 80) {
                minX -= 30;
                maxX += 30;
            }
        }

        // 屏幕边界安全裁剪
        minX = (std::max)(screenLeft, minX);
        minY = (std::max)(screenTop, minY);
        maxX = (std::min)(screenLeft + screenW, maxX);
        maxY = (std::min)(screenTop + screenH, maxY);

        int width = maxX - minX;
        int height = maxY - minY;

        if (width <= 10 || height <= 10 || width > 3840 || height > 2160) {
            return false;
        }

        // 1. 使用 GDI 截取选区屏幕图像
        struct GdiScope {
            HDC hdcScreen{nullptr};
            HDC hdcMem{nullptr};
            HBITMAP hbm{nullptr};
            ~GdiScope() {
                if (hbm) DeleteObject(hbm);
                if (hdcMem) DeleteDC(hdcMem);
                if (hdcScreen) ReleaseDC(nullptr, hdcScreen);
            }
        } gdi;

        gdi.hdcScreen = GetDC(nullptr);
        gdi.hdcMem = CreateCompatibleDC(gdi.hdcScreen);
        if (!gdi.hdcScreen || !gdi.hdcMem) {
            return false;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // Top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pPixels = nullptr;
        gdi.hbm = CreateDIBSection(gdi.hdcMem, &bmi, DIB_RGB_COLORS, &pPixels, nullptr, 0);
        if (!gdi.hbm || !pPixels) {
            return false;
        }

        HGDIOBJ oldBmp = SelectObject(gdi.hdcMem, gdi.hbm);
        BitBlt(gdi.hdcMem, 0, 0, width, height, gdi.hdcScreen, minX, minY, SRCCOPY);
        SelectObject(gdi.hdcMem, oldBmp);

        // 2. 将像素数据传入 WinRT SoftwareBitmap
        size_t totalBytes = static_cast<size_t>(width) * height * 4;
        winrt::Windows::Storage::Streams::DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(static_cast<const uint8_t*>(pPixels), totalBytes));
        auto buffer = writer.DetachBuffer();

        auto softwareBitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
            buffer,
            winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
            width,
            height
        );

        // 3. 运行 Windows 原生 Media.Ocr
        // 优先使用 en-US / 用户语言，保证代码、路径与英文字符零乱码识别
        static winrt::Windows::Media::Ocr::OcrEngine s_engine = []() {
            try {
                if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(winrt::Windows::Globalization::Language(L"en-US"))) {
                    return winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
                        winrt::Windows::Globalization::Language(L"en-US")
                    );
                }
                auto eng = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
                if (eng) return eng;
                return winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
                    winrt::Windows::Globalization::Language(L"zh-Hans-CN")
                );
            } catch (...) {
                return winrt::Windows::Media::Ocr::OcrEngine{nullptr};
            }
        }();

        if (!s_engine) {
            return false;
        }

        auto ocrResult = s_engine.RecognizeAsync(softwareBitmap).get();
        if (!ocrResult) {
            return false;
        }

        // 精确按行重组文本，保留终端各行的换行格式
        std::wstring reconstructed;
        for (const auto& line : ocrResult.Lines()) {
            std::wstring lineStr = line.Text().c_str();
            if (lineStr.empty()) continue;
            if (!reconstructed.empty()) {
                reconstructed += L"\n";
            }
            reconstructed += lineStr;
        }

        std::string text = Trim(WideToUtf8(reconstructed));
        if (text.empty()) {
            return false;
        }

        outText = text;

        // 计算识别行中最底部的坐标
        double maxLineBottom = 0;
        double maxLineRight = 0;
        for (const auto& line : ocrResult.Lines()) {
            auto rect = line.Words().Size() > 0 ? line.Words().GetAt(line.Words().Size() - 1).BoundingRect() : winrt::Windows::Foundation::Rect{};
            double right = rect.X + rect.Width;
            double bottom = rect.Y + rect.Height;
            if (bottom > maxLineBottom) maxLineBottom = bottom;
            if (right > maxLineRight) maxLineRight = right;
        }

        if (maxLineRight > 0 && maxLineBottom > 0) {
            outAnchorX = minX + static_cast<int>(maxLineRight);
            outAnchorY = minY + static_cast<int>(maxLineBottom) + 6;
        } else {
            outAnchorX = maxX;
            outAnchorY = maxY + 6;
        }

        return true;
    } catch (const std::exception& e) {
        LOG_WARN("ScreenOCR", std::string("WinRT OCR Exception: ") + e.what());
        return false;
    } catch (...) {
        LOG_WARN("ScreenOCR", "WinRT OCR unknown exception.");
        return false;
    }
}

} // namespace LinguaAlpaca

#else

namespace LinguaAlpaca {
bool WinMediaOcrHelper::TryExtract(int, int, int, int, std::string&, int&, int&) {
    return false;
}
} // namespace LinguaAlpaca

#endif
