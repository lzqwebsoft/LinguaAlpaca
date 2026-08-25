#pragma execution_character_set("utf-8")
#include "DictFormatter.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace LinguaAlpaca {

namespace {

// ============================================================================
// 高性能零拷贝辅助函数与快速字符判定
// ============================================================================

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline bool StartsWith(std::string_view str, std::string_view prefix) noexcept {
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

inline std::string_view Trim(std::string_view sv) noexcept {
    size_t start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    size_t end = sv.find_last_not_of(" \t\r\n");
    return sv.substr(start, end - start + 1);
}

inline uint32_t ReadUint32BigEndian(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

// 快速十六进制解析 (无异常开销)
inline uint32_t ParseHex(std::string_view sv) noexcept {
    uint32_t val = 0;
    for (char c : sv) {
        if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
        else return 0;
    }
    return val;
}

// 快速十进制解析 (无异常开销)
inline uint32_t ParseDec(std::string_view sv) noexcept {
    uint32_t val = 0;
    for (char c : sv) {
        if (c >= '0' && c <= '9') val = val * 10 + (c - '0');
        else return 0;
    }
    return val;
}

// 预定义词性前缀白名单 (用于例句排斥与单行排版)
static constexpr std::string_view kPosTags[] = {
    "vt. & vi.", "vt.&vi.", "aux. v.", "link v.", "phr. v.", "phr v",
    "interj.", "interj", "sing.", "prep.", "prep", "conj.", "conj",
    "pron.", "pron", "abbr.", "abbr", "aux.", "aux", "art.", "art", "num.", "num",
    "adj.", "adj", "adv.", "adv", "vt.", "vt", "vi.", "vi", "pl.", "pl",
    "pp.", "pt.", "int.", "ad.", "ad", "n.", "v.", "a."
};

inline bool IsPosPrefix(std::string_view str) noexcept {
    for (const auto& tag : kPosTags) {
        if (StartsWith(str, tag)) {
            if (str.size() == tag.size()) return true;
            char next = str[tag.size()];
            if (next == ' ' || next == '\t' || next == '[' || next == '\n' ||
                static_cast<unsigned char>(next) >= 0x80) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// ============================================================================
// 1. Unicode 码点转换与 HTML 实体解码
// ============================================================================

std::string DictFormatter::Utf32ToUtf8(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return "";
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string DictFormatter::DecodeHtmlEntities(const std::string& text) {
    if (text.find('&') == std::string::npos) return text;

    struct NamedEntity {
        std::string_view name;
        std::string_view value;
    };

    static constexpr NamedEntity kNamedEntities[] = {
        {"Prime", "″"},  {"amp", "&"},     {"apos", "'"},     {"asymp", "≈"},
        {"bull", "•"},   {"cent", "¢"},    {"clubs", "♣"},    {"copy", "©"},
        {"deg", "°"},    {"diams", "♦"},   {"divide", "÷"},   {"emsp", "  "},
        {"ensp", " "},   {"euro", "€"},    {"ge", "≥"},       {"gt", ">"},
        {"hearts", "♥"}, {"hellip", "…"},  {"iexcl", "¡"},    {"infin", "∞"},
        {"iquest", "¿"}, {"laquo", "«"},   {"ldquo", "“"},    {"le", "≤"},
        {"lsquo", "‘"},  {"lsaquo", "‹"},  {"lt", "<"},       {"mdash", "—"},
        {"micro", "µ"},  {"middot", "·"},  {"nbsp", " "},     {"ndash", "–"},
        {"ne", "≠"},     {"para", "¶"},    {"permil", "‰"},   {"plusmn", "±"},
        {"pound", "£"},  {"prime", "′"},   {"quot", "\""},    {"radic", "√"},
        {"raquo", "»"},  {"rdqu0", "”"},   {"rdquo", "”"},    {"reg", "®"},
        {"rsaquo", "›"}, {"rsquo", "’"},   {"sect", "§"},     {"sim", "∼"},
        {"spades", "♠"}, {"thinsp", " "},  {"times", "×"},    {"trade", "™"},
        {"yen", "¥"}
    };

    std::string out;
    out.reserve(text.size());

    const size_t n = text.size();
    for (size_t i = 0; i < n; ++i) {
        if (text[i] == '&') {
            size_t semi = text.find(';', i + 1);
            if (semi != std::string::npos && (semi - i) <= 12) {
                std::string_view ent(&text[i + 1], semi - (i + 1));
                if (!ent.empty()) {
                    if (ent[0] == '#') {
                        uint32_t cp = 0;
                        if (ent.size() >= 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                            cp = ParseHex(ent.substr(2));
                        } else if (ent.size() >= 2) {
                            cp = ParseDec(ent.substr(1));
                        }
                        if (cp > 0) {
                            if (cp == 160) out.push_back(' ');
                            else out += Utf32ToUtf8(cp);
                            i = semi;
                            continue;
                        }
                    } else {
                        auto it = std::lower_bound(
                            std::begin(kNamedEntities), std::end(kNamedEntities), ent,
                            [](const NamedEntity& e, std::string_view key) { return e.name < key; });
                        if (it != std::end(kNamedEntities) && it->name == ent) {
                            out.append(it->value);
                            i = semi;
                            continue;
                        }
                    }
                }
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string DictFormatter::NormalizeNewlinesAndTrim(const std::string& text) {
    if (text.empty()) return "";

    std::string clean;
    clean.reserve(text.size());
    int consecutiveNewlines = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            c = '\n';
        }
        if (c == '\n') {
            consecutiveNewlines++;
            if (consecutiveNewlines <= 2) clean.push_back('\n');
        } else {
            consecutiveNewlines = 0;
            clean.push_back(c);
        }
    }

    std::string_view trimmed = Trim(clean);
    return std::string(trimmed);
}

// ============================================================================
// 2. 纯文本转义与结构化排版 ('m')
// ============================================================================

std::string DictFormatter::FormatPlaintext(const std::string& text) {
    if (text.empty()) return "";

    // 1. 高速转义字符预解包
    std::string unescaped;
    unescaped.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char next = text[i + 1];
            if (next == 'n' || next == '0') {
                unescaped.push_back('\n');
                i++;
                continue;
            } else if (next == 'r') {
                i++;
                continue;
            } else if (next == 't') {
                unescaped.push_back('\t');
                i++;
                continue;
            } else if (next == '\\') {
                unescaped.push_back('\\');
                i++;
                continue;
            }
        }
        unescaped.push_back(text[i]);
    }

    if (unescaped.empty()) return "";

    // 2. 统一流式解析
    std::string out;
    out.reserve(unescaped.size() * 2);

    auto ensureNewline = [&](int count = 1) {
        int trailing = 0;
        for (int k = static_cast<int>(out.size()) - 1; k >= 0 && out[k] == '\n'; --k) {
            trailing++;
        }
        for (int k = trailing; k < count; ++k) {
            out.push_back('\n');
        }
    };

    static constexpr std::pair<std::string_view, std::string_view> kPosHeaders[] = {
        { "adj [", "adj. [" }, { "adj ", "adj. " },
        { "adv [", "adv. [" }, { "adv ", "adv. " },
        { "vt & vi ", "vt. & vi. " }, { "vt & vi[", "vt. & vi. [" },
        { "vt ", "vt. " }, { "vt [", "vt. [" },
        { "vi ", "vi. " }, { "vi [", "vi. [" },
        { "prep ", "prep. " }, { "prep [", "prep. [" },
        { "conj ", "conj. " }, { "conj [", "conj. [" },
        { "interj ", "interj. " }, { "interj [", "interj. [" },
        { "n [", "n. [" }, { "n ", "n. " },
        { "v [", "v. [" }, { "v ", "v. " }
    };

    size_t i = 0;
    const size_t n = unescaped.size();

    auto matchPrefix = [&](std::string_view pattern) -> bool {
        return (i + pattern.size() <= n &&
                std::string_view(&unescaped[i], pattern.size()) == pattern);
    };

    while (i < n) {
        if (unescaped[i] == '\n') {
            ensureNewline(1);
            i++;
            continue;
        }

        // A. 语义标签转换
        if (matchPrefix("(idm 习语)")) {
            ensureNewline(2);
            out.append("【习语】\n");
            i += 10;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }
        if (matchPrefix("(idm)")) {
            ensureNewline(2);
            out.append("【习语】\n");
            i += 5;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }
        if (matchPrefix("(phr v 短语动词)")) {
            ensureNewline(2);
            out.append("【短语动词】\n");
            i += 16;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }
        if (matchPrefix("(phr v)")) {
            ensureNewline(2);
            out.append("【短语动词】\n");
            i += 7;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }
        if (matchPrefix("(用法说明)") || matchPrefix("NOTE ON USAGE 用法:")) {
            ensureNewline(2);
            out.append("【用法说明】\n");
            i += matchPrefix("(用法说明)") ? 12 : 18;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }

        // B. 反义词标签
        if (matchPrefix("( ←→ ") || matchPrefix("(←→")) {
            size_t closePos = unescaped.find(')', i);
            if (closePos != std::string::npos && closePos - i <= 30) {
                ensureNewline(1);
                out.append("  【反义】 " + unescaped.substr(i, closePos + 1 - i) + "\n");
                i = closePos + 1;
                while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t' || unescaped[i] == '\n')) i++;
                continue;
            }
        }

        // C. 例句星号分隔符
        if (unescaped[i] == '*' && (i == 0 || unescaped[i-1] == ' ' || unescaped[i-1] == '\n' || unescaped[i-1] == '.') &&
            (i + 1 < n && (unescaped[i+1] == ' ' || unescaped[i+1] == '\t' || unescaped[i+1] == '\n'))) {
            ensureNewline(1);
            out.append("  • ");
            i++;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }

        // D. 词性前缀规范化 (如 "adj [attrib" -> "adj. [")
        if (i == 0 || (out.size() > 0 && out.back() == '\n')) {
            bool matched = false;
            for (const auto& kv : kPosHeaders) {
                if (matchPrefix(kv.first)) {
                    out.append(kv.second);
                    i += kv.first.size();
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }

        // E. 数字大义项检测 (行首数字或独立 Oxford 流数字)
        bool isAtLineStart = (i == 0 || unescaped[i-1] == '\n');
        bool isOxfordNum = (i >= 2 && unescaped[i-1] == ' ' && (unescaped[i-2] == ' ' || unescaped[i-2] == '.' || static_cast<unsigned char>(unescaped[i-2]) >= 0x80));
        if ((isAtLineStart || isOxfordNum) && std::isdigit(static_cast<unsigned char>(unescaped[i]))) {
            size_t numEnd = i;
            while (numEnd < n && std::isdigit(static_cast<unsigned char>(unescaped[numEnd]))) {
                numEnd++;
            }
            if (isAtLineStart) {
                size_t afterSp = unescaped.find_first_not_of(" \t.", numEnd);
                ensureNewline(2);
                out.append(unescaped.substr(i, numEnd - i) + ". ");
                i = (afterSp != std::string::npos) ? afterSp : numEnd;
                continue;
            } else if (isOxfordNum && numEnd < n && (unescaped[numEnd] == ' ' || unescaped[numEnd] == '.' || unescaped[numEnd] == '\t' || unescaped[numEnd] == '\n')) {
                size_t afterSp = unescaped.find_first_not_of(" \t.", numEnd);
                if (afterSp != std::string::npos && (unescaped[afterSp] == '[' || unescaped[afterSp] == '(' ||
                    StartsWith(std::string_view(&unescaped[afterSp], n - afterSp), "attrib") ||
                    StartsWith(std::string_view(&unescaped[afterSp], n - afterSp), "pred"))) {
                    ensureNewline(2);
                    out.append(unescaped.substr(i, numEnd - i) + ". ");
                    i = afterSp;
                    continue;
                }
            }
        }

        // F. 小义项子编号: "(a) ", "(b) "
        if (unescaped[i] == '(' && i + 3 < n && std::islower(static_cast<unsigned char>(unescaped[i+1])) &&
            unescaped[i+2] == ')' && unescaped[i+3] == ' ') {
            ensureNewline(1);
            out.append("  (");
            out.push_back(unescaped[i+1]);
            out.append(") ");
            i += 4;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }

        // G. 字母子义项: "a. ", "b. " 在行首 (排除词性缩写)
        if ((i == 0 || unescaped[i-1] == '\n' || unescaped[i-1] == ' ') &&
            i + 2 < n && std::islower(static_cast<unsigned char>(unescaped[i])) && unescaped[i+1] == '.' && unescaped[i+2] == ' ') {
            std::string_view subView(&unescaped[i], std::min<size_t>(10, n - i));
            if (!IsPosPrefix(subView)) {
                ensureNewline(1);
                out.append("  ");
                out.push_back(unescaped[i]);
                out.append(". ");
                i += 3;
                while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
                continue;
            }
        }

        // H. 短语编号: "(1) ", "(2) "
        if (unescaped[i] == '(' && i + 2 < n && std::isdigit(static_cast<unsigned char>(unescaped[i+1]))) {
            size_t closeParen = unescaped.find(')', i + 1);
            if (closeParen != std::string::npos && closeParen - i <= 4) {
                size_t afterSp = unescaped.find_first_not_of(" \t", closeParen + 1);
                ensureNewline(1);
                out.append("  " + unescaped.substr(i, closeParen + 1 - i) + " ");
                i = (afterSp != std::string::npos) ? afterSp : closeParen + 1;
                continue;
            }
        }

        // I. 释义与首个例句间冒号分行
        if (unescaped[i] == ':' && i + 2 < n && unescaped[i+1] == ' ' && 
            (std::isupper(static_cast<unsigned char>(unescaped[i+2])) || unescaped[i+2] == '[' || unescaped[i+2] == '\"' || unescaped[i+2] == '\'')) {
            out.append(":\n  • ");
            i += 2;
            continue;
        }

        // J. 行首例句与短语检测
        if ((i == 0 || unescaped[i-1] == '\n') && (out.size() > 0 && out.back() == '\n')) {
            size_t lineEnd = unescaped.find('\n', i);
            std::string_view currentLine = (lineEnd != std::string::npos) ?
                std::string_view(&unescaped[i], lineEnd - i) : std::string_view(&unescaped[i], n - i);
            size_t sp = currentLine.find_first_not_of(" \t");
            if (sp != std::string_view::npos) {
                std::string_view trimmedLine = currentLine.substr(sp);
                if (trimmedLine.front() != '<' && trimmedLine.front() != '[' && trimmedLine.front() != '《' &&
                    !std::isdigit(static_cast<unsigned char>(trimmedLine.front())) &&
                    !IsPosPrefix(trimmedLine) &&
                    !(trimmedLine.front() == '(' && trimmedLine.size() >= 3 && (std::isdigit(static_cast<unsigned char>(trimmedLine[1])) || trimmedLine.find("←→") != std::string_view::npos))) {
                    if (trimmedLine.find('~') != std::string_view::npos) {
                        out.append("  • ");
                        i += sp;
                        continue;
                    } else if (trimmedLine.size() > 2 && (std::isalpha(static_cast<unsigned char>(trimmedLine[0])) || trimmedLine[0] == '(')) {
                        bool hasChinese = false;
                        for (char ch : trimmedLine) {
                            if (static_cast<unsigned char>(ch) >= 0x80) {
                                hasChinese = true;
                                break;
                            }
                        }
                        if (hasChinese) {
                            out.append("  • ");
                            i += sp;
                            continue;
                        }
                    }
                }
            }
        }

        // K. 交叉引用
        if (matchPrefix("→")) {
            if (i == 0 || unescaped[i-1] == '\n') {
                ensureNewline(1);
                out.append("  → ");
            } else {
                out.append(" → ");
            }
            i += 3;
            while (i < n && (unescaped[i] == ' ' || unescaped[i] == '\t')) i++;
            continue;
        }

        // L. 清理多余排版符号
        if (unescaped[i] == '`') {
            i++;
            continue;
        }
        if (unescaped[i] == ',' && i + 1 < n && std::isalpha(static_cast<unsigned char>(unescaped[i+1])) &&
            (i == 0 || unescaped[i-1] == ' ' || unescaped[i-1] == '\n')) {
            i++;
            continue;
        }

        out.push_back(unescaped[i]);
        i++;
    }

    return NormalizeNewlinesAndTrim(out);
}

// ============================================================================
// 3. 音标格式化 ('t', 'y')
// ============================================================================

std::string DictFormatter::FormatPhonetic(const std::string& text) {
    std::string clean = DecodeHtmlEntities(text);
    std::string out;
    out.reserve(clean.size());
    for (char c : clean) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 32 || uc > 127) {
            out.push_back(c);
        }
    }
    return std::string(Trim(out));
}

// ============================================================================
// 4. HTML / XDXF / Pango / Kingsoft / MediaWiki 标记格式化
// ============================================================================

std::string DictFormatter::FormatHtml(const std::string& html) {
    if (html.empty()) return "";

    std::string out;
    out.reserve(html.size());

    bool inTag = false;
    std::string currentTag;
    bool skipContent = false;
    std::string skipUntilTag;

    int listDepth = 0;
    int listIndex = 0;
    bool inOrderedList = false;

    for (size_t i = 0; i < html.size(); ++i) {
        if (html.compare(i, 4, "<!--") == 0) {
            size_t commentEnd = html.find("-->", i + 4);
            if (commentEnd != std::string::npos) {
                i = commentEnd + 2;
                continue;
            }
        }

        char c = html[i];
        if (c == '<') {
            inTag = true;
            currentTag.clear();
            continue;
        }

        if (c == '>') {
            inTag = false;
            std::string_view tagView = Trim(currentTag);
            bool isClose = (!tagView.empty() && tagView.front() == '/');
            if (isClose) tagView.remove_prefix(1);
            size_t sp = tagView.find_first_of(" \t\n\r/>");
            std::string_view tagName = (sp != std::string_view::npos) ? tagView.substr(0, sp) : tagView;

            if (EqualsIgnoreCase(tagName, "script") || EqualsIgnoreCase(tagName, "style")) {
                if (!isClose) {
                    skipContent = true;
                    skipUntilTag = tagName;
                } else if (skipContent && EqualsIgnoreCase(tagName, skipUntilTag)) {
                    skipContent = false;
                    skipUntilTag.clear();
                }
                continue;
            }

            if (skipContent) continue;

            if (EqualsIgnoreCase(tagName, "br") || EqualsIgnoreCase(tagName, "p") || EqualsIgnoreCase(tagName, "div") || EqualsIgnoreCase(tagName, "tr")) {
                out.push_back('\n');
            } else if (EqualsIgnoreCase(tagName, "hr")) {
                out.append("\n────────────────\n");
            } else if (tagName.size() == 2 && (tagName[0] == 'h' || tagName[0] == 'H') && tagName[1] >= '1' && tagName[1] <= '6') {
                out.append(!isClose ? "\n\n【" : "】\n");
            } else if (EqualsIgnoreCase(tagName, "ul")) {
                if (!isClose) { listDepth++; inOrderedList = false; }
                else if (listDepth > 0) listDepth--;
                out.push_back('\n');
            } else if (EqualsIgnoreCase(tagName, "ol")) {
                if (!isClose) { listDepth++; inOrderedList = true; listIndex = 1; }
                else if (listDepth > 0) listDepth--;
                out.push_back('\n');
            } else if (EqualsIgnoreCase(tagName, "li")) {
                if (!isClose) {
                    out.push_back('\n');
                    for (int d = 1; d < listDepth; ++d) out.append("  ");
                    out.append(inOrderedList ? (std::to_string(listIndex++) + ". ") : " • ");
                }
            } else if (EqualsIgnoreCase(tagName, "dt")) {
                if (!isClose) out.append("\n▸ ");
            } else if (EqualsIgnoreCase(tagName, "dd")) {
                if (!isClose) out.append("\n   ");
            } else if (EqualsIgnoreCase(tagName, "blockquote")) {
                if (!isClose) out.append("\n  │ ");
            } else if (EqualsIgnoreCase(tagName, "td") || EqualsIgnoreCase(tagName, "th")) {
                if (!isClose) out.append(" | ");
            }
            continue;
        }

        if (inTag) {
            currentTag.push_back(c);
        } else if (!skipContent) {
            out.push_back(c);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatXdxf(const std::string& xdxf) {
    if (xdxf.empty()) return "";

    std::string out;
    out.reserve(xdxf.size());

    bool inTag = false;
    std::string currentTag;

    for (char c : xdxf) {
        if (c == '<') {
            inTag = true;
            currentTag.clear();
            continue;
        }
        if (c == '>') {
            inTag = false;
            std::string_view tagView = Trim(currentTag);
            bool isClose = (!tagView.empty() && tagView.front() == '/');
            if (isClose) tagView.remove_prefix(1);
            size_t sp = tagView.find_first_of(" \t\n\r/>");
            std::string_view tagName = (sp != std::string_view::npos) ? tagView.substr(0, sp) : tagView;

            if (EqualsIgnoreCase(tagName, "tr")) out.append(!isClose ? " [" : "] ");
            else if (EqualsIgnoreCase(tagName, "pos")) out.append(!isClose ? "\n[" : "] ");
            else if (EqualsIgnoreCase(tagName, "dtrn") || EqualsIgnoreCase(tagName, "def")) {
                if (!isClose) out.append("\n • ");
            } else if (EqualsIgnoreCase(tagName, "ex")) {
                if (!isClose) out.append("\n  【例】 ");
            } else if (EqualsIgnoreCase(tagName, "co")) out.append(!isClose ? " (" : ") ");
            else if (EqualsIgnoreCase(tagName, "abbr")) out.append(!isClose ? " [" : "] ");
            else if (EqualsIgnoreCase(tagName, "rref")) {
                if (!isClose) out.append(" -> ");
            } else if (EqualsIgnoreCase(tagName, "blockquote")) {
                if (!isClose) out.append("\n  │ ");
            } else if (EqualsIgnoreCase(tagName, "br") || EqualsIgnoreCase(tagName, "p")) {
                out.push_back('\n');
            }
            continue;
        }

        if (inTag) {
            currentTag.push_back(c);
        } else {
            out.push_back(c);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatPango(const std::string& pango) {
    if (pango.empty()) return "";

    std::string out;
    out.reserve(pango.size());

    bool inTag = false;
    for (char c : pango) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out.push_back(c);
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatKingsoft(const std::string& xml) {
    if (xml.empty()) return "";

    std::string out;
    out.reserve(xml.size());
    std::string currentTag;

    auto appendBlock = [&](std::string_view text, std::string_view prefix = "") {
        std::string trimmed = NormalizeNewlinesAndTrim(std::string(text));
        if (!trimmed.empty()) {
            if (!out.empty() && out.back() != '\n') out.push_back('\n');
            if (!prefix.empty()) out.append(prefix);
            out.append(trimmed);
            out.push_back('\n');
        }
    };

    size_t i = 0;
    while (i < xml.size()) {
        // 1. 处理 CDATA
        if (i + 9 <= xml.size() && StartsWith(std::string_view(&xml[i], 9), "<![CDATA[")) {
            size_t cdataEnd = xml.find("]]>", i + 9);
            std::string_view cdata = (cdataEnd != std::string::npos) ?
                std::string_view(&xml[i + 9], cdataEnd - (i + 9)) : std::string_view(&xml[i + 9], xml.size() - (i + 9));
            i = (cdataEnd != std::string::npos) ? (cdataEnd + 3) : xml.size();

            if (EqualsIgnoreCase(currentTag, "dx") || EqualsIgnoreCase(currentTag, "pos") ||
                EqualsIgnoreCase(currentTag, "jx") || EqualsIgnoreCase(currentTag, "def") ||
                EqualsIgnoreCase(currentTag, "sy") || EqualsIgnoreCase(currentTag, "dtrn")) {
                appendBlock(cdata);
            } else if (EqualsIgnoreCase(currentTag, "orig") || EqualsIgnoreCase(currentTag, "yw")) {
                appendBlock(cdata, "  【例】 ");
            } else if (EqualsIgnoreCase(currentTag, "trans") || EqualsIgnoreCase(currentTag, "zw")) {
                appendBlock(cdata, "       ");
            } else if (EqualsIgnoreCase(currentTag, "phrase") || EqualsIgnoreCase(currentTag, "cp")) {
                appendBlock(cdata, "【短语】 ");
            } else if (!EqualsIgnoreCase(currentTag, "cb") && !EqualsIgnoreCase(currentTag, "pron") &&
                       !EqualsIgnoreCase(currentTag, "phonetic") && !EqualsIgnoreCase(currentTag, "yx") &&
                       !EqualsIgnoreCase(currentTag, "js") && !EqualsIgnoreCase(currentTag, "cy") &&
                       !EqualsIgnoreCase(currentTag, "cx") && !EqualsIgnoreCase(currentTag, "yb")) {
                appendBlock(cdata);
            }
            continue;
        }

        // 2. 处理标签
        if (xml[i] == '<') {
            size_t tagEnd = xml.find('>', i + 1);
            if (tagEnd != std::string::npos) {
                std::string_view tagView = Trim(std::string_view(&xml[i + 1], tagEnd - (i + 1)));
                i = tagEnd + 1;
                bool isClose = (!tagView.empty() && tagView.front() == '/');
                if (isClose) tagView.remove_prefix(1);
                size_t sp = tagView.find_first_of(" \t\n\r/>");
                std::string_view tagName = (sp != std::string_view::npos) ? tagView.substr(0, sp) : tagView;

                if (isClose) currentTag.clear();
                else currentTag = tagName;
                continue;
            }
        }

        // 3. 处理普通标签间文本
        size_t nextLt = xml.find('<', i);
        std::string_view plainText = (nextLt != std::string::npos) ?
            std::string_view(&xml[i], nextLt - i) : std::string_view(&xml[i], xml.size() - i);
        i = (nextLt != std::string::npos) ? nextLt : xml.size();

        if (EqualsIgnoreCase(currentTag, "dx") || EqualsIgnoreCase(currentTag, "pos") ||
            EqualsIgnoreCase(currentTag, "jx") || EqualsIgnoreCase(currentTag, "def") ||
            EqualsIgnoreCase(currentTag, "sy") || EqualsIgnoreCase(currentTag, "dtrn")) {
            appendBlock(plainText);
        } else if (EqualsIgnoreCase(currentTag, "orig") || EqualsIgnoreCase(currentTag, "yw")) {
            appendBlock(plainText, "  【例】 ");
        } else if (EqualsIgnoreCase(currentTag, "trans") || EqualsIgnoreCase(currentTag, "zw")) {
            appendBlock(plainText, "       ");
        } else if (EqualsIgnoreCase(currentTag, "phrase") || EqualsIgnoreCase(currentTag, "cp")) {
            appendBlock(plainText, "【短语】 ");
        } else if (!EqualsIgnoreCase(currentTag, "cb") && !EqualsIgnoreCase(currentTag, "pron") &&
                   !EqualsIgnoreCase(currentTag, "phonetic") && !EqualsIgnoreCase(currentTag, "yx") &&
                   !EqualsIgnoreCase(currentTag, "js") && !EqualsIgnoreCase(currentTag, "cy") &&
                   !EqualsIgnoreCase(currentTag, "cx") && !EqualsIgnoreCase(currentTag, "yb")) {
            appendBlock(plainText);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatMediaWiki(const std::string& wiki) {
    if (wiki.empty()) return "";

    std::string out;
    out.reserve(wiki.size());

    size_t pos = 0;
    const size_t len = wiki.size();

    while (pos < len) {
        size_t nextNl = wiki.find('\n', pos);
        std::string_view line = (nextNl != std::string::npos) ?
            std::string_view(&wiki[pos], nextNl - pos) : std::string_view(&wiki[pos], len - pos);
        pos = (nextNl != std::string::npos) ? (nextNl + 1) : len;

        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;

        // 1. 标题
        if (line.size() >= 4 && line.front() == '=' && line.back() == '=') {
            size_t eqStart = line.find_first_not_of('=');
            size_t eqEnd = line.find_last_not_of('=');
            if (eqStart != std::string_view::npos && eqEnd != std::string_view::npos && eqEnd >= eqStart) {
                std::string_view h = Trim(line.substr(eqStart, eqEnd - eqStart + 1));
                if (eqStart >= 3) out.append("\n▪ " + std::string(h) + "\n");
                else out.append("\n【" + std::string(h) + "】\n");
                continue;
            }
        }

        // 2. 列表
        if (line[0] == '*' || line[0] == '#') {
            std::string_view item = Trim(line.substr(1));
            out.append(line[0] == '*' ? (" • " + std::string(item) + "\n") : (" 1. " + std::string(item) + "\n"));
            continue;
        }

        // 3. 剥离加粗与内链
        std::string processed;
        processed.reserve(line.size());
        for (size_t k = 0; k < line.size(); ++k) {
            if (k + 3 <= line.size() && line.substr(k, 3) == "'''") { k += 2; continue; }
            if (k + 2 <= line.size() && line.substr(k, 2) == "''") { k += 1; continue; }
            if (k + 2 <= line.size() && line.substr(k, 2) == "[[") {
                size_t linkEnd = line.find("]]", k + 2);
                if (linkEnd != std::string_view::npos) {
                    std::string_view linkContent = line.substr(k + 2, linkEnd - (k + 2));
                    size_t pipe = linkContent.find('|');
                    processed.append(pipe != std::string_view::npos ? linkContent.substr(pipe + 1) : linkContent);
                    k = linkEnd + 1;
                    continue;
                }
            }
            if (k + 2 <= line.size() && line.substr(k, 2) == "{{") {
                size_t tmplEnd = line.find("}}", k + 2);
                if (tmplEnd != std::string_view::npos) {
                    k = tmplEnd + 1;
                    continue;
                }
            }
            processed.push_back(line[k]);
        }
        out.append(processed + "\n");
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

// ============================================================================
// 5. 格式化统一总调度入口与智能音标提取
// ============================================================================

namespace {

inline void DispatchSegment(char type, const std::string& seg, std::string& outPhonetic, std::string& defAcc) {
    if (type == 't' || type == 'y') {
        if (outPhonetic.empty()) outPhonetic = DictFormatter::FormatPhonetic(seg);
    } else if (type == 'm') {
        if (!defAcc.empty()) defAcc.push_back('\n');
        defAcc += DictFormatter::FormatPlaintext(seg);
    } else if (type == 'h' || type == 'H') {
        if (!defAcc.empty()) defAcc.push_back('\n');
        defAcc += DictFormatter::FormatHtml(seg);
    } else if (type == 'x' || type == 'X') {
        if (!defAcc.empty()) defAcc.push_back('\n');
        defAcc += DictFormatter::FormatXdxf(seg);
    } else if (type == 'g') {
        if (!defAcc.empty()) defAcc.push_back('\n');
        defAcc += DictFormatter::FormatPango(seg);
    } else if (type == 'k') {
        if (!defAcc.empty()) defAcc.push_back('\n');
        defAcc += DictFormatter::FormatKingsoft(seg);
    } else if (type == 'w') {
        if (!defAcc.empty()) defAcc.push_back('\n');
        defAcc += DictFormatter::FormatMediaWiki(seg);
    }
}

inline void ExtractEmbeddedPhonetic(std::string& outPhonetic, std::string& outDefinition) {
    if (!outPhonetic.empty() || outDefinition.empty()) return;

    // 1. 探测 21 世纪双向词典开头的 (音标)
    size_t firstNl = outDefinition.find('\n');
    std::string_view firstLine = (firstNl != std::string::npos) ?
        std::string_view(&outDefinition[0], firstNl) : std::string_view(outDefinition);
    std::string_view trimmedFirst = Trim(firstLine);

    if (trimmedFirst.size() >= 3 && trimmedFirst.front() == '(' && trimmedFirst.back() == ')') {
        outPhonetic = std::string(trimmedFirst);
        outDefinition = (firstNl != std::string::npos) ? outDefinition.substr(firstNl + 1) : "";
        return;
    }

    if (firstNl != std::string::npos) {
        size_t secondNl = outDefinition.find('\n', firstNl + 1);
        std::string_view secondLine = (secondNl != std::string::npos) ?
            std::string_view(&outDefinition[firstNl + 1], secondNl - (firstNl + 1)) :
            std::string_view(&outDefinition[firstNl + 1], outDefinition.size() - (firstNl + 1));
        std::string_view trimmedSecond = Trim(secondLine);
        if (trimmedSecond.size() >= 3 && trimmedSecond.front() == '(' && trimmedSecond.back() == ')') {
            outPhonetic = std::string(trimmedSecond);
            outDefinition = (secondNl != std::string::npos) ? outDefinition.substr(secondNl + 1) : "";
            return;
        }
    }

    // 2. 探测星级及 [*][音标] / /*/音标/
    size_t startOffset = 0;
    while (startOffset < outDefinition.size() &&
           (outDefinition[startOffset] == ' ' || outDefinition[startOffset] == '\t' ||
            outDefinition[startOffset] == '\r' || outDefinition[startOffset] == '\n')) {
        startOffset++;
    }

    bool hasStar = false;
    while (startOffset < outDefinition.size()) {
        char ch = outDefinition[startOffset];
        if (ch == '*' || ch == '#' || ch == '^') {
            hasStar = true;
            startOffset++;
        } else if (startOffset + 3 <= outDefinition.size() &&
                   (outDefinition.compare(startOffset, 3, "★") == 0 ||
                    outDefinition.compare(startOffset, 3, "▲") == 0 ||
                    outDefinition.compare(startOffset, 3, "☆") == 0)) {
            hasStar = true;
            startOffset += 3;
        } else if (ch == ' ' || ch == '\t') {
            startOffset++;
        } else {
            break;
        }
    }

    if (startOffset < outDefinition.size()) {
        char openChar = outDefinition[startOffset];
        if (openChar == '[' || openChar == '/') {
            char closeChar = (openChar == '[') ? ']' : '/';
            size_t endPos = outDefinition.find(closeChar, startOffset + 1);
            if (endPos != std::string::npos && endPos - startOffset <= 50) {
                std::string rawPh = outDefinition.substr(startOffset, endPos - startOffset + 1);
                outPhonetic = hasStar ? ("★ " + rawPh) : rawPh;
                size_t nextPos = outDefinition.find_first_not_of(" \t\n\r", endPos + 1);
                outDefinition = (nextPos != std::string::npos) ? outDefinition.substr(nextPos) : "";
            }
        }
    }
}

} // namespace

void DictFormatter::Format(const std::string& rawData,
                           const std::string& sameTypeSequence,
                           std::string& outPhonetic,
                           std::string& outDefinition) {
    outPhonetic.clear();
    outDefinition.clear();

    if (rawData.empty()) return;

    if (!sameTypeSequence.empty()) {
        if (sameTypeSequence == "m") {
            outDefinition = FormatPlaintext(rawData);
        } else if (sameTypeSequence == "k") {
            // 提取金山词霸音标
            size_t cbStart = rawData.find("<CB>");
            if (cbStart == std::string::npos) cbStart = rawData.find("<cb>");
            if (cbStart == std::string::npos) cbStart = rawData.find("<pron>");
            if (cbStart == std::string::npos) cbStart = rawData.find("<phonetic>");

            if (cbStart != std::string::npos) {
                size_t tagClose = rawData.find('>', cbStart);
                if (tagClose != std::string::npos) {
                    size_t cbEnd = rawData.find("</", tagClose);
                    if (cbEnd != std::string::npos) {
                        std::string_view rawPh(&rawData[tagClose + 1], cbEnd - (tagClose + 1));
                        size_t cdStart = rawPh.find("<![CDATA[");
                        if (cdStart == std::string_view::npos) cdStart = rawPh.find("<![cdata[");
                        if (cdStart != std::string_view::npos) {
                            size_t cdEnd = rawPh.find("]]>", cdStart + 9);
                            if (cdEnd != std::string_view::npos) {
                                rawPh = rawPh.substr(cdStart + 9, cdEnd - (cdStart + 9));
                            }
                        }
                        std::string ph = NormalizeNewlinesAndTrim(std::string(rawPh));
                        if (!ph.empty()) {
                            outPhonetic = (ph.front() != '[' && ph.front() != '/') ? ("[" + ph + "]") : ph;
                        }
                    }
                }
            }
            outDefinition = FormatKingsoft(rawData);
        } else {
            size_t pos = 0;
            std::string defAcc;
            for (char type : sameTypeSequence) {
                if (pos >= rawData.size()) break;
                if (std::islower(static_cast<unsigned char>(type))) {
                    size_t strEnd = rawData.find('\0', pos);
                    std::string seg = (strEnd != std::string::npos) ?
                        rawData.substr(pos, strEnd - pos) : rawData.substr(pos);
                    pos = (strEnd != std::string::npos) ? (strEnd + 1) : rawData.size();
                    DispatchSegment(type, seg, outPhonetic, defAcc);
                } else {
                    if (pos + 4 <= rawData.size()) {
                        uint32_t len = ReadUint32BigEndian(reinterpret_cast<const uint8_t*>(&rawData[pos]));
                        pos += 4;
                        if (pos + len <= rawData.size()) {
                            std::string seg = rawData.substr(pos, len);
                            pos += len;
                            DispatchSegment(type, seg, outPhonetic, defAcc);
                        } else break;
                    } else break;
                }
            }
            outDefinition = defAcc.empty() ? FormatPlaintext(rawData) : defAcc;
        }
    } else {
        // 动态单条目多类型混排解析
        size_t pos = 0;
        std::string defAcc;
        while (pos < rawData.size()) {
            char type = rawData[pos++];
            if (std::islower(static_cast<unsigned char>(type))) {
                size_t strEnd = rawData.find('\0', pos);
                std::string seg = (strEnd != std::string::npos) ?
                    rawData.substr(pos, strEnd - pos) : rawData.substr(pos);
                pos = (strEnd != std::string::npos) ? (strEnd + 1) : rawData.size();
                DispatchSegment(type, seg, outPhonetic, defAcc);
            } else {
                if (pos + 4 <= rawData.size()) {
                    uint32_t len = ReadUint32BigEndian(reinterpret_cast<const uint8_t*>(&rawData[pos]));
                    pos += 4;
                    if (pos + len <= rawData.size()) {
                        std::string seg = rawData.substr(pos, len);
                        pos += len;
                        DispatchSegment(type, seg, outPhonetic, defAcc);
                    } else break;
                } else break;
            }
        }
        outDefinition = defAcc.empty() ? FormatPlaintext(rawData) : defAcc;
    }

    // 智能兜底提取音标与星号
    ExtractEmbeddedPhonetic(outPhonetic, outDefinition);
}

// ============================================================================
// 6. UI 富文本分段与词性/序号样式构建
// ============================================================================

std::vector<DictTextSegment> DictFormatter::BuildRichTextSegments(const std::vector<DictSearchResult>& results) {
    std::vector<DictTextSegment> segments;
    segments.reserve(results.size() * 16);

    static constexpr std::string_view kPosPrefixes[] = {
        "[adj.]", "[adv.]", "[prep.]", "[conj.]", "[pron.]", "[num.]", "[art.]", "[int.]", "[abbr.]", "[aux.]",
        "[interj.]", "[sing.]", "[vt.]", "[vi.]", "[pl.]", "[pp.]", "[pt.]", "[v.]", "[n.]", "[a.]", "[ad.]",
        "[adj]", "[adv]", "[prep]", "[conj]", "[pron]", "[num]", "[art]", "[int]", "[abbr]", "[aux]",
        "[vt]", "[vi]", "[pl]", "[v]", "[n]", "[a]", "[ad]",
        "【及物】", "【不及物】", "【名】", "【动】", "【形】", "【副】", "【介】", "【连】", "【代】", "【数】", "【量】",
        "【冠】", "【感】", "【助】", "【缩】", "【口】", "【俗】", "【叹】",
        "interj. ", "sing. ", "prep. ", "conj. ", "pron. ", "abbr. ", "aux. ", "art. ", "num. ",
        "vt. & vi. ", "link v. ", "aux. v. ", "phr. v. ", "adj. ", "adv. ", "vt. ", "vi. ", "pl. ", "pp. ", "pt. ", "int. ",
        "a. ", "ad. ", "n. ", "v. ",
        "interj.", "sing.", "prep.", "conj.", "pron.", "abbr.", "aux.", "art.", "num.",
        "vt. & vi.", "link v.", "aux. v.", "phr. v.", "adj.", "adv.", "vt.", "vi.", "pl.", "pp.", "pt.", "int.",
        "a.", "ad.", "n.", "v."
    };

    for (size_t d = 0; d < results.size(); ++d) {
        const auto& r = results[d];

        // 预先检测是否包含多级字母子义项结构 (如 b.)
        bool hasSubSenseList = (r.definition.find("b. ") != std::string::npos ||
                                r.definition.find("\nb.") != std::string::npos ||
                                r.definition.find(" b.") != std::string::npos);

        if (d > 0) {
            segments.push_back({ DictTextStyle::Divider, "\n\n────────────────────────────────────────────────\n\n" });
        }

        // 1. 词典名称标头
        segments.push_back({ DictTextStyle::DictHeader, " 📖 " + r.dictName + " " });
        segments.push_back({ DictTextStyle::Default, "\n" });

        // 2. 音标
        if (!r.phonetic.empty()) {
            segments.push_back({ DictTextStyle::Phonetic, "   🗣 " + r.phonetic + "\n" });
        }

        // 3. 高性能逐行分段
        std::string_view defView(r.definition);
        size_t linePos = 0;

        while (linePos < defView.size()) {
            size_t nextNl = defView.find('\n', linePos);
            std::string_view rawLine = (nextNl != std::string_view::npos) ?
                defView.substr(linePos, nextNl - linePos) : defView.substr(linePos);
            linePos = (nextNl != std::string_view::npos) ? (nextNl + 1) : defView.size();

            std::string_view line = Trim(rawLine);
            if (line.empty()) {
                segments.push_back({ DictTextStyle::Default, "\n" });
                continue;
            }

            // A. 检查词性胶囊徽章
            bool matchedPos = false;
            for (const auto& posTag : kPosPrefixes) {
                if (hasSubSenseList && (posTag == "a. " || posTag == "a." || posTag == "a")) {
                    continue;
                }
                if (StartsWith(line, posTag)) {
                    segments.push_back({ DictTextStyle::PartOfSpeech, " " + std::string(posTag) + " " });
                    std::string_view rest = Trim(line.substr(posTag.size()));

                    if (!rest.empty() && rest.front() == '[') {
                        size_t closeBracket = rest.find(']');
                        if (closeBracket != std::string_view::npos && closeBracket <= 30) {
                            std::string subTag(rest.substr(0, closeBracket + 1));
                            segments.push_back({ DictTextStyle::Tag, " " + subTag + " " });
                            std::string_view afterTag = Trim(rest.substr(closeBracket + 1));
                            segments.push_back({ DictTextStyle::Default, std::string(afterTag) + "\n" });
                            matchedPos = true;
                            break;
                        }
                    }

                    segments.push_back({ DictTextStyle::Default, std::string(rest) + "\n" });
                    matchedPos = true;
                    break;
                }
            }
            if (matchedPos) continue;

            // B. 词性与字源标签: <<形容词>> / 《源自...》
            if (StartsWith(line, "<<")) {
                size_t closePos = line.find(">>", 2);
                if (closePos != std::string_view::npos && closePos <= 30) {
                    segments.push_back({ DictTextStyle::PartOfSpeech, " " + std::string(line.substr(0, closePos + 2)) + " " });
                    std::string_view rest = Trim(line.substr(closePos + 2));
                    segments.push_back({ DictTextStyle::Default, std::string(rest) + "\n" });
                    continue;
                }
            }
            if (StartsWith(line, "《")) {
                size_t closePos = line.find("》", 3);
                if (closePos != std::string_view::npos && closePos <= 80) {
                    segments.push_back({ DictTextStyle::Tag, " " + std::string(line.substr(0, closePos + 3)) + " " });
                    std::string_view rest = Trim(line.substr(closePos + 3));
                    segments.push_back({ DictTextStyle::Default, std::string(rest) + "\n" });
                    continue;
                }
            }

            // C. 语法/分类/专业标签: [attrib 作定语] / 【例】 / 【化】
            if (line.front() == '[') {
                size_t closeBracket = line.find(']');
                if (closeBracket != std::string_view::npos && closeBracket <= 30) {
                    segments.push_back({ DictTextStyle::Tag, " " + std::string(line.substr(0, closeBracket + 1)) + " " });
                    std::string_view rest = Trim(line.substr(closeBracket + 1));
                    segments.push_back({ DictTextStyle::Default, std::string(rest) + "\n" });
                    continue;
                }
            }
            if (StartsWith(line, "【")) {
                size_t closePos = line.find("】", 3);
                if (closePos != std::string_view::npos && closePos <= 30) {
                    std::string_view tag = line.substr(0, closePos + 3);
                    std::string_view rest = Trim(line.substr(closePos + 3));
                    segments.push_back({ DictTextStyle::Tag, " " + std::string(tag) + " " });
                    if (tag.find("例") != std::string_view::npos || tag.find("短语") != std::string_view::npos ||
                        tag.find("句") != std::string_view::npos) {
                        segments.push_back({ DictTextStyle::Example, std::string(rest) + "\n" });
                    } else {
                        segments.push_back({ DictTextStyle::Default, std::string(rest) + "\n" });
                    }
                    continue;
                }
            }

            // D. 例句或圆点列表
            if (StartsWith(line, "•") || StartsWith(line, "e.g.") || StartsWith(line, "eg.") ||
                StartsWith(line, "Ex.") || StartsWith(line, "ex.")) {
                segments.push_back({ DictTextStyle::Example, std::string(line) + "\n" });
                continue;
            }

            // E. 序号条目 (如 1. / 2. / a. / (1) / ① / [1])
            bool matchedNum = false;
            char firstCh = line.front();
            char secondCh = (line.size() > 1) ? line[1] : '\0';

            bool isCandidate = (std::isdigit(static_cast<unsigned char>(firstCh)) ||
                                (std::islower(static_cast<unsigned char>(firstCh)) && secondCh == '.') ||
                                firstCh == '(' || firstCh == '[' ||
                                (static_cast<unsigned char>(firstCh) == 0xE2 && static_cast<unsigned char>(secondCh) == 0x91));

            if (isCandidate) {
                size_t splitIdx = std::string_view::npos;
                size_t dotPos = line.find('.');
                size_t parenPos = line.find(')');
                size_t bracketPos = line.find(']');

                if (dotPos != std::string_view::npos && dotPos <= 4) {
                    splitIdx = dotPos + 1;
                } else if (parenPos != std::string_view::npos && parenPos <= 5) {
                    splitIdx = parenPos + 1;
                } else if (bracketPos != std::string_view::npos && bracketPos <= 5) {
                    splitIdx = bracketPos + 1;
                } else if (std::isdigit(static_cast<unsigned char>(firstCh))) {
                    size_t digitEnd = 0;
                    while (digitEnd < line.size() && std::isdigit(static_cast<unsigned char>(line[digitEnd]))) {
                        digitEnd++;
                    }
                    splitIdx = digitEnd;
                } else if (static_cast<unsigned char>(firstCh) == 0xE2 && static_cast<unsigned char>(secondCh) == 0x91) {
                    splitIdx = 3;
                }

                if (splitIdx != std::string_view::npos && splitIdx <= line.size()) {
                    std::string numPart(line.substr(0, splitIdx));
                    std::string_view textPart = line.substr(splitIdx);

                    if (!numPart.empty() && std::isdigit(static_cast<unsigned char>(numPart.front())) &&
                        numPart.back() != '.' && numPart.back() != ')' && numPart.back() != ']') {
                        numPart += ".";
                    }

                    segments.push_back({ DictTextStyle::NumberedItem, " " + numPart + " " });

                    // 进一步检查行内字母子义项 (如 "1. a. 善...")
                    std::string_view trimmedText = Trim(textPart);
                    if (trimmedText.size() >= 2 && std::islower(static_cast<unsigned char>(trimmedText[0])) &&
                        trimmedText[1] == '.') {
                        std::string subLetterPart(trimmedText.substr(0, 2));
                        std::string_view restPart = Trim(trimmedText.substr(2));
                        segments.push_back({ DictTextStyle::NumberedItem, " " + subLetterPart + " " });
                        segments.push_back({ DictTextStyle::Default, std::string(restPart) + "\n" });
                    } else {
                        segments.push_back({ DictTextStyle::Default, std::string(textPart) + "\n" });
                    }
                    matchedNum = true;
                }
            }
            if (matchedNum) continue;

            // F. 普通释义文本
            segments.push_back({ DictTextStyle::Default, std::string(line) + "\n" });
        }
    }

    return segments;
}

} // namespace LinguaAlpaca
