#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace LinguaAlpaca {

/**
 * @brief 词典元数据信息
 */
struct DictInfo {
    std::string id;              // 词典唯一标识（通常为文件名或目录名）
    std::string bookName;        // 词典名称 (bookname)
    std::string version;         // 版本号 (version)
    std::string description;     // 词典描述 (description)
    std::string author;          // 作者 (author)
    uint32_t wordCount{0};       // 词条总数 (wordcount)
    uint32_t synWordCount{0};    // 同义词数 (synwordcount)
    uint32_t idxFileSize{0};     // 索引文件大小 (idxfilesize)
    int idxOffsetBits{32};       // 索引偏移位数 (32 或 64)
    std::string sameTypeSequence;// 统一数据类型序列 (如 "m", "tm", "g", "h", "x")
    std::string ifoPath;         // .ifo 文件绝对路径
    std::string idxPath;         // .idx / .idx.dz 路径
    std::string dictPath;        // .dict / .dict.dz 路径
    bool isDz{false};            // 是否为 .dict.dz 压缩格式
};

/**
 * @brief 索引词条项
 */
struct DictWordEntry {
    std::string word;
    std::string lowerWord;       // 小写形式（用于前缀联想与模糊二分检索）
    uint64_t offset{0};
    uint32_t size{0};
};

/**
 * @brief 单词查询结果
 */
struct DictSearchResult {
    std::string word;            // 检索的单词
    std::string dictId;          // 词典 ID
    std::string dictName;        // 词典名称
    std::string phonetic;        // 音标（如提取到 [fə'netɪk]）
    std::string definition;      // 格式化后的排版释义文本
    std::string rawData;         // 原始数据
};

/**
 * @brief 单本 StarDict 词典实例
 */
class StarDictBook {
public:
    StarDictBook() = default;
    ~StarDictBook();

    bool Load(const std::string& ifoPath);
    bool IsLoaded() const { return m_loaded; }

    const DictInfo& GetInfo() const { return m_info; }
    
    // 查询单词释义
    bool Lookup(const std::string& word, DictSearchResult& result);

    // 获取前缀联想词
    void GetSuggestions(const std::string& prefixLower, std::vector<std::string>& outSuggestions, size_t limit);

private:
    bool ParseIfo(const std::string& ifoPath);
    bool LoadIndex(const std::string& idxPath);
    bool InitDictReader();
    std::string ReadDictData(uint64_t offset, uint32_t size);
    void FormatDefinition(const std::string& raw, DictSearchResult& result);

    DictInfo m_info;
    bool m_loaded{false};
    std::vector<DictWordEntry> m_entries;
    std::vector<size_t> m_lowerOrder;

    // DictZip 支持结构
    bool m_isDictZip{false};
    uint32_t m_chunkLen{58312};
    uint32_t m_chunkCount{0};
    std::vector<uint32_t> m_chunkSizes;
    std::vector<uint64_t> m_chunkOffsets; // 每个分块在压缩文件中的绝对偏移
    uint64_t m_dataStartOffset{0};

    // 简单 LRU 分块解压缓存 (使用 shared_ptr 避免 64KB vector 频繁深度复制)
    mutable std::mutex m_cacheMutex;
    struct ChunkCacheEntry {
        uint32_t chunkIndex;
        std::shared_ptr<const std::vector<uint8_t>> data;
    };
    std::vector<ChunkCacheEntry> m_chunkCache;
    static constexpr size_t MAX_CHUNK_CACHE = 8;

    std::shared_ptr<const std::vector<uint8_t>> GetDecompressedChunk(uint32_t chunkIndex);
};

/**
 * @brief StarDict 词典聚合引擎
 */
class DictEngine {
public:
    DictEngine();
    ~DictEngine() = default;

    // 扫描并加载指定目录下的所有 StarDict 词典
    size_t LoadDictionaries(const std::string& directoryPath);

    // 重新扫描并加载
    size_t Reload();

    // 获取当前配置的词典目录
    std::string GetCurrentDictDir() const;
    void SetDictDir(const std::string& dirPath);

    // 获取所有已载入词典的信息
    std::vector<DictInfo> GetLoadedDictionaries() const;

    // 获取词典总词条数
    size_t GetTotalWordCount() const;

    // 查询单词 (dictId 为空时在所有已加载词典中聚合检索)
    std::vector<DictSearchResult> Lookup(const std::string& word, const std::string& dictId = "");

    // 前缀联想候选词
    std::vector<std::string> GetSuggestions(const std::string& prefix, size_t limit = 20, const std::string& dictId = "");

    // 检查是否有可用词典
    bool HasDictionaries() const;

private:
    mutable std::mutex m_mutex;
    std::string m_dictDirPath;
    std::vector<std::shared_ptr<StarDictBook>> m_books;
};

} // namespace LinguaAlpaca
