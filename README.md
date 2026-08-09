# LinguaAlpaca · 译灵

**LinguaAlpaca (译灵)** 是一款使用 C++17 与 wxWidgets 打造的现代化、高颜值的跨平台桌面离线 AI 翻译软件。项目基于 **llama.cpp** 原生 C API 驱动本地 GGUF 大模型（默认推荐腾讯 **Hy-MT2-1.8B-GGUF** 中英日韩高性能离线翻译模型），严格遵循 **领域驱动设计 (DDD)** 与 **干净架构 (Clean Architecture)** 思想构建，具备高扩展性、模块化解耦及完整的单元测试支持。

---

## 🌟 核心功能与亮点 (Key Features)

- 🤖 **原生 llama.cpp 本地大模型推理引擎**：基于 `llama.cpp` 官方 C API，实现 `llama_tokenize` Token 化、`llama_decode` 批解码与 `llama_sampler_chain` 采样链推理。
- 🎯 **腾讯 Hy-MT2 专用原生指令 Prompt 模版**：完美对齐腾讯混元 Hy-MT2-1.8B 官方 ChatML 指令结构，内置`，注意只需要输出翻译后的结果，不要额外解释：`严格约束，避免生成无关对话或英文回答。
- 🛡️ **三重防 Token 泄露与多句完整性屏蔽架构**：
  - **KV 内存重置**：每次翻译前调用 `llama_memory_clear` 清空上下文 KV Cache，彻底消除跨请求重复死循环。
  - **BPE 特征与 EOT 特殊标记深度拦截**：精确判断 `llama_vocab_eot` / `llama_vocab_eos` 及 BPE 切分的 `<|im_start|>`、`<|im_end|>`，在采样源头截断。
  - **前端 UI 覆盖同步**：流式生成完成后强制用裁切干净的 `fullText` 刷新 UI 文本框，绝对无 `<|im_end|>` 遗留。
- ⚙️ **精准的采样超参数链配置**：
  | 超参数 (Hyperparameter) | 配置值 (Configured Value) | 说明 |
  | :--- | :--- | :--- |
  | `temperature` | `0.7` | 平衡输出随机性与翻译稳定性 |
  | `top_p` | `0.6` | 避免尾部低概率字词干扰 |
  | `top_k` | `20` | 限制候选 Token 采样范围 |
  | `repetition_penalty` | `1.05` | 避免多句翻译重复输出 |
  | `max_tokens` | `4096` | 允许长文本与段落一次性翻译 |
- ⏹ **红色【中断翻译】按键**：翻译生成过程中一键中断 `llama.cpp` 输出，实时终止流式 Token 采样。
- 📥 **自动模型下载与跨平台目录管理**：内置一键下载腾讯 `Hy-MT2-1.8B-Q4_K_M.gguf` (~1.2GB) 模型，带 UI 实时百分比进度条。模型文件存储于系统标准数据目录 (`AppData/Roaming/LinguaAlpaca/models`)，免特权且升级软件永不丢失。
- 💾 **配置持久化存盘框架 (`config.ini`)**：使用 wxWidgets 原生 `wxFileConfig`，应用启动时自动恢复上次保存的模型路径、多语言偏好与主题配置。
- 🎨 **极致美学与深色模式**：像素级对齐现代化 UI 规范，支持一键切换黑暗/白天模式，窗口启动自动居中屏中。

---

## 🏛 架构设计思想 (Architecture Design)

本项目采用分层架构设计，确保 **界面 (UI)**、**应用用例 (Application)**、**领域核心逻辑 (Domain)** 与 **底层基础设施 (Infrastructure)** 完全解耦：

```
+-----------------------------------------------------------------------+
|                         Presentation Layer (UI)                       |
| (MainFrame, SidebarNav, TextTranslationView, SettingsView, CustomUI)  |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                         Application Layer                             |
| (TranslationService, ConfigurationService, ThemeManager, DTOs)        |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                           Domain Layer                                |
| (Entities: TranslationTask, AppConfig, Language, Engine Interface)    |
+-----------------------------------------------------------------------+
                                   ^
                                   | (Dependency Inversion 依赖倒置)
+-----------------------------------------------------------------------+
|                        Infrastructure Layer                           |
| (LlamaCppEngine, ModelDownloader, IniConfigRepository, HistoryRepo)   |
+-----------------------------------------------------------------------+
```

---

## 📁 目录结构与作用说明 (Directory Structure)

```
LinguaAlpaca/
├── CMakeLists.txt                  # 顶层 CMake 构建配置文件 (配置第三方依赖、/utf-8 选项及构建目标)
├── README.md                       # 项目主文档
├── tests/                          # 单元测试模块
│   ├── CMakeLists.txt              # Catch2 单元测试 CMake 配置
│   ├── TranslationServiceTest.cpp  # TranslationService 业务用例测试
│   └── LlamaCppEngineTest.cpp      # LlamaCppTranslationEngine 离线引擎测试
├── src/                            # 源代码根目录
├── main.cpp                    # wxApp 应用程序主入口 (配置读取、模型加载、依赖注入与窗口启动)
├── domain/                     # 【1. 领域层 - Domain Layer】(纯业务实体与核心接口，零 UI 依赖)
│   ├── model/                  # 领域模型实体与值对象
│   │   ├── Language.hpp        # 语言枚举 (LanguageCode)、元数据与转换工具
│   │   ├── TranslationTask.hpp # 翻译任务实体 (包含原文、译文、状态、时间戳等)
│   │   ├── HistoryRecord.hpp   # 翻译历史记录模型
│   │   ├── AppConfig.hpp       # 应用配置模型 (ModelPath, ThemeMode, AutoRead 等)
│   │   └── AppTheme.hpp        # 应用主题枚举 (Light / Dark)
│   └── repository/             # 领域服务与仓储抽象接口
│       ├── ITranslationEngine.hpp # 翻译引擎抽象接口 (定义同步/流式翻译与加载接口)
│       └── IHistoryRepository.hpp # 历史记录持久化抽象接口
├── application/                # 【2. 应用层 - Application Layer】(用例 Orchestration & DTO)
│   ├── dto/                    # 数据传输对象
│   │   └── TranslationDto.hpp  # 请求 (TranslationRequestDto) 与响应 (TranslationResponseDto)
│   └── service/                # 应用服务
│       ├── TranslationService.hpp / .cpp     # 翻译用例服务 (调度引擎与历史记录)
│       ├── ConfigurationService.hpp / .cpp   # 配置持久化服务 (调度 INI 配置文件)
│       └── ThemeManager.hpp                  # 全局深色/浅色主题状态管理器
├── infrastructure/             # 【3. 基础设施层 - Infrastructure Layer】(技术细节实现)
│   ├── engine/                 # 翻译引擎实现
│   │   ├── LlamaCppTranslationEngine.hpp / .cpp # 本地 llama.cpp LLM 引擎 (原生 C API + Sampler Chain)
│   │   └── MockTranslationEngine.hpp / .cpp     # 单元测试专用智能 Mock 引擎
│   ├── downloader/             # 资源下载基础设施
│   │   └── ModelDownloader.hpp / .cpp            # 异步 HTTP 模型下载服务 (带百分比进度回调)
│   └── repository/             # 持久化仓储实现
│       ├── IniConfigRepository.hpp / .cpp       # 基于 wxFileConfig 的 config.ini 持久化仓储
│       └── InMemoryHistoryRepository.hpp / .cpp # 线程安全的内存历史记录仓储
└── presentation/               # 【4. 表现层 - Presentation Layer】(wxWidgets 高颜值界面)
    ├── theme/                  # 界面主题与样式代币
    │   └── ThemeColors.hpp     # 浅色/深色模式动态调色板、字号与圆角规范
    ├── components/             # 自定义现代化 UI 控件
    │   ├── CustomButton.hpp / .cpp         # 自绘制圆角胶囊按钮 (支持 Primary/Secondary/Green/Danger 样式)
    │   ├── SidebarNav.hpp / .cpp           # 左侧导航侧边栏 (划词, 文本, OCR, 历史, 设置)
    │   ├── LanguageSelectorBar.hpp / .cpp  # 双语选择器与一键交换按钮栏
    │   ├── CardPanel.hpp / .cpp            # 带有工具栏的现代阴影卡片容器
    │   └── WelcomeModelDialog.hpp / .cpp   # 原生欢迎与模型未配置提示模态对话框
    └── views/                  # 功能视图与主窗口
        ├── TextTranslationView.hpp / .cpp  # 文本/划词翻译主视图 (含红色的 [中断翻译] 按钮)
        ├── SettingsView.hpp / .cpp         # 离线模型配置、推荐下载与偏好设置视图 (基于 wxScrolledWindow)
        ├── MainFrame.hpp / .cpp            # 应用程序主窗口 (包含 Header, 居中显示与拖动)
        └── PlaceholderView.hpp             # 占位视图
```

---

## 🛠 快速开始与构建 (Quick Start)

### 1. 初始化 Git 子模块
拉取项目及所有嵌套依赖（包含 `wxWidgets` 及其第三方依赖库）：

```bash
git submodule update --init --recursive --force
```

### 2. CMake 配置与项目生成 (Windows / Visual Studio)
使用 CMake 生成 Visual Studio 工程：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
```

### 3. 编译与运行主程序

```powershell
# 编译 Release/Debug 主程序
cmake --build build --config Debug --target LinguaAlpaca

# 运行主程序
.\build\bin\Debug\LinguaAlpaca.exe
```

### 4. 运行单元测试
项目集成了 Catch2 单元测试套件（覆盖 TranslationService 与 LlamaCppTranslationEngine），可独立编译运行：

```powershell
# 编译单元测试
cmake --build build --config Debug --target unit_tests

# 执行单元测试
.\build\bin\Debug\unit_tests.exe
```

---

## 📦 依赖项说明

- **[wxWidgets 3.3.4](https://www.wxwidgets.org/)**：提供跨平台 GUI 原生组件、绘图及 `wxFileConfig` / `wxStandardPaths` 支持。
- **[llama.cpp](https://github.com/ggerganov/llama.cpp)**：提供高效的 CPU/GPU 本地 LLM 大模型 C API 推理支持。
- **[Catch2](https://github.com/catchorg/Catch2)**：轻量级现代化 C++ 单元测试框架。
