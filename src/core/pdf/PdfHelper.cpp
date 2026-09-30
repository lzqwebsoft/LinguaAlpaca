#pragma execution_character_set("utf-8")
#include "PdfHelper.hpp"
#include "core/Logger.hpp"

#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/filefn.h>
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#pragma comment(lib, "windowsapp.lib")
#endif

namespace LinguaAlpaca {

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
    try {
        std::wstring wpath = wxString::FromUTF8(filePath).ToStdWstring();
        // 替换斜杠为 Windows 标准反斜杠以兼容 WinRT StorageFile
        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');

        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(wpath).get();
        auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
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
#else
    // macOS 或其他平台支持
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
    try {
        std::wstring wpath = wxString::FromUTF8(filePath).ToStdWstring();
        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');

        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(wpath).get();
        auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(doc.PageCount())) {
            return false;
        }

        auto page = doc.GetPage(static_cast<uint32_t>(pageIndex));
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
    try {
        std::wstring wpath = wxString::FromUTF8(filePath).ToStdWstring();
        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');

        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(wpath).get();
        auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(doc.PageCount())) {
            return "";
        }

        auto page = doc.GetPage(static_cast<uint32_t>(pageIndex));
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
#else
    return "";
#endif
}

} // namespace LinguaAlpaca
