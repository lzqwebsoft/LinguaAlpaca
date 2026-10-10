#include <catch2/catch.hpp>
#include "core/markdown/HtmlExporter.hpp"
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>

using namespace LinguaAlpaca;

TEST_CASE("HtmlExporter - Generate Standalone HTML", "[core][markdown][html_exporter]") {
    SECTION("Generates valid HTML document with embedded markdown and title") {
        std::string sampleMd = "# OCR 测试文档\n\n这是一个关于 $E=mc^2$ 的公式测试段落。\n\n| 表头1 | 表头2 |\n| :--- | :--- |\n| 数据A | 数据B |\n";
        std::string html = HtmlExporter::GenerateStandaloneHtml(sampleMd, "", "量子物理研究报告");

        REQUIRE(!html.empty());
        REQUIRE(html.find("<!DOCTYPE html>") != std::string::npos);
        REQUIRE(html.find("<title>量子物理研究报告</title>") != std::string::npos);
        REQUIRE(html.find("LinguaAlpaca 文档解析结果") != std::string::npos);
        REQUIRE(html.find("renderMarkdown") != std::string::npos);
        // 校验包含关键内容转义
        REQUIRE(html.find("OCR 测试文档") != std::string::npos);
    }

    SECTION("Handles title escaping correctly") {
        std::string md = "内容";
        std::string html = HtmlExporter::GenerateStandaloneHtml(md, "", "测试 <>&\" 标题");
        REQUIRE(html.find("&lt;&gt;&amp;&quot;") != std::string::npos);
    }

    SECTION("Safe from script tag breakout") {
        std::string maliciousMd = "测试 </script><script>alert(1)</script> 语法断裂保护";
        std::string html = HtmlExporter::GenerateStandaloneHtml(maliciousMd, "", "测试");
        // </ 必须被安全转义为 <\/
        REQUIRE(html.find("</script><script>") == std::string::npos);
    }
}

TEST_CASE("HtmlExporter - Export to File on Disk", "[core][markdown][html_exporter]") {
    SECTION("Exports standalone HTML file to filesystem and cleans up") {
        wxString tempDir = wxStandardPaths::Get().GetTempDir() + "/LinguaAlpacaTest";
        if (!wxDirExists(tempDir)) {
            wxFileName::Mkdir(tempDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }
        wxString testOutPath = tempDir + "/test_ocr_export.html";

        std::string sampleMd = "## 人工智能报告\n\nPaddleOCR-VL 识别结果测试。\n";
        bool ok = HtmlExporter::ExportToStandaloneHtml(
            sampleMd,
            "",
            testOutPath.ToUTF8().data(),
            "AI 测试报告"
        );

        REQUIRE(ok);
        REQUIRE(wxFileExists(testOutPath));

        wxULongLong sz = wxFileName(testOutPath).GetSize();
        REQUIRE(sz > 500); // 至少包含基本样式与 HTML 骨架

        // 清理临时文件
        wxRemoveFile(testOutPath);
    }
}
