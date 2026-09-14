#pragma execution_character_set("utf-8")
#include "VersionChecker.hpp"
#include "Logger.hpp"

#include <algorithm>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include <wx/webrequest.h>

namespace LinguaAlpaca {

std::string VersionChecker::CleanVersionString(const std::string& ver) {
    if (ver.empty()) return "";

    // 去除首尾空白
    size_t start = ver.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = ver.find_last_not_of(" \t\r\n");
    std::string s = ver.substr(start, end - start + 1);

    // 去除前导 'v' 或 'V'
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
        s = s.substr(1);
    }
    return s;
}

int VersionChecker::CompareVersions(const std::string& v1, const std::string& v2) {
    auto clean1 = CleanVersionString(v1);
    auto clean2 = CleanVersionString(v2);

    auto parseComponents = [](const std::string& s) {
        std::vector<int> components;
        std::string cur;
        for (char c : s) {
            if (c == '.') {
                if (!cur.empty()) {
                    try {
                        components.push_back(std::stoi(cur));
                    } catch (...) {
                        components.push_back(0);
                    }
                    cur.clear();
                }
            } else if (c >= '0' && c <= '9') {
                cur += c;
            } else {
                // 遇到非数字字符（如 -beta, +build），终止当前主版本号数值解析
                break;
            }
        }
        if (!cur.empty()) {
            try {
                components.push_back(std::stoi(cur));
            } catch (...) {
                components.push_back(0);
            }
        }
        return components;
    };

    auto p1 = parseComponents(clean1);
    auto p2 = parseComponents(clean2);

    size_t maxLen = std::max(p1.size(), p2.size());
    for (size_t i = 0; i < maxLen; ++i) {
        int c1 = i < p1.size() ? p1[i] : 0;
        int c2 = i < p2.size() ? p2[i] : 0;
        if (c1 > c2) return 1;
        if (c1 < c2) return -1;
    }
    return 0;
}

VersionCheckResult VersionChecker::CheckLatestVersion(
    const std::string& currentVersion,
    const std::string& repoOwner,
    const std::string& repoName) {

    VersionCheckResult result;
    result.currentVersion = CleanVersionString(currentVersion);
    std::string fallbackReleasesUrl = "https://github.com/" + repoOwner + "/" + repoName + "/releases";
    result.releaseUrl = fallbackReleasesUrl;

    std::string apiUrl = "https://api.github.com/repos/" + repoOwner + "/" + repoName + "/releases/latest";
    LOG_INFO("VersionChecker", "Checking latest release from: " + apiUrl);

    try {
        wxWebRequestSync request = wxWebSessionSync::GetDefault().CreateRequest(wxString::FromUTF8(apiUrl));
        if (!request.IsOk()) {
            result.success = false;
            result.errorMessage = "创建网络请求失败";
            LOG_WARN("VersionChecker", result.errorMessage);
            return result;
        }

        std::string ua = "LinguaAlpaca/" + (currentVersion.empty() ? "1.0.0" : currentVersion);
        request.SetHeader("User-Agent", wxString::FromUTF8(ua));
        request.SetHeader("Accept", "application/vnd.github.v3+json");
        request.SetTimeouts(5000, 8000);

        auto reqResult = request.Execute();
        if (reqResult.state != wxWebRequest::State_Completed) {
            result.success = false;
            std::string errStr = reqResult.error.ToStdString();
            result.errorMessage = "网络请求失败: " + (errStr.empty() ? "请求未能正常完成" : errStr);
            LOG_WARN("VersionChecker", result.errorMessage);
            return result;
        }

        wxWebResponse response = request.GetResponse();
        int statusCode = response.GetStatus();
        if (statusCode != 200) {
            result.success = false;
            result.errorMessage = "GitHub API 返回错误状态码: " + std::to_string(statusCode);
            LOG_WARN("VersionChecker", result.errorMessage);
            return result;
        }

        std::string responseBody = response.AsString().ToUTF8().data();
        auto j = nlohmann::json::parse(responseBody);
        std::string tagName = j.value("tag_name", "");
        std::string htmlUrl = j.value("html_url", "");
        std::string bodyText = j.value("body", "");

        result.latestVersion = CleanVersionString(tagName);
        if (!htmlUrl.empty()) {
            result.releaseUrl = htmlUrl;
        }
        result.releaseNotes = bodyText;
        result.success = true;

        if (CompareVersions(result.currentVersion, result.latestVersion) < 0) {
            result.hasUpdate = true;
            LOG_INFO("VersionChecker", "New version detected: v" + result.latestVersion + " (current: v" + result.currentVersion + ")");
        } else {
            result.hasUpdate = false;
            LOG_INFO("VersionChecker", "Already up to date. (current: v" + result.currentVersion + ", latest: v" + result.latestVersion + ")");
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("解析更新信息异常: ") + e.what();
        LOG_ERROR("VersionChecker", result.errorMessage);
    }

    return result;
}

void VersionChecker::CheckLatestVersionAsync(
    const std::string& currentVersion,
    Callback onComplete,
    const std::string& repoOwner,
    const std::string& repoName) {

    std::thread([currentVersion, onComplete, repoOwner, repoName]() {
        VersionCheckResult res = CheckLatestVersion(currentVersion, repoOwner, repoName);
        if (onComplete) {
            onComplete(res);
        }
    }).detach();
}

} // namespace LinguaAlpaca
