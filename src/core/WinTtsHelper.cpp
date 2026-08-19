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

WinTtsHelper& WinTtsHelper::GetInstance() {
    static WinTtsHelper instance;
    return instance;
}

WinTtsHelper::WinTtsHelper() : m_impl(std::make_unique<Impl>()) {}

WinTtsHelper::~WinTtsHelper() = default;

bool WinTtsHelper::Speak(const std::wstring& text, LanguageCode lang) {
    return m_impl->Speak(text, lang);
}

bool WinTtsHelper::Speak(const std::string& utf8Text, LanguageCode lang) {
    return Speak(Utf8ToWide(utf8Text), lang);
}

void WinTtsHelper::Stop() {
    m_impl->Stop();
}

bool WinTtsHelper::IsSpeaking() const {
    return m_impl->IsSpeaking();
}

void WinTtsHelper::SetRate(long rate) {
    m_impl->SetRate(rate);
}

void WinTtsHelper::SetVolume(unsigned short volume) {
    m_impl->SetVolume(volume);
}

} // namespace LinguaAlpaca

#else

namespace LinguaAlpaca {

class WinTtsHelper::Impl {};

WinTtsHelper& WinTtsHelper::GetInstance() {
    static WinTtsHelper instance;
    return instance;
}

WinTtsHelper::WinTtsHelper() = default;
WinTtsHelper::~WinTtsHelper() = default;
bool WinTtsHelper::Speak(const std::wstring&, LanguageCode) { return false; }
bool WinTtsHelper::Speak(const std::string&, LanguageCode) { return false; }
void WinTtsHelper::Stop() {}
bool WinTtsHelper::IsSpeaking() const { return false; }
void WinTtsHelper::SetRate(long) {}
void WinTtsHelper::SetVolume(unsigned short) {}

} // namespace LinguaAlpaca

#endif
