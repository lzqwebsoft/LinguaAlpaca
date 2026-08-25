#include <catch2/catch.hpp>
#include "core/dict/DictFormatter.hpp"

using namespace LinguaAlpaca;

TEST_CASE("DictFormatter - Utf32ToUtf8 and DecodeHtmlEntities", "[core][dict_formatter]") {
    SECTION("Utf32ToUtf8 converts correctly") {
        REQUIRE(DictFormatter::Utf32ToUtf8(0x41) == "A");
        REQUIRE(DictFormatter::Utf32ToUtf8(0xE9) == "é");
        REQUIRE(DictFormatter::Utf32ToUtf8(0x4E2D) == "中");
        REQUIRE(DictFormatter::Utf32ToUtf8(0x1F600) == "😀");
    }

    SECTION("DecodeHtmlEntities decodes named and numeric entities") {
        std::string raw = "&lt;hello&gt; &amp; &quot;world&quot; &apos;test&apos;&nbsp;&#160;&#x4E2D;&#x6587;";
        std::string decoded = DictFormatter::DecodeHtmlEntities(raw);
        REQUIRE(decoded.find("<hello> & \"world\" 'test'  中文") != std::string::npos);
    }
}

TEST_CASE("DictFormatter - Plaintext Unescape ('m')", "[core][dict_formatter]") {
    SECTION("Unescapes escaped characters and normalizes newlines") {
        std::string raw = "First line\\nSecond line\\tIndented\\\\Backslash\\r\\n\\n\\n\\nThird line";
        std::string res = DictFormatter::UnescapePlaintext(raw);

        REQUIRE(res.find("First line\nSecond line\tIndented\\Backslash") != std::string::npos);
        REQUIRE(res.find("Third line") != std::string::npos);
        // 不超过连续两个换行
        REQUIRE(res.find("\n\n\n") == std::string::npos);
    }

    SECTION("Trims leading and trailing empty lines") {
        std::string raw = "  \\n\\n\\r\\nActual content\\n\\n  ";
        std::string res = DictFormatter::UnescapePlaintext(raw);
        REQUIRE(res == "Actual content");
    }
}

TEST_CASE("DictFormatter - Phonetic Formatting ('t', 'y')", "[core][dict_formatter]") {
    SECTION("Cleans phonetic string and decodes entities") {
        std::string raw = "  [&#601;&#39;l&#230;m]  ";
        std::string res = DictFormatter::FormatPhonetic(raw);
        REQUIRE(res == "[ə'læm]");
    }
}

TEST_CASE("DictFormatter - HTML Formatting ('h', 'H')", "[core][dict_formatter]") {
    SECTION("Converts block elements, paragraphs and headings") {
        std::string html = "<h1>Dictionary Header</h1><p>First paragraph with <b>bold</b> and <i>italic</i>.</p><hr/><p>Second paragraph.</p>";
        std::string res = DictFormatter::FormatHtml(html);

        REQUIRE(res.find("【Dictionary Header】") != std::string::npos);
        REQUIRE(res.find("First paragraph with bold and italic.") != std::string::npos);
        REQUIRE(res.find("────────────────") != std::string::npos);
        REQUIRE(res.find("Second paragraph.") != std::string::npos);
    }

    SECTION("Converts lists and definition terms") {
        std::string html = "<ul><li>Apple</li><li>Banana</li></ul><ol><li>First</li><li>Second</li></ol><dl><dt>Noun</dt><dd>A word that names a thing.</dd></dl>";
        std::string res = DictFormatter::FormatHtml(html);

        REQUIRE(res.find("• Apple") != std::string::npos);
        REQUIRE(res.find("• Banana") != std::string::npos);
        REQUIRE(res.find("1. First") != std::string::npos);
        REQUIRE(res.find("2. Second") != std::string::npos);
        REQUIRE(res.find("▸ Noun") != std::string::npos);
        REQUIRE(res.find("A word that names a thing.") != std::string::npos);
    }

    SECTION("Converts tables and ignores scripts/styles/comments") {
        std::string html = "<style>.x{color:red;}</style><!-- comment --><table><tr><td>Col1</td><td>Col2</td></tr></table>";
        std::string res = DictFormatter::FormatHtml(html);

        REQUIRE(res.find(".x{color:red;}") == std::string::npos);
        REQUIRE(res.find("comment") == std::string::npos);
        REQUIRE(res.find("Col1 | Col2") != std::string::npos);
    }
}

TEST_CASE("DictFormatter - XDXF Formatting ('x', 'X')", "[core][dict_formatter]") {
    SECTION("Parses XDXF tags cleanly") {
        std::string xdxf = "<k>alpaca</k><tr>[æl'pækə]</tr><pos>n.</pos><dtrn>羊驼；羊驼毛</dtrn><ex>The alpaca is native to South America.</ex>";
        std::string res = DictFormatter::FormatXdxf(xdxf);

        REQUIRE(res.find("[æl'pækə]") != std::string::npos);
        REQUIRE(res.find("[n.]") != std::string::npos);
        REQUIRE(res.find("• 羊驼；羊驼毛") != std::string::npos);
        REQUIRE(res.find("【例】 The alpaca is native to South America.") != std::string::npos);
    }
}

TEST_CASE("DictFormatter - Pango, Kingsoft & MediaWiki", "[core][dict_formatter]") {
    SECTION("Pango markup ('g')") {
        std::string pango = "<span foreground=\"#0000FF\" font_desc=\"12\"><b>Hello</b></span> <i>world</i> &amp; test";
        std::string res = DictFormatter::FormatPango(pango);
        REQUIRE(res == "Hello world & test");
    }

    SECTION("Kingsoft XML ('k')") {
        std::string ks = "<pos>n.</pos><pron>hə'ləʊ</pron><def>问候；你好</def><sent><orig>Say hello to him.</orig><trans>向他问好。</trans></sent>";
        std::string res = DictFormatter::FormatKingsoft(ks);

        REQUIRE(res.find("n.") != std::string::npos);
        REQUIRE(res.find("问候；你好") != std::string::npos);
        REQUIRE(res.find("【例】 Say hello to him.") != std::string::npos);
        REQUIRE(res.find("向他问好。") != std::string::npos);

        // CDATA 格式 (PowerWord 2011/2012)
        std::string ksCdata = "<JS><CY><CX><YX><![CDATA[good]]></YX><YB><CB><![CDATA[ɡud]]></CB></YB><DX><![CDATA[adj.]]></DX><JX><![CDATA[良好的, 令人满意的]]></JX><DX><![CDATA[n.]]></DX><JX><![CDATA[善；好处]]></JX></CX></CY></JS>";
        std::string phonetic, def;
        DictFormatter::Format(ksCdata, "k", phonetic, def);
        REQUIRE(phonetic == "[ɡud]");
        REQUIRE(def.find("adj.") != std::string::npos);
        REQUIRE(def.find("良好的, 令人满意的") != std::string::npos);
        REQUIRE(def.find("n.") != std::string::npos);
        REQUIRE(def.find("善；好处") != std::string::npos);
    }

    SECTION("MediaWiki ('w')") {
        std::string wiki = "== Section 1 ==\n'''Bold Text''' and ''Italic Text''\n* List Item 1\n* List Item 2\n[[Target|Display Text]]";
        std::string res = DictFormatter::FormatMediaWiki(wiki);

        REQUIRE(res.find("【Section 1】") != std::string::npos);
        REQUIRE(res.find("Bold Text and Italic Text") != std::string::npos);
        REQUIRE(res.find("• List Item 1") != std::string::npos);
        REQUIRE(res.find("Display Text") != std::string::npos);
    }

    SECTION("Oxford / Dense Plaintext Formatting ('m')") {
        std::string raw = "adj [attrib 作定语] with no part left out; whole; complete 全部的; 整个的; 完全的: The entire village was destroyed. 整个村子被毁. * I've wasted an entire day on this. 我为此事浪费了一整天的时间. * We are in entire agreement with you. 我们完全同意你的意见.";
        std::string res = DictFormatter::FormatOxfordPlaintext(raw);

        REQUIRE(res.find("adj. [attrib 作定语]") != std::string::npos);
        REQUIRE(res.find("全部的; 整个的; 完全的:") != std::string::npos);
        REQUIRE(res.find("• The entire village was destroyed. 整个村子被毁.") != std::string::npos);
        REQUIRE(res.find("• I've wasted an entire day on this. 我为此事浪费了一整天的时间.") != std::string::npos);
        REQUIRE(res.find("• We are in entire agreement with you. 我们完全同意你的意见.") != std::string::npos);
    }

    SECTION("21st Century Dictionary Formatting ('m')") {
        std::string raw = "<<名词>>\n1 (U) (尤指基督教的) 上帝,造物主\nthe Almighty ~ 全能的神\nthe Lord ~ 主,上帝\n2 [g] (C)\na. (异教的) 神; (神话等的) 男神\nthe gods of Greece and Rome 希腊、罗马的诸神\n( ←→ evil)\n→ good thing";
        std::string res = DictFormatter::Format21Century(raw);

        REQUIRE(res.find("<<名词>>") != std::string::npos);
        REQUIRE(res.find("1. (U)") != std::string::npos);
        REQUIRE(res.find("• the Almighty ~ 全能的神") != std::string::npos);
        REQUIRE(res.find("• the Lord ~ 主,上帝") != std::string::npos);
        REQUIRE(res.find("2. [g] (C)") != std::string::npos);
        REQUIRE(res.find("a. (异教的) 神") != std::string::npos);
        REQUIRE(res.find("• the gods of Greece and Rome 希腊、罗马的诸神") != std::string::npos);
        REQUIRE(res.find("【反义】 ( ←→ evil)") != std::string::npos);
        REQUIRE(res.find("→ good thing") != std::string::npos);
    }
}

TEST_CASE("DictFormatter - Universal Dispatcher ('tm', mixed, and fallback)", "[core][dict_formatter]") {
    SECTION("sametypesequence = 'tm'") {
        std::string raw = std::string("[bə'nɑːnə]") + '\0' + "n. 香蕉, 芭蕉\\n1. [植]香蕉树";
        std::string phonetic, def;
        DictFormatter::Format(raw, "tm", phonetic, def);

        REQUIRE(phonetic == "[bə'nɑːnə]");
        REQUIRE(def.find("n. 香蕉, 芭蕉") != std::string::npos);
        REQUIRE(def.find("1. [植]香蕉树") != std::string::npos);
    }

    SECTION("Fallback phonetic extraction at definition start") {
        std::string raw = "[hə'ləʊ] int. 喂, 你好; 问候";
        std::string phonetic, def;
        DictFormatter::Format(raw, "m", phonetic, def);

        REQUIRE(phonetic == "[hə'ləʊ]");
        REQUIRE(def == "int. 喂, 你好; 问候");
    }

    SECTION("Dynamic mixed types with null terminator") {
        std::string raw;
        raw.push_back('t');
        raw += "[test]";
        raw.push_back('\0');
        raw.push_back('m');
        raw += "n. 测试, 检验";
        raw.push_back('\0');

        std::string phonetic, def;
        DictFormatter::Format(raw, "", phonetic, def);

        REQUIRE(phonetic == "[test]");
        REQUIRE(def == "n. 测试, 检验");
    }

    SECTION("Fallback phonetic extraction with star marker") {
        std::string raw = "*['likwid]\n n. 液体, 流体, 流音\na. 液体的, 透明的\n【化】 液体; 液态的\n【医】 液体";
        std::string phonetic, def;
        DictFormatter::Format(raw, "m", phonetic, def);

        REQUIRE(phonetic == "★ ['likwid]");
        REQUIRE(def.find("*['likwid]") == std::string::npos);
        REQUIRE(def.find("n. 液体") != std::string::npos);
        REQUIRE(def.find("a. 液体的") != std::string::npos);
        REQUIRE(def.find("【化】") != std::string::npos);
    }
}

TEST_CASE("DictFormatter - BuildRichTextSegments for UI", "[core][dict_formatter]") {
    DictSearchResult res1;
    res1.dictName = "朗道英汉字典";
    res1.phonetic = "[test]";
    res1.definition = "n. 测试, 试验\na. 液体的\nv. 检验\n【例】 Take a test.\n【化】 液体; 液态的\ne.g. Example text\n1. First item\n① Circled item";

    DictSearchResult res2;
    res2.dictName = "牛津双解";
    res2.definition = "[n.] 检验";

    std::vector<DictSearchResult> results = { res1, res2 };
    auto segments = DictFormatter::BuildRichTextSegments(results);

    REQUIRE(!segments.empty());
    
    // 检查是否包含 Header, Phonetic, PartOfSpeech, Tag, Example, NumberedItem, Divider 等样式
    bool hasHeader = false;
    bool hasPhonetic = false;
    bool hasPos = false;
    bool hasTag = false;
    bool hasExample = false;
    bool hasNumbered = false;
    bool hasDivider = false;

    for (const auto& seg : segments) {
        if (seg.style == DictTextStyle::DictHeader) hasHeader = true;
        if (seg.style == DictTextStyle::Phonetic) hasPhonetic = true;
        if (seg.style == DictTextStyle::PartOfSpeech) hasPos = true;
        if (seg.style == DictTextStyle::Tag) hasTag = true;
        if (seg.style == DictTextStyle::Example) hasExample = true;
        if (seg.style == DictTextStyle::NumberedItem) hasNumbered = true;
        if (seg.style == DictTextStyle::Divider) hasDivider = true;
    }

    REQUIRE(hasHeader);
    REQUIRE(hasPhonetic);
    REQUIRE(hasPos);
    REQUIRE(hasTag);
    REQUIRE(hasExample);
    REQUIRE(hasNumbered);
    REQUIRE(hasDivider);
}

