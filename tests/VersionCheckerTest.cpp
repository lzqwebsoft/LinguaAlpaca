#include <catch2/catch.hpp>
#include "core/VersionChecker.hpp"

using namespace LinguaAlpaca;

TEST_CASE("VersionChecker - Version Normalization and Comparison", "[core][version_checker]") {
    SECTION("CleanVersionString removes prefixes and whitespaces") {
        REQUIRE(VersionChecker::CleanVersionString("1.0.3") == "1.0.3");
        REQUIRE(VersionChecker::CleanVersionString("v1.0.3") == "1.0.3");
        REQUIRE(VersionChecker::CleanVersionString("V1.0.4") == "1.0.4");
        REQUIRE(VersionChecker::CleanVersionString("  v2.1.0 \r\n") == "2.1.0");
        REQUIRE(VersionChecker::CleanVersionString("") == "");
    }

    SECTION("CompareVersions handles equal, older, and newer versions accurately") {
        // Equal
        REQUIRE(VersionChecker::CompareVersions("1.0.3", "1.0.3") == 0);
        REQUIRE(VersionChecker::CompareVersions("v1.0.3", "1.0.3") == 0);
        REQUIRE(VersionChecker::CompareVersions("1.0.3", "v1.0.3") == 0);
        REQUIRE(VersionChecker::CompareVersions("1.0", "1.0.0") == 0);

        // Newer
        REQUIRE(VersionChecker::CompareVersions("1.0.3", "1.0.4") == -1);
        REQUIRE(VersionChecker::CompareVersions("1.0.3", "1.1.0") == -1);
        REQUIRE(VersionChecker::CompareVersions("1.0.3", "2.0.0") == -1);
        REQUIRE(VersionChecker::CompareVersions("0.9.9", "1.0.0") == -1);

        // Older
        REQUIRE(VersionChecker::CompareVersions("1.0.4", "1.0.3") == 1);
        REQUIRE(VersionChecker::CompareVersions("1.1.0", "1.0.3") == 1);
        REQUIRE(VersionChecker::CompareVersions("2.0.0", "1.9.9") == 1);
        REQUIRE(VersionChecker::CompareVersions("1.0.3.1", "1.0.3") == 1);
    }
}

TEST_CASE("VersionChecker - GitHub Release Fetching", "[core][version_checker]") {
    SECTION("Fetch latest release from GitHub API") {
        // LinguaAlpaca official repository
        VersionCheckResult result = VersionChecker::CheckLatestVersion("1.0.0");
        if (result.success) {
            REQUIRE_FALSE(result.latestVersion.empty());
            REQUIRE(result.hasUpdate == true);
            REQUIRE(result.releaseUrl.find("github.com") != std::string::npos);
        } else {
            // In offline or restricted network environments, verify error message is populated
            REQUIRE_FALSE(result.errorMessage.empty());
        }
    }
}
