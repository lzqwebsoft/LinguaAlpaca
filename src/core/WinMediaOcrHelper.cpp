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

        // 如果只是单点点击或极小位移，扩大采样区域
        if (maxX - minX < 80) {
            minX -= 40;
            maxX += 40;
        }
        if (maxY - minY < 40) {
            minY -= 20;
            maxY += 20;
        }

        int width = maxX - minX;
        int height = maxY - minY;

        if (width <= 10 || height <= 10 || width > 1600 || height > 1200) {
            return false;
        }

        // 1. 使用 GDI 截取选区屏幕图像
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcScreen || !hdcMem) {
            if (hdcMem) DeleteDC(hdcMem);
            if (hdcScreen) ReleaseDC(nullptr, hdcScreen);
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
        HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pPixels, nullptr, 0);
        if (!hbm || !pPixels) {
            if (hbm) DeleteObject(hbm);
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return false;
        }

        HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);
        BitBlt(hdcMem, 0, 0, width, height, hdcScreen, minX, minY, SRCCOPY);
        SelectObject(hdcMem, oldBmp);

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

        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);

        // 3. 运行 Windows 原生 Media.Ocr
        auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
                winrt::Windows::Globalization::Language(L"zh-Hans-CN")
            );
        }

        if (!engine) {
            return false;
        }

        auto ocrResult = engine.RecognizeAsync(softwareBitmap).get();
        if (!ocrResult) {
            return false;
        }

        std::wstring rawText = ocrResult.Text().c_str();
        std::string text = Trim(WideToUtf8(rawText));
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
