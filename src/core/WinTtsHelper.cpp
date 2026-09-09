#pragma execution_character_set("utf-8")
#include "WinTtsHelper.hpp"
#include "Logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sapi.h>
#include <vector>

namespace LinguaAlpaca {

namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wstr[0], size);
    return wstr;
}

std::wstring GetLanguageAttribute(LanguageCode lang, const std::wstring& sampleText) {
    if (lang == LanguageCode::AutoDetect) {
        bool hasCjk = false;
        for (wchar_t ch : sampleText) {
            if ((ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3400 && ch <= 0x4DBF)) {
                hasCjk = true;
                break;
            }
        }
        return hasCjk ? L"Language=804" : L"Language=409";
    }

    switch (lang) {
    case LanguageCode::Chinese:
    case LanguageCode::Cantonese:
        return L"Language=804"; // zh-CN
    case LanguageCode::TraditionalChinese:
        return L"Language=404"; // zh-TW
    case LanguageCode::English:
        return L"Language=409"; // en-US
    case LanguageCode::French:
        return L"Language=40C"; // fr-FR
    case LanguageCode::Portuguese:
        return L"Language=416"; // pt-BR
    case LanguageCode::Spanish:
        return L"Language=40A"; // es-ES
    case LanguageCode::Japanese:
        return L"Language=411"; // ja-JP
    case LanguageCode::Russian:
        return L"Language=419"; // ru-RU
    case LanguageCode::Korean:
        return L"Language=412"; // ko-KR
    case LanguageCode::German:
        return L"Language=407"; // de-DE
    case LanguageCode::Italian:
        return L"Language=410"; // it-IT
    case LanguageCode::Arabic:
        return L"Language=401"; // ar-SA
    case LanguageCode::Turkish:
        return L"Language=41F"; // tr-TR
    case LanguageCode::Vietnamese:
        return L"Language=42A"; // vi-VN
    case LanguageCode::Hindi:
        return L"Language=439"; // hi-IN
    case LanguageCode::Polish:
        return L"Language=415"; // pl-PL
    case LanguageCode::Dutch:
        return L"Language=413"; // nl-NL
    default:
        return L"";
    }
}

} // namespace

class WinTtsHelper::Impl {
public:
    Impl() {
        HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_coInitialized = SUCCEEDED(hrInit) || hrInit == RPC_E_CHANGED_MODE;

        HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (FAILED(hr) || !m_pVoice) {
            LOG_ERROR("TTS", "Failed to initialize Windows SAPI ISpVoice instance");
            m_pVoice = nullptr;
        } else {
            LOG_INFO("TTS", "Windows SAPI TTS engine initialized successfully");
        }
    }

    ~Impl() {
        Stop();
        if (m_pVoice) {
            m_pVoice->Release();
            m_pVoice = nullptr;
        }
        if (m_coInitialized) {
            CoUninitialize();
        }
    }

    bool Speak(const std::wstring& text, LanguageCode lang) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pVoice || text.empty()) {
            return false;
        }

        // Toggle 逻辑：若正在播放完全相同的文本，再次点击则停止播放
        if (IsSpeakingInternal() && m_lastSpokenText == text) {
            StopInternal();
            return true;
        }

        // 根据语言尝试切换匹配的语音包
        SetVoiceForLanguage(lang, text);

        m_lastSpokenText = text;
        ULONG streamNumber = 0;
        HRESULT hr = m_pVoice->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, &streamNumber);
        if (FAILED(hr)) {
            LOG_WARN("TTS", "Failed to speak text, HRESULT=" + std::to_string(hr));
            return false;
        }
        return true;
    }

    bool Speak(const std::string& utf8Text, LanguageCode lang) {
        return Speak(Utf8ToWide(utf8Text), lang);
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        StopInternal();
    }

    bool IsSpeaking() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return IsSpeakingInternal();
    }

    void SetRate(long rate) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pVoice) {
            m_pVoice->SetRate(rate);
        }
    }

    void SetVolume(unsigned short volume) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pVoice) {
            m_pVoice->SetVolume(volume);
        }
    }

private:
    void StopInternal() {
        if (m_pVoice) {
            m_pVoice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        }
        m_lastSpokenText.clear();
    }

    bool IsSpeakingInternal() const {
        if (!m_pVoice) return false;
        SPVOICESTATUS status;
        if (SUCCEEDED(m_pVoice->GetStatus(&status, nullptr))) {
            return status.dwRunningState == SPRS_IS_SPEAKING;
        }
        return false;
    }

    void SetVoiceForLanguage(LanguageCode lang, const std::wstring& sampleText) {
        if (!m_pVoice) return;
        std::wstring attr = GetLanguageAttribute(lang, sampleText);
        if (attr.empty()) return;

        ISpObjectTokenCategory* pCategory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                      IID_ISpObjectTokenCategory, (void**)&pCategory);
        if (FAILED(hr) || !pCategory) return;

        if (SUCCEEDED(pCategory->SetId(SPCAT_VOICES, FALSE))) {
            IEnumSpObjectTokens* pEnum = nullptr;
            if (SUCCEEDED(pCategory->EnumTokens(attr.c_str(), nullptr, &pEnum)) && pEnum) {
                ISpObjectToken* pToken = nullptr;
                ULONG fetched = 0;
                if (pEnum->Next(1, &pToken, &fetched) == S_OK && pToken) {
                    m_pVoice->SetVoice(pToken);
                    pToken->Release();
                }
                pEnum->Release();
            }
        }
        pCategory->Release();
    }

    mutable std::mutex m_mutex;
    ISpVoice* m_pVoice{nullptr};
    std::wstring m_lastSpokenText;
    bool m_coInitialized{false};
};

} // namespace LinguaAlpaca

#elif defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <algorithm>
#include <atomic>

namespace LinguaAlpaca {

namespace {

std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    NSStringEncoding encoding = NSUTF32LittleEndianStringEncoding;
#else
    NSStringEncoding encoding = NSUTF32BigEndianStringEncoding;
#endif
    NSString* nsStr = [[NSString alloc] initWithBytes:wstr.data()
                                               length:wstr.size() * sizeof(wchar_t)
                                             encoding:encoding];
    if (nsStr && [nsStr UTF8String]) {
        return [nsStr UTF8String];
    }
    return "";
}

NSString* GetMacLanguageCode(LanguageCode lang, NSString* sampleText) {
    if (lang == LanguageCode::AutoDetect) {
        bool hasCjk = false;
        NSUInteger len = [sampleText length];
        for (NSUInteger i = 0; i < len; ++i) {
            unichar ch = [sampleText characterAtIndex:i];
            if ((ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3400 && ch <= 0x4DBF)) {
                hasCjk = true;
                break;
            }
        }
        return hasCjk ? @"zh-CN" : @"en-US";
    }

    switch (lang) {
    case LanguageCode::Chinese:
        return @"zh-CN";
    case LanguageCode::Cantonese:
        return @"zh-HK";
    case LanguageCode::TraditionalChinese:
        return @"zh-TW";
    case LanguageCode::English:
        return @"en-US";
    case LanguageCode::French:
        return @"fr-FR";
    case LanguageCode::Portuguese:
        return @"pt-BR";
    case LanguageCode::Spanish:
        return @"es-ES";
    case LanguageCode::Japanese:
        return @"ja-JP";
    case LanguageCode::Russian:
        return @"ru-RU";
    case LanguageCode::Korean:
        return @"ko-KR";
    case LanguageCode::German:
        return @"de-DE";
    case LanguageCode::Italian:
        return @"it-IT";
    case LanguageCode::Arabic:
        return @"ar-SA";
    case LanguageCode::Turkish:
        return @"tr-TR";
    case LanguageCode::Vietnamese:
        return @"vi-VN";
    case LanguageCode::Thai:
        return @"th-TH";
    case LanguageCode::Hindi:
        return @"hi-IN";
    case LanguageCode::Polish:
        return @"pl-PL";
    case LanguageCode::Czech:
        return @"cs-CZ";
    case LanguageCode::Dutch:
        return @"nl-NL";
    case LanguageCode::Indonesian:
        return @"id-ID";
    case LanguageCode::Malay:
        return @"ms-MY";
    case LanguageCode::Ukrainian:
        return @"uk-UA";
    case LanguageCode::Hebrew:
        return @"he-IL";
    default:
        return @"en-US";
    }
}

float CalculateSpeechRate(long rate) {
    rate = std::clamp(rate, -10L, 10L);
    if (rate == 0) {
        return AVSpeechUtteranceDefaultSpeechRate;
    } else if (rate < 0) {
        return 0.5f + (rate / 10.0f) * 0.3f;
    } else {
        return 0.5f + (rate / 10.0f) * 0.35f;
    }
}

} // namespace

} // namespace LinguaAlpaca

@interface AlpacaSpeechDelegate : NSObject <AVSpeechSynthesizerDelegate>
@property (nonatomic, copy) void (^onSpeechFinished)();
@end

@implementation AlpacaSpeechDelegate
- (void)speechSynthesizer:(AVSpeechSynthesizer *)synthesizer didFinishSpeechUtterance:(AVSpeechUtterance *)utterance {
    if (self.onSpeechFinished) {
        self.onSpeechFinished();
    }
}
- (void)speechSynthesizer:(AVSpeechSynthesizer *)synthesizer didCancelSpeechUtterance:(AVSpeechUtterance *)utterance {
    if (self.onSpeechFinished) {
        self.onSpeechFinished();
    }
}
@end

namespace LinguaAlpaca {

class WinTtsHelper::Impl {
public:
    Impl() : m_state(std::make_shared<SharedState>()) {
        m_synthesizer = [[AVSpeechSynthesizer alloc] init];
        m_delegate = [[AlpacaSpeechDelegate alloc] init];
        auto weakState = std::weak_ptr<SharedState>(m_state);
        m_delegate.onSpeechFinished = ^{
            if (auto state = weakState.lock()) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->lastSpokenText.clear();
            }
        };
        m_synthesizer.delegate = m_delegate;
        LOG_INFO("TTS", "macOS AVFoundation AVSpeechSynthesizer initialized successfully");
    }

    ~Impl() {
        Stop();
        if (m_delegate) {
            m_delegate.onSpeechFinished = nil;
        }
        if (m_synthesizer) {
            m_synthesizer.delegate = nil;
        }
    }

    bool Speak(const std::string& utf8Text, LanguageCode lang) {
        if (!m_synthesizer || utf8Text.empty()) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            // Toggle 逻辑：若正在播放完全相同的文本，再次点击则停止播放
            if (IsSpeakingInternal() && m_state->lastSpokenText == utf8Text) {
                StopInternal();
                return true;
            }

            // 若正在播放其他文本，先打断之前的朗读
            if (IsSpeakingInternal()) {
                StopInternal();
            }
        }

        NSString* nsText = [NSString stringWithUTF8String:utf8Text.c_str()];
        if (!nsText) {
            nsText = [[NSString alloc] initWithBytes:utf8Text.data()
                                              length:utf8Text.size()
                                            encoding:NSUTF8StringEncoding];
        }
        if (!nsText || [nsText length] == 0) {
            return false;
        }

        AVSpeechUtterance* utterance = [AVSpeechUtterance speechUtteranceWithString:nsText];
        NSString* langCode = GetMacLanguageCode(lang, nsText);
        AVSpeechSynthesisVoice* voice = [AVSpeechSynthesisVoice voiceWithLanguage:langCode];
        if (!voice) {
            NSRange dash = [langCode rangeOfString:@"-"];
            if (dash.location != NSNotFound) {
                NSString* prefix = [langCode substringToIndex:dash.location];
                voice = [AVSpeechSynthesisVoice voiceWithLanguage:prefix];
            }
        }
        if (!voice && [langCode hasPrefix:@"zh"]) {
            voice = [AVSpeechSynthesisVoice voiceWithLanguage:@"zh-CN"];
        }
        if (voice) {
            utterance.voice = voice;
        }

        utterance.rate = CalculateSpeechRate(m_rate.load());
        utterance.volume = std::clamp(m_volume.load() / 100.0f, 0.0f, 1.0f);

        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->lastSpokenText = utf8Text;
        }

        [m_synthesizer speakUtterance:utterance];
        return true;
    }

    bool Speak(const std::wstring& text, LanguageCode lang) {
        return Speak(WStringToUtf8(text), lang);
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        StopInternal();
    }

    bool IsSpeaking() const {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        return IsSpeakingInternal();
    }

    void SetRate(long rate) {
        m_rate.store(std::clamp(rate, -10L, 10L));
    }

    void SetVolume(unsigned short volume) {
        m_volume.store(std::clamp(volume, static_cast<unsigned short>(0), static_cast<unsigned short>(100)));
    }

private:
    void StopInternal() {
        if (m_synthesizer && [m_synthesizer isSpeaking]) {
            [m_synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        }
        m_state->lastSpokenText.clear();
    }

    bool IsSpeakingInternal() const {
        if (!m_synthesizer) return false;
        return [m_synthesizer isSpeaking];
    }

    struct SharedState {
        std::mutex mutex;
        std::string lastSpokenText;
    };

    std::shared_ptr<SharedState> m_state;
    AVSpeechSynthesizer* m_synthesizer{nil};
    AlpacaSpeechDelegate* m_delegate{nil};
    std::atomic<long> m_rate{0};
    std::atomic<unsigned short> m_volume{100};
};

} // namespace LinguaAlpaca

#else

namespace LinguaAlpaca {

class WinTtsHelper::Impl {
public:
    bool Speak(const std::wstring&, LanguageCode) { return false; }
    bool Speak(const std::string&, LanguageCode) { return false; }
    void Stop() {}
    bool IsSpeaking() const { return false; }
    void SetRate(long) {}
    void SetVolume(unsigned short) {}
};

} // namespace LinguaAlpaca

#endif

namespace LinguaAlpaca {

WinTtsHelper& WinTtsHelper::GetInstance() {
    static WinTtsHelper instance;
    return instance;
}

WinTtsHelper::WinTtsHelper() : m_impl(std::make_unique<Impl>()) {}

WinTtsHelper::~WinTtsHelper() = default;

bool WinTtsHelper::Speak(const std::wstring& text, LanguageCode lang) {
    return m_impl ? m_impl->Speak(text, lang) : false;
}

bool WinTtsHelper::Speak(const std::string& utf8Text, LanguageCode lang) {
    return m_impl ? m_impl->Speak(utf8Text, lang) : false;
}

void WinTtsHelper::Stop() {
    if (m_impl) {
        m_impl->Stop();
    }
}

bool WinTtsHelper::IsSpeaking() const {
    return m_impl ? m_impl->IsSpeaking() : false;
}

void WinTtsHelper::SetRate(long rate) {
    if (m_impl) {
        m_impl->SetRate(rate);
    }
}

void WinTtsHelper::SetVolume(unsigned short volume) {
    if (m_impl) {
        m_impl->SetVolume(volume);
    }
}

} // namespace LinguaAlpaca
