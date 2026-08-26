<p align="center">
  <img src="resources/logo.png" alt="LinguaAlpaca Logo" width="130" />
</p>

<h1 align="center">LinguaAlpaca · 译灵驼</h1>

<p align="center">
  “凭本地之智，见世界之全 —— 端侧多模态全能离线翻译助手”
</p>

<br/>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B" alt="C++17" />
  <img src="https://img.shields.io/badge/wxWidgets-3.3+-007ACC?style=flat-square" alt="wxWidgets" />
  <img src="https://img.shields.io/badge/llama.cpp-Embedded-7B1FA2?style=flat-square" alt="llama.cpp" />
  <img src="https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square&logo=windows" alt="Platform" />
  <img src="https://img.shields.io/badge/License-MIT-2E7D32?style=flat-square" alt="License" />
</p>

**LinguaAlpaca (译灵驼)** 是一款基于 C++17 与 wxWidgets 打造的现代化、高颜值桌面离线 AI 翻译助手。项目深度整合 **llama.cpp** 原生引擎与内嵌后台服务（推荐搭载腾讯 **Hy-MT2-1.8B-GGUF** 高性能离线翻译模型与 **PaddleOCR-VL** 视觉大模型），集**端侧大模型流式翻译**、**多模态 OCR 视觉解析**（支持剪贴板截图实时粘贴 Ctrl+V）、**StarDict 本地百万词典秒查**与**系统级全局划词悬浮气泡**于一体。具备毫秒级秒开启动、按需异步模型装载、全文 TTS 语音朗读与多主题无缝切换，全流程坚持 **100% 本地离线计算**，彻底杜绝数据外泄，严密守护隐私安全。

---

## 🏛 架构设计思想 (Architecture Design)

项目遵循高内聚、低耦合的模块化设计，划分为 **核心层 (Core)**、**原生引擎层 (Engine)** 与 **界面表现层 (UI)**：

```text
┌────────────────────────────────────────────────────────────────────────┐
│                               UI 表现层                                │
│   - MainFrame (主窗口与路由调度)        - SplashScreen (现代启动页)   │
│   - TextView (流式翻译视图)              - OcrView (多模态视觉识别)    │
│   - DictView (StarDict 查词视图)        - LogView (日志诊断监控视图)  │
│   - SettingsView (设置与下载)           - WelcomeModelDialog (引导弹窗)│
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
│ ├─ ScreenTextExtractor (UIAutomation / 剪贴板双通道文本提取器)        │
│ ├─ ClipboardHelper / WinTtsHelper / WinMediaOcrHelper (系统能力封装)   │
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
│   ├── ClipboardHelper.hpp/.cpp # Win32 剪贴板保护读写辅助工具
│   ├── ScreenTextExtractor.hpp/.cpp # 屏幕划词多通道文本提取器
│   ├── SelectionService.hpp/.cpp # Win32 全局划词捕获监听服务
│   ├── WinUIAutomationHelper.hpp/.cpp # Windows UI Automation 无障碍选区提取
│   ├── WinMediaOcrHelper.hpp/.cpp     # Windows 原生 OCR 提取辅助
│   ├── WinTtsHelper.hpp/.cpp          # Windows SAPI / WinRT 离线语音合成朗读
│   ├── dict/                # StarDict 词典核心引擎
│   │   ├── DictEngine.hpp/.cpp   # 词典解压、索引建立与多词典聚合检索
│   │   └── DictFormatter.hpp/.cpp# Pango/MediaWiki/Kingsoft 等字典标记富文本解析
│   ├── llama/               # 嵌入式 llama_server 与 SSE 客户端
│   │   ├── LlamaServer.hpp/.cpp  # 后台服务进程守护、健康探针与自动端口分配
│   │   └── LlamaClient.hpp/.cpp  # 标准 HTTP SSE 流式打字机通信客户端
│   ├── ModelManager.hpp/.cpp# ★ 统一模型管理中枢 (生命周期管理、按需模型加载与推理调度)
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
│   │   ├── AppIcons.hpp     # 统一 SVG 矢量图标常量规范
│   │   └── IconManager.hpp/.cpp # SVG 矢量图标高质量抗锯齿渲染器
│   ├── widgets/             # 自定义复用组件库
│   │   ├── SplashScreen.hpp/.cpp        # ★ 现代自适应启动页
│   │   ├── WelcomeModelDialog.hpp/.cpp  # 首次使用模型配置引导对话框
│   │   ├── AboutDialog.hpp/.cpp         # 官方关于与主页介绍对话框
│   │   ├── FloatingIconFrame.hpp/.cpp   # 分层抗锯齿悬浮划词图标
│   │   ├── TranslationBubbleFrame.hpp/.cpp # 多显示器智能贴边悬浮翻译气泡
│   │   ├── CustomButton.hpp/.cpp        # 自绘制圆角胶囊按钮
│   │   ├── CustomChoice.hpp/.cpp        # 自绘制圆角下拉选择框
│   │   ├── CustomInputBox.hpp/.cpp      # 自绘制文本输入框
│   │   ├── TextCtrl.hpp/.cpp            # 现代化多行富文本编辑器
│   │   ├── CardPanel.hpp/.cpp           # 现代化卡片容器组件
│   │   ├── StatusBadge.hpp/.cpp         # 实时服务状态彩色徽标
│   │   ├── SidebarNav.hpp/.cpp          # 侧边导航栏 (文本, OCR, 词典, 日志, 设置)
│   │   ├── LanguageBar.hpp/.cpp         # 语言选择器与一键互换条
│   │   ├── ScrollBar.hpp/.cpp           # 现代化细条滚动条
│   │   ├── SplitterWindow.hpp/.cpp      # 分割窗口容器
│   │   ├── SuggestListBox.hpp/.cpp      # 词典前缀补全下拉列表
│   │   └── ImagePreviewDialog.hpp/.cpp  # 图片大图平移缩放预览对话框
│   ├── TextView.hpp/.cpp    # 文本流式翻译视图 (打字机效果、实时状态 Badge)
│   ├── OcrView.hpp/.cpp     # 图片 OCR 视觉识别视图 (拖拽/剪贴板粘贴上传、多任务切换)
│   ├── DictView.hpp/.cpp    # StarDict 离线词典检索与管理视图
│   ├── LogView.hpp/.cpp     # 系统运行与服务诊断实时日志视图
│   ├── SettingsView.hpp/.cpp# 模型配置、硬件加速、划词、词典与主题设置视图
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
- **Vulkan SDK** _(可选，用于 GPU 加速推理)_

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

项目集成了 Catch2 单元测试套件，覆盖核心配置、语言转换、词典加载与检索、划词提取与坐标计算等关键逻辑：

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
- **[PaddleOCR-VL](https://huggingface.co/PaddlePaddle/PaddleOCR-VL-1.6-GGUF)**：多模态端到端视觉文档解析模型。
- **[StarDict 离线词典库](https://stardict.uber.space/)**：免费丰富的跨语言离线词典资源。
- **[nlohmann/json](https://github.com/nlohmann/json)**：现代 C++ JSON 序列化与解析库。
- **[Catch2](https://github.com/catchorg/Catch2)**：现代化 C++ 单元测试框架。
