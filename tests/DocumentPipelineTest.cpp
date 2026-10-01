#include <catch2/catch.hpp>
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include "core/pdf/PdfHelper.hpp"
#include "engine/DocLayoutEngine.hpp"
#include "core/document/DocumentPipeline.hpp"
#include "core/Config.hpp"

using namespace LinguaAlpaca;

TEST_CASE("PdfHelper - Extension and file detection", "[pdf]") {
    REQUIRE(PdfHelper::IsPdfFile("document.pdf") == true);
    REQUIRE(PdfHelper::IsPdfFile("DOCUMENT.PDF") == true);
    REQUIRE(PdfHelper::IsPdfFile("path/to/my_file.pdf") == true);
    REQUIRE(PdfHelper::IsPdfFile("image.png") == false);
    REQUIRE(PdfHelper::IsPdfFile("scan.jpg") == false);
    REQUIRE(PdfHelper::IsPdfFile("") == false);
}

TEST_CASE("DocLayoutEngine - Heuristic layout fallback", "[layout]") {
    DocLayoutEngine engine;
    REQUIRE_FALSE(engine.IsLoaded());

    DocumentLayoutResult result;
    // Analyzing non-existent image safely fails with error
    bool okNonExistent = engine.AnalyzeLayout("non_existent_file.png", result);
    REQUIRE(okNonExistent == false);

    // Analyzing real image without ONNX model triggers heuristic fallback
    wxImage dummyImg(200, 300);
    wxString tempPath = wxFileName::CreateTempFileName("test_layout_") + ".png";
    dummyImg.SaveFile(tempPath, wxBITMAP_TYPE_PNG);

    bool ok = engine.AnalyzeLayout(tempPath.ToUTF8().data(), result);
    REQUIRE(ok == true);
    REQUIRE(result.imageWidth == 200);
    REQUIRE(result.imageHeight == 300);
    REQUIRE(result.elements.size() == 1);
    REQUIRE(result.elements[0].type == LayoutElementType::Text);
    REQUIRE(result.elements[0].x2 == 200);
    REQUIRE(result.elements[0].y2 == 300);

    wxRemoveFile(tempPath);
}

TEST_CASE("DocLayoutEngine - Test with User Newspaper Image", "[layout]") {
    DocLayoutEngine engine;

    // 动态从配置、环境变量或项目默认模型路径读取，绝不硬编码机器绝对路径
    std::string modelPath;
    try {
        ConfigManager configManager;
        modelPath = configManager.GetConfig().layoutModelPath;
    } catch (...) {}

    std::string resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(modelPath);

    if (resolvedPath.empty()) {
        wxString userDir = wxStandardPaths::Get().GetUserConfigDir() + wxFileName::GetPathSeparator() + "LinguaAlpaca";
        wxString iniPath = userDir + wxFileName::GetPathSeparator() + "config.ini";
        if (wxFileExists(iniPath)) {
            wxFileConfig fileConfig("LinguaAlpaca", "", iniPath);
            std::string pathInFile = fileConfig.Read("/LayoutModel/Path", "").ToUTF8().data();
            resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(pathInFile);
        }
    }

    if (resolvedPath.empty()) {
        wxString envPath;
        if (wxGetEnv("LINGUA_LAYOUT_MODEL_PATH", &envPath) && !envPath.IsEmpty()) {
            resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(envPath.ToUTF8().data());
        }
    }

    if (resolvedPath.empty()) {
        const std::vector<std::string> defaultCandidates = {
            "models/PP-DocLayoutV3.onnx",
            "../models/PP-DocLayoutV3.onnx",
            "../../models/PP-DocLayoutV3.onnx",
            "models/PP-DocLayoutV2.onnx",
            "../models/PP-DocLayoutV2.onnx",
            "../../models/PP-DocLayoutV2.onnx"
        };
        for (const auto& cand : defaultCandidates) {
            std::string res = DocLayoutEngine::ResolveLayoutModelPath(cand);
            if (!res.empty()) {
                resolvedPath = res;
                break;
            }
        }
    }

    if (!resolvedPath.empty()) {
        bool initOk = engine.Initialize(resolvedPath, 0, 4);
        REQUIRE(initOk == true);
        REQUIRE(engine.IsLoaded() == true);
    }

    DocumentLayoutResult result;

    std::string imgPath = "tests/test_newspaper.jpg";
    if (!wxFileExists(imgPath)) {
        imgPath = "../tests/test_newspaper.jpg";
    }
    if (!wxFileExists(imgPath)) {
        imgPath = "../../tests/test_newspaper.jpg";
    }

    if (wxFileExists(imgPath)) {
        bool ok = engine.AnalyzeLayout(imgPath, result);
        REQUIRE(ok == true);
        REQUIRE(result.imageWidth > 0);
        REQUIRE(result.imageHeight > 0);
        REQUIRE(result.elements.size() >= 1);
        std::cout << "\n>>> Detected " << result.elements.size() << " layout elements in test_newspaper.jpg:\n";
        for (const auto& elem : result.elements) {
            std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                 << " (score=" << elem.score << ") bbox=[" 
                 << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
        }

        // Verify bounding boxes stay within image bounds and no duplicate containment
        for (size_t i = 0; i < result.elements.size(); ++i) {
            const auto& a = result.elements[i];
            REQUIRE(a.x1 >= 0);
            REQUIRE(a.y1 >= 0);
            REQUIRE(a.x2 <= result.imageWidth);
            REQUIRE(a.y2 <= result.imageHeight);

            int areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
            for (size_t j = i + 1; j < result.elements.size(); ++j) {
                const auto& b = result.elements[j];
                int areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
                int ix1 = (std::max)(a.x1, b.x1);
                int iy1 = (std::max)(a.y1, b.y1);
                int ix2 = (std::min)(a.x2, b.x2);
                int iy2 = (std::min)(a.y2, b.y2);
                int iw = (std::max)(0, ix2 - ix1);
                int ih = (std::max)(0, iy2 - iy1);
                int interArea = iw * ih;
                if (interArea > 0 && areaA > 0 && areaB > 0) {
                    float ios = static_cast<float>(interArea) / (std::min)(areaA, areaB);
                    if (a.labelName == b.labelName) {
                        // 相同类别绝不能存在高度包含的重复嵌套框
                        CHECK(ios < 0.70f);
                    }
                }
            }
        }
    }
}

TEST_CASE("DocLayoutEngine - Suppress Contained and Duplicate Footnote Boxes", "[layout]") {
    // 模拟官方 PaddleOCR 示例中 test_newspaper 检测到的 3 个图注候选框
    // 框 1: 单行切片框 (score=0.4316)
    // 框 2: 多行完整块 (score=0.6346)
    // 框 3: 右下落款切片框 (score=0.3509)
    std::vector<LayoutElement> elements = {
        LayoutElement{1, LayoutElementType::Text, "vision_footnote", 0.4316f, 810, 702, 1452, 724, 0},
        LayoutElement{2, LayoutElementType::Text, "vision_footnote", 0.6346f, 809, 702, 1486, 750, 0},
        LayoutElement{3, LayoutElementType::Text, "vision_footnote", 0.3509f, 1246, 729, 1487, 750, 0}
    };

    DocLayoutFilterConfig cfg;
    cfg.mergeLayoutBlocks = true;
    cfg.containmentIosThreshold = 0.70f;

    DocLayoutEngine::SuppressContainedOrDuplicateBoxes(elements, cfg);

    // 核心断言：两个单行局部碎片框必须被抑制，仅保留唯一的多行完整图注大框！
    REQUIRE(elements.size() == 1);
    CHECK(elements[0].labelName == "vision_footnote");
    CHECK(elements[0].score >= 0.6346f);
    CHECK(elements[0].x1 <= 809);
    CHECK(elements[0].y1 <= 702);
    CHECK(elements[0].x2 >= 1486);
    CHECK(elements[0].y2 >= 750);
}

TEST_CASE("DocumentPipeline - Never delete original input image or document file", "[pipeline]") {
    // 模拟用户原始输入图片文件
    wxString tempOriginal = wxFileName::CreateTempFileName("user_orig_doc_") + ".png";
    {
        wxImage dummyImg(300, 200);
        dummyImg.Clear(255);
        REQUIRE(dummyImg.SaveFile(tempOriginal, wxBITMAP_TYPE_PNG));
    }
    REQUIRE(wxFileExists(tempOriginal));

    auto pipeline = std::make_shared<DocumentPipeline>(nullptr, nullptr);

    std::promise<bool> finishPromise;
    auto finishFuture = finishPromise.get_future();

    wxString outDir = wxFileName::CreateTempFileName("doc_out_test");
    wxRemoveFile(outDir);

    pipeline->StartParseAsync(
        tempOriginal.ToUTF8().data(),
        outDir.ToUTF8().data(),
        false,
        nullptr,
        [&finishPromise](bool success, const std::string& md, const std::string& json, const std::string& err) {
            finishPromise.set_value(true);
        }
    );

    finishFuture.wait_for(std::chrono::seconds(5));

    // 核心断言：流水线处理完成后，用户原始文件绝对不能被删除！
    REQUIRE(wxFileExists(tempOriginal) == true);

    // 仅在测试结束时清理测试用例创建的文件
    wxRemoveFile(tempOriginal);
    if (wxDirExists(outDir)) {
        wxFileName::Rmdir(outDir, wxPATH_RMDIR_RECURSIVE);
    }
}

TEST_CASE("DocumentPipeline - Convert PaddleOCR OTSL table to HTML table", "[table]") {
    std::string otsl =
        "<fcel>Strategy<fcel>Discrepancy<fcel>w/ sensitive keyword<lcel><lcel><fcel>w/o sensitive keyword<lcel><nl>"
        "<ucel><ucel><fcel>Success<fcel>Failure 1<fcel>Failure 2<fcel>Success<fcel>Failure 1<nl>"
        "<fcel>No Strategy<fcel>N/A<fcel>2.8%<fcel>0.4%<fcel>96.8%<fcel>98.9%<fcel>1.1%<nl>";

    std::string html = DocumentPipeline::ConvertOtslToHtml(otsl);

    REQUIRE_FALSE(html.empty());
    REQUIRE(html.find("<table>") != std::string::npos);
    REQUIRE(html.find("<thead>") != std::string::npos);
    REQUIRE(html.find("<tbody>") != std::string::npos);
    // 验证合并单元格生成正确
    REQUIRE(html.find("rowspan=\"2\">Strategy</th>") != std::string::npos);
    REQUIRE(html.find("rowspan=\"2\">Discrepancy</th>") != std::string::npos);
    REQUIRE(html.find("colspan=\"3\">w/ sensitive keyword</th>") != std::string::npos);
    REQUIRE(html.find("colspan=\"2\">w/o sensitive keyword</th>") != std::string::npos);
    REQUIRE(html.find("<td>No Strategy</td>") != std::string::npos);
    REQUIRE(html.find("<td>96.8%</td>") != std::string::npos);
    REQUIRE(html.find("</table>") != std::string::npos);
}

