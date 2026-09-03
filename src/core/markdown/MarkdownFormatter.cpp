#pragma execution_character_set("utf-8")
#include "MarkdownFormatter.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace LinguaAlpaca {

std::string_view MarkdownFormatter::Trim(std::string_view s) {
    return TrimRight(TrimLeft(s));
}

std::string_view MarkdownFormatter::TrimLeft(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        ++start;
    }
    return s.substr(start);
}

std::string_view MarkdownFormatter::TrimRight(std::string_view s) {
    if (s.empty()) return s;
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(0, end);
}

bool MarkdownFormatter::IsHorizontalRule(std::string_view line) {
    std::string_view trimmed = Trim(line);
    if (trimmed.size() < 3) return false;

    char firstChar = trimmed[0];
    if (firstChar != '-' && firstChar != '*' && firstChar != '_') return false;

    size_t count = 0;
    for (char c : trimmed) {
        if (c == firstChar) {
            ++count;
        } else if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return count >= 3;
}

bool MarkdownFormatter::IsHeading(std::string_view line, int& outLevel, std::string_view& outContent) {
    std::string_view trimmed = TrimLeft(line);
    if (trimmed.empty() || trimmed[0] != '#') return false;

    size_t i = 0;
    while (i < trimmed.size() && trimmed[i] == '#' && i < 6) {
        ++i;
    }

    if (i > 0 && i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t')) {
        outLevel = static_cast<int>(i);
        outContent = Trim(trimmed.substr(i));
        return true;
    }
    return false;
}

bool MarkdownFormatter::IsOrderedList(std::string_view line, std::string_view& outNum, std::string_view& outContent, size_t& outIndent) {
    size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
        ++indent;
    }

    size_t i = indent;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        ++i;
    }

    if (i > indent && i < line.size() && (line[i] == '.' || line[i] == ')') && (i + 1 < line.size()) && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
        outIndent = indent;
        outNum = line.substr(indent, (i - indent + 1));
        outContent = TrimLeft(line.substr(i + 2));
        return true;
    }
    return false;
}

bool MarkdownFormatter::IsUnorderedList(std::string_view line, std::string_view& outContent, size_t& outIndent) {
    size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
        ++indent;
    }

    if (indent + 1 < line.size()) {
        char c = line[indent];
        char next = line[indent + 1];
        if ((c == '-' || c == '*' || c == '+') && (next == ' ' || next == '\t')) {
            if (!IsHorizontalRule(line)) {
                outIndent = indent;
                outContent = TrimLeft(line.substr(indent + 2));
                return true;
            }
        }
    }
    return false;
}

void MarkdownFormatter::ParseInlineElements(std::string_view line,
                                           std::vector<MarkdownSegment>& outSegments,
                                           MarkdownStyle baseStyle) {
    size_t i = 0;
    size_t plainStart = 0;

    auto flushPlain = [&](size_t end) {
        if (end > plainStart) {
            outSegments.push_back({ baseStyle, std::string(line.substr(plainStart, end - plainStart)) });
        }
        plainStart = end;
    };

    while (i < line.size()) {
        // 1. 行内代码 `code`
        if (line[i] == '`') {
            size_t closePos = line.find('`', i + 1);
            if (closePos != std::string_view::npos) {
                flushPlain(i);
                std::string codeText = std::string(line.substr(i + 1, closePos - (i + 1)));
                outSegments.push_back({ MarkdownStyle::InlineCode, " " + codeText + " " });
                i = closePos + 1;
                plainStart = i;
                continue;
            }
        }

        // 2. 粗斜体 ***text*** 或 ___text___
        if (i + 2 < line.size() &&
            ((line[i] == '*' && line[i + 1] == '*' && line[i + 2] == '*') ||
             (line[i] == '_' && line[i + 1] == '_' && line[i + 2] == '_'))) {
            std::string_view delim = line.substr(i, 3);
            size_t closePos = line.find(delim, i + 3);
            if (closePos != std::string_view::npos) {
                flushPlain(i);
                std::string content = std::string(line.substr(i + 3, closePos - (i + 3)));
                outSegments.push_back({ MarkdownStyle::BoldItalic, content });
                i = closePos + 3;
                plainStart = i;
                continue;
            }
        }

        // 3. 加粗 **text** 或 __text__
        if (i + 1 < line.size() &&
            ((line[i] == '*' && line[i + 1] == '*') ||
             (line[i] == '_' && line[i + 1] == '_'))) {
            std::string_view delim = line.substr(i, 2);
            size_t closePos = line.find(delim, i + 2);
            if (closePos != std::string_view::npos) {
                flushPlain(i);
                std::string content = std::string(line.substr(i + 2, closePos - (i + 2)));
                outSegments.push_back({ MarkdownStyle::Bold, content });
                i = closePos + 2;
                plainStart = i;
                continue;
            }
        }

        // 4. 删除线 ~~text~~
        if (i + 1 < line.size() && line[i] == '~' && line[i + 1] == '~') {
            size_t closePos = line.find("~~", i + 2);
            if (closePos != std::string_view::npos) {
                flushPlain(i);
                std::string content = std::string(line.substr(i + 2, closePos - (i + 2)));
                outSegments.push_back({ MarkdownStyle::Strikethrough, content });
                i = closePos + 2;
                plainStart = i;
                continue;
            }
        }

        // 5. 斜体 *text* 或 _text_
        if ((line[i] == '*' || line[i] == '_')) {
            char delim = line[i];
            size_t closePos = line.find(delim, i + 1);
            // 确保不是空斜体且闭合有效
            if (closePos != std::string_view::npos && closePos > i + 1) {
                flushPlain(i);
                std::string content = std::string(line.substr(i + 1, closePos - (i + 1)));
                outSegments.push_back({ MarkdownStyle::Italic, content });
                i = closePos + 1;
                plainStart = i;
                continue;
            }
        }

        // 6. 链接 [text](url)
        if (line[i] == '[') {
            size_t closeBracket = line.find(']', i + 1);
            if (closeBracket != std::string_view::npos && closeBracket + 1 < line.size() && line[closeBracket + 1] == '(') {
                size_t closeParen = line.find(')', closeBracket + 2);
                if (closeParen != std::string_view::npos) {
                    flushPlain(i);
                    std::string linkText = std::string(line.substr(i + 1, closeBracket - (i + 1)));
                    outSegments.push_back({ MarkdownStyle::LinkText, linkText });
                    i = closeParen + 1;
                    plainStart = i;
                    continue;
                }
            }
        }

        ++i;
    }

    flushPlain(line.size());
}

std::vector<MarkdownSegment> MarkdownFormatter::Parse(const std::string& markdown) {
    std::vector<MarkdownSegment> segments;
    if (markdown.empty()) return segments;

    std::string_view view(markdown);
    size_t pos = 0;
    bool inCodeBlock = false;
    std::string codeBlockContent;

    while (pos < view.size()) {
        size_t nextNl = view.find('\n', pos);
        std::string_view rawLine = (nextNl != std::string_view::npos) ?
            view.substr(pos, nextNl - pos) : view.substr(pos);
        pos = (nextNl != std::string_view::npos) ? (nextNl + 1) : view.size();

        // 移除行末 \r
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine = rawLine.substr(0, rawLine.size() - 1);
        }

        std::string_view trimmedLeft = TrimLeft(rawLine);

        // 1. 代码块检测 ```
        if (trimmedLeft.size() >= 3 && trimmedLeft.substr(0, 3) == "```") {
            if (inCodeBlock) {
                // 结束代码块
                inCodeBlock = false;
                if (!codeBlockContent.empty() && codeBlockContent.back() == '\n') {
                    codeBlockContent.pop_back();
                }
                segments.push_back({ MarkdownStyle::CodeBlock, codeBlockContent + "\n" });
                codeBlockContent.clear();
            } else {
                // 进入代码块
                inCodeBlock = true;
                codeBlockContent.clear();
            }
            continue;
        }

        if (inCodeBlock) {
            codeBlockContent += std::string(rawLine) + "\n";
            continue;
        }

        // 2. 空行
        if (trimmedLeft.empty()) {
            segments.push_back({ MarkdownStyle::Default, "\n" });
            continue;
        }

        // 3. 水平分割线
        if (IsHorizontalRule(rawLine)) {
            segments.push_back({ MarkdownStyle::Divider, "────────────────────────────────────────\n" });
            continue;
        }

        // 4. 标题 (# ~ ######)
        int headLevel = 0;
        std::string_view headContent;
        if (IsHeading(rawLine, headLevel, headContent)) {
            MarkdownStyle hStyle = MarkdownStyle::Heading1;
            switch (headLevel) {
                case 1: hStyle = MarkdownStyle::Heading1; break;
                case 2: hStyle = MarkdownStyle::Heading2; break;
                case 3: hStyle = MarkdownStyle::Heading3; break;
                case 4: hStyle = MarkdownStyle::Heading4; break;
                case 5: hStyle = MarkdownStyle::Heading5; break;
                case 6: hStyle = MarkdownStyle::Heading6; break;
                default: hStyle = MarkdownStyle::Heading1; break;
            }
            ParseInlineElements(headContent, segments, hStyle);
            segments.push_back({ hStyle, "\n" });
            continue;
        }

        // 5. 引用块 (> )
        if (trimmedLeft.size() >= 1 && trimmedLeft[0] == '>') {
            std::string_view quoteContent = Trim(trimmedLeft.substr(1));
            segments.push_back({ MarkdownStyle::BlockquoteBar, "▍ " });
            ParseInlineElements(quoteContent, segments, MarkdownStyle::Blockquote);
            segments.push_back({ MarkdownStyle::Blockquote, "\n" });
            continue;
        }

        // 6. 无序列表 (- / * / +)
        std::string_view listContent;
        size_t listIndent = 0;
        if (IsUnorderedList(rawLine, listContent, listIndent)) {
            std::string indentStr(listIndent > 0 ? std::string(listIndent, ' ') : "");
            segments.push_back({ MarkdownStyle::Default, indentStr });
            segments.push_back({ MarkdownStyle::ListBullet, "• " });
            ParseInlineElements(listContent, segments, MarkdownStyle::Default);
            segments.push_back({ MarkdownStyle::Default, "\n" });
            continue;
        }

        // 7. 有序列表 (1. 2. 等)
        std::string_view listNum;
        if (IsOrderedList(rawLine, listNum, listContent, listIndent)) {
            std::string indentStr(listIndent > 0 ? std::string(listIndent, ' ') : "");
            segments.push_back({ MarkdownStyle::Default, indentStr });
            segments.push_back({ MarkdownStyle::ListNumber, std::string(listNum) + " " });
            ParseInlineElements(listContent, segments, MarkdownStyle::Default);
            segments.push_back({ MarkdownStyle::Default, "\n" });
            continue;
        }

        // 8. 普通段落与行内 Markdown
        ParseInlineElements(rawLine, segments, MarkdownStyle::Default);
        segments.push_back({ MarkdownStyle::Default, "\n" });
    }

    // 处理未闭合的代码块
    if (inCodeBlock && !codeBlockContent.empty()) {
        if (codeBlockContent.back() == '\n') {
            codeBlockContent.pop_back();
        }
        segments.push_back({ MarkdownStyle::CodeBlock, codeBlockContent + "\n" });
    }

    return segments;
}

std::string MarkdownFormatter::StripMarkdown(const std::string& markdown) {
    auto segments = Parse(markdown);
    std::string result;
    result.reserve(markdown.size());
    for (const auto& seg : segments) {
        if (seg.style == MarkdownStyle::BlockquoteBar) {
            continue;
        }
        result += seg.text;
    }
    return result;
}

} // namespace LinguaAlpaca
