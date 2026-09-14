#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <functional>

namespace LinguaAlpaca {

/**
 * @brief 版本检查结果结构体
 */
struct VersionCheckResult {
    bool success{false};
    bool hasUpdate{false};
    std::string currentVersion;
    std::string latestVersion;
    std::string releaseUrl;       // GitHub release 页面链接
    std::string releaseNotes;     // 发行说明内容概要
    std::string errorMessage;
};

/**
 * @brief 应用程序版本检测服务
 * 
 * 通过 GitHub REST API 查询远程 Releases 最新版本，
 * 提取版本号进行语义化对比，并支持异步回调。
 */
class VersionChecker {
public:
    using Callback = std::function<void(const VersionCheckResult& result)>;

    /**
     * @brief 对比两个语义化版本号
     * @param v1 本地/基础版本号 (例如 "1.0.3", "v1.0.3")
     * @param v2 目标/远程版本号 (例如 "1.0.4", "v1.0.4")
     * @return 1 表示 v1 > v2; -1 表示 v1 < v2; 0 表示版本相同
     */
    static int CompareVersions(const std::string& v1, const std::string& v2);

    /**
     * @brief 规范化版本号字符串 (去除空白与前导 'v' / 'V')
     */
    static std::string CleanVersionString(const std::string& ver);

    /**
     * @brief 同步查询 GitHub 仓库最新发布版本
     * @param currentVersion 当前应用版本号
     * @param repoOwner 仓库所有者 (默认 "lzqwebsoft")
     * @param repoName 仓库名称 (默认 "LinguaAlpaca")
     * @return 包含是否有更新及新版信息的 VersionCheckResult
     */
    static VersionCheckResult CheckLatestVersion(
        const std::string& currentVersion,
        const std::string& repoOwner = "lzqwebsoft",
        const std::string& repoName = "LinguaAlpaca"
    );

    /**
     * @brief 异步查询 GitHub 仓库最新发布版本 (后台线程执行)
     * @param currentVersion 当前应用版本号
     * @param onComplete 回调函数
     * @param repoOwner 仓库所有者 (默认 "lzqwebsoft")
     * @param repoName 仓库名称 (默认 "LinguaAlpaca")
     */
    static void CheckLatestVersionAsync(
        const std::string& currentVersion,
        Callback onComplete,
        const std::string& repoOwner = "lzqwebsoft",
        const std::string& repoName = "LinguaAlpaca"
    );
};

} // namespace LinguaAlpaca
