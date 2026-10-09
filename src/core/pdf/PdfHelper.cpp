#pragma execution_character_set("utf-8")
#include "PdfHelper.hpp"
#include "core/Logger.hpp"

#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/filefn.h>
#include <algorithm>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#pragma comment(lib, "windowsapp.lib")

namespace {
// 全局 PDF 文档对象级互斥锁与单例缓存（杜绝频繁翻页或批量渲染时重复从磁盘解析数百页 PDF 结构）
std::mutex s_pdfDocMutex;
std::wstring s_cachedPdfPath;
winrt::Windows::Data::Pdf::PdfDocument s_cachedPdfDoc{nullptr};

struct WinRtApartmentScope {
    bool initialized{false};
    WinRtApartmentScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            initialized = true;
        }
    }
    ~WinRtApartmentScope() {
        if (initialized) {
            CoUninitialize();
        }
    }
};

winrt::Windows::Data::Pdf::PdfDocument GetOrLoadPdfDocLocked(const std::wstring& wpath) {
    if (s_cachedPdfDoc && s_cachedPdfPath == wpath) {
        return s_cachedPdfDoc;
    }
    s_cachedPdfDoc = nullptr;
    s_cachedPdfPath.clear();

    auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(wpath).get();
    auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
    s_cachedPdfPath = wpath;
    s_cachedPdfDoc = doc;
    return doc;
}
} // namespace
#endif

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <cmath>

namespace {
std::mutex s_macPdfDocMutex;
std::string s_macCachedPdfPath;
CGPDFDocumentRef s_macCachedPdfDoc = nullptr;

void ClearMacPdfCacheLocked() {
    if (s_macCachedPdfDoc) {
        CGPDFDocumentRelease(s_macCachedPdfDoc);
        s_macCachedPdfDoc = nullptr;
    }
    s_macCachedPdfPath.clear();
}

CGPDFDocumentRef GetOrLoadMacPdfDocLocked(const std::string& filePath) {
    if (s_macCachedPdfDoc && s_macCachedPdfPath == filePath) {
        return s_macCachedPdfDoc;
    }
    ClearMacPdfCacheLocked();

    CGDataProviderRef provider = CGDataProviderCreateWithFilename(filePath.c_str());
    if (!provider) {
        LOG_WARN("PdfHelper", "Failed to create CGDataProvider for: " + filePath);
        return nullptr;
    }
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
    CGDataProviderRelease(provider);

    if (doc) {
        s_macCachedPdfDoc = doc;
        s_macCachedPdfPath = filePath;
    } else {
        LOG_WARN("PdfHelper", "Failed to create CGPDFDocument for: " + filePath);
    }
    return doc;
}
} // namespace
#endif

namespace LinguaAlpaca {

void PdfHelper::ClearCache() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(s_pdfDocMutex);
    s_cachedPdfDoc = nullptr;
    s_cachedPdfPath.clear();
#elif defined(__APPLE__)
    std::lock_guard<std::mutex> lock(s_macPdfDocMutex);
    ClearMacPdfCacheLocked();
#endif
}

bool PdfHelper::IsPdfFile(const std::string& filePath) {
    if (filePath.empty()) return false;
    wxFileName fn(wxString::FromUTF8(filePath));
    wxString ext = fn.GetExt().Lower();
    return ext == "pdf";
}

int PdfHelper::GetPageCount(const std::string& filePath) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return 0;
    }
    if (!IsPdfFile(filePath)) {
        return 1; // 普通图像视为单页
    }

#ifdef _WIN32
    WinRtApartmentScope apt;
    try {
        std::wstring wpath = wxString::FromUTF8(filePath).ToStdWstring();
        // 替换斜杠为 Windows 标准反斜杠以兼容 WinRT StorageFile
        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');

        std::lock_guard<std::mutex> lock(s_pdfDocMutex);
        auto doc = GetOrLoadPdfDocLocked(wpath);
        return static_cast<int>(doc.PageCount());
    } catch (const winrt::hresult_error& ex) {
        LOG_WARN("PdfHelper", "Failed to query PDF page count (HRESULT): " + std::to_string(ex.code()));
        return 0;
    } catch (const std::exception& ex) {
        LOG_WARN("PdfHelper", "Failed to query PDF page count: " + std::string(ex.what()));
        return 0;
    } catch (...) {
        LOG_WARN("PdfHelper", "Unknown exception while querying PDF page count");
        return 0;
    }
#elif defined(__APPLE__)
    try {
        std::lock_guard<std::mutex> lock(s_macPdfDocMutex);
        auto doc = GetOrLoadMacPdfDocLocked(filePath);
        if (!doc) {
            return 0;
        }
        return static_cast<int>(CGPDFDocumentGetNumberOfPages(doc));
    } catch (const std::exception& ex) {
        LOG_WARN("PdfHelper", "Failed to query PDF page count: " + std::string(ex.what()));
        return 0;
    } catch (...) {
        LOG_WARN("PdfHelper", "Unknown exception while querying PDF page count");
        return 0;
    }
#else
    // Linux 或其他平台支持
    return 1;
#endif
}

bool PdfHelper::RenderPage(const std::string& filePath, int pageIndex, wxImage& outImage, int targetWidth) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return false;
    }
    if (!IsPdfFile(filePath)) {
        return outImage.LoadFile(wxString::FromUTF8(filePath));
    }

#ifdef _WIN32
    WinRtApartmentScope apt;
    try {
        std::wstring wpath = wxString::FromUTF8(filePath).ToStdWstring();
        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');

        winrt::Windows::Data::Pdf::PdfPage page{nullptr};
        {
            std::lock_guard<std::mutex> lock(s_pdfDocMutex);
            auto doc = GetOrLoadPdfDocLocked(wpath);
            if (pageIndex < 0 || pageIndex >= static_cast<int>(doc.PageCount())) {
                return false;
            }
            page = doc.GetPage(static_cast<uint32_t>(pageIndex));
        }

        if (!page) return false;

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        if (targetWidth > 0) {
            options.DestinationWidth(static_cast<uint32_t>(targetWidth));
        }

        page.RenderToStreamAsync(stream, options).get();

        uint64_t streamSize = stream.Size();
        if (streamSize == 0) return false;

        std::vector<uint8_t> buffer(static_cast<size_t>(streamSize));
        auto dataReader = winrt::Windows::Storage::Streams::DataReader(stream.GetInputStreamAt(0));
        dataReader.LoadAsync(static_cast<uint32_t>(streamSize)).get();
        dataReader.ReadBytes(winrt::array_view<uint8_t>(buffer));

        wxMemoryInputStream memStream(buffer.data(), buffer.size());
        return outImage.LoadFile(memStream, wxBITMAP_TYPE_PNG);
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPage exception: " + std::string(ex.what()));
        return false;
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPage unknown exception");
        return false;
    }
#elif defined(__APPLE__)
    try {
        std::lock_guard<std::mutex> lock(s_macPdfDocMutex);
        auto doc = GetOrLoadMacPdfDocLocked(filePath);
        if (!doc) {
            return false;
        }

        size_t totalPages = CGPDFDocumentGetNumberOfPages(doc);
        if (pageIndex < 0 || static_cast<size_t>(pageIndex) >= totalPages) {
            return false;
        }

        CGPDFPageRef page = CGPDFDocumentGetPage(doc, static_cast<size_t>(pageIndex + 1));
        if (!page) {
            return false;
        }

        CGPDFBox whichBox = kCGPDFCropBox;
        CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
        if (CGRectIsEmpty(box)) {
            box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
            whichBox = kCGPDFMediaBox;
        }
        if (box.size.width <= 0 || box.size.height <= 0) {
            return false;
        }

        int rotation = CGPDFPageGetRotationAngle(page);
        double origW = (rotation == 90 || rotation == 270) ? box.size.height : box.size.width;
        double origH = (rotation == 90 || rotation == 270) ? box.size.width : box.size.height;

        double scale = 1.0;
        if (targetWidth > 0 && origW > 0) {
            scale = static_cast<double>(targetWidth) / origW;
        }

        size_t outW = static_cast<size_t>(std::round(origW * scale));
        size_t outH = static_cast<size_t>(std::round(origH * scale));
        if (outW == 0 || outH == 0) {
            return false;
        }

        size_t bytesPerRow = outW * 4;
        std::vector<uint8_t> rgbaBuffer(outH * bytesPerRow);

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            rgbaBuffer.data(),
            outW,
            outH,
            8,
            bytesPerRow,
            colorSpace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
        );
        CGColorSpaceRelease(colorSpace);
        if (!ctx) {
            return false;
        }

        // 填充白底背景 (防止透明 PDF 页面渲染为黑色背景)
        CGRect targetRect = CGRectMake(0, 0, static_cast<CGFloat>(outW), static_cast<CGFloat>(outH));
        CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
        CGContextFillRect(ctx, targetRect);

        // CoreGraphics CGPDFPageGetDrawingTransform 在 targetRect 大于原始尺寸时不会主动放大(scale 保持 1.0)，
        // 因此先以原始物理尺寸 (origW, origH) 计算基础映射变换(正确处理旋转、裁切框与原点偏移)，
        // 再组合缩放矩阵 (scale, scale)，确保页面精准填满整个光栅化目标画布而杜绝四周大面积留白。
        CGRect baseRect = CGRectMake(0, 0, static_cast<CGFloat>(origW), static_cast<CGFloat>(origH));
        CGAffineTransform baseTransform = CGPDFPageGetDrawingTransform(page, whichBox, baseRect, 0, true);
        CGAffineTransform fullTransform = CGAffineTransformConcat(baseTransform, CGAffineTransformMakeScale(scale, scale));

        CGContextSaveGState(ctx);
        CGContextConcatCTM(ctx, fullTransform);
        CGContextClipToRect(ctx, box);
        CGContextDrawPDFPage(ctx, page);
        CGContextRestoreGState(ctx);
        CGContextRelease(ctx);

        // 转换 RGBA 像素至 wxImage RGB 格式
        unsigned char* rgbData = static_cast<unsigned char*>(malloc(outW * outH * 3));
        if (!rgbData) {
            return false;
        }

        const uint8_t* src = rgbaBuffer.data();
        unsigned char* dst = rgbData;
        size_t totalPixels = outW * outH;
        for (size_t i = 0; i < totalPixels; ++i) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst += 3;
            src += 4;
        }

        outImage.Create(static_cast<int>(outW), static_cast<int>(outH), rgbData, false);
        return outImage.IsOk();
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPage exception: " + std::string(ex.what()));
        return false;
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPage unknown exception");
        return false;
    }
#else
    return false;
#endif
}

std::string PdfHelper::RenderPageToTempFile(const std::string& filePath, int pageIndex, const std::string& tempDir, int targetWidth) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return "";
    }
    if (!IsPdfFile(filePath)) {
        // 普通图像已是栅格图像，无需再次光栅化，直接返回原图路径（调用方严禁作为临时文件删除）
        return filePath;
    }

#ifdef _WIN32
    WinRtApartmentScope apt;
    try {
        std::wstring wpath = wxString::FromUTF8(filePath).ToStdWstring();
        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');

        winrt::Windows::Data::Pdf::PdfPage page{nullptr};
        {
            std::lock_guard<std::mutex> lock(s_pdfDocMutex);
            auto doc = GetOrLoadPdfDocLocked(wpath);
            if (pageIndex < 0 || pageIndex >= static_cast<int>(doc.PageCount())) {
                return "";
            }
            page = doc.GetPage(static_cast<uint32_t>(pageIndex));
        }

        if (!page) return "";

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        if (targetWidth > 0) {
            options.DestinationWidth(static_cast<uint32_t>(targetWidth));
        }

        page.RenderToStreamAsync(stream, options).get();

        uint64_t streamSize = stream.Size();
        if (streamSize == 0) return "";

        std::vector<uint8_t> buffer(static_cast<size_t>(streamSize));
        auto dataReader = winrt::Windows::Storage::Streams::DataReader(stream.GetInputStreamAt(0));
        dataReader.LoadAsync(static_cast<uint32_t>(streamSize)).get();
        dataReader.ReadBytes(winrt::array_view<uint8_t>(buffer));

        // 构造输出目标路径
        wxString dir = wxString::FromUTF8(tempDir);
        if (!wxDirExists(dir)) {
            wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        wxFileName sourceFn(wxString::FromUTF8(filePath));
        wxString targetPath = wxString::Format("%s/%s_p%04d.png", dir, sourceFn.GetName(), pageIndex + 1);

        std::ofstream out(targetPath.ToStdWstring(), std::ios::binary);
        if (!out) return "";
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        out.close();

        return targetPath.ToUTF8().data();
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPageToTempFile exception: " + std::string(ex.what()));
        return "";
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPageToTempFile unknown exception");
        return "";
    }
#elif defined(__APPLE__)
    try {
        wxImage img;
        if (!RenderPage(filePath, pageIndex, img, targetWidth)) {
            return "";
        }

        wxString dir = wxString::FromUTF8(tempDir);
        if (!wxDirExists(dir)) {
            wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        wxFileName sourceFn(wxString::FromUTF8(filePath));
        wxString targetPath = wxString::Format("%s/%s_p%04d.png", dir, sourceFn.GetName(), pageIndex + 1);

        if (!img.SaveFile(targetPath, wxBITMAP_TYPE_PNG)) {
            LOG_ERROR("PdfHelper", "RenderPageToTempFile failed to save image to: " + std::string(targetPath.ToUTF8().data()));
            return "";
        }

        return targetPath.ToUTF8().data();
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPageToTempFile exception: " + std::string(ex.what()));
        return "";
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPageToTempFile unknown exception");
        return "";
    }
#else
    return "";
#endif
}

} // namespace LinguaAlpaca
