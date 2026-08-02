# LinguaAlpaca Agent Guidelines & Technical Insights

This file records project-scoped rules, developer guidelines, and architectural lessons learned for agents working on the **LinguaAlpaca** codebase.

---

## 🤖 1. Native `llama.cpp` Engine Rules

- **Zero Mocking / No Dummy String Matching**: All translation features must execute 100% native `llama.cpp` C API calls (`llama_tokenize`, `llama_decode`, `llama_sampler_sample`). Never introduce dummy string matching fallbacks in production engine code.
- **Mandatory KV Memory Reset**: Before starting a new tokenization/decoding sequence, ALWAYS clear context KV memory using:
  ```cpp
  llama_memory_t mem = llama_get_memory(m_ctx);
  llama_memory_clear(mem, true);
  ```
  *Rationale*: Failure to clear KV memory causes position sequence contamination from prior runs, leading to infinite token repeating loops.
- **Sampler Chain Accept History**: When sampling tokens in a loop, invoke `llama_sampler_accept(smpl, id)` immediately after sampling to update penalty context for `repetition_penalty`.

---

## 🎯 2. Model Prompt Alignment (Tencent Hy-MT2)

- **Official Native Instruction Format**: Specialized machine translation models (such as `tencent/Hy-MT2-1.8B-GGUF`) expect their fine-tuned ChatML directive template. Use Chinese instruction directives without default system prompt:
  ```text
  <|im_start|>user
  将以下{src_lang}文本翻译为{target_lang}，注意只需要输出翻译后的结果，不要额外解释：

  {src_text}<|im_end|>
  <|im_start|>assistant
  ```
- **Avoid Verbose Meta-Instructions**: Do not use verbose English meta-prompts on Chinese-tuned MT models. English instructions can confuse the model into entering general LLM chat mode rather than dedicated translation mode.

---

## 🛡️ 3. Control Token Anti-Leakage & UI Sync Architecture

- **NEVER Check `LLAMA_TOKEN_ATTR_USER_DEFINED`**:
  - In SentencePiece / BPE tokenizers, thousands of standard vocabulary words, punctuation marks, and Chinese characters carry the `USER_DEFINED` attribute.
  - Checking `(attr & LLAMA_TOKEN_ATTR_USER_DEFINED)` will cause premature translation abortion mid-sentence!
- **Multi-Layer Control Token Filtration (`IsChatMLControlToken`)**:
  - Combine `llama_vocab_is_eog`, `llama_vocab_is_control`, `id == llama_vocab_eot(vocab)`, `id == llama_vocab_eos(vocab)`, and raw text substring checks (`im_end`, `im_start`, `endoftext`, `<|im_`).
- **UI Completion Sync (`TextTranslationView`)**:
  - During streaming, real-time `onToken` callbacks append text pieces to the UI text control (`AppendText`).
  - Upon task completion, the `onComplete(success, fullText)` callback **MUST** synchronize the final text box value using `SetValue(cleanFullText)`. This eliminates any partial sub-token leakage that rendered during streaming.

---

## 🛠️ 4. Build & Environment Operations

- **Windows MSVC LNK1168 File Locking**:
  - When `LinguaAlpaca.exe` is running, MSVC linker returns `fatal error LNK1168: 无法打开 LinguaAlpaca.exe 进行写入`.
  - Always terminate running instances before rebuilding:
    ```powershell
    taskkill /F /IM LinguaAlpaca.exe
    ```
- **Unit Testing**:
  - All engine and service modifications must pass Catch2 unit tests (`.\build\bin\Debug\unit_tests.exe`).
  - In Catch2 v2 multi-file setups, `#define CATCH_CONFIG_MAIN` must appear in only ONE test source file (`TranslationServiceTest.cpp`).
