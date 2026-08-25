<p align="center">
  <img src="resources/logo.png" alt="LinguaAlpaca Logo" width="130" />
</p>

<h1 align="center">LinguaAlpaca · 译灵驼</h1>

<p align="center">
  “凭本地之智，见世界之全，守私密之心”
</p>

<br/>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B" alt="C++17" />
  <img src="https://img.shields.io/badge/wxWidgets-3.3+-007ACC?style=flat-square" alt="wxWidgets" />
  <img src="https://img.shields.io/badge/llama.cpp-Embedded-7B1FA2?style=flat-square" alt="llama.cpp" />
  <img src="https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square&logo=windows" alt="Platform" />
  <img src="https://img.shields.io/badge/License-MIT-2E7D32?style=flat-square" alt="License" />
</p>

**LinguaAlpaca (译灵驼)** 是一款使用 C++17 与 wxWidgets 打造的现代化、高颜值的跨平台桌面离线 AI 翻译、StarDict 离线词典与多模态 OCR 助手。项目深度整合 **llama.cpp** 原生引擎与内嵌服务（默认推荐腾讯 **Hy-MT2-1.8B-GGUF** 高性能离线翻译模型与 **PaddleOCR-VL** 视觉大模型），采用模块化清晰扁平架构，具备毫秒级快速启动、按需异步调度、全局划词悬浮气泡与完整的自动化单元测试。

---

## 🌟 核心功能与亮点 (Key Features)

- ⚡ **秒级无缝启动与自适应启动页 (Splash Screen)**：
  - 无边框物理圆角裁剪（Win32 `SetWindowRgn`），零直角溢出白边。
  - 启动 UI 毫秒级即刻显示（< 30ms），耗时的词典库与大模型初始化全部异步推进。
  - 内置 60fps 平滑缓动渐变进度条与自适应防重叠状态反馈。
- 🤖 **内嵌 `llama_server` 与 HTTP SSE 流式推理**：
  - 基于后台独立线程与动态端口分配，通过标准 HTTP SSE 协议实现 Token 级打字机流式翻译与视觉识别。
  - 严格对齐腾讯混元 Hy-MT2-1.8B 官方 ChatML 模版指令，输出纯净且准确。
- 📖 **高性能 StarDict 本地离线词典引擎**：
  - 支持 `.idx`、`.dict`、`.dict.dz`、`.ifo` 等格式的离线词典解析。
  - 支持前缀模糊联想、毫秒级二分快速检索与独立字典管理视图。
- 🖼️ **多模态 PaddleOCR-VL 视觉识别**：
  - 支持通识 OCR、表格识别、公式识别、图表识别、文本定位与印章识别等多种视觉大模型任务。
- 🖱️ **全局划词与现代悬浮翻译气泡**：
  - 基于系统原生钩子的无感全局划词捕获（`SelectionService`）。
  - Win32 Per-Pixel Alpha 分层平滑抗锯齿悬浮图标（`FloatingIconFrame`）与多显示器智能贴边避让气泡（`TranslationBubbleFrame`）。
- 🛡️ **线程安全 UI 同步机制 (`AsyncTrackable` & `BindUi`)**：
  - 针对多线程后台回调封装了 RAII 机制的 `BindUi` 辅助器与原子生命周期令牌，彻底杜绝悬空指针与跨线程 UI 崩溃。
- 🔄 **按需启动与无缝模型切换 (On-Demand Loading)**：
  - 功能页面切换时异步按需拉起并加载对应模型，实时 `/health` 探针与 Badge 徽标动态联动。
- 🎨 **现代化美学与深色模式**：
  - 极简卡片式流式布局，自绘微渐变与发光边框，完美支持 Windows Per-Monitor V2 高分屏高 DPI 缩放（`_dip` 语法糖）。

---

## 🏛 架构设计思想 (Architecture Design)

项目遵循高内聚、低耦合的模块化设计，划分为 **核心层 (Core)**、**原生引擎层 (Engine)** 与 **界面表现层 (UI)**：

```text
┌────────────────────────────────────────────────────────────────────────┐
│                               UI 表现层                                │
│   - MainFrame (主窗口与路由调度)        - SplashScreen (现代启动页)   │
│   - TextView (流式翻译视图)              - OcrView (多模态视觉识别)    │
│   - DictView (StarDict 查词视图)        - SettingsView (设置与下载)   │
│   - FloatingIcon / TranslationBubble (全局划词悬浮组件)                │
│   - AsyncTrackable / ThemeManager / IconManager (主题与线程安全设施)   │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼ (统一中枢交互调度)
┌────────────────────────────────────────────────────────────────────────┐
│                              Core 核心层                               │
│ ★ ModelManager (统一模型管理中枢: 进程生命周期 / 按需切换 / 探针 / 推理调度)│
│ ├─ LlamaServer (嵌入式 llama_server 线程与端口宿主)                    │
│ ├─ LlamaClient (OpenAI 兼容的 HTTP SSE 流式通信客户端)                 │
│ ├─ DictEngine (StarDict 离线词典索引与查询聚合引擎)                    │
│ ├─ SelectionService (全局鼠标键盘钩子与划词文本监听服务)               │
│ ├─ ConfigManager (轻量化 config.ini 持久化管理)                       │
│ ├─ Downloader (HuggingFace / 镜像源断点续传模型下载器)                 │
│ └─ Logger / Types.hpp (统一日志设施与数据结构规范)                     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    ▲
                                    │ (保留原生 C API 学习参考引擎)
┌───────────────────────────────────┴────────────────────────────────────┐
│                             Engine 引擎层                              │
│ - LlamaCppTranslationEngine (llama.cpp 原生 C API 文本翻译实现)         │
│ - LlamaCppOcrEngine (llama.cpp 原生多模态 mtmd OCR 视觉识别实现)         │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 📁 目录结构说明 (Directory Structure)

```text
src/
├── core/                    # 【核心基础层】(服务进程、通信、词典引擎、划词监听、调度中枢与配置)
│   ├── Types.hpp            # 统一数据结构 (LanguageCode, ServerStatusInfo, TranslationTask 等)
│   ├── Logger.hpp / .cpp    # 轻量化带时间戳与等级的日志系统
│   ├── Config.hpp / .cpp    # 基于 wxFileConfig 的配置管理器 (ConfigManager)
│   ├── dict/                # StarDict 词典核心引擎
│   │   └── DictEngine.hpp/.cpp # 词典解压、索引建立与多词典聚合检索
│   ├── llama/               # 嵌入式 llama_server 与 SSE 客户端
│   │   ├── LlamaServer.hpp/.cpp # 后台服务进程守护、健康探针与自动端口分配
│   │   └── LlamaClient.hpp/.cpp # 标准 HTTP SSE 流式打字机通信客户端
│   ├── ModelManager.hpp/.cpp# ★ 统一模型管理中枢 (生命周期管理、按需模型加载与推理调度)
│   ├── SelectionService.hpp/.cpp # Win32 全局划词捕获监听服务
│   └── Downloader.hpp/.cpp  # 异步 HTTP 模型断点续传下载器
│
├── engine/                  # 【原生引擎层】(保留 100% 原生 C API 离线实现，供深入学习参考)
│   ├── IEngine.hpp          # 引擎纯虚接口 (ITranslationEngine, IOcrEngine)
│   ├── LlamaCppTranslationEngine.hpp/.cpp # 原生 C API 文本翻译引擎
│   └── LlamaCppOcrEngine.hpp/.cpp         # 原生多模态 C API 视觉 OCR (mtmd) 引擎
│
├── ui/                      # 【界面展现层】(wxWidgets 现代化视图与控件体系)
│   ├── AsyncTrackable.hpp   # 跨线程 UI 回调 RAII 安全机制 (BindUi 辅助器)
│   ├── theme/               # 主题调色板、DPI 语法糖与 SVG 矢量图标库
│   │   ├── Theme.hpp        # 调色板代币规范与主题管理器 (ThemeManager)
│   │   ├── Dpi.hpp          # Modern C++ DPI 缩放语法糖 (_dip / dip)
│   │   ├── AppIcons.hpp     # 统一 SVG 矢量图标常量
│   │   └── IconManager.hpp/.cpp # SVG 矢量图标高质量抗锯齿渲染器
│   ├── widgets/             # 自定义复用组件库
│   │   ├── SplashScreen.hpp/.cpp        # ★ 物理圆角现代启动页
│   │   ├── FloatingIconFrame.hpp/.cpp   # 分层抗锯齿悬浮划词图标
│   │   ├── TranslationBubbleFrame.hpp/.cpp # 多显示器智能贴边悬浮翻译气泡
│   │   ├── CustomButton.hpp/.cpp        # 自绘制圆角胶囊按钮
│   │   ├── SidebarNav.hpp/.cpp          # 侧边导航栏 (文本, OCR, 词典, 设置)
│   │   ├── LanguageBar.hpp/.cpp         # 语言选择器与一键互换条
│   │   ├── CardPanel.hpp/.cpp           # 现代化卡片容器
│   │   ├── WelcomeModelDialog.hpp/.cpp  # 首次使用模型引导对话框
│   │   └── ImagePreviewDialog.hpp/.cpp  # 图片大图平移缩放预览对话框
│   ├── TextView.hpp/.cpp    # 文本流式翻译视图 (打字机效果、实时状态 Badge)
│   ├── OcrView.hpp/.cpp     # 图片 OCR 视觉识别视图 (拖拽上传、多类型切换)
│   ├── DictView.hpp/.cpp    # StarDict 离线词典检索与管理视图
│   ├── SettingsView.hpp/.cpp# 模型配置、词典目录与下载设置视图
│   ├── PlaceholderView.hpp  # 通用占位视图
│   └── MainFrame.hpp/.cpp   # 主窗口框架 (路由切换与按需模型加载驱动)
│
└── main.cpp                 # 应用程序主入口 (DPI 感知配置、启动页与生命周期装配)
```

---

## 🛠 快速开始与构建 (Quick Start)

### 1. 环境准备

- **操作系统**：Windows 10 / 11 (x64)
- **编译器**：Visual Studio 2022 (MSVC v143) 或更高，支持 C++17
- **构建工具**：CMake 3.20+
- **Vulkan SDK** *(可选，用于 GPU 加速推理)*

### 2. 初始化 Git 子模块

拉取项目及所有嵌套依赖（包含 `wxWidgets`、`llama.cpp` 及其第三方依赖库）：

```bash
git submodule update --init --recursive --force
```

### 3. CMake 配置与项目生成

```powershell
# 生成 Visual Studio 2022 x64 工程
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

### 4. 编译与运行主程序

```powershell
# 编译主程序
cmake --build build --config Debug --target LinguaAlpaca

# 运行主程序
.\build\bin\Debug\LinguaAlpaca.exe
```

### 5. 运行自动化单元测试

项目集成了 Catch2 单元测试套件，覆盖了核心配置、语言转换、词典加载与检索等逻辑：

```powershell
# 编译单元测试
cmake --build build --config Debug --target unit_tests

# 执行单元测试
.\build\bin\Debug\unit_tests.exe
```

---

## 📦 核心依赖与致谢

- **[wxWidgets 3.3.4](https://www.wxwidgets.org/)**：跨平台 GUI 原生组件、Direct2D/GDI+ 绘图、High-DPI 缩放及 `wxStandardPaths` 支持。
- **[llama.cpp](https://github.com/ggerganov/llama.cpp)**：提供高效的嵌入式 `llama_server`、CPU/Vulkan GPU 后端推理引擎与多模态 mtmd 视觉支持。
- **[Tencent Hy-MT2](https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF)**：腾讯开源的高性能 1.8B 离线翻译模型。
- **[PaddleOCR-VL](https://github.com/PaddlePaddle/PaddleOCR)**：多模态端到端视觉文档解析模型。
- **[nlohmann/json](https://github.com/nlohmann/json)**：现代 C++ JSON 序列化与解析库。
- **[Catch2](https://github.com/catchorg/Catch2)**：现代化 C++ 单元测试框架。
