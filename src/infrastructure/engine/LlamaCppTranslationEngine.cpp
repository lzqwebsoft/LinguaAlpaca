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
    const struct llama_model* model,
    const std::string& srcText,
    Domain::Model::LanguageCode srcLang,
    Domain::Model::LanguageCode targetLang) {

    std::string targetLangName = Domain::Model::LanguageHelper::GetDisplayName(targetLang);
    std::string srcLangName = Domain::Model::LanguageHelper::GetDisplayName(srcLang);

    std::string userContent;
    if (srcLang == Domain::Model::LanguageCode::AutoDetect) {
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

    // 调用 llama.cpp 原生 C API 从本地 GGUF 文件真正加载大模型权重
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99;
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

        // 动态构造基于 llama_model_chat_template 与 ChatML 规范提示词
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

        // 逐字 Token 采样循环 (参考 simple-chat.cpp generate 实现)
        while (n_cur < n_max_tokens && !m_cancelRequested) {
            // 检查上下文容量限制
            int n_ctx = llama_n_ctx(m_ctx);
            int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(m_ctx), 0) + 1;
            if (n_ctx_used + 1 > n_ctx) {
                break;
            }

            // 采样下一个 Token
            llama_token id = llama_sampler_sample(smpl, m_ctx, -1);

            // 1. 判断是否为 EOG 或 Control 标记 (使用 llama 原生 API)
            if (IsControlToken(vocab, id)) {
                break;
            }

            // 接受采样 token 进采样器更新重复惩罚历史
            llama_sampler_accept(smpl, id);

            // 2. 将 token 转换成文本片段 (参考 simple-chat.cpp 使用 buf 缓冲区与 llama_token_to_piece)
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
