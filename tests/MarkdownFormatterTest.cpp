#include <catch2/catch.hpp>
#include "core/markdown/MarkdownFormatter.hpp"

using namespace LinguaAlpaca;

TEST_CASE("MarkdownFormatter - Headings Parsing", "[core][markdown]") {
    SECTION("Parses H1 through H6 headings with levels") {
        std::string md = "# Heading 1\n## Heading 2\n### Heading 3\n#### Heading 4\n##### Heading 5\n###### Heading 6\n";
        auto segments = MarkdownFormatter::Parse(md);

        bool foundH1 = false, foundH2 = false, foundH3 = false;
        bool foundH4 = false, foundH5 = false, foundH6 = false;

        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::Heading1 && seg.text.find("Heading 1") != std::string::npos) foundH1 = true;
            if (seg.style == MarkdownStyle::Heading2 && seg.text.find("Heading 2") != std::string::npos) foundH2 = true;
            if (seg.style == MarkdownStyle::Heading3 && seg.text.find("Heading 3") != std::string::npos) foundH3 = true;
            if (seg.style == MarkdownStyle::Heading4 && seg.text.find("Heading 4") != std::string::npos) foundH4 = true;
            if (seg.style == MarkdownStyle::Heading5 && seg.text.find("Heading 5") != std::string::npos) foundH5 = true;
            if (seg.style == MarkdownStyle::Heading6 && seg.text.find("Heading 6") != std::string::npos) foundH6 = true;
        }

        REQUIRE(foundH1);
        REQUIRE(foundH2);
        REQUIRE(foundH3);
        REQUIRE(foundH4);
        REQUIRE(foundH5);
        REQUIRE(foundH6);
    }
}

TEST_CASE("MarkdownFormatter - Inline Elements", "[core][markdown]") {
    SECTION("Parses bold, italic, bold-italic, inline code and strikethrough") {
        std::string md = "Normal **bold text** and *italic text* and ***bold italic*** with `inline_code()` and ~~deleted~~.";
        auto segments = MarkdownFormatter::Parse(md);

        bool hasBold = false;
        bool hasItalic = false;
        bool hasBoldItalic = false;
        bool hasInlineCode = false;
        bool hasStrike = false;

        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::Bold && seg.text == "bold text") hasBold = true;
            if (seg.style == MarkdownStyle::Italic && seg.text == "italic text") hasItalic = true;
            if (seg.style == MarkdownStyle::BoldItalic && seg.text == "bold italic") hasBoldItalic = true;
            if (seg.style == MarkdownStyle::InlineCode && seg.text.find("inline_code()") != std::string::npos) hasInlineCode = true;
            if (seg.style == MarkdownStyle::Strikethrough && seg.text == "deleted") hasStrike = true;
        }

        REQUIRE(hasBold);
        REQUIRE(hasItalic);
        REQUIRE(hasBoldItalic);
        REQUIRE(hasInlineCode);
        REQUIRE(hasStrike);
    }

    SECTION("Parses links [text](url)") {
        std::string md = "Click [here](https://example.com) for details.";
        auto segments = MarkdownFormatter::Parse(md);

        bool hasLink = false;
        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::LinkText && seg.text == "here") {
                hasLink = true;
            }
        }
        REQUIRE(hasLink);
    }

    SECTION("Does NOT parse intra-word underscores (snake_case variables) as italic") {
        std::string md = "use processor_kwargs={\"text_kwargs\": {\"padding\": True}} together with other options.";
        auto segments = MarkdownFormatter::Parse(md);

        for (const auto& seg : segments) {
            REQUIRE(seg.style != MarkdownStyle::Italic);
            REQUIRE(seg.style != MarkdownStyle::Bold);
        }
        std::string stripped = MarkdownFormatter::StripMarkdown(md);
        REQUIRE(stripped.find("processor_kwargs={\"text_kwargs\": {\"padding\": True}}") != std::string::npos);
    }

    SECTION("Parses user exact Chinese and English translation sentences without false italics") {
        std::string zhMd = "对于长度不同的批量数据，应同时使用“processor_kwargs={\"text_kwargs\": {\"padding\": True}}”以及其他处理选项。在调用处理程序之前，需排除那些为空的批量数据。在历史性的fc501源环境中，曾出现“audio=[]”导致IndexError的情况；该现象并非5.17.0版本的新测试结果。";
        auto zhSegments = MarkdownFormatter::Parse(zhMd);

        for (const auto& seg : zhSegments) {
            REQUIRE(seg.style != MarkdownStyle::Italic);
            REQUIRE(seg.style != MarkdownStyle::Bold);
            REQUIRE(seg.style != MarkdownStyle::LinkText);
        }
        std::string zhStripped = MarkdownFormatter::StripMarkdown(zhMd);
        REQUIRE(zhStripped.find("processor_kwargs={\"text_kwargs\": {\"padding\": True}}") != std::string::npos);

        std::string enMd = "language accepts Chinese, English, and Japanese as ISO codes, full English names, or the checkpoint Chinese names (中文, 英文, 日文). Keep batch inputs and decoded outputs in order; use processor_kwargs={\"text_kwargs\": {\"padding\": True}} together with the other processor options for different-length batches. Reject empty batches before calling the processor. An upstream IndexError for audio=[] was observed in the historical fc501 source environment; that observation is not a new 5.17.0 test result.";
        auto enSegments = MarkdownFormatter::Parse(enMd);

        for (const auto& seg : enSegments) {
            REQUIRE(seg.style != MarkdownStyle::Italic);
            REQUIRE(seg.style != MarkdownStyle::Bold);
            REQUIRE(seg.style != MarkdownStyle::LinkText);
        }
        std::string enStripped = MarkdownFormatter::StripMarkdown(enMd);
        REQUIRE(enStripped.find("processor_kwargs={\"text_kwargs\": {\"padding\": True}}") != std::string::npos);
    }

    SECTION("Parses multiple inline code blocks with plain text between them") {
        std::string md = "使用 `processor_kwargs={\"text_kwargs\": {\"padding\": True}}` 参数。在调用处理器之前需排除空批次。在历史性的fc501源环境中，当 `audio=[]` 时会出现上游的 `IndexError` 错误；该问题并非5.17.0版本的新测试结果。";
        auto segments = MarkdownFormatter::Parse(md);

        int codeCount = 0;
        int defaultCount = 0;
        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::InlineCode) {
                codeCount++;
            } else if (seg.style == MarkdownStyle::Default) {
                defaultCount++;
            }
        }
        // There should be exactly 3 inline code segments: processor_kwargs, audio=[], IndexError
        REQUIRE(codeCount == 3);
        REQUIRE(defaultCount >= 3);
    }

    SECTION("Parses legitimate underscore emphasis and math expressions") {
        std::string md1 = "This is _italic text_ and __bold text__ with underscores.";
        auto segs1 = MarkdownFormatter::Parse(md1);
        bool hasUnderscoreItalic = false;
        bool hasUnderscoreBold = false;
        for (const auto& seg : segs1) {
            if (seg.style == MarkdownStyle::Italic && seg.text == "italic text") hasUnderscoreItalic = true;
            if (seg.style == MarkdownStyle::Bold && seg.text == "bold text") hasUnderscoreBold = true;
        }
        REQUIRE(hasUnderscoreItalic);
        REQUIRE(hasUnderscoreBold);

        std::string md2 = "Check “_quoted italic_” and (_parens italic_).";
        auto segs2 = MarkdownFormatter::Parse(md2);
        bool hasQuotedItalic = false;
        bool hasParensItalic = false;
        for (const auto& seg : segs2) {
            if (seg.style == MarkdownStyle::Italic && seg.text == "quoted italic") hasQuotedItalic = true;
            if (seg.style == MarkdownStyle::Italic && seg.text == "parens italic") hasParensItalic = true;
        }
        REQUIRE(hasQuotedItalic);
        REQUIRE(hasParensItalic);

        std::string mdMath = "Calculate 5 * 4 * 3 and a + b * c + d * e.";
        auto mathSegs = MarkdownFormatter::Parse(mdMath);
        for (const auto& seg : mathSegs) {
            REQUIRE(seg.style != MarkdownStyle::Italic);
        }
    }
}

TEST_CASE("MarkdownFormatter - Code Blocks", "[core][markdown]") {
    SECTION("Parses multi-line code block") {
        std::string md = "```cpp\n#include <iostream>\nint main() {\n    return 0;\n}\n```\n";
        auto segments = MarkdownFormatter::Parse(md);

        bool hasCodeBlock = false;
        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::CodeBlock) {
                hasCodeBlock = true;
                REQUIRE(seg.text.find("#include <iostream>") != std::string::npos);
                REQUIRE(seg.text.find("return 0;") != std::string::npos);
            }
        }
        REQUIRE(hasCodeBlock);
    }
}

TEST_CASE("MarkdownFormatter - Lists and Quotes and Dividers", "[core][markdown]") {
    SECTION("Parses unordered and ordered lists") {
        std::string md = "- First item\n- Second item with **bold**\n1. Numbered one\n2. Numbered two\n";
        auto segments = MarkdownFormatter::Parse(md);

        bool hasBullet = false;
        bool hasNum = false;
        bool hasBoldInList = false;

        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::ListBullet) hasBullet = true;
            if (seg.style == MarkdownStyle::ListNumber) hasNum = true;
            if (seg.style == MarkdownStyle::Bold && seg.text == "bold") hasBoldInList = true;
        }

        REQUIRE(hasBullet);
        REQUIRE(hasNum);
        REQUIRE(hasBoldInList);
    }

    SECTION("Parses blockquote and horizontal rules") {
        std::string md = "> This is a quote\n\n---\n";
        auto segments = MarkdownFormatter::Parse(md);

        bool hasQuoteBar = false;
        bool hasQuoteText = false;
        bool hasDivider = false;

        for (const auto& seg : segments) {
            if (seg.style == MarkdownStyle::BlockquoteBar) hasQuoteBar = true;
            if (seg.style == MarkdownStyle::Blockquote && seg.text.find("This is a quote") != std::string::npos) hasQuoteText = true;
            if (seg.style == MarkdownStyle::Divider) hasDivider = true;
        }

        REQUIRE(hasQuoteBar);
        REQUIRE(hasQuoteText);
        REQUIRE(hasDivider);
    }
}

TEST_CASE("MarkdownFormatter - StripMarkdown", "[core][markdown]") {
    SECTION("Strips markdown tokens to plain text") {
        std::string md = "# Title\nThis is **bold** and `code`.\n- List item\n> Quote\n";
        std::string stripped = MarkdownFormatter::StripMarkdown(md);

        REQUIRE(stripped.find("Title") != std::string::npos);
        REQUIRE(stripped.find("bold") != std::string::npos);
        REQUIRE(stripped.find("code") != std::string::npos);
        REQUIRE(stripped.find("**") == std::string::npos);
        REQUIRE(stripped.find("`") == std::string::npos);
    }
}
