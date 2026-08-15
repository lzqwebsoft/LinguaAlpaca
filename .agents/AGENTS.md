# LinguaAlpaca Agent Guidelines & Technical Insights

This file records project-scoped rules, developer guidelines, and architectural lessons learned for agents working on the **LinguaAlpaca** codebase.

---

## 🤖 1. Native `llama.cpp` Engine Rules

- **Zero Mocking / No Dummy String Matching**: All translation features must execute 100% native `llama.cpp` API calls (`llama_server` / C API). Never introduce dummy string matching fallbacks in production engine code.
- **Mandatory KV Memory Reset**: Before starting a new tokenization/decoding sequence in direct C API code, ALWAYS clear context KV memory using:
  ```cpp
  llama_memory_t mem = llama_get_memory(m_ctx);
  llama_memory_clear(mem, true);
  ```
  *Rationale*: Failure to clear KV memory causes position sequence contamination from prior runs, leading to infinite token repeating loops.
- **Sampler Chain Accept History**: When sampling tokens in a loop, invoke `llama_sampler_accept(smpl, id)` immediately after sampling to update penalty context for `repetition_penalty`.

---

## 🎯 2. Model Prompt Alignment (Tencent Hy-MT2 & Vision OCR)

- **Official Native Instruction Format**: Specialized machine translation models (such as `tencent/Hy-MT2-1.8B-GGUF`) expect their fine-tuned ChatML directive template. Use Chinese instruction directives without default system prompt:
  ```text
  <|im_start|>user
  将以下{src_lang}文本翻译为{target_lang}，注意只需要输出翻译后的结果，不要额外解释：

  {src_text}<|im_end|>
  <|im_start|>assistant
  ```
- **Vision OCR Prompt Prefixes**: For multimodal models (such as PaddleOCR-VL), map tasks cleanly:
  - `table` $\rightarrow$ `Table Recognition:`
  - `formula` $\rightarrow$ `Formula Recognition:`
  - `chart` $\rightarrow$ `Chart Recognition:`
  - `spotting` $\rightarrow$ `Spotting:`
  - `seal` $\rightarrow$ `Seal Recognition:`
  - default $\rightarrow$ `OCR:`
- **Avoid Verbose Meta-Instructions**: Do not use verbose English meta-prompts on Chinese-tuned models. English instructions confuse the model into entering general chat mode.

---

## 🛡️ 3. Control Token Anti-Leakage & UI Sync Architecture

- **NEVER Check `LLAMA_TOKEN_ATTR_USER_DEFINED`**:
  - In SentencePiece / BPE tokenizers, thousands of standard vocabulary words, punctuation marks, and Chinese characters carry the `USER_DEFINED` attribute.
  - Checking `(attr & LLAMA_TOKEN_ATTR_USER_DEFINED)` will cause premature translation abortion mid-sentence!
- **UI Completion Sync & Thread-Safe Callbacks (`TextView` & `OcrView`)**:
  - **Mandatory `wxWeakRef`**: In all async streaming (`onToken`) and completion (`onComplete`) callbacks from background worker threads, **ALWAYS** capture `wxWeakRef<MyView> weakSelf(this)` rather than raw `this`. Inside `wxTheApp->CallAfter`, verify `if (!weakSelf || !weakSelf->m_ctrl) return;` before updating UI controls to prevent access violation crashes (`0xC0000005`) if the view is repainted, re-created, or in transition.
  - During streaming, real-time `onToken` callbacks append text pieces to the UI text control (`AppendText`).
  - Upon task completion, the `onComplete(success, fullText)` callback **MUST** synchronize the final text box value using `SetValue(cleanFullText)` via `wxTheApp->CallAfter`. This eliminates any partial sub-token leakage that rendered during streaming.

---

## 🏛️ 4. ModelManager & Modular Architecture Rules

- **Modular Three-Pillar Architecture (`core/`, `engine/`, `ui/`)**:
  - Inspired by `llama.cpp` and `wxWidgets` source design, the codebase avoids over-engineered enterprise layering (such as multi-tier DDD/clean architecture abstractions, pass-through services, or redundant DTO copies).
  - `src/core/`: Contains fundamental types ([Types.hpp](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/core/Types.hpp)), configuration ([Config.hpp](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/core/Config.hpp)), server runner ([LlamaServer.hpp](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/core/LlamaServer.hpp)), SSE client ([LlamaClient.hpp](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/core/LlamaClient.hpp)), downloader ([Downloader.hpp](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/core/Downloader.hpp)), and the central hub ([ModelManager.hpp](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/core/ModelManager.hpp)).
  - `src/engine/`: Fully retained native C API implementations ([LlamaCppTranslationEngine](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/engine/LlamaCppTranslationEngine.hpp), [LlamaCppOcrEngine](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/engine/LlamaCppOcrEngine.hpp)) as clean learning reference engines.
  - `src/ui/`: Presentation views ([MainFrame](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/ui/MainFrame.hpp), [TextView](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/ui/TextView.hpp), [OcrView](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/ui/OcrView.hpp), [SettingsView](file:///e:/backup/cpp_workspaces/LinguaAlpaca/src/ui/SettingsView.hpp)), widgets, and theme styling.
- **Single Application Hub (`ModelManager`)**:
  - `ModelManager` is the single application coordinator for model lifecycle, on-demand loading, health probes, and inference dispatch.
  - Views (`TextView`, `OcrView`, `SettingsView`) interact directly with `ModelManager`.
- **Strict Infrastructure Separation in Core**:
  - `LlamaServer` (`src/core/LlamaServer.hpp`): Strictly manages the background `llama_server` thread, TCP port allocation, and raw HTTP `/health` probes. It remains 100% agnostic of high-level UI tabs or business features.
  - `LlamaClient` (`src/core/LlamaClient.hpp`): Pure HTTP SSE client for `/v1/chat/completions`. It must NOT manage process restarts or server lifecycles.
- **On-Demand Asynchronous Model Loading**:
  - When `MainFrame` switches navigation tabs (`OnNavChanged`), trigger `m_modelManager->EnsureModelAsync(TargetModelType)` in a background thread. Never execute synchronous model switching on the main UI thread!
- **Real-Time `/health` UI Badge Synchronization**:
  - Views must use a lightweight `wxTimer` (e.g. 1500ms interval) to query `m_modelManager->GetHealthStatus(TargetModelType)` and update status badge colors and labels dynamically without blocking.
- **OCR VRAM Offload Strategy**:
  - Multimodal vision models (`PaddleOCR-VL` + `mmproj`) allocate substantial memory for high-resolution image embeddings. On Vulkan devices with $\le 8\text{GB}$ VRAM, full offload (`ngl=99`) causes `vk::OutOfDeviceMemoryError`.
  - Default `ocrGpuLayers` to `0` (CPU processing) in `AppConfig` while setting translation `gpuLayers` to `99`.

---

## 🛠️ 5. Build, Environment & Submodule Patching

- **Third-Party Submodule Patches**:
  - Never dirty `third_party/llama.cpp` directly in Git.
  - Store standard unified diffs in `patches/` (e.g., `patches/llama-pr-19357.patch`).
  - CMake automatically applies patches during configure time using `git apply --check`.
- **Windows MSVC LNK1168 File Locking**:
  - When `LinguaAlpaca.exe` is running, MSVC linker returns `fatal error LNK1168: 无法打开 LinguaAlpaca.exe 进行写入`.
  - Always terminate running instances before rebuilding:
    ```powershell
    taskkill /F /IM LinguaAlpaca.exe
    ```
- **Unit Testing with wxWidgets Runtime**:
  - In Catch2 test runners that instantiate components calling `ConfigManager` (which relies on `wxStandardPaths`), always initialize wxWidgets runtime via `wxInitializer initializer;` inside `main(int argc, char* argv[])` with `#define CATCH_CONFIG_RUNNER`.
  - Run tests with `.\build\bin\Debug\unit_tests.exe`.
