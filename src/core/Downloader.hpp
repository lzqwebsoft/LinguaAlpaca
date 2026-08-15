#pragma once
#pragma execution_character_set("utf-8")

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <wx/wx.h>

namespace LinguaAlpaca {

using ProgressCallback = std::function<void(size_t downloadedBytes, size_t totalBytes, double percentage)>;
using CompleteCallback = std::function<void(bool success, const std::string& filePath, const std::string& error)>;

class Downloader {
public:
    Downloader();
    ~Downloader();

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

} // namespace LinguaAlpaca
