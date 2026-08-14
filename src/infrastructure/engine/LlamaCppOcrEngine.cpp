#pragma execution_character_set("utf-8")
#include "LlamaCppOcrEngine.hpp"
#include <algorithm>
#include <chat.h>
#include <chrono>
#include <climits>
#include <common.h>
#include <llama.h>
#include <mtmd-helper.h>
#include <mtmd.h>
#include <sampling.h>
#include <vector>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/wx.h>

namespace LinguaAlpaca::Infrastructure::Engine {

static void CleanOcrTextArtifacts(std::string &str) {
  while (!str.empty() && (str.back() == ' ' || str.back() == '\n' ||
                          str.back() == '\r' || str.back() == '\t')) {
    str.pop_back();
  }
}

static std::string GetOcrPromptPrefix(const std::string &taskType) {
  if (taskType == "table")
    return "Table Recognition:";
  if (taskType == "formula")
    return "Formula Recognition:";
  if (taskType == "chart")
    return "Chart Recognition:";
  if (taskType == "spotting")
    return "Spotting:";
  if (taskType == "seal")
    return "Seal Recognition:";
  return "OCR:";
}

LlamaCppOcrEngine::LlamaCppOcrEngine() { llama_backend_init(); }

LlamaCppOcrEngine::~LlamaCppOcrEngine() {
  Cancel();
  FreeLoadedModels();
  llama_backend_free();
}

void LlamaCppOcrEngine::Cancel() {
  m_cancelRequested = true;
  if (m_workerThread.joinable()) {
    m_workerThread.join();
  }
  m_isProcessing = false;
}

void LlamaCppOcrEngine::FreeLoadedModels() {
  std::lock_guard<std::mutex> lock(m_modelMutex);
  if (m_mtmdCtx) {
    mtmd_free(m_mtmdCtx);
    m_mtmdCtx = nullptr;
  }
  if (m_model) {
    llama_model_free(m_model);
    m_model = nullptr;
  }
  m_isLoaded = false;
}

bool LlamaCppOcrEngine::LoadModel(const std::string &modelPath,
                                  const std::string &mmprojPath) {
  std::string actualModelPath = modelPath.empty() ? m_modelPath : modelPath;
  std::string actualMmprojPath = mmprojPath.empty() ? m_mmprojPath : mmprojPath;

  if (actualModelPath.empty() ||
      !wxFileExists(wxString::FromUTF8(actualModelPath)) ||
      actualMmprojPath.empty() ||
      !wxFileExists(wxString::FromUTF8(actualMmprojPath))) {
    FreeLoadedModels();
    return false;
  }

  std::lock_guard<std::mutex> lock(m_modelMutex);

  // 模型常驻比对逻辑：模型路径一致且已在内存中就绪则直接重用
  if (m_isLoaded && m_model && m_mtmdCtx && m_modelPath == actualModelPath &&
      m_mmprojPath == actualMmprojPath) {
    return true;
  }

  if (m_mtmdCtx) {
    mtmd_free(m_mtmdCtx);
    m_mtmdCtx = nullptr;
  }
  if (m_model) {
    llama_model_free(m_model);
    m_model = nullptr;
  }

  m_modelPath = actualModelPath;
  m_mmprojPath = actualMmprojPath;

  // 1. 加载 PaddleOCR-VL 文本模型
  llama_model_params mparams = llama_model_default_params();
  mparams.n_gpu_layers = 99; // CPU 运行

  m_model = llama_load_model_from_file(actualModelPath.c_str(), mparams);
  if (!m_model) {
    m_isLoaded = false;
    return false;
  }

  // 2. 初始化 mtmd 视觉多模态 Context (使用 AUTO 模式自动协商 Flash
  // Attention，防止在 Vulkan 上生成 NaN)
  mtmd_context_params mtmd_params = mtmd_context_params_default();
  mtmd_params.use_gpu = true;
  mtmd_params.warmup = true;
  mtmd_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;

  m_mtmdCtx =
      mtmd_init_from_file(actualMmprojPath.c_str(), m_model, mtmd_params);
  if (!m_mtmdCtx) {
    llama_model_free(m_model);
    m_model = nullptr;
    m_isLoaded = false;
    return false;
  }

  m_isLoaded = true;
  return true;
}

void LlamaCppOcrEngine::RecognizeStream(
    const std::string &imagePath, const std::string &taskType,
    const std::string &modelPath, const std::string &mmprojPath,
    Domain::Repository::OcrTokenCallback onToken,
    Domain::Repository::OcrCompleteCallback onComplete) {

  Cancel(); // 确保先前任务已安全结束

  std::string actualModelPath = modelPath.empty() ? m_modelPath : modelPath;
  std::string actualMmprojPath = mmprojPath.empty() ? m_mmprojPath : mmprojPath;

  m_cancelRequested = false;
  m_isProcessing = true;

  m_workerThread = std::thread([this, imagePath, taskType, actualModelPath,
                                actualMmprojPath, onToken, onComplete]() {
    // 1. 确保模型常驻内存
    if (!LoadModel(actualModelPath, actualMmprojPath)) {
      m_isProcessing = false;
      if (onComplete) {
        onComplete("", false,
                   "PaddleOCR-VL 主模型或 mmproj "
                   "视觉投影器未能成功加载，请检查文件路径");
      }
      return;
    }

    if (imagePath.empty() || !wxFileExists(wxString::FromUTF8(imagePath))) {
      m_isProcessing = false;
      if (onComplete) {
        onComplete("", false, "待识别图像文件不存在 (" + imagePath + ")");
      }
      return;
    }

    std::lock_guard<std::mutex> lock(m_modelMutex);

    const struct llama_vocab *vocab = llama_model_get_vocab(m_model);

    int n_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (n_threads <= 0)
      n_threads = 4;

    // 2. 创建临时 Context (设置 AUTO 模式使 Vulkan 安全适配 Attention
    // 算法，物理微批次设为 512)
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 8192;
    cparams.n_batch = 2048;
    cparams.n_ubatch = 512;
    cparams.n_threads = n_threads;
    cparams.n_threads_batch = n_threads;
    cparams.offload_kqv = true;
    cparams.op_offload = true;
    cparams.kv_unified = true;
    cparams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;

    llama_context *ctx = llama_new_context_with_model(m_model, cparams);
    if (!ctx) {
      m_isProcessing = false;
      if (onComplete) {
        onComplete("", false, "创建 llama_context 失败");
      }
      return;
    }

    // 3. 遵从 AGENTS.md 规范：清空 KV 缓存
    llama_memory_t mem = llama_get_memory(ctx);
    llama_memory_clear(mem, true);

    // 4. 读取并解析图像为 mtmd_bitmap (对齐 mtmd-cli.cpp load_media 流程)
    mtmd_helper_bitmap_wrapper bmp_wrap =
        mtmd_helper_bitmap_init_from_file(m_mtmdCtx, imagePath.c_str(), false);
    if (!bmp_wrap.bitmap) {
      llama_free(ctx);
      m_isProcessing = false;
      if (onComplete) {
        onComplete("", false, "无法读取或解析图像文件 (" + imagePath + ")");
      }
      return;
    }

    // 5. 提取 GGUF 元数据中的 chat_template 构建 Prompt 并 Token 化
    const char *marker = mtmd_get_marker(m_mtmdCtx);
    std::string markerStr =
        (marker && strlen(marker) > 0) ? marker : mtmd_default_marker();
    std::string userContent = markerStr + "\n" + GetOcrPromptPrefix(taskType);

    std::string promptStr;
    auto chat_templates = common_chat_templates_init(m_model, "");
    if (chat_templates) {
      common_chat_templates_inputs inputs;
      inputs.messages = {{"user", userContent}};
      inputs.add_generation_prompt = true;
      inputs.use_jinja = true;
      promptStr =
          common_chat_templates_apply(chat_templates.get(), inputs).prompt;
    }

    if (promptStr.empty()) {
      promptStr = std::string("<|im_start|>user\n") + userContent +
                  "<|im_end|>\n<|im_start|>assistant\n";
    }

    mtmd_input_chunks *chunks = mtmd_input_chunks_init();
    mtmd_input_text txt_input;
    txt_input.text = promptStr.c_str();
    txt_input.text_len = promptStr.size();
    txt_input.add_special = true;
    txt_input.parse_special = true;

    const mtmd_bitmap *bitmaps_arr[1] = {bmp_wrap.bitmap};
    int tok_res = mtmd_tokenize(m_mtmdCtx, chunks, &txt_input, bitmaps_arr, 1);

    if (tok_res != 0) {
      mtmd_bitmap_free(bmp_wrap.bitmap);
      mtmd_input_chunks_free(chunks);
      llama_free(ctx);
      m_isProcessing = false;
      if (onComplete) {
        onComplete("", false, "mtmd_tokenize 图像与 Prompt Token 化失败");
      }
      return;
    }

    mtmd_bitmap_free(bmp_wrap.bitmap);

    // 6. 逐 Chunk 编码媒体 Embedding 与解码 (完全参照 mtmd-cli.cpp eval_message
    // 流程 L276-L360)
    llama_pos n_past = 0;
    size_t n_chunks = mtmd_input_chunks_size(chunks);
    bool eval_success = true;
    mtmd_batch *mbatch = nullptr;

    for (size_t i = 0; i < n_chunks; i++) {
      if (m_cancelRequested.load()) {
        eval_success = false;
        break;
      }

      auto chunk = mtmd_input_chunks_get(chunks, i);
      auto chunk_type = mtmd_input_chunk_get_type(chunk);

      if (chunk_type == MTMD_INPUT_CHUNK_TYPE_TEXT) {
        llama_pos new_n_past = n_past;
        int res =
            mtmd_helper_eval_chunk_single(m_mtmdCtx, ctx, chunk, n_past, 0,
                                          2048, i == n_chunks - 1, &new_n_past);
        if (res != 0) {
          eval_success = false;
          break;
        }
        n_past = new_n_past;
      } else {
        // 媒体/图像 Chunk：优先尝试从当前已编码的 mbatch 中获取 Embedding
        // (实现向量复用)
        float *embd = nullptr;
        if (mbatch) {
          embd = mtmd_batch_get_output_embd(mbatch, chunk);
        }

        // 若未在当前 mbatch 中（缓存未命中），则新建 mbatch 并合并后续媒体
        // Chunk 批量编码
        if (!embd) {
          if (mbatch) {
            mtmd_batch_free(mbatch);
            mbatch = nullptr;
          }

          mbatch = mtmd_batch_init(m_mtmdCtx);
          if (!mbatch) {
            eval_success = false;
            break;
          }

          if (mtmd_batch_add_chunk(mbatch, chunk) != 0) {
            eval_success = false;
            break;
          }

          // 批处理合并后续的连续图像 Chunk
          for (size_t j = i + 1; j < n_chunks; j++) {
            auto next_chunk = mtmd_input_chunks_get(chunks, j);
            if (mtmd_input_chunk_get_type(next_chunk) ==
                MTMD_INPUT_CHUNK_TYPE_TEXT) {
              break;
            }
            if (mtmd_batch_add_chunk(mbatch, next_chunk) != 0) {
              break;
            }
          }

          int enc_res = mtmd_batch_encode(mbatch);
          if (enc_res != 0) {
            eval_success = false;
            break;
          }

          embd = mtmd_batch_get_output_embd(mbatch, chunk);
          if (!embd) {
            eval_success = false;
            break;
          }
        }

        llama_pos new_n_past = n_past;
        int dec_res = mtmd_helper_decode_image_chunk(
            m_mtmdCtx, ctx, chunk, embd, n_past, 0, 2048, &new_n_past, nullptr,
            nullptr);

        if (dec_res != 0) {
          eval_success = false;
          break;
        }
        n_past = new_n_past;
      }
    }

    if (mbatch) {
      mtmd_batch_free(mbatch);
      mbatch = nullptr;
    }

    mtmd_input_chunks_free(chunks);

    if (!eval_success) {
      llama_free(ctx);
      m_isProcessing = false;
      if (onComplete) {
        onComplete("", false, "mtmd 图像与文本 Chunk 特征向量编码评估失败");
      }
      return;
    }

    // 7. 构建 Sampler 采样链 (严格对齐 mtmd-cli.cpp common_sampler
    // 贪婪采样模式)
    common_params_sampling sparams;
    sparams.temp = 0.0f; // 贪婪采样
    common_sampler *smpl = common_sampler_init(m_model, sparams);

    std::vector<llama_token> generated_tokens;
    int maxTokens = INT_MAX;
    llama_pos current_n_past = n_past;

    // 预初始化批处理结构，避免循环内部频繁分配/释放内存 (对齐 common_batch
    // 机制)
    llama_batch batch = llama_batch_init(1, 0, 1);

    bool decode_error = false;

    // 8. 逐 Token 解码推理循环 (严格参照 mtmd-cli.cpp generate_response 流程
    // L194-L224)
    for (int i = 0; i < maxTokens; i++) {
      if (m_cancelRequested.load()) {
        break;
      }

      llama_token id = common_sampler_sample(smpl, ctx, -1);
      generated_tokens.push_back(id);
      common_sampler_accept(smpl, id,
                            true); // 遵从 AGENTS.md 规范：更新 Sampler 历史

      if (llama_vocab_is_eog(vocab, id)) {
        break;
      }

      // 使用 common_token_to_piece 转为 String 并进行 Control Token 过滤 (对齐
      // mtmd-cli.cpp L209)
      std::string piece_str = common_token_to_piece(vocab, id, true);

      if (onToken) {
        onToken(piece_str);
      }

      if (m_cancelRequested.load()) {
        break;
      }

      // 对齐 mtmd-cli.cpp generate_response 解码流程：复用 batch 并传入递增的
      // current_n_past++ 位置 (L218-L220)
      common_batch_clear(batch);
      common_batch_add(batch, id, current_n_past++, {0}, true);

      if (llama_decode(ctx, batch)) {
        decode_error = true;
        break;
      }
    }

    llama_batch_free(batch);

    // 参照 mtmd-cli.cpp L226，使用 common_detokenize 完整解码整个生成的 Token
    // 序列，消除流式切片漏字或多字节乱码
    std::string fullText = common_detokenize(vocab, generated_tokens);

    // 9. 释放临时 Context 资源，保留常驻 m_model 与 m_mtmdCtx
    common_sampler_free(smpl);
    llama_free(ctx);

    m_isProcessing = false;

    if (m_cancelRequested.load()) {
      if (onComplete)
        onComplete(fullText, false, "已取消");
    } else if (decode_error) {
      CleanOcrTextArtifacts(fullText);
      if (onComplete)
        onComplete(fullText, false, "解码 Token 失败 (llama_decode error)");
    } else {
      CleanOcrTextArtifacts(fullText);
      if (onComplete)
        onComplete(fullText, true, "");
    }
  });
}

} // namespace LinguaAlpaca::Infrastructure::Engine
