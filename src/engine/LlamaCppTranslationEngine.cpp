#pragma execution_character_set("utf-8")
#include "LlamaCppTranslationEngine.hpp"
#include "core/Types.hpp"
#include <wx/wx.h>
#include <wx/filename.h>
#include <llama.h>
#include <chrono>
#include <vector>
#include <algorithm>

namespace LinguaAlpaca::Engine {

static std::string BuildTranslationPrompt(
    const struct llama_model* model,
    const std::string& srcText,
    LanguageCode srcLang,
    LanguageCode targetLang) {

    std::string targetLangName = LanguageHelper::GetDisplayName(targetLang);
    std::string srcLangName = LanguageHelper::GetDisplayName(srcLang);

    std::string userContent;
    if (srcLang == LanguageCode::AutoDetect) {
        userContent = "将以下文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
    } else {
        userContent = "将以下" + srcLangName + "文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText;
    }

    const char* tmpl = model ? llama_model_chat_template(model, nullptr) : nullptr;

    llama_chat_message message = { "user", userContent.c_str() };

    std::vector<char> formatted(2048);
    int new_len = llama_chat_apply_template(tmpl, &message, 1, true, formatted.data(), (int)formatted.size());
    if (new_len > (int)formatted.size()) {
        formatted.resize(new_len + 1);
        new_len = llama_chat_apply_template(tmpl, &message, 1, true, formatted.data(), (int)formatted.size());
    }

    if (new_len > 0) {
        return std::string(formatted.data(), new_len);
    }

    // 回退机制：若 Chat Template 解析/应用失败，手动拼接 ChatML 规范格式
    return "<|im_start|>user\n" + userContent + "<|im_end|>\n<|im_start|>assistant\n";
}

static bool IsControlToken(const struct llama_vocab* vocab, llama_token id) {
    if (id == LLAMA_TOKEN_NULL) return true;

    // 纯 native llama.cpp 原生 API 判定：EOG、Control 标记及 CONTROL 属性
    if (llama_vocab_is_eog(vocab, id) ||
        llama_vocab_is_control(vocab, id) ||
        id == llama_vocab_eos(vocab) ||
        id == llama_vocab_eot(vocab)) {
        return true;
    }

    auto attr = llama_vocab_get_attr(vocab, id);
    if (attr & LLAMA_TOKEN_ATTR_CONTROL) {
        return true;
    }

    return false;
}

static void TrimTrailingControlArtifacts(std::string& str) {
    while (!str.empty() && (str.back() == ' ' || str.back() == '\n' || str.back() == '\r' || str.back() == '\t')) {
        str.pop_back();
    }
}

LlamaCppTranslationEngine::LlamaCppTranslationEngine(const std::string& modelPath) {
    llama_backend_init();
    if (!modelPath.empty()) {
        LoadModel(modelPath);
    }
}

LlamaCppTranslationEngine::~LlamaCppTranslationEngine() {
    CancelCurrentTask();
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
    llama_backend_free();
}

bool LlamaCppTranslationEngine::LoadModel(const std::string& modelPath) {
    if (modelPath.empty() || !wxFileExists(wxString::FromUTF8(modelPath))) {
        m_isLoaded = false;
        return false;
    }

    m_modelPath = modelPath;

    // 清理先前的 context 与 model
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }

    llama_model_params model_params = llama_model_default_params();
    m_model = llama_model_load_from_file(modelPath.c_str(), model_params);

    if (m_model) {
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 4096;
        ctx_params.n_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
        ctx_params.n_threads_batch = ctx_params.n_threads;
        m_ctx = llama_init_from_model(m_model, ctx_params);
    }

    m_isLoaded = (m_model != nullptr && m_ctx != nullptr);
    return m_isLoaded;
}

void LlamaCppTranslationEngine::TranslateStreamAsync(
    const TranslationTask& task,
    StreamTokenCallback onToken,
    StreamCompleteCallback onComplete) {

    CancelCurrentTask();
    m_cancelRequested = false;
    m_isProcessing = true;

    std::thread([this, task, onToken, onComplete]() {
        if (!m_isLoaded || !m_model || !m_ctx) {
            if (onComplete) {
                if (wxTheApp) {
                    wxTheApp->CallAfter([onComplete]() {
                        onComplete(false, "", "Llama.cpp 模型未加载或无法推理，请在设置中配置有效的 GGUF 模型文件。");
                    });
                } else {
                    onComplete(false, "", "Llama.cpp 模型未加载或无法推理，请在设置中配置有效的 GGUF 模型文件。");
                }
            }
            m_isProcessing = false;
            return;
        }

        llama_memory_t mem = llama_get_memory(m_ctx);
        llama_memory_clear(mem, true);

        std::string src = task.GetSourceText();
        LanguageCode srcLang = task.GetSourceLanguage();
        LanguageCode targetLang = task.GetTargetLanguage();
        std::string fullOutput = "";

        const struct llama_vocab* vocab = llama_model_get_vocab(m_model);
        std::string prompt = BuildTranslationPrompt(m_model, src, srcLang, targetLang);

        int n_prompt_tokens = -llama_tokenize(vocab, prompt.c_str(), (int)prompt.length(), nullptr, 0, true, true);
        if (n_prompt_tokens <= 0) {
            if (onComplete) {
                wxTheApp->CallAfter([onComplete]() {
                    onComplete(false, "", "Llama.cpp Tokenize 失败。");
                });
            }
            m_isProcessing = false;
            return;
        }

        std::vector<llama_token> prompt_tokens(n_prompt_tokens);
        if (llama_tokenize(vocab, prompt.c_str(), (int)prompt.length(), prompt_tokens.data(), (int)prompt_tokens.size(), true, true) <= 0) {
            if (onComplete) {
                wxTheApp->CallAfter([onComplete]() {
                    onComplete(false, "", "Llama.cpp Tokenize 处理失败。");
                });
            }
            m_isProcessing = false;
            return;
        }

        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), (int)prompt_tokens.size());

        if (llama_decode(m_ctx, batch) != 0) {
            if (onComplete) {
                wxTheApp->CallAfter([onComplete]() {
                    onComplete(false, "", "Llama.cpp 解码推理失败 (llama_decode error)。");
                });
            }
            m_isProcessing = false;
            return;
        }

        auto sparams = llama_sampler_chain_default_params();
        llama_sampler* smpl = llama_sampler_chain_init(sparams);

        llama_sampler_chain_add(smpl, llama_sampler_init_penalties(64, 1.05f, 0.0f, 0.0f));
        llama_sampler_chain_add(smpl, llama_sampler_init_top_k(20));
        llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.6f, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        int n_max_tokens = 4096;
        int n_cur = 0;

        while (n_cur < n_max_tokens && !m_cancelRequested) {
            int n_ctx = llama_n_ctx(m_ctx);
            int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(m_ctx), 0) + 1;
            if (n_ctx_used + 1 > n_ctx) {
                break;
            }

            llama_token id = llama_sampler_sample(smpl, m_ctx, -1);

            if (IsControlToken(vocab, id)) {
                break;
            }

            llama_sampler_accept(smpl, id);

            char buf[256] = { 0 };
            int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
            std::string token_str;
            if (n < 0) {
                std::vector<char> piece_buf(-n);
                int n_retry = llama_token_to_piece(vocab, id, piece_buf.data(), (int)piece_buf.size(), 0, true);
                if (n_retry > 0) {
                    token_str = std::string(piece_buf.data(), n_retry);
                }
            } else if (n > 0) {
                token_str = std::string(buf, n);
            }

            if (!token_str.empty()) {
                fullOutput += token_str;

                if (onToken) {
                    if (wxTheApp) {
                        wxTheApp->CallAfter([onToken, token_str]() { onToken(token_str); });
                    } else {
                        onToken(token_str);
                    }
                }
            }

            batch = llama_batch_get_one(&id, 1);
            if (llama_decode(m_ctx, batch) != 0) {
                break;
            }
            n_cur++;
        }

        llama_sampler_free(smpl);
        TrimTrailingControlArtifacts(fullOutput);

        bool wasCancelled = m_cancelRequested.load();
        m_isProcessing = false;

        if (onComplete) {
            if (wxTheApp) {
                wxTheApp->CallAfter([onComplete, wasCancelled, fullOutput]() {
                    onComplete(!wasCancelled, fullOutput, wasCancelled ? "已取消" : "");
                });
            } else {
                onComplete(!wasCancelled, fullOutput, wasCancelled ? "已取消" : "");
            }
        }
    }).detach();
}

void LlamaCppTranslationEngine::CancelCurrentTask() {
    m_cancelRequested = true;
}

} // namespace LinguaAlpaca::Engine
