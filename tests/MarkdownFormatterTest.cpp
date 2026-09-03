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
