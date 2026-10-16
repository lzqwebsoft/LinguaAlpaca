#include <catch2/catch.hpp>
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include "core/pdf/PdfHelper.hpp"
#include "engine/DocLayoutEngine.hpp"
#include "core/document/DocumentPipeline.hpp"
#include "core/Config.hpp"
#include <fstream>
#include <regex>
#include <nlohmann/json.hpp>

using namespace LinguaAlpaca;

namespace {

std::string FindTestAsset(const std::string& relativePath) {
    if (wxFileExists(wxString::FromUTF8(relativePath))) return relativePath;
    std::string cand1 = "../" + relativePath;
    if (wxFileExists(wxString::FromUTF8(cand1))) return cand1;
    std::string cand2 = "../../" + relativePath;
    if (wxFileExists(wxString::FromUTF8(cand2))) return cand2;
    return relativePath;
}

std::string ResolveTestLayoutModelPath() {
    std::string modelPath;
    try {
        ConfigManager cfgMgr;
        modelPath = cfgMgr.GetConfig().layoutModelPath;
    } catch (...) {}

    std::string resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(modelPath);
    if (!resolvedPath.empty()) return resolvedPath;

    wxString userDir = wxStandardPaths::Get().GetUserConfigDir() + wxFileName::GetPathSeparator() + "LinguaAlpaca";
    wxString iniPath = userDir + wxFileName::GetPathSeparator() + "config.ini";
    if (wxFileExists(iniPath)) {
        wxFileConfig fileConfig("LinguaAlpaca", "", iniPath);
        std::string pathInFile = fileConfig.Read("/LayoutModel/Path", "").ToUTF8().data();
        resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(pathInFile);
        if (!resolvedPath.empty()) return resolvedPath;
    }

    wxString envPath;
    if (wxGetEnv("LINGUA_LAYOUT_MODEL_PATH", &envPath) && !envPath.IsEmpty()) {
        resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(envPath.ToUTF8().data());
        if (!resolvedPath.empty()) return resolvedPath;
    }

    const std::vector<std::string> defaultCandidates = {
        "models/PP-DocLayoutV3.onnx",
        "../models/PP-DocLayoutV3.onnx",
        "../../models/PP-DocLayoutV3.onnx",
        "models/PP-DocLayoutV3_onnx/inference.onnx",
        "../models/PP-DocLayoutV3_onnx/inference.onnx",
        "../../models/PP-DocLayoutV3_onnx/inference.onnx",
        "models/PP-DocLayoutV2.onnx",
        "../models/PP-DocLayoutV2.onnx",
        "../../models/PP-DocLayoutV2.onnx"
    };
    for (const auto& cand : defaultCandidates) {
        std::string res = DocLayoutEngine::ResolveLayoutModelPath(cand);
        if (!res.empty()) return res;
    }
    return "";
}

} // namespace

TEST_CASE("PdfHelper - Extension and file detection", "[pdf]") {
    REQUIRE(PdfHelper::IsPdfFile("document.pdf") == true);
    REQUIRE(PdfHelper::IsPdfFile("DOCUMENT.PDF") == true);
    REQUIRE(PdfHelper::IsPdfFile("path/to/my_file.pdf") == true);
    REQUIRE(PdfHelper::IsPdfFile("image.png") == false);
    REQUIRE(PdfHelper::IsPdfFile("scan.jpg") == false);
    REQUIRE(PdfHelper::IsPdfFile("") == false);
}

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
TEST_CASE("PdfHelper - Real PDF rendering on macOS", "[pdf]") {
    wxString tempPdfPath = wxFileName::CreateTempFileName("test_pdf_") + ".pdf";
    CFStringRef pathStr = CFStringCreateWithCString(kCFAllocatorDefault, tempPdfPath.ToUTF8().data(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, pathStr, kCFURLPOSIXPathStyle, false);
    CFRelease(pathStr);
    CGRect mediaBox = CGRectMake(0, 0, 200, 200);
    CGContextRef pdfCtx = CGPDFContextCreateWithURL(url, &mediaBox, nullptr);
    CFRelease(url);
    REQUIRE(pdfCtx != nullptr);
    CGPDFContextBeginPage(pdfCtx, nullptr);
    CGContextSetRGBFillColor(pdfCtx, 1, 0, 0, 1);
    CGContextFillRect(pdfCtx, CGRectMake(20, 20, 160, 160));
    CGPDFContextEndPage(pdfCtx);
    CGPDFContextClose(pdfCtx);
    CGContextRelease(pdfCtx);

    REQUIRE(PdfHelper::GetPageCount(tempPdfPath.ToUTF8().data()) == 1);

    wxImage img;
    REQUIRE(PdfHelper::RenderPage(tempPdfPath.ToUTF8().data(), 0, img, 400) == true);
    REQUIRE(img.IsOk());
    REQUIRE(img.GetWidth() == 400);
    REQUIRE(img.GetHeight() == 400);

    std::string tempPng = PdfHelper::RenderPageToTempFile(tempPdfPath.ToUTF8().data(), 0, wxStandardPaths::Get().GetTempDir().ToUTF8().data(), 400);
    REQUIRE(!tempPng.empty());
    REQUIRE(wxFileExists(wxString::FromUTF8(tempPng)));

    PdfHelper::ClearCache();

    wxRemoveFile(tempPdfPath);
    wxRemoveFile(wxString::FromUTF8(tempPng));
}
#endif

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
    std::string resolvedPath = ResolveTestLayoutModelPath();

    if (!resolvedPath.empty()) {
        bool initOk = engine.Initialize(resolvedPath, 0, 4);
        REQUIRE(initOk == true);
        REQUIRE(engine.IsLoaded() == true);
    }

    DocumentLayoutResult result;
    std::string imgPath = FindTestAsset("tests/test_newspaper.jpg");

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

        // 验证数学之美第 18 页页眉 "II || 数学之美" 必须被正确识别过滤，不得误入正文
        // 图片固定存放在 tests/ 目录中，脱离对外部 PDF 路径的依赖
        std::string sxzm18Path = "tests/test_sxzm_page18.png";
        if (!wxFileExists(sxzm18Path)) { sxzm18Path = "../tests/test_sxzm_page18.png"; }
        if (!wxFileExists(sxzm18Path)) { sxzm18Path = "../../tests/test_sxzm_page18.png"; }

        REQUIRE(wxFileExists(sxzm18Path));
        {
            DocumentLayoutResult resSxzm;
            bool okSxzm = engine.AnalyzeLayout(sxzm18Path, resSxzm);
            REQUIRE(okSxzm == true);
            CHECK(resSxzm.elements.size() >= 10);
            std::cout << "\n--- 数学之美 Page 18 (W=" << resSxzm.imageWidth << ", H=" << resSxzm.imageHeight << ") detected " << resSxzm.elements.size() << " elements:\n";
            bool foundLeakedHeader = false;
            for (const auto& elem : resSxzm.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                // 页眉位于顶部 y <= 150 (真实页眉 bbox ~ [300, 79, 497, 121])
                if (elem.y2 <= 150) {
                    foundLeakedHeader = true;
                }
            }
            CHECK(foundLeakedHeader == false);
        }

        // 验证数学之美第 56 页行内公式 (10^-20, 10^-70 等) 落在段落内部时必须被抑制，不得重复生成独立公式块
        std::string sxzm56Path = "tests/test_sxzm_page56.png";
        if (!wxFileExists(sxzm56Path)) { sxzm56Path = "../tests/test_sxzm_page56.png"; }
        if (!wxFileExists(sxzm56Path)) { sxzm56Path = "../../tests/test_sxzm_page56.png"; }

        REQUIRE(wxFileExists(sxzm56Path));
        {
            DocumentLayoutResult res56;
            bool ok56 = engine.AnalyzeLayout(sxzm56Path, res56);
            REQUIRE(ok56 == true);
            std::cout << "\n--- 数学之美 Page 56 (W=" << res56.imageWidth << ", H=" << res56.imageHeight << ") detected " << res56.elements.size() << " elements:\n";
            bool foundDuplicateInlineFormula = false;
            for (const auto& elem : res56.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                // 落在段落 [325, 1345, 1444, 1645] 内部的 10^-20 与 10^-70 行内公式必须被抑制
                if (elem.labelName == "inline_formula" && elem.y1 >= 1345 && elem.y2 <= 1645) {
                    foundDuplicateInlineFormula = true;
                }
                // 落在段落 [324, 1699, 1448, 2063] 内部的行内公式也必须被抑制
                if (elem.labelName == "inline_formula" && elem.y1 >= 1699 && elem.y2 <= 2063) {
                    foundDuplicateInlineFormula = true;
                }
            }
            CHECK(foundDuplicateInlineFormula == false);
        }

        // 验证数学之美第 236 页表格内部的公式 (k=1, k=2, k=5, k=6, k=7 等) 落在表格内部时必须被抑制，不得重复生成独立公式块
        std::string sxzm236Path = "tests/test_sxzm_page236.png";
        if (!wxFileExists(sxzm236Path)) { sxzm236Path = "../tests/test_sxzm_page236.png"; }
        if (!wxFileExists(sxzm236Path)) { sxzm236Path = "../../tests/test_sxzm_page236.png"; }

        REQUIRE(wxFileExists(sxzm236Path));
        {
            DocumentLayoutResult res236;
            bool ok236 = engine.AnalyzeLayout(sxzm236Path, res236);
            REQUIRE(ok236 == true);
            std::cout << "\n--- 数学之美 Page 236 (W=" << res236.imageWidth << ", H=" << res236.imageHeight << ") detected " << res236.elements.size() << " elements:\n";
            bool foundDuplicateFormulaInTable = false;
            for (const auto& elem : res236.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                // 检查是否有落在表格内部的公式块 (y >= 1600)
                if ((elem.type == LayoutElementType::Formula || elem.labelName == "inline_formula" || elem.labelName == "display_formula") && elem.y1 >= 1600) {
                    foundDuplicateFormulaInTable = true;
                }
            }
            CHECK(foundDuplicateFormulaInTable == false);
        }

        // 验证用户真实图书第 5 页 (篇章标题 "总  序") 与 第 8 页 (目录 "目录 Contents") 必须被正确保留
        // 图片固定存放在 tests/ 目录中，脱离对外部网络共享 PDF 路径的依赖
        std::string page5Path = "tests/test_book_page5.png";
        std::string page8Path = "tests/test_book_page8.png";
        if (!wxFileExists(page5Path)) { page5Path = "../tests/test_book_page5.png"; }
        if (!wxFileExists(page5Path)) { page5Path = "../../tests/test_book_page5.png"; }
        if (!wxFileExists(page8Path)) { page8Path = "../tests/test_book_page8.png"; }
        if (!wxFileExists(page8Path)) { page8Path = "../../tests/test_book_page8.png"; }

        // 验证第 5 页 (总序)
        REQUIRE(wxFileExists(page5Path));
        {
            DocumentLayoutResult res5;
            bool ok5 = engine.AnalyzeLayout(page5Path, res5);
            REQUIRE(ok5 == true);
            CHECK(res5.elements.size() >= 3);
            std::cout << "\n--- Page 5 (W=" << res5.imageWidth << ", H=" << res5.imageHeight << ") detected " << res5.elements.size() << " elements:\n";
            bool foundTitle = false;
            for (const auto& elem : res5.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                if (elem.y1 >= 280 && elem.y2 <= 420 && elem.x1 >= 700 && elem.x2 <= 1000) {
                    foundTitle = true;
                }
            }
            CHECK(foundTitle == true);
        }

        // 验证第 8 页 (目录 Contents)
        REQUIRE(wxFileExists(page8Path));
        {
            DocumentLayoutResult res8;
            bool ok8 = engine.AnalyzeLayout(page8Path, res8);
            REQUIRE(ok8 == true);
            CHECK(res8.elements.size() >= 14);
            std::cout << "\n--- Page 8 (W=" << res8.imageWidth << ", H=" << res8.imageHeight << ") detected " << res8.elements.size() << " elements:\n";
            bool foundContents = false;
            for (const auto& elem : res8.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                if (elem.y1 >= 300 && elem.y2 <= 420 && elem.x1 >= 200 && elem.x2 <= 550) {
                    foundContents = true;
                }
            }
            CHECK(foundContents == true);
        }

        // 验证用户真实图书《中县干部：以成长历程为中心》第 97 页
        // 包含表格5-20 (含上标 7.5^①)、表格5-21、底部注解 ① 及页脚页码 97
        std::string zxgb97Path = "tests/test_zxgb_page97.png";
        if (!wxFileExists(zxgb97Path)) { zxgb97Path = "../tests/test_zxgb_page97.png"; }
        if (!wxFileExists(zxgb97Path)) { zxgb97Path = "../../tests/test_zxgb_page97.png"; }

        REQUIRE(wxFileExists(zxgb97Path));
        {
            DocumentLayoutResult res97;
            bool ok97 = engine.AnalyzeLayout(zxgb97Path, res97);
            REQUIRE(ok97 == true);
            CHECK(res97.elements.size() >= 10);
            std::cout << "\n--- 中县干部 Page 97 (W=" << res97.imageWidth << ", H=" << res97.imageHeight << ") detected " << res97.elements.size() << " elements:\n";
            bool foundTable520 = false;
            bool foundTable521 = false;
            bool foundBottomFootnote = false;
            bool leakedPageNumber97 = false;

            for (const auto& elem : res97.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                // 验证表 5-20 (y 落在 1400..1700 区间) 正确被识别为表格
                if (elem.type == LayoutElementType::Table && elem.y1 >= 1400 && elem.y2 <= 1700) {
                    foundTable520 = true;
                }
                // 验证表 5-21 (y 落在 2500..4000 区间) 正确被识别为表格
                if (elem.type == LayoutElementType::Table && elem.y1 >= 2500 && elem.y2 <= 4000) {
                    foundTable521 = true;
                }
                // 验证底部注解 ① (y >= 4000) 正确被识别并保留
                if ((elem.labelName == "vision_footnote" || elem.labelName == "footnote") && elem.y1 >= 4000) {
                    foundBottomFootnote = true;
                }
                // 验证右下角独立页码 "97" (x > 2600, y > 4150) 必须被过滤掉，不得误入正文
                if (elem.labelName == "number" || (elem.x1 >= 2600 && elem.y1 >= 4150)) {
                    leakedPageNumber97 = true;
                }
            }
            CHECK(foundTable520 == true);
            CHECK(foundTable521 == true);
            CHECK(foundBottomFootnote == true);
            CHECK(leakedPageNumber97 == false);
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

TEST_CASE("DocLayoutEngine - Suppress Contained Formula and Text Boxes in Table", "[layout]") {
    // 模拟表格区域及内部单元格误检为公式、文本、数字切片的候选框
    // 框 1: 表格全图大框 (score=0.9105)
    // 框 2: 表格首行单元格公式 k=1 (score=0.3102, y 落在表格内)
    // 框 3: 表格首行单元格公式 k=5 (score=0.3432, y 落在表格内)
    // 框 4: 表格首行单元格公式 k=6 (score=0.4055, y 落在表格内)
    // 框 5: 单元格数字/文本碎片 (score=0.5200, 落在表格内)
    // 框 6: 表格上方的独立图表标题 (score=0.5532, y 落在表格外，必须保留)
    std::vector<LayoutElement> elements = {
        LayoutElement{1, LayoutElementType::Table, "table", 0.9105f, 330, 1680, 1444, 2052, 0},
        LayoutElement{2, LayoutElementType::Formula, "inline_formula", 0.3102f, 604, 1688, 674, 1721, 0},
        LayoutElement{3, LayoutElementType::Formula, "inline_formula", 0.3432f, 982, 1687, 1054, 1722, 0},
        LayoutElement{4, LayoutElementType::Formula, "inline_formula", 0.4055f, 1111, 1687, 1183, 1722, 0},
        LayoutElement{5, LayoutElementType::Text, "text", 0.5200f, 400, 1750, 500, 1790, 0},
        LayoutElement{6, LayoutElementType::Title, "figure_title", 0.5532f, 321, 1595, 1052, 1633, 0}
    };

    DocLayoutFilterConfig cfg;
    cfg.mergeLayoutBlocks = true;
    cfg.containmentIosThreshold = 0.70f;

    DocLayoutEngine::SuppressContainedOrDuplicateBoxes(elements, cfg);

    // 核心断言：落在表格内部的所有公式与文本切片必须全部被抑制！仅保留表格和表头标题！
    REQUIRE(elements.size() == 2);
    bool foundTable = false;
    bool foundFigureTitle = false;
    for (const auto& elem : elements) {
        if (elem.type == LayoutElementType::Table || elem.labelName == "table") {
            foundTable = true;
            CHECK(elem.score >= 0.9105f);
        }
        if (elem.type == LayoutElementType::Title || elem.labelName == "figure_title") {
            foundFigureTitle = true;
            CHECK(elem.y2 <= 1633);
        }
    }
    CHECK(foundTable == true);
    CHECK(foundFigureTitle == true);
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

    // 验证用户真实文档《中县干部：以成长历程为中心》第 97 页 表5-20 
    // 单元格包含上标与末尾游离定界符 (7.5^{①}\()，必须正确转换为语义化 <sup> 标签且清除游离界符
    std::string otslZxgbTable =
        "<fcel>晋升为副科（年）<fcel>副科到正科（年）<fcel>正科到副处（年）<fcel>副处到正处（年）<nl>"
        "<fcel>8<fcel>2.7<fcel>7.3<fcel>7.5^{①}\\(<nl>";

    std::string htmlZxgb = DocumentPipeline::ConvertOtslToHtml(otslZxgbTable);
    REQUIRE_FALSE(htmlZxgb.empty());
    REQUIRE(htmlZxgb.find("<th>晋升为副科（年）</th>") != std::string::npos);
    REQUIRE(htmlZxgb.find("<th>副处到正处（年）</th>") != std::string::npos);
    REQUIRE(htmlZxgb.find("<td>8</td>") != std::string::npos);
    REQUIRE(htmlZxgb.find("<td>2.7</td>") != std::string::npos);
    REQUIRE(htmlZxgb.find("<td>7.3</td>") != std::string::npos);
    // 核心断言：7.5^{①}\( 必须转为 7.5<sup>①</sup>，绝不得泄漏未闭合的 \(
    REQUIRE(htmlZxgb.find("<td>7.5<sup>①</sup></td>") != std::string::npos);
    REQUIRE(htmlZxgb.find("\\(") == std::string::npos);

    // 验证数字电路文档第 16 页表格项中的 \n 转换为 <br>
    std::string otslPage16Table =
        "<ecel><fcel>2. Decoders<ecel><nl>"
        "<fcel>6<fcel>1. Program Control Structures (branches and loops)\\n2. Multiplexers<fcel>Chapter 8: Exercises 1, 2, 3 & 4<nl>"
        "<fcel>7<fcel>1. Reentrant subprograms and program stack\n2. Latches and flip-flops<fcel>Chapter 9: Exercise 1<nl>";
    std::string htmlPage16 = DocumentPipeline::ConvertOtslToHtml(otslPage16Table);
    REQUIRE_FALSE(htmlPage16.empty());
    // 单元格中的字面量 \n 必须转换为 <br>
    REQUIRE(htmlPage16.find("<td>1. Program Control Structures (branches and loops)<br>2. Multiplexers</td>") != std::string::npos);
    // 单元格中的实际换行符 \n 也必须转换为 <br>
    REQUIRE(htmlPage16.find("<td>1. Reentrant subprograms and program stack<br>2. Latches and flip-flops</td>") != std::string::npos);
    // 实体 & 必须正确转义为 &amp;
    REQUIRE(htmlPage16.find("<td>Chapter 8: Exercises 1, 2, 3 &amp; 4</td>") != std::string::npos);

    // 验证数字电路文档第 26 页表格中的布尔运算上单引号 (prime) 与公式
    // 单引号 ' 绝不能被转义为 &#039;，已有 &#039; 需自动还原为 '
    std::string otslPage26Table =
        "<fcel>Relationship<fcel>Rule No.<fcel>Relationship<fcel>Rule No.<nl>"
        "<fcel>\\(x + x' = 1\\)<fcel>7<fcel>\\(x \\times x' = 0\\)<fcel>8<nl>"
        "<fcel>\\(x + x&#039; = 1\\)<fcel>7<fcel>\\((x + y)' = x'y'\\)<fcel>15<nl>"
        "<fcel>x + x&#039; = 1<fcel>7<fcel>A'B'C'<fcel>Minterm<nl>";
    std::string htmlPage26 = DocumentPipeline::ConvertOtslToHtml(otslPage26Table);
    REQUIRE_FALSE(htmlPage26.empty());
    REQUIRE(htmlPage26.find("<td>\\(x + x' = 1\\)</td>") != std::string::npos);
    REQUIRE(htmlPage26.find("<td>\\(x \\times x' = 0\\)</td>") != std::string::npos);
    REQUIRE(htmlPage26.find("<td>\\((x + y)' = x'y'\\)</td>") != std::string::npos);
    REQUIRE(htmlPage26.find("<td>x + x' = 1</td>") != std::string::npos);
    REQUIRE(htmlPage26.find("<td>A'B'C'</td>") != std::string::npos);
    // 绝对不能存在破坏公式的 &#039; 实体泄漏
    REQUIRE(htmlPage26.find("&#039;") == std::string::npos);

    // 验证公式内部的 \neq 等以 \n 开头的 LaTeX 宏命令不会被误转为 <br>
    std::string otslFormulaWithNeq =
        "<fcel>Condition<fcel>Action<nl>"
        "<fcel>\\(x \\neq 0\\)\\nNext row<fcel>Execute<nl>";
    std::string htmlFormula = DocumentPipeline::ConvertOtslToHtml(otslFormulaWithNeq);
    REQUIRE(htmlFormula.find("\\(x \\neq 0\\)<br>Next row") != std::string::npos);
}

TEST_CASE("DocLayoutEngine - Filter Header and Footer Page Numbers", "[layout][filter]") {
    DocLayoutFilterConfig cfg;
    const int origW = 2800;
    const int origH = 3900;

    // 1. 用户真实文档：左侧页眉独立页码 (如第 2 页: [446, 445, 509, 512], type="number")
    LayoutElement leftHeaderNum{1, LayoutElementType::Text, "number", 0.95f, 446, 445, 509, 512, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(leftHeaderNum, origW, origH, cfg) == true);

    // 2. 用户真实文档：右侧页眉独立页码 (如第 5 页: [2411, 456, 2466, 518], type="number")
    LayoutElement rightHeaderNum{1, LayoutElementType::Text, "number", 0.96f, 2411, 456, 2466, 518, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(rightHeaderNum, origW, origH, cfg) == true);

    // 3. 底部页脚独立页码 (如居中: [1380, 3680, 1440, 3740], y ≈ 94%H)
    LayoutElement footerNum{10, LayoutElementType::Text, "number", 0.92f, 1380, 3680, 1440, 3740, 10};
    CHECK(DocLayoutEngine::ShouldFilterElement(footerNum, origW, origH, cfg) == true);

    // 4. 几何兜底：被模型误判为 "text" 的左上角或右上角单行页码
    LayoutElement misclassifiedCorner{1, LayoutElementType::Text, "text", 0.80f, 446, 445, 509, 512, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(misclassifiedCorner, origW, origH, cfg) == true);

    // 5. 显式模型页眉与页脚标签
    LayoutElement modelHeader{1, LayoutElementType::Header, "header", 0.88f, 1000, 420, 1800, 480, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(modelHeader, origW, origH, cfg) == true);

    LayoutElement modelFooter{20, LayoutElementType::Footer, "footer", 0.85f, 800, 3650, 2000, 3720, 20};
    CHECK(DocLayoutEngine::ShouldFilterElement(modelFooter, origW, origH, cfg) == true);

    // 6. 反向校验：页面正文段落 (绝不能被误删)
    LayoutElement bodyParagraph{2, LayoutElementType::Text, "text", 0.99f, 350, 800, 2450, 1800, 2};
    CHECK(DocLayoutEngine::ShouldFilterElement(bodyParagraph, origW, origH, cfg) == false);

    // 7. 反向校验：顶部文档主标题 (doc_title 绝不能被几何兜底误删)
    LayoutElement docTitle{1, LayoutElementType::Title, "doc_title", 0.98f, 600, 450, 2200, 580, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(docTitle, origW, origH, cfg) == false);

    // 8. 绝对保护：显式 footnote 与 vision_footnote 标签 (绝不能作为页眉/页脚被过滤)
    LayoutElement modelFootnote{10, LayoutElementType::Text, "footnote", 0.90f, 400, 3650, 2400, 3720, 10};
    CHECK(DocLayoutEngine::ShouldFilterElement(modelFootnote, origW, origH, cfg) == false);

    LayoutElement visionFootnote{11, LayoutElementType::Text, "vision_footnote", 0.88f, 500, 3660, 2200, 3730, 11};
    CHECK(DocLayoutEngine::ShouldFilterElement(visionFootnote, origW, origH, cfg) == false);

    // 9. 绝对保护：底部宽文本块 (可能为未被模型识别为 footnote 的普通注解文本，放行给 OCR 与正文标记研判)
    LayoutElement wideBottomAnnotation{12, LayoutElementType::Text, "text", 0.85f, 400, 3650, 2400, 3720, 12};
    CHECK(DocLayoutEngine::ShouldFilterElement(wideBottomAnnotation, origW, origH, cfg) == false);
}

TEST_CASE("DocumentPipeline - Secondary Page Number Filtering", "[pipeline][filter]") {
    const int pageW = 2800;
    const int pageH = 3900;

    // 1. 显式 number 标签在顶部/底部
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("2", "number", 446, 445, 509, 512, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("5", "number", 2411, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("86", "number", 1380, 3680, 1440, 3740, pageW, pageH) == true);

    // 2. 文本标签但在边角且文本为纯数字/装饰页码
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("2", "text", 446, 445, 509, 512, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("5", "text", 2411, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("- 2 -", "text", 446, 445, 550, 512, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("· 5 ·", "text", 2350, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("第 5 页", "text", 1300, 3680, 1500, 3740, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("Page 12", "text", 2300, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("IV", "text", 446, 445, 509, 512, pageW, pageH) == true);

    // 3. 反向校验：正文中包含数字的普通句子
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("In 1984, George Orwell wrote 1984", "text", 350, 800, 2450, 1800, pageW, pageH) == false);

    // 4. 反向校验：标题
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("第一章 绪论", "paragraph_title", 500, 450, 2000, 550, pageW, pageH) == false);

    // 5. 反向校验：注解文本绝不被误判为页码
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("[1] 参考文献详情说明", "text", 400, 3650, 2400, 3720, pageW, pageH) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("① 底部注解详情内容", "text", 400, 3650, 2400, 3720, pageW, pageH) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("注：底部特别说明", "text", 400, 3650, 2400, 3720, pageW, pageH) == false);

    // 6. 反向校验：目录标题与篇章标题即便带有短小英文或位于顶部角落，绝不能被误判为页码
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("目录 Contents", "text", 235, 323, 500, 401, 1600, 2318) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("目录", "text", 235, 323, 400, 401, 1600, 2318) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("总  序", "text", 235, 432, 500, 498, 1600, 2318) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("", "text", 235, 323, 500, 401, 1600, 2318) == false);
}

TEST_CASE("DocumentPipeline - Annotation and Footnote Marker Detection", "[pipeline][footnote]") {
    const int pageH = 3900;

    // 1. 正文注解数字标记检测 (HasAnnotationMarkers)
    // LaTeX 上标
    CHECK(DocumentPipeline::HasAnnotationMarkers("在模型注意力机制中\\(^{[1]}\\)，计算复杂度为O(N^2)") == true);
    CHECK(DocumentPipeline::HasAnnotationMarkers("实验结果显示\\(^1\\)显著优于基线模型") == true);
    // 带圈数字
    CHECK(DocumentPipeline::HasAnnotationMarkers("根据最新人口普查数据①，总人口趋于平稳") == true);
    CHECK(DocumentPipeline::HasAnnotationMarkers("第二项研究结论见附录②。") == true);
    // 方括号数字
    CHECK(DocumentPipeline::HasAnnotationMarkers("Transformer架构[1]已被广泛应用于各领域") == true);
    // 中文注记
    CHECK(DocumentPipeline::HasAnnotationMarkers("详细参数配置见【注1】") == true);
    CHECK(DocumentPipeline::HasAnnotationMarkers("测试方案[注2]已通过审核") == true);
    // Markdown 脚注引用
    CHECK(DocumentPipeline::HasAnnotationMarkers("如前文所述[^ref1]，系统保持高可用") == true);
    // 反向校验：普通文本无标记
    CHECK(DocumentPipeline::HasAnnotationMarkers("普通段落文本，没有任何注解或脚注标记。") == false);
    CHECK(DocumentPipeline::HasAnnotationMarkers("2024年10月8日系统运行正常") == false);

    // 2. 底部注解判定 (IsFootnoteOrAnnotation)
    // 显式模型标签
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("普通文本", "footnote", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("图表说明", "vision_footnote", 3500, 3600, pageH, false) == true);

    // 文本带有典型注解前缀
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("① 本文所引数据来源于国家统计局2023年统计年鉴。", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("[1] Vaswani A, et al. Attention is all you need. NeurIPS, 2017.", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("\\(^{[1]}\\) 这里是具体的底部详细说明文字。", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("注：表中所有数据均已剔除异常值。", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("Note: P-values less than 0.05 are considered statistically significant.", "text", 3500, 3600, pageH, false) == true);

    // 底部编号解释性文本 (topRatio >= 0.72f)
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("1. 本指标不含港澳台地区统计数据。", "text", 3200, 3300, pageH, false) == true);

    // 正文中存在注解标记，且底部存在具有内容的解释文本
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("由国家自然科学基金重点项目资助。", "text", 3500, 3600, pageH, true) == true);

    // 反向校验：纯页码或运行页眉绝不能被误判为注解
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("5", "text", 3500, 3600, pageH, false) == false);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("- 5 -", "text", 3500, 3600, pageH, false) == false);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("第 5 页", "text", 3500, 3600, pageH, false) == false);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("5", "number", 3500, 3600, pageH, true) == false);
}

TEST_CASE("DocLayoutEngine & DocumentPipeline - Empty Page and Image Content Detection", "[layout][pipeline][blank_page]") {
    // 1. 纯白空页判定 (255, 255, 255)
    wxImage whiteImg(300, 400);
    whiteImg.SetRGB(wxRect(0, 0, 300, 400), 255, 255, 255);
    CHECK(DocLayoutEngine::IsImageContentEmpty(whiteImg) == true);
    CHECK(DocumentPipeline::IsImageContentEmpty(whiteImg) == true);

    // 2. 纯浅色/米黄色复古纸张空页判定 (245, 240, 235)
    wxImage creamImg(300, 400);
    creamImg.SetRGB(wxRect(0, 0, 300, 400), 245, 240, 235);
    CHECK(DocLayoutEngine::IsImageContentEmpty(creamImg) == true);
    CHECK(DocumentPipeline::IsImageContentEmpty(creamImg) == true);

    // 3. 带有正文笔墨/图表的非空页面
    wxImage textImg(300, 400);
    textImg.SetRGB(wxRect(0, 0, 300, 400), 255, 255, 255);
    // 模拟正文区域黑色字符块 (80x100 黑色区域)
    textImg.SetRGB(wxRect(50, 50, 80, 100), 10, 10, 10);
    CHECK(DocLayoutEngine::IsImageContentEmpty(textImg) == false);
    CHECK(DocumentPipeline::IsImageContentEmpty(textImg) == false);

    // 4. 空/无效位图安全防护
    wxImage invalidImg;
    CHECK(DocLayoutEngine::IsImageContentEmpty(invalidImg) == true);
    CHECK(DocumentPipeline::IsImageContentEmpty(invalidImg) == true);
}

TEST_CASE("DocumentPipeline - Document-level translate_enabled configuration in JSON", "[pipeline][config]") {
    wxString tempDir = wxFileName::CreateTempFileName("doc_json_test");
    wxRemoveFile(tempDir);
    wxFileName::Mkdir(tempDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    std::string baseName = "test_doc";
    wxString dummyDocPath = tempDir + "/" + baseName + ".pdf";
    {
        std::ofstream f(dummyDocPath.ToStdString());
        f << "dummy content";
    }

    wxString outDir = tempDir + "/" + baseName;
    wxFileName::Mkdir(outDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    // 1. 测试当 JSON 中记录 translate_enabled: true 时
    {
        wxString jsonPath = outDir + "/" + baseName + ".json";
        wxString mdPath = outDir + "/" + baseName + ".md";
        {
            std::ofstream f(mdPath.ToStdString());
            f << "# Test Title\n\nContent";
        }
        {
            nlohmann::json j;
            j["file_name"] = baseName;
            j["total_pages"] = 2;
            j["completed_pages"] = 2;
            j["is_completed"] = true;
            j["translate_enabled"] = true;
            std::ofstream f(jsonPath.ToStdString());
            f << j.dump(2);
        }

        auto info = DocumentPipeline::CheckResumeInfo(dummyDocPath.ToUTF8().data(), outDir.ToUTF8().data());
        CHECK(info.hasResumeData == true);
        CHECK(info.translateEnabled == true);
    }

    // 2. 测试当 JSON 中记录 translate_enabled: false 时
    {
        wxString jsonPath = outDir + "/" + baseName + ".json";
        {
            nlohmann::json j;
            j["file_name"] = baseName;
            j["total_pages"] = 2;
            j["completed_pages"] = 2;
            j["is_completed"] = true;
            j["translate_enabled"] = false;
            std::ofstream f(jsonPath.ToStdString());
            f << j.dump(2);
        }

        auto info = DocumentPipeline::CheckResumeInfo(dummyDocPath.ToUTF8().data(), outDir.ToUTF8().data());
        CHECK(info.hasResumeData == true);
        CHECK(info.translateEnabled == false);
    }

    wxFileName::Rmdir(tempDir, wxPATH_RMDIR_RECURSIVE);
}

TEST_CASE("DocumentPipeline - AlignMarkdownFormat list and heading alignment", "[pipeline][markdown]") {
    // 1. 标题层级对齐
    CHECK(DocumentPipeline::AlignMarkdownFormat("# Document Title", "文档标题") == "# 文档标题");
    CHECK(DocumentPipeline::AlignMarkdownFormat("## Section Title", "章节标题") == "## 章节标题");
    CHECK(DocumentPipeline::AlignMarkdownFormat("### Sub Title", "# 误导标题") == "### 误导标题");

    // 2. 有序列表起始编号对齐 (单行)
    // 原文以 6. 起始，模型翻译误以 1. 起始，强制对齐为 6.，绝不累加或错位
    CHECK(DocumentPipeline::AlignMarkdownFormat("6. Cultural Models: 139", "1. 文化模式：139") == "6. 文化模式：139");
    // 原文以 6. 起始，模型翻译未输出编号，自动补全 6.
    CHECK(DocumentPipeline::AlignMarkdownFormat("6. Cultural Models: 139", "文化模式：139") == "6. 文化模式：139");
    // 原文以 6. 起始，模型翻译输出为无序列表，对齐为 6.
    CHECK(DocumentPipeline::AlignMarkdownFormat("6. Cultural Models: 139", "- 文化模式：139") == "6. 文化模式：139");
    // 原文为无序列表，模型翻译误写成有序列表，对齐为无序列表
    CHECK(DocumentPipeline::AlignMarkdownFormat("- Key Point", "1. 关键点") == "- 关键点");

    // 3. 有序列表编号一致时保持原样
    CHECK(DocumentPipeline::AlignMarkdownFormat("1. Introduction: 36 Ways", "1. 引言：36种方法") == "1. 引言：36种方法");

    // 4. 多行有序列表跨行起始偏移 (Delta) 对齐
    std::string multiSource = "6. Cultural Models: 139\n7. The Social Mind: 169\n8. Conclusion: 199";
    std::string multiTrans = "1. 文化模式：139\n2. 社会思维：169\n3. 结论：199";
    std::string expectedTrans = "6. 文化模式：139\n7. 社会思维：169\n8. 结论：199";
    CHECK(DocumentPipeline::AlignMarkdownFormat(multiSource, multiTrans) == expectedTrans);

    // 5. 引用块对齐
    CHECK(DocumentPipeline::AlignMarkdownFormat("> Important note", "重要说明") == "> 重要说明");
}

TEST_CASE("DocumentPipeline - AppendToMarkdown list separation without accumulation", "[pipeline][markdown]") {
    // 1. 普通文本段落之间保持标准双换行分隔
    std::string md1;
    DocumentPipeline::AppendToMarkdown(md1, "First paragraph.");
    DocumentPipeline::AppendToMarkdown(md1, "Second paragraph.");
    CHECK(md1 == "First paragraph.\n\nSecond paragraph.");

    // 2. 当末尾为列表项且追加内容也是列表项时，自动注入 <!-- --> 隔离标记，防止 marked.js 自动合并累加
    std::string md2;
    DocumentPipeline::AppendToMarkdown(md2, "1. Introduction: 36 Ways 1");
    DocumentPipeline::AppendToMarkdown(md2, "1. 引言：学习 1");
    CHECK(md2 == "1. Introduction: 36 Ways 1\n\n<!-- -->\n\n1. 引言：学习 1");

    // 3. 继续追加下一个列表项时，同样保持隔离
    DocumentPipeline::AppendToMarkdown(md2, "2. Semiotic Domains: 13");
    CHECK(md2 == "1. Introduction: 36 Ways 1\n\n<!-- -->\n\n1. 引言：学习 1\n\n<!-- -->\n\n2. Semiotic Domains: 13");

    // 4. 防御：已存在隔离标记时不重复插入
    std::string md3 = "1. First item\n\n<!-- -->";
    DocumentPipeline::AppendToMarkdown(md3, "1. First translation");
    CHECK(md3 == "1. First item\n\n<!-- -->\n\n1. First translation");
}

TEST_CASE("DocumentPipeline - Parse and Translate What-Video-Games Page 6 Table of Contents without Number Accumulation", "[pipeline][toc][translate]") {
    // 1. 定位并准备真实测试图片 (tests/test_videogames_page6.png)
    std::string page6ImgPath = FindTestAsset("tests/test_videogames_page6.png");

    // 如果图片不存在但用户下载目录存在原始 PDF，则通过 PdfHelper 自动提取第 6 页为测试图片
    if (!wxFileExists(page6ImgPath)) {
        std::string pdfPath = "/Users/qizhi/Downloads/What-Video-Games-Have-to-Teach-us-About-Learning-and-Literacy-2003.-ilovepdf-compressed.pdf";
        if (wxFileExists(pdfPath)) {
            wxImage pageImg;
            if (PdfHelper::RenderPage(pdfPath, 5, pageImg, 1600) && pageImg.IsOk()) {
                pageImg.SaveFile("tests/test_videogames_page6.png", wxBITMAP_TYPE_PNG);
                page6ImgPath = "tests/test_videogames_page6.png";
            }
        }
    }

    REQUIRE(wxFileExists(page6ImgPath));

    // 2. 验证版面分析：解析第 6 页的目录版面结构
    DocLayoutEngine engine;
    std::string resolvedLayout = ResolveTestLayoutModelPath();

    if (!resolvedLayout.empty()) {
        bool initOk = engine.Initialize(resolvedLayout, 0, 4);
        REQUIRE(initOk == true);

        DocumentLayoutResult layoutRes;
        bool analyzed = engine.AnalyzeLayout(page6ImgPath, layoutRes);
        REQUIRE(analyzed == true);
        CHECK(layoutRes.elements.size() >= 1);

        // 验证 CONTENTS 目录标题及目录主体文本块均被正确检测
        bool foundTitleOrTOC = false;
        for (const auto& elem : layoutRes.elements) {
            if (elem.type == LayoutElementType::Title || elem.type == LayoutElementType::Text) {
                foundTitleOrTOC = true;
                break;
            }
        }
        CHECK(foundTitleOrTOC == true);
    }

    // 3. 模拟第 6 页目录 OCR 识别得到的原文与模型翻译译文
    std::string sourceTOC =
        "1. Introduction: 36 Ways to Learn a Video Game 1\n"
        "2. Semiotic Domains:\n"
        "Is Playing Video Games a “Waste of Time”? 13\n"
        "3. Learning and Identity:\n"
        "What Does It Mean to Be a Half-Elf? 51\n"
        "4. Situated Meaning and Learning:\n"
        "What Should You Do After You Have\n"
        "Destroyed the Global Conspiracy? 73\n"
        "5. Telling and Doing:\n"
        "Why Doesn’t Lara Croft Obey Professor Von Croy? 113\n"
        "6. Cultural Models:\n"
        "Do You Want to Be the Blue Sonic or the Dark Sonic? 139\n"
        "7. The Social Mind:\n"
        "How Do You Get Your Corpse Back After You’ve Died? 169\n"
        "8. Conclusion: Duped or Not? 199\n"
        "Appendix: The 36 Learning Principles 207\n"
        "References 213\n"
        "Index 221";

    std::string transTOC =
        "1. 引言：学习电子游戏的36种方法 1\n"
        "2. 符号学领域：玩电子游戏算是“浪费时间”吗？ 13\n"
        "3. 学习与身份认同：成为半精灵意味着什么？ 51\n"
        "4. 情境意义与学习：摧毁全球阴谋之后该做什么？ 73\n"
        "5. 陈述与行动：为什么劳拉·克劳福特不听从冯·克罗伊教授的命令？ 113\n"
        "6. 文化模式：你想成为蓝色索尼克还是黑暗索尼克？ 139\n"
        "7. 社会思维：死后如何找回自己的尸体？ 169\n"
        "8. 结论：真的被欺骗了吗？ 199\n"
        "附录：36条学习原则 207\n"
        "参考文献 213\n"
        "索引 221";

    // 4. 验证 AlignMarkdownFormat：保持译文序列与原序列 1..8 严格对应，不发生偏移
    std::string alignedTOC = DocumentPipeline::AlignMarkdownFormat(sourceTOC, transTOC);
    CHECK(alignedTOC.find("1. 引言") != std::string::npos);
    CHECK(alignedTOC.find("2. 符号学领域") != std::string::npos);
    CHECK(alignedTOC.find("8. 结论") != std::string::npos);
    // 绝对不能包含累加编号 9..16
    CHECK(alignedTOC.find("9. 引言") == std::string::npos);
    CHECK(alignedTOC.find("10. 符号学") == std::string::npos);
    CHECK(alignedTOC.find("16. 结论") == std::string::npos);

    // 5. 验证双语内容组装 (AssemblePageContent 核心逻辑)：注入 <!-- --> 隔离标记
    // 使得 Markdown 渲染引擎能够识别这是两个独立的有序列表，杜绝自动递增累加为 9..16
    static const std::regex listRegex(R"((\n|^)\s*(\d+[\.\)]|[\-\*\+\•])\s+)");
    std::string assembledTOC;
    if (std::regex_search(alignedTOC, listRegex) || std::regex_search(sourceTOC, listRegex)) {
        assembledTOC = sourceTOC + "\n\n<!-- -->\n\n" + alignedTOC;
    } else {
        assembledTOC = sourceTOC + "\n\n" + alignedTOC;
    }

    CHECK(assembledTOC.find("<!-- -->") != std::string::npos);

    // 6. 验证 AppendToMarkdown 追加隔离机制
    std::string pageMarkdown;
    DocumentPipeline::AppendToMarkdown(pageMarkdown, sourceTOC);
    DocumentPipeline::AppendToMarkdown(pageMarkdown, alignedTOC);

    CHECK(pageMarkdown.find("<!-- -->") != std::string::npos);
    CHECK(pageMarkdown.find("1. Introduction") != std::string::npos);
    CHECK(pageMarkdown.find("8. Conclusion") != std::string::npos);
    CHECK(pageMarkdown.find("1. 引言") != std::string::npos);
    CHECK(pageMarkdown.find("8. 结论") != std::string::npos);
    CHECK(pageMarkdown.find("9. 引言") == std::string::npos);
    CHECK(pageMarkdown.find("16. 结论") == std::string::npos);

    // 7. 验证单项列表对齐：例如版面分析将列表项 6 独立切片
    // 原文为 "6. Cultural Models: 139"，若模型翻译误写为 "1. 文化模式：139"，必须自动纠偏为 6.
    std::string singleSource = "6. Cultural Models:\nDo You Want to Be the Blue Sonic or the Dark Sonic? 139";
    std::string singleTransMisnumbered = "1. 文化模式：\n你想成为蓝色索尼克还是黑暗索尼克？ 139";
    std::string singleAligned = DocumentPipeline::AlignMarkdownFormat(singleSource, singleTransMisnumbered);
    CHECK(singleAligned.rfind("6. 文化模式", 0) == 0);

    // 组合追加时，原文与译文之间以及与前后列表项之间均有隔离标记，杜绝累加
    std::string singleAssembled;
    DocumentPipeline::AppendToMarkdown(singleAssembled, singleSource);
    DocumentPipeline::AppendToMarkdown(singleAssembled, singleAligned);
    CHECK(singleAssembled.find("<!-- -->") != std::string::npos);
    CHECK(singleAssembled.find("6. Cultural Models") != std::string::npos);
    CHECK(singleAssembled.find("6. 文化模式") != std::string::npos);
}

TEST_CASE("CleanAndFormatFormula: Runaway repetition truncation and delimiter/brace balancing", "[formula][pipeline]") {
    // 1. 验证数字电路第 59 页自回归失控重复与单侧 \[ 定界符截断
    std::string repeatingFormula = "\\[\\mathrm{A p}~=~\\left(\\mathrm{A}^{\\star}\\mathrm{B}\\right)^{\\star}\\left(\\mathrm{C}^{\\star}\\mathrm{D}\\right)\\quad\\mathrm{X p}~=~\\left(\\mathrm{A}^{\\star}\\mathrm{B}\\right)^{\\star}\\left(\\mathrm{C}^{\\star}\\mathrm{D}\\right)\\quad\\mathrm{N O p}~=~\\mathrm{!}\\left(\\right.\\left(\\mathrm{!}\\left(\\mathrm{A}+\\mathrm{B}\\right)+\\mathrm{!}\\left(\\mathrm{C}+\\mathrm{D}\\right)\\right)\\mathrm{~a n d~}\\mathrm{b a s s i s i o n~t o~}";
    for (int k = 0; k < 50; ++k) {
        repeatingFormula += "}\\mathrm{C}^{\\star}\\mathrm{D}\\mathrm{A}\\mathrm{B";
    }

    std::string cleaned1 = DocumentPipeline::CleanAndFormatFormula(repeatingFormula);

    // 必须剥离单侧开头的 \[
    CHECK(cleaned1.find("\\[") == std::string::npos);
    // 必须截断自回归死循环模式
    CHECK(cleaned1.find("b a s s i s i o n") == std::string::npos);
    CHECK(cleaned1.find("~a n d~") == std::string::npos);
    // 必须完整保留有效核心公式
    CHECK(cleaned1.find("\\mathrm{A p}") != std::string::npos);
    CHECK(cleaned1.find("\\mathrm{X p}") != std::string::npos);
    CHECK(cleaned1.find("\\mathrm{N O p}") != std::string::npos);
    // 必须包裹在标准独立公式块 $$ ... $$ 中
    CHECK(cleaned1.rfind("$$\n", 0) == 0);
    CHECK(cleaned1.find("\n$$") != std::string::npos);

    // 2. 验证多行公式环境未闭合花括号自动平衡 (如第 59 页 El 4 的 \\begin{array} 缺少闭合 })
    std::string unclosedArray = "\\begin{array}{r l r l}{\\mathrm{O p}}&{=}&{\\left(\\mathrm{A}+\\mathrm{B}\\right)+\\left(\\mathrm{C}+\\mathrm{D}\\right)}&{\\quad\\mathrm{N A p}}\\\\ &{}&{=}&{\\mathrm{!}\\left(\\right.\\left.\\left(\\mathrm{A}^{\\star}\\mathrm{B}\\right)^{\\star}\\right.\\left.\\mathrm{!}\\left(\\mathrm{C}^{\\star}\\mathrm{D}\\right)\\right.\\mathrm{)}\\end{array}";
    std::string cleanedArray = DocumentPipeline::CleanAndFormatFormula(unclosedArray);

    int openBraces = 0, closeBraces = 0;
    for (size_t i = 0; i < cleanedArray.size(); ++i) {
        if (cleanedArray[i] == '{' && (i == 0 || cleanedArray[i-1] != '\\')) openBraces++;
        if (cleanedArray[i] == '}' && (i == 0 || cleanedArray[i-1] != '\\')) closeBraces++;
    }
    CHECK(openBraces == closeBraces);
    CHECK(cleanedArray.find("\\end{array}") != std::string::npos);

    // 3. 验证未闭合的 \\left 自动追加 \\right.
    std::string unclosedLeft = "f(x) = \\left( \\frac{1}{2} + x";
    std::string cleanedLeft = DocumentPipeline::CleanAndFormatFormula(unclosedLeft);
    CHECK(cleanedLeft.find("\\right.") != std::string::npos);

    // 4. 验证 HTML 实体与 Unicode prime 正确规范化为 '
    std::string entityFormula = "x + x&#039; = 1 \\quad y + y’ = 1 \\quad z + z′ = 1";
    std::string cleanedEntity = DocumentPipeline::CleanAndFormatFormula(entityFormula);
    CHECK(cleanedEntity.find("x + x' = 1") != std::string::npos);
    CHECK(cleanedEntity.find("y + y' = 1") != std::string::npos);
    CHECK(cleanedEntity.find("z + z' = 1") != std::string::npos);
    CHECK(cleanedEntity.find("&#039;") == std::string::npos);
}

TEST_CASE("SanitizeInlineLatex: Repair unclosed braces and LaTeX decoration commands", "[latex][pipeline]") {
    // 1. 验证第 14 页翻译中缺失闭合花括号的 \(\underline{\text{URL}\) 自动补齐闭合花括号
    std::string unclosedMath1 = "该文本可从 \\(\\underline{\\text{http://cupola.gettysburg.edu/oer/1}\\) 下载。关于该文本的更多资料，可访问 \\(\\underline{\\text{http://chuckkann.com}\\)。";
    std::string sanitized1 = DocumentPipeline::SanitizeInlineLatex(unclosedMath1);
    CHECK(sanitized1 == "该文本可从 \\(\\underline{\\text{http://cupola.gettysburg.edu/oer/1}}\\) 下载。关于该文本的更多资料，可访问 \\(\\underline{\\text{http://chuckkann.com}}\\)。");

    // 2. 验证已闭合的双花括号保持不变
    std::string closedMath = "This text can be downloaded from \\(\\underline{\\text{http://cupola.gettysburg.edu/oer/1}}\\).";
    CHECK(DocumentPipeline::SanitizeInlineLatex(closedMath) == closedMath);

    // 3. 验证无 \\( \\) 定界符但外层丢失闭合花括号的排版命令自动补齐
    std::string nakedCmd = "该文本可从 \\underline{\\text{http://cupola.gettysburg.edu/oer/1} 下载。可访问 \\underline{\\text{http://chuckkann.com}。";
    std::string sanitizedNaked = DocumentPipeline::SanitizeInlineLatex(nakedCmd);
    CHECK(sanitizedNaked.find("\\underline{\\text{http://cupola.gettysburg.edu/oer/1}} 下载") != std::string::npos);
    CHECK(sanitizedNaked.find("\\underline{\\text{http://chuckkann.com}}。") != std::string::npos);

    // 4. 验证行内普通公式与多层嵌套正常保留
    std::string formulaInline = "公式为 $x + y = z$ 以及 $f(x) = \\sqrt{x^2 + 1}$ 无需额外修改";
    CHECK(DocumentPipeline::SanitizeInlineLatex(formulaInline) == formulaInline);
}

TEST_CASE("DocLayoutEngine - Duplicate Header/Footer Suppression Protection", "[layout][filter]") {
    // 模拟真实被过滤的页眉/页脚/页码候选框
    std::vector<LayoutElement> filteredBoxes = {
        // 1. 顶部页眉
        LayoutElement{1, LayoutElementType::Header, "header", 0.92f, 200, 100, 1400, 160, 1},
        // 2. 底部页码 (小框，位于页面底部中央: [1459, 4293, 1519, 4353])
        LayoutElement{2, LayoutElementType::Text, "number", 0.63f, 1459, 4293, 1519, 4353, 2}
    };

    // 场景 1: 模型在同一物理位置多类别输出的重影副标签 (例如在顶部页眉位置同时输出了 paragraph_title)
    // 物理位置高度重合 (IoU >= 0.50)，必须被抑制！
    LayoutElement ghostHeaderTitle{3, LayoutElementType::Title, "paragraph_title", 0.85f, 210, 102, 1395, 158, 1};
    CHECK(DocLayoutEngine::IsDuplicateOfFilteredHeaderFooter(ghostHeaderTitle, filteredBoxes) == true);

    // 场景 2: 面积相近且大部分落在页眉区域内的同位副标签 (coverageA >= 0.70 且 areaA <= areaB * 1.5)
    LayoutElement ghostHeaderFragment{4, LayoutElementType::Text, "text", 0.70f, 250, 105, 1200, 155, 1};
    CHECK(DocLayoutEngine::IsDuplicateOfFilteredHeaderFooter(ghostHeaderFragment, filteredBoxes) == true);

    // 场景 3 (核心 Bug 修复防护): 正文大段落/目录汇总块偶然包含或覆盖底部的页码小框
    // (例如 authenticaccount02stau.pdf 第 11 页：正文大块 [144, 2159, 2699, 4360]，内部恰好包含了页码 [1459, 4293, 1519, 4353])
    // 正文块面积 560 万像素，页码仅 3600 像素，coverageA 仅 0.06%，绝不能判定为页码的重影而误杀！
    LayoutElement largeBodyText{5, LayoutElementType::Text, "text", 0.86f, 144, 2159, 2699, 4360, 3};
    CHECK(DocLayoutEngine::IsDuplicateOfFilteredHeaderFooter(largeBodyText, filteredBoxes) == false);

    // 场景 4: 普通正文段落，与页眉页脚无任何交集
    LayoutElement regularParagraph{6, LayoutElementType::Text, "text", 0.95f, 200, 300, 1400, 800, 4};
    CHECK(DocLayoutEngine::IsDuplicateOfFilteredHeaderFooter(regularParagraph, filteredBoxes) == false);

    // 场景 5: 表格区域覆盖下半区
    LayoutElement tableBlock{7, LayoutElementType::Table, "table", 0.90f, 150, 2000, 1450, 4200, 5};
    CHECK(DocLayoutEngine::IsDuplicateOfFilteredHeaderFooter(tableBlock, filteredBoxes) == false);
}

TEST_CASE("DocLayoutEngine - Page 11 Layout Analysis Regression", "[layout][page11]") {
    std::string testImage = FindTestAsset("tests/test_authenticaccount_page11.png");
    std::string resolvedModel = ResolveTestLayoutModelPath();

    if (!resolvedModel.empty() && wxFileExists(wxString::FromUTF8(resolvedModel)) && wxFileExists(wxString::FromUTF8(testImage))) {
        DocLayoutEngine engine;
        REQUIRE(engine.Initialize(resolvedModel, 0, 4) == true);

        DocumentLayoutResult res;
        REQUIRE(engine.AnalyzeLayout(testImage, res) == true);

        // 第 11 页共有 6 个元素：doc_title, text("OF THE"), text("SECOND VOLUME"),
        // paragraph_title("CHAPTER I."), text("PROGRESS OF THE EMBASSY..."), 以及大正文块！
        CHECK(res.elements.size() == 6);

        // 验证正文大块成功被保留，未被当做页码的重影误杀
        bool foundLargeBodyText = false;
        for (const auto& el : res.elements) {
            int area = (el.x2 - el.x1) * (el.y2 - el.y1);
            if (area > 1000000 && el.labelName == "text") {
                foundLargeBodyText = true;
                break;
            }
        }
        CHECK(foundLargeBodyText == true);
    }
}

TEST_CASE("PdfHelper & DocLayoutEngine - Page 56 PDFium Engine Rendering & Empty Filter", "[pdf][layout][page56]") {
    // 1. 验证空纸张渐变图像被正确识别为空白页 (Scheme 1)
    wxImage blankParchment(800, 1000);
    blankParchment.SetRGB(wxRect(0, 0, 800, 1000), 242, 227, 191);
    // 模拟轻微扫描仪阴影与纸张渐变 (无深色文字墨迹)
    for (int y = 0; y < 1000; ++y) {
        for (int x = 0; x < 50; ++x) {
            blankParchment.SetRGB(x, y, 225, 210, 175);
        }
    }
    CHECK(DocLayoutEngine::IsImageContentEmpty(blankParchment) == true);

    // 2. 验证受 JBIG2 解码异常影响的 PDF 第 56 页能够被 PdfHelper 成功保底渲染出文本 (Scheme 2)
    std::string pdfPath = "/Volumes/SHARE FILES/authenticaccount02stau.pdf";
    if (wxFileExists(wxString::FromUTF8(pdfPath))) {
        wxImage page56Img;
        // 第 56 页对应 pageIndex = 55
        bool renderOk = PdfHelper::RenderPage(pdfPath, 55, page56Img, 1000);
        REQUIRE(renderOk == true);
        REQUIRE(page56Img.IsOk() == true);
        CHECK(page56Img.GetWidth() == 1000);

        // 统计渲染图像中的有效深色笔墨像素
        int w = page56Img.GetWidth();
        int h = page56Img.GetHeight();
        const unsigned char* data = page56Img.GetData();
        int darkPixels = 0;
        int totalPixels = w * h;
        for (int i = 0; i < totalPixels; ++i) {
            int r = data[i * 3];
            int g = data[i * 3 + 1];
            int b = data[i * 3 + 2];
            if (r < 140 && g < 140 && b < 140) {
                darkPixels++;
            }
        }

        // 验证文本层成功合成到了光栅化图像上，包含大量深色笔墨
        CHECK(darkPixels > 10000);

        // 验证合成文本后的页面不会被 IsImageContentEmpty 视为空白页
        CHECK(DocLayoutEngine::IsImageContentEmpty(page56Img) == false);
    }
}
