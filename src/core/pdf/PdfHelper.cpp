#pragma execution_character_set("utf-8")
#include "PdfHelper.hpp"
#include "core/Logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <fpdfview.h>
#include <fpdf_doc.h>

#include <wx/filename.h>
#include <wx/filefn.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace {

// 全局 PDFium 运行环境生命周期守护
struct PdfiumScope {
    PdfiumScope() {
        FPDF_LIBRARY_CONFIG config;
        config.version = 2;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
        LOG_INFO("PdfHelper", "Initialized Google PDFium engine");
    }
    ~PdfiumScope() { FPDF_DestroyLibrary(); }
};

void EnsurePdfiumInitialized() {
    static PdfiumScope s_scope;
}

// 全局 PDF 文档互斥锁与单例缓存（杜绝频繁翻页或批量渲染时重复从磁盘解析大体量 PDF 结构）
std::mutex s_pdfDocMutex;
std::string s_cachedPdfPath;
FPDF_DOCUMENT s_cachedPdfDoc = nullptr;

void ClearCacheLocked() {
    if (s_cachedPdfDoc) {
        FPDF_CloseDocument(s_cachedPdfDoc);
        s_cachedPdfDoc = nullptr;
    }
    s_cachedPdfPath.clear();
}

FPDF_DOCUMENT GetOrLoadPdfDocLocked(const std::string& filePath) {
    if (s_cachedPdfDoc && s_cachedPdfPath == filePath) {
        return s_cachedPdfDoc;
    }
    ClearCacheLocked();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath.c_str(), nullptr);
    if (doc) {
        s_cachedPdfDoc = doc;
        s_cachedPdfPath = filePath;
    } else {
        LOG_WARN("PdfHelper", "Failed to load PDF document: " + filePath);
    }
    return doc;
}

} // namespace

namespace LinguaAlpaca {

void PdfHelper::ClearCache() {
    std::lock_guard<std::mutex> lock(s_pdfDocMutex);
    ClearCacheLocked();
}

bool PdfHelper::IsPdfFile(const std::string& filePath) {
    if (filePath.empty())
        return false;
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

    try {
        EnsurePdfiumInitialized();
        std::lock_guard<std::mutex> lock(s_pdfDocMutex);
        FPDF_DOCUMENT doc = GetOrLoadPdfDocLocked(filePath);
        if (!doc)
            return 0;
        return FPDF_GetPageCount(doc);
    } catch (const std::exception& ex) {
        LOG_WARN("PdfHelper", "Failed to query PDF page count: " + std::string(ex.what()));
        return 0;
    } catch (...) {
        LOG_WARN("PdfHelper", "Unknown exception while querying PDF page count");
        return 0;
    }
}

bool PdfHelper::RenderPage(const std::string& filePath, int pageIndex, wxImage& outImage, int targetWidth) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return false;
    }
    if (!IsPdfFile(filePath)) {
        return outImage.LoadFile(wxString::FromUTF8(filePath));
    }

    try {
        EnsurePdfiumInitialized();
        FPDF_PAGE page = nullptr;
        double origW = 0.0;
        double origH = 0.0;

        {
            std::lock_guard<std::mutex> lock(s_pdfDocMutex);
            FPDF_DOCUMENT doc = GetOrLoadPdfDocLocked(filePath);
            if (!doc)
                return false;

            int totalPages = FPDF_GetPageCount(doc);
            if (pageIndex < 0 || pageIndex >= totalPages) {
                LOG_WARN("PdfHelper", "Page index out of range: " + std::to_string(pageIndex) + " / " + std::to_string(totalPages));
                return false;
            }

            page = FPDF_LoadPage(doc, pageIndex);
            if (!page) {
                LOG_ERROR("PdfHelper", "Failed to load page " + std::to_string(pageIndex + 1));
                return false;
            }

            origW = FPDF_GetPageWidthF(page);
            origH = FPDF_GetPageHeightF(page);
        }

        if (origW <= 0.0 || origH <= 0.0) {
            FPDF_ClosePage(page);
            return false;
        }

        double scale = 1.0;
        if (targetWidth > 0 && origW > 0.0) {
            scale = static_cast<double>(targetWidth) / origW;
        }

        int outW = static_cast<int>(std::round(origW * scale));
        int outH = static_cast<int>(std::round(origH * scale));
        if (outW <= 0 || outH <= 0) {
            FPDF_ClosePage(page);
            return false;
        }

        // 创建 PDFium 渲染位图 (0 表示白底背景，不带 alpha 通道)
        FPDF_BITMAP bitmap = FPDFBitmap_Create(outW, outH, 0);
        if (!bitmap) {
            FPDF_ClosePage(page);
            return false;
        }

        // 默认填充纯白背景 (防止透明背景 PDF 渲染为黑底)
        FPDFBitmap_FillRect(bitmap, 0, 0, outW, outH, 0xFFFFFFFF);

        // 使用 Google PDFium 渲染页面：
        // FPDF_ANNOT: 渲染全部可见文本、图形、掩模与标注
        // FPDF_REVERSE_BYTE_ORDER: 直接生成 RGBA 顺序字节，便于高效转换为 wxImage RGB 格式
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, outW, outH, 0, FPDF_ANNOT | FPDF_REVERSE_BYTE_ORDER);

        const uint8_t* buffer = static_cast<const uint8_t*>(FPDFBitmap_GetBuffer(bitmap));
        int stride = FPDFBitmap_GetStride(bitmap);

        // 转换位图数据至 wxImage RGB 格式
        unsigned char* rgbData = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(outW) * outH * 3));
        if (!rgbData) {
            FPDFBitmap_Destroy(bitmap);
            FPDF_ClosePage(page);
            return false;
        }

        for (int y = 0; y < outH; ++y) {
            const uint8_t* srcRow = buffer + y * stride;
            unsigned char* dstRow = rgbData + static_cast<size_t>(y) * outW * 3;
            for (int x = 0; x < outW; ++x) {
                dstRow[x * 3] = srcRow[x * 4];
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
            }
        }

        FPDFBitmap_Destroy(bitmap);
        FPDF_ClosePage(page);

        // wxImage 接管已分配的 rgbData 内存
        outImage.Create(outW, outH, rgbData, false);
        return outImage.IsOk();
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPage exception: " + std::string(ex.what()));
        return false;
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPage unknown exception");
        return false;
    }
}

std::string PdfHelper::RenderPageToTempFile(const std::string& filePath, int pageIndex, const std::string& tempDir, int targetWidth) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return "";
    }
    if (!IsPdfFile(filePath)) {
        // 普通图像已是栅格图像，无需再次光栅化，直接返回原图路径（调用方严禁作为临时文件删除）
        return filePath;
    }

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
}

} // namespace LinguaAlpaca
