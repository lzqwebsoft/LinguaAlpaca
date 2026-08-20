#include <catch2/catch.hpp>
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <vector>
#include <string>

#include "core/DictEngine.hpp"

using namespace LinguaAlpaca;

namespace {

inline void WriteBigEndian32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// 创建测试用 StarDict 词典文件
std::string CreateTestStarDict(const std::string& baseDir, const std::string& dictName) {
    wxString dir = wxString::FromUTF8(baseDir);
    if (!wxDirExists(dir)) {
        wxFileName::Mkdir(dir, 0777, wxPATH_MKDIR_FULL);
    }

    wxString basePath = dir + wxFileName::GetPathSeparator() + wxString::FromUTF8(dictName);

    struct TestEntry {
        std::string word;
        std::string def;
    };

    std::vector<TestEntry> entries = {
        { "alpaca", "[æl'pækə] n. 羊驼, 羊驼毛" },
        { "apple", "[ˈæpl] n. 苹果; 苹果树" },
        { "banana", "[bə'nɑːnə] n. 香蕉, 芭蕉" },
        { "hello", "[hə'ləʊ] int. 喂, 你好; 问候" },
        { "world", "[wɜːld] n. 世界, 地球" }
    };

    // 1. 构建 .dict 数据
    std::string dictData;
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> sizes;
    for (const auto& e : entries) {
        offsets.push_back(static_cast<uint32_t>(dictData.size()));
        sizes.push_back(static_cast<uint32_t>(e.def.size()));
        dictData += e.def;
    }

    wxFile dictFile(basePath + ".dict", wxFile::write);
    dictFile.Write(dictData.data(), dictData.size());
    dictFile.Close();

    // 2. 构建 .idx 索引
    std::vector<uint8_t> idxBytes;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& w = entries[i].word;
        idxBytes.insert(idxBytes.end(), w.begin(), w.end());
        idxBytes.push_back(0); // null 结尾
        WriteBigEndian32(idxBytes, offsets[i]);
        WriteBigEndian32(idxBytes, sizes[i]);
    }

    wxFile idxFile(basePath + ".idx", wxFile::write);
    idxFile.Write(idxBytes.data(), idxBytes.size());
    idxFile.Close();

    // 3. 构建 .ifo 元数据
    std::string ifoContent =
        "StarDict's dict ifo file\n"
        "version=2.4.2\n"
        "wordcount=" + std::to_string(entries.size()) + "\n"
        "idxfilesize=" + std::to_string(idxBytes.size()) + "\n"
        "bookname=Test English-Chinese Dictionary\n"
        "sametypesequence=m\n"
        "description=A unit test dictionary for LinguaAlpaca\n"
        "author=DeepMind Agent\n";

    wxFile ifoFile(basePath + ".ifo", wxFile::write);
    ifoFile.Write(ifoContent.data(), ifoContent.size());
    ifoFile.Close();

    return (basePath + ".ifo").ToUTF8().data();
}

} // namespace

TEST_CASE("DictEngine - StarDict Parser & Lookup", "[core][dict_engine]") {
    wxString tempDir = wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator() + "lingua_alpaca_test_dicts";
    if (!wxDirExists(tempDir)) {
        wxFileName::Mkdir(tempDir, 0777, wxPATH_MKDIR_FULL);
    }

    std::string ifoPath = CreateTestStarDict(tempDir.ToUTF8().data(), "test_dict");

    SECTION("StarDictBook Load and Lookup Test") {
        StarDictBook book;
        REQUIRE(book.Load(ifoPath) == true);
        REQUIRE(book.IsLoaded() == true);

        const auto& info = book.GetInfo();
        REQUIRE(info.bookName == "Test English-Chinese Dictionary");
        REQUIRE(info.wordCount == 5);

        // 精确检索
        DictSearchResult res;
        REQUIRE(book.Lookup("alpaca", res) == true);
        REQUIRE(res.word == "alpaca");
        REQUIRE(res.phonetic == "[æl'pækə]");
        REQUIRE(res.definition.find("羊驼") != std::string::npos);

        // 大小写不敏感检索
        DictSearchResult resUpper;
        REQUIRE(book.Lookup("ALPACA", resUpper) == true);
        REQUIRE(resUpper.word == "alpaca");

        // 单词不存在
        DictSearchResult resNotFound;
        REQUIRE(book.Lookup("nonexistent_word", resNotFound) == false);

        // 前缀联想
        std::vector<std::string> suggestions;
        book.GetSuggestions("a", suggestions, 10);
        REQUIRE(suggestions.size() == 2); // alpaca, apple
        REQUIRE(suggestions[0] == "alpaca");
        REQUIRE(suggestions[1] == "apple");
    }

    SECTION("DictEngine Directory Loading and Aggregation Test") {
        DictEngine engine;
        size_t loadedCount = engine.LoadDictionaries(tempDir.ToUTF8().data());
        REQUIRE(loadedCount >= 1);
        REQUIRE(engine.HasDictionaries() == true);
        REQUIRE(engine.GetTotalWordCount() >= 5);

        auto dicts = engine.GetLoadedDictionaries();
        REQUIRE(!dicts.empty());
        REQUIRE(dicts[0].bookName == "Test English-Chinese Dictionary");

        // 多词典聚合查询
        auto results = engine.Lookup("hello");
        REQUIRE(!results.empty());
        REQUIRE(results[0].word == "hello");
        REQUIRE(results[0].phonetic == "[hə'ləʊ]");
        REQUIRE(results[0].definition.find("你好") != std::string::npos);

        // 前缀候选词列表
        auto sugg = engine.GetSuggestions("b", 10);
        REQUIRE(!sugg.empty());
        REQUIRE(sugg[0] == "banana");
    }
}
