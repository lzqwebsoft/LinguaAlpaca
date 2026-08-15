# LinguaAlpaca · 译驼灵

**LinguaAlpaca (译驼灵)** 是一款使用 C++17 与 wxWidgets 打造的现代化、高颜值的跨平台桌面离线 AI 翻译与多模态 OCR 工具。项目基于 **llama.cpp** 内嵌服务（默认推荐腾讯 **Hy-MT2-1.8B-GGUF** 中英日韩高性能离线翻译模型 与 **PaddleOCR-VL-1.6** 视觉大模型），采用清晰扁平、高内聚低耦合的模块化设计，具备单一中枢调度、按需异步加载、实时健康探针及完整的自动化单元测试支持。

---

## 🌟 核心功能与亮点 (Key Features)

- 🤖 **内嵌 `llama_server` 与 HTTP SSE 流式推理**：基于后台独立线程运行的嵌入式 `llama_server`，通过标准 HTTP SSE 协议实现 Token 级打字机流式推理与视觉多模态识别。
- 🔄 **按需启动与无缝模型切换 (On-Demand Loading)**：
  - 应用启动零阻塞，仅在页面切换（文本翻译 $\leftrightarrow$ 图片 OCR）时按需拉起并加载对应模型。
  - 模型切换全异步在后台线程完成，UI 界面丝滑流畅无卡顿。
- 💓 **实时 `/health` 状态监控与 Badge 联动**：
  - 各功能页面内置轻量级探针定时轮询 `/health` 端点。
  - 顶部状态徽标（Badge）实时、平滑展示「服务就绪 / 正在加载模型 / 模型未配置 / 服务离线」等状态。
- 🎯 **腾讯 Hy-MT2 专用原生指令 Prompt 模版**：完美对齐腾讯混元 Hy-MT2-1.8B 官方 ChatML 指令结构，内置严格约束，避免生成无关对话或多余解释。
- 🖼️ **多模态 PaddleOCR-VL 视觉识别**：支持通识 OCR、表格识别、公式识别、图表识别、文本定位与印章识别等多种视觉识别任务。
- 🛡️ **防 Token 泄露与 UI 同步机制**：
  - 流式生成完成后自动以纯净 `fullText` 刷新文本控件，彻底消除 sub-token 渲染残留。
- ⏹ **一键中断推理**：翻译与 OCR 识别过程中可随时点击红色的【中断】按钮终止当前推理。
- 📥 **自动模型下载与跨平台存储**：支持一键下载官方推荐模型并显示实时进度条，模型文件保存在标准用户数据目录 (`AppData/Roaming/LinguaAlpaca/models`)。
- 🎨 **极致美学与深色模式**：遵循现代化 UI 规范，支持一键切换日间/夜间模式，窗口自绘阴影与圆角。

---

## 🏛 架构设计思想 (Architecture Design)

本项目摈弃了传统企业级开发中繁重的多层抽象与冗余 DTO 映射，参考 `llama.cpp` 与 `wxWidgets` 优秀 C++ 开源项目的架构风格，划分为 **三大核心功能模块**：

```
+-----------------------------------------------------------------------+
|                               UI 表现层                               |
|        (MainFrame, TextView, OcrView, SettingsView, widgets, theme)   |
+-----------------------------------------------------------------------+
                                   │
                                   ▼ (统一交互调度)
+-----------------------------------------------------------------------+
|                              Core 核心层                              |
| ★ ModelManager (统一模型管理中枢: 进程生命周期 / 按需切换 / 探针 / 推理调度)|
| - LlamaServer (嵌入式 llama_server 线程与端口宿主)                     |
| - LlamaClient (HTTP SSE 流式通信客户端)                                |
| - ConfigManager (轻量化 config.ini 持久化管理)                        |
| - Downloader (HuggingFace 模型断点续传下载)                           |
| - Types.hpp (统一枚举、语言工具及任务数据结构)                          |
+-----------------------------------------------------------------------+
                                   ▲
                                   │ (保留的原生 C API 离线引擎)
+-----------------------------------------------------------------------+
|                             Engine 引擎层                             |
| - LlamaCppTranslationEngine (llama.cpp 原生 C API 文本翻译实现)        |
| - LlamaCppOcrEngine (llama.cpp 原生多模态 mtmd OCR 视觉识别实现)        |
+-----------------------------------------------------------------------+
```

---

## 📁 目录结构与作用说明 (Directory Structure)

```text
src/
├── core/                    # 【核心基础层】(数据类型、服务进程、通信、调度中枢与配置)
│   ├── Types.hpp            # 统一数据结构 (LanguageCode, ServerStatus, TranslationTask 等)
│   ├── Config.hpp / .cpp    # 基于 wxFileConfig 的轻量化配置管理器 (ConfigManager)
│   ├── LlamaServer.hpp/.cpp # 嵌入式 llama_server 原生线程与 /health 探针
│   ├── LlamaClient.hpp/.cpp # OpenAI 兼容的 HTTP SSE 流式推理客户端 (LlamaClient)
│   ├── ModelManager.hpp/.cpp# ★ 统一模型管理中枢 (生命周期管理、按需模型加载与推理调度)
│   └── Downloader.hpp/.cpp  # 异步 HTTP 模型下载器 (Downloader)
│
├── engine/                  # 【原生引擎层】(保留 100% 原生 C API 离线实现，供深入学习参考)
│   ├── IEngine.hpp          # 引擎纯虚接口 (ITranslationEngine, IOcrEngine)
│   ├── LlamaCppTranslationEngine.hpp/.cpp # 原生 C API 文本翻译引擎
│   └── LlamaCppOcrEngine.hpp/.cpp         # 原生多模态 C API 视觉 OCR (mtmd) 引擎
│
├── ui/                      # 【界面展现层】(wxWidgets 现代化视图与控件体系)
│   ├── theme/               # 主题调色板与 SVG 矢量图标库
│   │   ├── Theme.hpp        # 调色板代币规范与主题管理器 (ThemeManager)
│   │   ├── AppIcons.hpp     # SVG 矢量图标常量
│   │   └── IconManager.hpp/.cpp # SVG 矢量图标渲染生成器
│   ├── widgets/             # 自定义复用组件库
│   │   ├── CustomButton.hpp/.cpp        # 自绘制圆角胶囊按钮
│   │   ├── SidebarNav.hpp/.cpp          # 侧边导航栏 (文本, OCR, 历史, 设置)
│   │   ├── LanguageBar.hpp/.cpp         # 语言选择器与一键互换条
│   │   ├── CardPanel.hpp/.cpp           # 现代化卡片容器
│   │   ├── WelcomeModelDialog.hpp/.cpp  # 首次使用模型引导对话框
│   │   └── ImagePreviewDialog.hpp/.cpp  # 图片大图平移缩放预览对话框
│   ├── TextView.hpp/.cpp    # 文本流式翻译视图 (打字机效果、实时状态 Badge)
│   ├── OcrView.hpp/.cpp     # 图片 OCR 视觉识别视图 (拖拽上传、多类型切换)
│   ├── SettingsView.hpp/.cpp# 模型配置、测试与下载设置视图
│   ├── PlaceholderView.hpp  # 通用占位视图
│   └── MainFrame.hpp/.cpp   # 主窗口框架 (路由切换与按需模型加载驱动)
│
└── main.cpp                 # 极简应用程序主入口 (wxApp 装配与启动)
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
# 编译主程序
cmake --build build --config Debug --target LinguaAlpaca

# 运行主程序
.\build\bin\Debug\LinguaAlpaca.exe
```

### 4. 运行单元测试
项目集成了 Catch2 单元测试套件，可独立编译运行：

```powershell
# 编译单元测试
cmake --build build --config Debug --target unit_tests

# 执行单元测试
.\build\bin\Debug\unit_tests.exe
```

---

## 📦 核心依赖说明

- **[wxWidgets 3.3.4](https://www.wxwidgets.org/)**：跨平台 GUI 原生组件、绘图及 `wxFileConfig` / `wxStandardPaths` 支持。
- **[llama.cpp](https://github.com/ggerganov/llama.cpp)**：提供高效的嵌入式 `llama_server`、CPU/Vulkan GPU 后端推理引擎。
- **[nlohmann/json](https://github.com/nlohmann/json)**：现代化 C++ JSON 序列化与解析库。
- **[Catch2](https://github.com/catchorg/Catch2)**：现代化 C++ 单元测试框架。
