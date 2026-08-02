#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <wx/wx.h>

namespace LinguaAlpaca::Infrastructure::Downloader {

using ProgressCallback = std::function<void(size_t downloadedBytes, size_t totalBytes, double percentage)>;
using CompleteCallback = std::function<void(bool success, const std::string& filePath, const std::string& error)>;

class ModelDownloader {
public:
    ModelDownloader();
    ~ModelDownloader();

    static std::string GetDefaultModelDir();

    void DownloadModelAsync(
        const std::string& url,
        const std::string& targetFileName,
        ProgressCallback onProgress,
        CompleteCallback onComplete
    );

    void CancelDownload();
    bool IsDownloading() const { return m_isDownloading; }

private:
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_isDownloading{false};
};

} // namespace LinguaAlpaca::Infrastructure::Downloader
