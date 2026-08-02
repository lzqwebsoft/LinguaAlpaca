#pragma execution_character_set("utf-8")
#include "LlamaCppTranslationEngine.hpp"
#include "../../domain/model/Language.hpp"
#include <wx/wx.h>
#include <wx/filename.h>
#include <llama.h>
#include <chrono>
#include <vector>
#include <algorithm>

namespace LinguaAlpaca::Infrastructure::Engine {

static std::string BuildTranslationPrompt(
    const std::string& srcText,
    Domain::Model::LanguageCode srcLang,
    Domain::Model::LanguageCode targetLang) {

    std::string targetLangName = Domain::Model::LanguageHelper::GetDisplayName(targetLang);
    std::string srcLangName = Domain::Model::LanguageHelper::GetDisplayName(srcLang);

    // 腾讯 Hy-MT2 混元大模型中文指令模版格式 (带只输出译文无额外解释的约束)
    std::string prompt;
    if (srcLang == Domain::Model::LanguageCode::AutoDetect) {
        prompt = "<|im_start|>user\n将以下文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText + "<|im_end|>\n<|im_start|>assistant\n";
    } else {
        prompt = "<|im_start|>user\n将以下" + srcLangName + "文本翻译为" + targetLangName + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + srcText + "<|im_end|>\n<|im_start|>assistant\n";
    }
    return prompt;
}

static bool IsChatMLControlToken(const struct llama_vocab* vocab, llama_token id, const std::string& piece_str) {
    if (id == LLAMA_TOKEN_NULL) return true;

    // 1. 原生 API 属性判断 (包含 EOG, EOS, EOT 与 CONTROL Token 显式检查)
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

    // 2. 检查词表中原始 token 文本 (全面匹配 im_start / im_end / endoftext 等指令标记)
    const char* raw = llama_vocab_get_text(vocab, id);
    if (raw) {
        std::string s(raw);
        if (s.find("im_start") != std::string::npos ||
            s.find("im_end") != std::string::npos ||
            s.find("endoftext") != std::string::npos ||
            s.find("endofpiece") != std::string::npos ||
            s.find("<|") != std::string::npos) {
            return true;
        }
    }

    // 3. 检查 piece 解码后的字符串
    if (piece_str.find("im_start") != std::string::npos ||
        piece_str.find("im_end") != std::string::npos ||
        piece_str.find("endoftext") != std::string::npos ||
        piece_str.find("endofpiece") != std::string::npos ||
        piece_str.find("<|") != std::string::npos) {
        return true;
    }

    return false;
}

static void TrimTrailingControlArtifacts(std::string& str) {
    static const std::vector<std::string> controlTags = {
        "<|im_start|>", "<|im_end|>", "<|endoftext|>", "<|im_start", "<|im_end", "<|im_", "<|", "im_start", "im_end"
    };

    for (const auto& tag : controlTags) {
        size_t pos = str.find(tag);
        if (pos != std::string::npos) {
            str = str.substr(0, pos);
        }
    }

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

    // 调用 llama.cpp 原生 C API 从本地 GGUF 文件真正加载大模型权重
    llama_model_params model_params = llama_model_default_params();
    m_model = llama_model_load_from_file(modelPath.c_str(), model_params);

    if (m_model) {
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 4096;
        ctx_params.n_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
        ctx_params.n_threads_batch = ctx_params.n_threads;
        m_ctx = llama_init_from_model(m_model, ctx_params);
    }

    // 只有模型句柄和推理上下文都成功创建才判定为加载就绪
    m_isLoaded = (m_model != nullptr && m_ctx != nullptr);
    return m_isLoaded;
}

Domain::Model::TranslationTask LlamaCppTranslationEngine::Translate(const Domain::Model::TranslationTask& task) {
    Domain::Model::TranslationTask result = task;
    if (!m_isLoaded || !m_model || !m_ctx) {
        result.SetErrorMessage("Llama.cpp 模型未加载，请在设置中配置 GGUF 路径。");
        return result;
    }

    result.SetTranslatedText("[Llama.cpp 原生模型推理]");
    return result;
}

std::string LlamaCppTranslationEngine::QuickTranslate(const std::string& text, Domain::Model::LanguageCode sourceLang, Domain::Model::LanguageCode targetLang) {
    if (text.empty()) return "";
    return "即时译文 (Llama.cpp): " + text;
}

void LlamaCppTranslationEngine::TranslateStreamAsync(
    const Domain::Model::TranslationTask& task,
    Domain::Repository::StreamTokenCallback onToken,
    Domain::Repository::StreamCompleteCallback onComplete) {

    CancelCurrentTask();
    m_cancelRequested = false;
    m_isProcessing = true;

    std::thread([this, task, onToken, onComplete]() {
        // 校验引擎与模型是否真实加载
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

        // 每次推理前彻底重置并清理 KV cache 内存
        llama_memory_t mem = llama_get_memory(m_ctx);
        llama_memory_clear(mem, true);

        std::string src = task.GetSourceText();
        Domain::Model::LanguageCode srcLang = task.GetSourceLanguage();
        Domain::Model::LanguageCode targetLang = task.GetTargetLanguage();
        std::string fullOutput = "";

        // 纯粹的 llama.cpp 原生 C API 原生 Token 化、解码与 Sampler Chain 采样推理
        const struct llama_vocab* vocab = llama_model_get_vocab(m_model);

        // 动态构造无 system_prompt 的 ChatML 规范提示词
        std::string prompt = BuildTranslationPrompt(src, srcLang, targetLang);

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

        // 初始化用户配置的采样器链 (Sampler Chain)
        auto sparams = llama_sampler_chain_default_params();
        llama_sampler* smpl = llama_sampler_chain_init(sparams);

        // 1. 重复惩罚: repetition_penalty = 1.05
        llama_sampler_chain_add(smpl, llama_sampler_init_penalties(64, 1.05f, 0.0f, 0.0f));

        // 2. Top-K 采样: top_k = 20
        llama_sampler_chain_add(smpl, llama_sampler_init_top_k(20));

        // 3. Top-P 采样: top_p = 0.6
        llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.6f, 1));

        // 4. 温度采样: temperature = 0.7
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));

        // 5. 随机分布采样器 (dist)
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        int n_max_tokens = 4096;
        int n_cur = 0;

        // 逐字 Token 采样循环
        while (n_cur < n_max_tokens && !m_cancelRequested) {
            llama_token id = llama_sampler_sample(smpl, m_ctx, -1);

            char piece_buf[128] = { 0 };
            int p_len = llama_token_to_piece(vocab, id, piece_buf, sizeof(piece_buf), 0, true);
            std::string token_str = (p_len > 0) ? std::string(piece_buf, p_len) : "";

            // 1. 深度拦截判定：只要匹配到 EOT, EOS, im_end, im_start, endoftext, <| 任何特征，立即中断并禁止输出
            if (IsChatMLControlToken(vocab, id, token_str)) {
                break;
            }

            // 接受采样 token 进采样器更新重复惩罚历史
            llama_sampler_accept(smpl, id);

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

        // 清理末尾可能的残余空白符或控制标记
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

} // namespace LinguaAlpaca::Infrastructure::Engine
