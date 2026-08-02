#pragma execution_character_set("utf-8")
#include "ModelDownloader.hpp"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <fstream>
#include <chrono>

namespace LinguaAlpaca::Infrastructure::Downloader {

ModelDownloader::ModelDownloader() {}

ModelDownloader::~ModelDownloader() {
    CancelDownload();
}

std::string ModelDownloader::GetDefaultModelDir() {
    wxString userDir = wxStandardPaths::Get().GetUserDataDir();
    wxString modelDir = userDir + wxFileName::GetPathSeparator() + "models";
    if (!wxDirExists(modelDir)) {
        wxFileName::Mkdir(modelDir, 0777, wxPATH_MKDIR_FULL);
    }
    return modelDir.ToUTF8().data();
}

void ModelDownloader::DownloadModelAsync(
    const std::string& url,
    const std::string& targetFileName,
    ProgressCallback onProgress,
    CompleteCallback onComplete) {

    CancelDownload();
    m_cancelRequested = false;
    m_isDownloading = true;

    std::thread([this, url, targetFileName, onProgress, onComplete]() {
        wxString modelDir = wxString::FromUTF8(GetDefaultModelDir());
        wxString fullPath = modelDir + wxFileName::GetPathSeparator() + wxString::FromUTF8(targetFileName);
        std::string targetFilePath = fullPath.ToUTF8().data();

        // 如果文件已存在则直接返回成功
        if (wxFileExists(fullPath)) {
            if (onComplete) {
                if (wxTheApp) {
                    wxTheApp->CallAfter([onComplete, targetFilePath]() {
                        onComplete(true, targetFilePath, "");
                    });
                } else {
                    onComplete(true, targetFilePath, "");
                }
            }
            m_isDownloading = false;
            return;
        }

        // 模拟/HTTP 分块下载流程
        size_t totalBytes = 1200 * 1024 * 1024; // 1.2 GB
        size_t downloadedBytes = 0;
        size_t chunkSize = 15 * 1024 * 1024;  // 每块 15 MB 模拟极速下载

        std::ofstream outFile(targetFilePath, std::ios::binary);

        while (downloadedBytes < totalBytes && !m_cancelRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            downloadedBytes += chunkSize;
            if (downloadedBytes > totalBytes) downloadedBytes = totalBytes;

            double pct = (double)downloadedBytes / totalBytes * 100.0;
            
            std::vector<char> dummyBuffer(1024, '0');
            if (outFile.is_open()) {
                outFile.write(dummyBuffer.data(), dummyBuffer.size());
            }

            if (onProgress) {
                if (wxTheApp) {
                    wxTheApp->CallAfter([onProgress, downloadedBytes, totalBytes, pct]() {
                        onProgress(downloadedBytes, totalBytes, pct);
                    });
                } else {
                    onProgress(downloadedBytes, totalBytes, pct);
                }
            }
        }

        if (outFile.is_open()) {
            outFile.close();
        }

        bool wasCancelled = m_cancelRequested.load();
        m_isDownloading = false;

        if (wasCancelled && wxFileExists(fullPath)) {
            wxRemoveFile(fullPath);
        }

        if (onComplete) {
            if (wxTheApp) {
                wxTheApp->CallAfter([onComplete, wasCancelled, targetFilePath]() {
                    onComplete(!wasCancelled, targetFilePath, wasCancelled ? "下载被取消" : "");
                });
            } else {
                onComplete(!wasCancelled, targetFilePath, wasCancelled ? "下载被取消" : "");
            }
        }
    }).detach();
}

void ModelDownloader::CancelDownload() {
    m_cancelRequested = true;
}

} // namespace LinguaAlpaca::Infrastructure::Downloader
