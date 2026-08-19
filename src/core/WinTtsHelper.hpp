#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <memory>
#include <mutex>
#include "Types.hpp"

namespace LinguaAlpaca {

class WinTtsHelper {
public:
    static WinTtsHelper& GetInstance();

    // 朗读指定文本 (宽字符)，若正在播放同一文本则停止播放 (Toggle 逻辑)
    bool Speak(const std::wstring& text, LanguageCode lang = LanguageCode::AutoDetect);

    // 朗读指定 UTF-8 文本
    bool Speak(const std::string& utf8Text, LanguageCode lang = LanguageCode::AutoDetect);

    // 停止当前正在进行的朗读
    void Stop();

    // 查询当前是否正在朗读
    bool IsSpeaking() const;

    // 设置语速 (-10 到 10，0 为默认)
    void SetRate(long rate);

    // 设置音量 (0 到 100，100 为最大)
    void SetVolume(unsigned short volume);

private:
    WinTtsHelper();
    ~WinTtsHelper();

    WinTtsHelper(const WinTtsHelper&) = delete;
    WinTtsHelper& operator=(const WinTtsHelper&) = delete;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LinguaAlpaca
