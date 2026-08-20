#pragma execution_character_set("utf-8")
#include "DictEngine.hpp"
#include "Logger.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/mstream.h>
#include <wx/textfile.h>
#include <wx/log.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace LinguaAlpaca {

namespace {

// 通用 inflate 解压函数，支持 raw deflate (DictZip 分块 / -15), gzip (31), zlib (15)
bool InflateData(const uint8_t* inData, size_t inSize, size_t expectedOutSize, std::vector<uint8_t>& outData) {
    outData.clear();
    if (!inData || inSize == 0) return false;

    size_t initCap = (expectedOutSize > 0) ? (expectedOutSize + 1024) : 65536;
    outData.resize(initCap);

    int windowBitsList[] = { -15, -MAX_WBITS, 31, MAX_WBITS + 16, 15, MAX_WBITS };

    for (int wbits : windowBitsList) {
        z_stream strm;
        std::memset(&strm, 0, sizeof(strm));
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = static_cast<uInt>(inSize);
        strm.next_in = const_cast<Bytef*>(inData);
        strm.avail_out = static_cast<uInt>(outData.size());
        strm.next_out = reinterpret_cast<Bytef*>(outData.data());

        if (inflateInit2(&strm, wbits) != Z_OK) {
            continue;
        }

        int ret = Z_OK;
        while (strm.avail_in > 0 && ret != Z_STREAM_END) {
            ret = inflate(&strm, Z_SYNC_FLUSH);
            if (ret == Z_BUF_ERROR && strm.avail_out == 0) {
                size_t currentOut = strm.total_out;
                outData.resize(outData.size() * 2);
                strm.next_out = reinterpret_cast<Bytef*>(outData.data() + currentOut);
                strm.avail_out = static_cast<uInt>(outData.size() - currentOut);
            } else if (ret != Z_OK && ret != Z_STREAM_END) {
                break;
            }
        }

        inflateEnd(&strm);

        if ((ret == Z_OK || ret == Z_STREAM_END) && strm.total_out > 0) {
            outData.resize(strm.total_out);
            return true;
        }
    }

    outData.clear();
    return false;
}

inline std::string ToLowerUtf8(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (char c : str) {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return res;
}

inline uint16_t ReadUint16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t ReadUint32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

inline uint64_t ReadUint64BE(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | p[i];
    }
    return val;
}

// 清理去除 HTML / XDXF 标签并保留整洁的排版
std::string CleanTagsAndFormat(const std::string& text) {
    if (text.empty()) return "";

    // 1. 转义字符还原 (\n, \r, \t, \\)
    std::string unescaped;
    unescaped.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char next = text[i + 1];
            if (next == 'n') {
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

    // 2. 剥离 HTML / XDXF 标签并保留换行
    std::string out;
    out.reserve(unescaped.size());
    bool inTag = false;
    std::string currentTag;

    for (size_t i = 0; i < unescaped.size(); ++i) {
        char c = unescaped[i];
        if (c == '<') {
            inTag = true;
            currentTag.clear();
            continue;
        }
        if (c == '>') {
            inTag = false;
            std::string lowerTag = ToLowerUtf8(currentTag);
            size_t sp = lowerTag.find_first_of(" \t\n\r/");
            std::string tagName = (sp != std::string::npos) ? lowerTag.substr(0, sp) : lowerTag;
            if (tagName == "br" || tagName == "p" || tagName == "div" ||
                tagName == "tr" || tagName == "li" || tagName == "dtrn" ||
                tagName == "dt" || tagName == "dd" || tagName == "blockquote") {
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

    // 3. 替换常见 HTML 实体
    std::string decoded;
    decoded.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '&') {
            if (out.compare(i, 6, "&nbsp;") == 0) {
                decoded.push_back(' ');
                i += 5;
                continue;
            } else if (out.compare(i, 5, "&amp;") == 0) {
                decoded.push_back('&');
                i += 4;
                continue;
            } else if (out.compare(i, 4, "&lt;") == 0) {
                decoded.push_back('<');
                i += 3;
                continue;
            } else if (out.compare(i, 4, "&gt;") == 0) {
                decoded.push_back('>');
                i += 3;
                continue;
            } else if (out.compare(i, 6, "&quot;") == 0) {
                decoded.push_back('"');
                i += 5;
                continue;
            } else if (out.compare(i, 6, "&#39;") == 0 || out.compare(i, 6, "&apos;") == 0) {
                decoded.push_back('\'');
                i += 5;
                continue;
            }
        }
        decoded.push_back(out[i]);
    }

    // 4. 清理多余空行与格式规范化
    std::string clean;
    clean.reserve(decoded.size());
    int consecutiveNewlines = 0;
    for (char c : decoded) {
        if (c == '\r') continue;
        if (c == '\n') {
            consecutiveNewlines++;
            if (consecutiveNewlines <= 2) {
                clean.push_back('\n');
            }
        } else {
            consecutiveNewlines = 0;
            clean.push_back(c);
        }
    }

    // 5. 移除前导和后继空白
    size_t start = clean.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = clean.find_last_not_of(" \t\n\r");
    return clean.substr(start, end - start + 1);
}

} // namespace

StarDictBook::~StarDictBook() = default;

bool StarDictBook::Load(const std::string& ifoPath) {
    m_loaded = false;
    m_entries.clear();
    m_chunkSizes.clear();
    m_chunkOffsets.clear();
    m_chunkCache.clear();

    if (!ParseIfo(ifoPath)) {
        LOG_WARN("StarDict", "Failed to parse ifo: " + ifoPath);
        return false;
    }

    // 寻找匹配的 .idx 文件
    wxFileName ifoFn(wxString::FromUTF8(ifoPath));
    wxString basePath = ifoFn.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + ifoFn.GetName();

    wxString idxPath = basePath + ".idx";
    if (!wxFileExists(idxPath)) {
        idxPath = basePath + ".idx.dz";
        if (!wxFileExists(idxPath)) {
            idxPath = basePath + ".idx.gz";
        }
    }

    if (!wxFileExists(idxPath)) {
        LOG_WARN("StarDict", "Index file not found for: " + ifoPath);
        return false;
    }
    m_info.idxPath = idxPath.ToUTF8().data();

    // 寻找匹配的 .dict 文件
    wxString dictPath = basePath + ".dict.dz";
    if (wxFileExists(dictPath)) {
        m_info.isDz = true;
    } else {
        dictPath = basePath + ".dict";
        m_info.isDz = false;
    }

    if (!wxFileExists(dictPath)) {
        LOG_WARN("StarDict", "Dict data file not found for: " + ifoPath);
        return false;
    }
    m_info.dictPath = dictPath.ToUTF8().data();

    if (!LoadIndex(m_info.idxPath)) {
        LOG_WARN("StarDict", "Failed to load index from: " + m_info.idxPath);
        return false;
    }

    if (!InitDictReader()) {
        LOG_WARN("StarDict", "Failed to init dict reader for: " + m_info.dictPath);
        return false;
    }

    m_loaded = true;
    LOG_INFO("StarDict", "Successfully loaded dictionary: " + m_info.bookName +
             " (" + std::to_string(m_entries.size()) + " words)");
    return true;
}

bool StarDictBook::ParseIfo(const std::string& ifoPath) {
    wxTextFile file;
    if (!file.Open(wxString::FromUTF8(ifoPath))) {
        return false;
    }

    m_info = DictInfo();
    m_info.ifoPath = ifoPath;
    wxFileName fn(wxString::FromUTF8(ifoPath));
    m_info.id = fn.GetName().ToUTF8().data();
    m_info.bookName = m_info.id;

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        wxString line = file.GetLine(i).Trim(true).Trim(false);
        if (line.IsEmpty() || line.StartsWith("#")) continue;

        int eqPos = line.Find('=');
        if (eqPos == wxNOT_FOUND) continue;

        wxString key = line.Left(eqPos).Trim(true).Lower();
        wxString val = line.Mid(eqPos + 1).Trim(false);

        if (key == "bookname") {
            m_info.bookName = val.ToUTF8().data();
        } else if (key == "version") {
            m_info.version = val.ToUTF8().data();
        } else if (key == "wordcount") {
            m_info.wordCount = static_cast<uint32_t>(wxAtol(val));
        } else if (key == "synwordcount") {
            m_info.synWordCount = static_cast<uint32_t>(wxAtol(val));
        } else if (key == "idxfilesize") {
            m_info.idxFileSize = static_cast<uint32_t>(wxAtol(val));
        } else if (key == "idxoffsetbits") {
            m_info.idxOffsetBits = wxAtoi(val);
            if (m_info.idxOffsetBits != 64) m_info.idxOffsetBits = 32;
        } else if (key == "sametypesequence") {
            m_info.sameTypeSequence = val.ToUTF8().data();
        } else if (key == "description") {
            m_info.description = val.ToUTF8().data();
        } else if (key == "author") {
            m_info.author = val.ToUTF8().data();
        }
    }
    file.Close();
    return true;
}

bool StarDictBook::LoadIndex(const std::string& idxPath) {
    wxLogNull noLog;
    std::vector<uint8_t> buffer;

    if (idxPath.find(".dz") != std::string::npos || idxPath.find(".gz") != std::string::npos) {
        wxFile file(wxString::FromUTF8(idxPath), wxFile::read);
        if (!file.IsOpened()) return false;
        wxFileOffset len = file.Length();
        if (len <= 0) return false;

        std::vector<uint8_t> compressed(len);
        file.Read(compressed.data(), len);
        file.Close();

        if (!InflateData(compressed.data(), compressed.size(), m_info.idxFileSize, buffer)) {
            return false;
        }
    } else {
        wxFile file(wxString::FromUTF8(idxPath), wxFile::read);
        if (!file.IsOpened()) return false;
        wxFileOffset len = file.Length();
        if (len <= 0) return false;
        buffer.resize(len);
        file.Read(buffer.data(), len);
        file.Close();
    }

    if (buffer.empty()) return false;

    m_entries.clear();
    if (m_info.wordCount > 0) {
        m_entries.reserve(m_info.wordCount);
    }

    const uint8_t* ptr = buffer.data();
    const uint8_t* end = buffer.data() + buffer.size();
    int offsetBytes = (m_info.idxOffsetBits == 64) ? 8 : 4;
    int recordBytes = offsetBytes + 4; // offset + size

    while (ptr < end) {
        // 查找以 null 结尾的单词字符串
        const uint8_t* strEnd = ptr;
        while (strEnd < end && *strEnd != 0) {
            strEnd++;
        }
        if (strEnd >= end || strEnd + 1 + recordBytes > end) {
            break;
        }

        std::string word(reinterpret_cast<const char*>(ptr), strEnd - ptr);
        const uint8_t* metaPtr = strEnd + 1;

        uint64_t offset = (offsetBytes == 8) ? ReadUint64BE(metaPtr) : ReadUint32BE(metaPtr);
        uint32_t size = ReadUint32BE(metaPtr + offsetBytes);

        DictWordEntry entry;
        entry.word = word;
        entry.lowerWord = ToLowerUtf8(word);
        entry.offset = offset;
        entry.size = size;
        m_entries.push_back(std::move(entry));

        ptr = metaPtr + recordBytes;
    }

    if (m_entries.empty()) return false;

    // 确保 m_entries 严格有序
    std::sort(m_entries.begin(), m_entries.end(), [](const DictWordEntry& a, const DictWordEntry& b) {
        return a.word < b.word;
    });

    // 构建基于 lowerWord 的快速全小写二分与前缀索引
    m_lowerOrder.resize(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); ++i) {
        m_lowerOrder[i] = i;
    }
    std::sort(m_lowerOrder.begin(), m_lowerOrder.end(), [this](size_t a, size_t b) {
        if (m_entries[a].lowerWord != m_entries[b].lowerWord) {
            return m_entries[a].lowerWord < m_entries[b].lowerWord;
        }
        return m_entries[a].word < m_entries[b].word;
    });

    return true;
}

bool StarDictBook::InitDictReader() {
    if (!m_info.isDz) {
        m_isDictZip = false;
        return true;
    }

    // 解析 DictZip 头
    wxFile file(wxString::FromUTF8(m_info.dictPath), wxFile::read);
    if (!file.IsOpened()) return false;

    uint8_t header[10];
    if (file.Read(header, 10) != 10) return false;

    // Gzip ID1, ID2
    if (header[0] != 0x1f || header[1] != 0x8b || header[2] != 0x08) {
        return false;
    }

    uint8_t flg = header[3];
    if (!(flg & 0x04)) { // 必须包含 FEXTRA 扩展段
        LOG_WARN("StarDict", "Not a valid DictZip file (missing FEXTRA): " + m_info.dictPath);
        return false;
    }

    uint8_t xlenBuf[2];
    if (file.Read(xlenBuf, 2) != 2) return false;
    uint16_t xlen = ReadUint16LE(xlenBuf);

    std::vector<uint8_t> extra(xlen);
    if (file.Read(extra.data(), xlen) != xlen) return false;

    // 在 extra 中寻找 "RA" 子字段
    size_t pos = 0;
    bool foundRa = false;
    while (pos + 4 <= extra.size()) {
        uint8_t si1 = extra[pos];
        uint8_t si2 = extra[pos + 1];
        uint16_t sublen = ReadUint16LE(&extra[pos + 2]);
        pos += 4;

        if (si1 == 'R' && si2 == 'A') {
            if (pos + sublen <= extra.size() && sublen >= 6) {
                // uint16_t ver = ReadUint16LE(&extra[pos]);
                m_chunkLen = ReadUint16LE(&extra[pos + 2]);
                m_chunkCount = ReadUint16LE(&extra[pos + 4]);

                m_chunkSizes.resize(m_chunkCount);
                size_t cPos = pos + 6;
                for (uint32_t i = 0; i < m_chunkCount && cPos + 2 <= extra.size(); ++i, cPos += 2) {
                    m_chunkSizes[i] = ReadUint16LE(&extra[cPos]);
                }
                foundRa = true;
                break;
            }
        }
        pos += sublen;
    }

    if (!foundRa) {
        LOG_WARN("StarDict", "DictZip RA header not found in: " + m_info.dictPath);
        return false;
    }

    // 跳过 FNAME / FCOMMENT / FHCRC
    if (flg & 0x08) { // FNAME
        char c;
        while (file.Read(&c, 1) == 1 && c != 0) {}
    }
    if (flg & 0x10) { // FCOMMENT
        char c;
        while (file.Read(&c, 1) == 1 && c != 0) {}
    }
    if (flg & 0x02) { // FHCRC
        file.Seek(2, wxFromCurrent);
    }

    m_dataStartOffset = file.Tell();
    file.Close();

    // 构建各分块在文件中的绝对偏移
    m_chunkOffsets.resize(m_chunkCount);
    uint64_t currOffset = m_dataStartOffset;
    for (uint32_t i = 0; i < m_chunkCount; ++i) {
        m_chunkOffsets[i] = currOffset;
        currOffset += m_chunkSizes[i];
    }

    m_isDictZip = true;
    return true;
}

std::vector<uint8_t> StarDictBook::GetDecompressedChunk(uint32_t chunkIndex) {
    if (chunkIndex >= m_chunkCount) return {};

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (auto it = m_chunkCache.begin(); it != m_chunkCache.end(); ++it) {
        if (it->chunkIndex == chunkIndex) {
            ChunkCacheEntry entry = *it;
            m_chunkCache.erase(it);
            m_chunkCache.push_back(entry);
            return entry.data;
        }
    }

    wxLogNull noLog;
    wxFile file(wxString::FromUTF8(m_info.dictPath), wxFile::read);
    if (!file.IsOpened()) return {};

    uint64_t chunkOffset = m_chunkOffsets[chunkIndex];
    uint32_t chunkSize = m_chunkSizes[chunkIndex];

    file.Seek(chunkOffset);
    std::vector<uint8_t> compressed(chunkSize);
    if (file.Read(compressed.data(), chunkSize) != chunkSize) {
        return {};
    }
    file.Close();

    std::vector<uint8_t> decompressed;
    if (!InflateData(compressed.data(), compressed.size(), m_chunkLen, decompressed)) {
        return {};
    }

    if (m_chunkCache.size() >= MAX_CHUNK_CACHE) {
        m_chunkCache.erase(m_chunkCache.begin());
    }
    m_chunkCache.push_back({ chunkIndex, decompressed });

    return decompressed;
}

std::string StarDictBook::ReadDictData(uint64_t offset, uint32_t size) {
    if (size == 0) return "";

    if (!m_info.isDz || !m_isDictZip) {
        wxFile file(wxString::FromUTF8(m_info.dictPath), wxFile::read);
        if (!file.IsOpened()) return "";

        file.Seek(offset);
        std::string buffer(size, '\0');
        file.Read(&buffer[0], size);
        return buffer;
    }

    // DictZip 分块按需提取
    uint32_t startChunk = static_cast<uint32_t>(offset / m_chunkLen);
    uint32_t endChunk = static_cast<uint32_t>((offset + size - 1) / m_chunkLen);

    std::string result;
    result.reserve(size);

    for (uint32_t c = startChunk; c <= endChunk; ++c) {
        std::vector<uint8_t> chunkData = GetDecompressedChunk(c);
        if (chunkData.empty()) continue;

        uint64_t chunkStartOffset = static_cast<uint64_t>(c) * m_chunkLen;
        uint64_t readStart = (offset > chunkStartOffset) ? (offset - chunkStartOffset) : 0;
        uint64_t readEnd = ((offset + size) < (chunkStartOffset + chunkData.size())) ?
                           (offset + size - chunkStartOffset) : chunkData.size();

        if (readStart < chunkData.size() && readEnd > readStart) {
            result.append(reinterpret_cast<const char*>(&chunkData[readStart]), readEnd - readStart);
        }
    }

    return result;
}

void StarDictBook::FormatDefinition(const std::string& raw, DictSearchResult& result) {
    result.rawData = raw;
    result.phonetic.clear();
    result.definition.clear();

    if (raw.empty()) return;

    if (!m_info.sameTypeSequence.empty()) {
        // 如果指定了统一序列类型
        if (m_info.sameTypeSequence == "m" || m_info.sameTypeSequence == "g") {
            result.definition = CleanTagsAndFormat(raw);
        } else if (m_info.sameTypeSequence == "h" || m_info.sameTypeSequence == "x") {
            result.definition = CleanTagsAndFormat(raw);
        } else if (m_info.sameTypeSequence == "t") {
            result.phonetic = raw;
        } else if (m_info.sameTypeSequence == "tm") {
            // 前半部分音标，后半部分释义
            size_t nullPos = raw.find('\0');
            if (nullPos != std::string::npos) {
                result.phonetic = raw.substr(0, nullPos);
                result.definition = CleanTagsAndFormat(raw.substr(nullPos + 1));
            } else {
                result.definition = CleanTagsAndFormat(raw);
            }
        } else {
            result.definition = CleanTagsAndFormat(raw);
        }
    } else {
        // 动态多类型解析
        size_t pos = 0;
        std::string defAcc;
        while (pos < raw.size()) {
            char type = raw[pos++];
            if (std::islower(static_cast<unsigned char>(type))) {
                // null 结尾字符串
                size_t strEnd = raw.find('\0', pos);
                std::string seg;
                if (strEnd != std::string::npos) {
                    seg = raw.substr(pos, strEnd - pos);
                    pos = strEnd + 1;
                } else {
                    seg = raw.substr(pos);
                    pos = raw.size();
                }

                if (type == 't') {
                    if (result.phonetic.empty()) result.phonetic = seg;
                } else if (type == 'm' || type == 'g' || type == 'h' || type == 'x') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += CleanTagsAndFormat(seg);
                }
            } else {
                // 大写类型：4字节大端长度 + 数据
                if (pos + 4 <= raw.size()) {
                    uint32_t len = ReadUint32BE(reinterpret_cast<const uint8_t*>(&raw[pos]));
                    pos += 4 + len;
                } else {
                    break;
                }
            }
        }
        result.definition = defAcc.empty() ? CleanTagsAndFormat(raw) : defAcc;
    }

    // 若未通过标签提取到音标，尝试在释义开头探测 [音标] 或 /音标/
    if (result.phonetic.empty() && !result.definition.empty()) {
        std::string& def = result.definition;
        if (def.front() == '[' || def.front() == '/') {
            char closeChar = (def.front() == '[') ? ']' : '/';
            size_t endPos = def.find(closeChar, 1);
            if (endPos != std::string::npos && endPos <= 40) {
                result.phonetic = def.substr(0, endPos + 1);
                // 剔除释义开头的音标部分以避免重复
                size_t nextPos = def.find_first_not_of(" \t\n\r", endPos + 1);
                if (nextPos != std::string::npos) {
                    def = def.substr(nextPos);
                }
            }
        }
    }
}

bool StarDictBook::Lookup(const std::string& word, DictSearchResult& result) {
    if (!m_loaded || m_entries.empty() || word.empty()) return false;

    // 1. 精确匹配（二分查找）
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), word,
        [](const DictWordEntry& a, const std::string& val) {
            return a.word < val;
        });

    const DictWordEntry* found = nullptr;
    if (it != m_entries.end() && it->word == word) {
        found = &(*it);
    }

    // 2. 大小写不敏感降级匹配（在基于 lowerWord 排序的索引中二分查找）
    if (!found && !m_lowerOrder.empty()) {
        std::string lower = ToLowerUtf8(word);
        auto itLower = std::lower_bound(m_lowerOrder.begin(), m_lowerOrder.end(), lower,
            [this](size_t idx, const std::string& val) {
                return m_entries[idx].lowerWord < val;
            });
        if (itLower != m_lowerOrder.end() && m_entries[*itLower].lowerWord == lower) {
            found = &m_entries[*itLower];
        }
    }

    if (!found) return false;

    std::string rawData = ReadDictData(found->offset, found->size);
    if (rawData.empty()) return false;

    result.word = found->word;
    result.dictId = m_info.id;
    result.dictName = m_info.bookName;
    FormatDefinition(rawData, result);
    return true;
}

void StarDictBook::GetSuggestions(const std::string& prefixLower, std::vector<std::string>& outSuggestions, size_t limit) {
    if (!m_loaded || m_entries.empty() || m_lowerOrder.empty() || prefixLower.empty()) return;

    auto it = std::lower_bound(m_lowerOrder.begin(), m_lowerOrder.end(), prefixLower,
        [this](size_t idx, const std::string& val) {
            return m_entries[idx].lowerWord < val;
        });

    size_t count = 0;
    while (it != m_lowerOrder.end() && count < limit) {
        const auto& entry = m_entries[*it];
        if (entry.lowerWord.rfind(prefixLower, 0) != 0) {
            break;
        }
        // 去重添加
        if (std::find(outSuggestions.begin(), outSuggestions.end(), entry.word) == outSuggestions.end()) {
            outSuggestions.push_back(entry.word);
            count++;
        }
        ++it;
    }
}

// -------------------------------------------------------------
// DictEngine 实现
// -------------------------------------------------------------

DictEngine::DictEngine() = default;

size_t DictEngine::LoadDictionaries(const std::string& directoryPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dictDirPath = directoryPath;
    m_books.clear();

    if (directoryPath.empty() || !wxDirExists(wxString::FromUTF8(directoryPath))) {
        LOG_WARN("StarDict", "Dictionary directory not found: " + directoryPath);
        return 0;
    }

    wxArrayString ifoFiles;
    wxDir::GetAllFiles(wxString::FromUTF8(directoryPath), &ifoFiles, "*.ifo", wxDIR_FILES | wxDIR_DIRS);

    for (const auto& ifo : ifoFiles) {
        auto book = std::make_shared<StarDictBook>();
        if (book->Load(ifo.ToUTF8().data())) {
            m_books.push_back(book);
        }
    }

    LOG_INFO("StarDict", "Loaded " + std::to_string(m_books.size()) +
             " StarDict dictionaries from " + directoryPath);
    return m_books.size();
}

size_t DictEngine::Reload() {
    std::string dir = GetCurrentDictDir();
    return LoadDictionaries(dir);
}

std::string DictEngine::GetCurrentDictDir() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dictDirPath;
}

void DictEngine::SetDictDir(const std::string& dirPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dictDirPath = dirPath;
}

std::vector<DictInfo> DictEngine::GetLoadedDictionaries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DictInfo> infos;
    infos.reserve(m_books.size());
    for (const auto& book : m_books) {
        if (book && book->IsLoaded()) {
            infos.push_back(book->GetInfo());
        }
    }
    return infos;
}

size_t DictEngine::GetTotalWordCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t total = 0;
    for (const auto& book : m_books) {
        if (book && book->IsLoaded()) {
            total += book->GetInfo().wordCount;
        }
    }
    return total;
}

std::vector<DictSearchResult> DictEngine::Lookup(const std::string& word, const std::string& dictId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DictSearchResult> results;

    for (const auto& book : m_books) {
        if (!book || !book->IsLoaded()) continue;
        if (!dictId.empty() && book->GetInfo().id != dictId) continue;

        DictSearchResult res;
        if (book->Lookup(word, res)) {
            results.push_back(std::move(res));
        }
    }

    return results;
}

std::vector<std::string> DictEngine::GetSuggestions(const std::string& prefix, size_t limit, const std::string& dictId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> suggestions;
    if (prefix.empty()) return suggestions;

    std::string prefixLower = ToLowerUtf8(prefix);

    for (const auto& book : m_books) {
        if (!book || !book->IsLoaded()) continue;
        if (!dictId.empty() && book->GetInfo().id != dictId) continue;

        book->GetSuggestions(prefixLower, suggestions, limit);
        if (suggestions.size() >= limit) break;
    }

    return suggestions;
}

bool DictEngine::HasDictionaries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_books.empty();
}

} // namespace LinguaAlpaca
