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
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20macOS-0078D6?style=flat-square" alt="Platform" />
  <img src="https://img.shields.io/badge/License-MIT-2E7D32?style=flat-square" alt="License" />
</p>

**LinguaAlpaca (译灵驼)** 是一款基于 **C++17** 与 **wxWidgets** 打造的现代化、高颜值、高性能桌面离线 AI 翻译助手。项目深度内嵌 **llama.cpp** 原生后端引擎（推荐搭载腾讯 **Hy-MT2-1.8B-GGUF** 高质量离线翻译大模型与 **PaddleOCR-VL** 多模态视觉模型），集**端侧大模型流式打字翻译**、**多模态 OCR 视觉解析**（支持截图实时粘贴与识别后一键翻译）、**StarDict 本地百万词典秒查**与**系统级全局划词悬浮气泡**于一体。

具备启动秒开、按需模型热切换、离线 TTS 语音朗读与深浅调色板热更新。全流程坚持 **100% 本地离线计算**，彻底杜绝隐私与敏感数据外泄风险。

---

## 🏛 架构设计思想 (Architecture Design)

项目遵循三层模块化架构规范，划分为 **UI 表现层**、**Core 核心层** 与 **Engine 原生引擎层**：

```text
┌────────────────────────────────────────────────────────────────────────┐
│                               UI 表现层                                │
│   - MainFrame (主窗口与路由调度)        - SplashScreen (现代启动页)   │
│   - TextView (流式翻译视图)              - OcrView (多模态视觉识别)    │
│   - DictView (StarDict 查词视图)        - LogView (日志诊断监控视图)  │
│   - SettingsView (设置与下载)           - WelcomeModelDialog (引导弹窗)│
│   - FloatingIcon / TranslationBubble (全局划词悬浮球与智能贴边翻译气泡)│
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
│ ├─ AppVersion (跨平台统一应用版本获取: Info.plist / CMake 宏)         │
│ ├─ Downloader (HuggingFace / 镜像源断点续传模型下载器)                 │
│ ├─ TableParser / MarkdownFormatter (表格与富文本解析引擎)             │
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
LinguaAlpaca/
├── CMakeLists.txt           # 根 CMake 构建配置文件 (定义统一工程版本与编译目标)
├── resources/               # 应用图标、Plist 模板与 Windows 资源文件
│   ├── app_icon.icns        # macOS 高清多尺寸应用图标
│   ├── app_icon.ico         # Windows 高清多分辨率应用图标
│   ├── app_icon.png         # 通用应用图标 PNG 资源
│   ├── logo.png             # 灵驼品牌 Logo
│   ├── Info.plist.in        # macOS App Bundle 元信息与权限配置模板
│   └── app.rc               # Windows 原生 PE 资源描述文件
│
├── patches/                 # llama.cpp 第三方库本地自动补丁
├── scripts/                 # 跨平台构建与打包脚本 (含 macOS .dmg 自动化流水线)
├── tests/                   # Catch2 自动化单元测试套件
│
└── src/                     # 源代码主目录
    ├── core/                # 【核心基础层】(服务进程、通信、词典引擎、划词监听、调度中枢与配置)
    │   ├── Types.hpp        # 统一数据结构 (LanguageCode, ServerStatusInfo, TranslationTask 等)
    │   ├── Logger.hpp/.cpp  # 轻量化带时间戳与等级的日志系统
    │   ├── Config.hpp/.cpp  # 基于 wxFileConfig 的轻量化配置管理器 (ConfigManager)
    │   ├── AppVersion.hpp/.cpp # 跨平台统一应用发布版本获取 (Info.plist / CMake 宏)
    │   ├── ClipboardHelper.hpp/.cpp # 跨平台剪贴板安全读写与文本保护工具
    │   ├── ScreenTextExtractor.hpp/.cpp # 屏幕划词多通道文本提取器
    │   ├── SelectionContext.hpp # 划词事件上下文与几何坐标数据结构
    │   ├── SelectionService.hpp/.cpp # 全局划词捕获监听服务 (Win32 Hook / macOS Monitor)
    │   ├── WinUIAutomationHelper.hpp/.cpp # Windows UI Automation 原生选区提取
    │   ├── WinMediaOcrHelper.hpp/.cpp     # Windows 原生 OCR 提取辅助
    │   ├── WinTtsHelper.hpp/.cpp          # 离线语音合成朗读 (Windows SAPI/WinRT & macOS AVFoundation)
    │   ├── ModelManager.hpp/.cpp# ★ 统一模型管理中枢 (生命周期管理、按需模型加载与推理调度)
    │   ├── Downloader.hpp/.cpp  # 异步 HTTP 模型断点续传下载器
    │   ├── dict/            # StarDict 词典核心引擎
    │   │   ├── DictEngine.hpp/.cpp   # 词典解压、索引建立与多词典聚合检索
    │   │   └── DictFormatter.hpp/.cpp# Pango/MediaWiki/Kingsoft 等字典标记富文本解析
    │   ├── llama/           # 嵌入式 llama_server 与 SSE 客户端
    │   │   ├── LlamaServer.hpp/.cpp  # 后台服务进程守护、健康探针与自动端口分配
    │   │   └── LlamaClient.hpp/.cpp  # 标准 HTTP SSE 流式打字机通信客户端
    │   ├── table/           # 表格结构分析与转换模块
    │   │   └── TableParser.hpp/.cpp  # Markdown 表格解析、语音描述生成与 Excel 格式化
    │   └── markdown/        # 富文本轻量解析渲染模块
    │       └── MarkdownFormatter.hpp/.cpp # 文本格式化与排版清洗
    │
    ├── engine/              # 【原生引擎层】(保留 100% 原生 C API 离线实现，供深入学习参考)
    │   ├── IEngine.hpp      # 引擎纯虚接口 (ITranslationEngine, IOcrEngine)
    │   ├── LlamaCppTranslationEngine.hpp/.cpp # 原生 C API 文本翻译引擎
    │   └── LlamaCppOcrEngine.hpp/.cpp         # 原生多模态 C API 视觉 OCR (mtmd) 引擎
    │
    ├── ui/                  # 【界面展现层】(wxWidgets 现代化视图与自研控件体系)
    │   ├── AsyncTrackable.hpp # 跨线程 UI 回调 RAII 安全机制 (BindUi 辅助器)
    │   ├── MainFrame.hpp/.cpp # 主窗口框架 (路由切换、无边框窗体控制与按需模型加载驱动)
    │   ├── TextView.hpp/.cpp  # 文本流式翻译视图 (打字机效果、实时状态 Badge、快捷朗读)
    │   ├── OcrView.hpp/.cpp   # 图片 OCR 视觉识别视图 (拖拽/剪贴板粘贴上传、6大模式、一键联动翻译)
    │   ├── DictView.hpp/.cpp  # StarDict 离线词典检索与管理视图 (实时前缀推荐补全)
    │   ├── LogView.hpp/.cpp   # 系统运行与服务诊断实时日志视图 (等级过滤、导出清空)
    │   ├── SettingsView.hpp/.cpp # 模型配置、硬件加速、划词、词典与主题偏好设置
    │   ├── theme/           # 主题调色板、DPI 语法糖与 SVG 矢量图标库
    │   │   ├── Theme.hpp    # 调色板代币规范与主题管理器 (ThemeManager)
    │   │   ├── ThemeFont.hpp# 跨平台全局排版字体规范与角色分级 (ThemeFont)
    │   │   ├── Dpi.hpp      # Modern C++ DPI 缩放语法糖 (_dip / dip)
    │   │   ├── AppIcons.hpp # 统一 SVG 矢量图标常量库
    │   │   ├── IconManager.hpp/.cpp # SVG 矢量图标高质量抗锯齿渲染器
    │   │   └── PlatformThemeHelper.hpp/.cpp # 原生系统主题与深色窗口外观辅助器
    │   └── widgets/         # 自定义复用组件库
    │       ├── SplashScreen.hpp/.cpp        # ★ 现代自适应渐变启动页
    │       ├── AppTaskBarIcon.hpp/.cpp      # 系统托盘与状态栏菜单控制器
    │       ├── FloatingIconFrame.hpp/.cpp   # 分层抗锯齿悬浮划词图标
    │       ├── TranslationBubbleFrame.hpp/.cpp # 智能多屏贴边悬浮翻译气泡 (折叠/TTS/缩放)
    │       ├── CardPanel.hpp/.cpp           # 现代化卡片容器组件 (支持多视图切换)
    │       ├── CustomButton.hpp/.cpp        # 自绘制圆角胶囊按钮 (支持多种风格与矢量图标)
    │       ├── CustomChoice.hpp/.cpp        # 自绘制圆角下拉选择框
    │       ├── CustomInputBox.hpp/.cpp      # 自绘制文本输入框 (前缀图标与清除按钮)
    │       ├── CustomTableView.hpp/.cpp     # 自绘制轻量表格数据视图 (支持 Excel 复制)
    │       ├── TextCtrl.hpp/.cpp            # 现代化多行富文本编辑器 (内置平滑细滚动条)
    │       ├── ScrollBar.hpp/.cpp           # 现代化自绘制细条圆角滑动条
    │       ├── SidebarNav.hpp/.cpp          # 侧边导航栏 (文本, OCR, 词典, 日志, 设置)
    │       ├── StatusBadge.hpp/.cpp         # 实时服务状态彩色徽标
    │       ├── LanguageBar.hpp/.cpp         # 语言选择器与一键互换工具条
    │       ├── SuggestListBox.hpp/.cpp      # 词典前缀补全下拉浮动推荐列表
    │       ├── SplitterWindow.hpp/.cpp      # 弹性分割窗口容器
    │       ├── ImagePreviewDialog.hpp/.cpp  # 图片大图平移滚轮缩放预览对话框
    │       ├── WelcomeModelDialog.hpp/.cpp  # 首次使用模型配置引导对话框
    │       └── AboutDialog.hpp/.cpp         # 官方关于与版本信息对话框
    │
    └── main.cpp             # 应用程序主入口 (DPI 感知配置、启动页初始化与应用生命周期装配)
```

---

## 🛠 快速开始与构建 (Quick Start)

### 1. 初始化 Git 子模块

拉取项目及所有嵌套依赖（包含 `wxWidgets`、`llama.cpp` 及其第三方依赖库）：

```bash
git submodule update --init --recursive --force
```

---

### Windows 构建与运行指南

#### 1. 环境准备
- **操作系统**：Windows 10 / 11 (x64)
- **编译器**：Visual Studio 2022 (MSVC v143) 或更高版本，支持 C++17
- **构建工具**：CMake 3.20+
- **GPU 加速**：Vulkan SDK _(可选，用于 Windows 下 Vulkan GPU 加速推理)_

#### 2. CMake 配置与工程生成
```powershell
# 生成 Visual Studio 2022 x64 解决方案
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

#### 3. 编译与运行主程序
```powershell
# 编译主程序 (推荐 Release 配置以获得最佳端侧大模型推理性能)
cmake --build build --config Release --target LinguaAlpaca

# 运行主程序
.\build\bin\Release\LinguaAlpaca.exe
```
> **提示**：若进行代码调试，可切换为 `--config Debug` 编译运行。

#### 4. 运行自动化单元测试
项目集成了 Catch2 单元测试套件，全面覆盖核心配置、语言转换、词典加载与索引、划词提取与几何坐标计算等关键业务逻辑：
```powershell
# 编译并运行单元测试
cmake --build build --config Release --target unit_tests
.\build\bin\Release\unit_tests.exe
```

---

### macOS 构建与运行指南

#### 1. 环境准备
- **操作系统**：macOS 12.0+ (Monterey / Ventura / Sonoma / Sequoia)，原生支持 Apple Silicon (M 系列芯片) 及 Intel x86_64
- **编译器**：Apple Clang / Xcode Command Line Tools (`xcode-select --install`)，支持 C++17
- **构建工具**：CMake 3.20+ (`brew install cmake`)，推荐搭配 Ninja (`brew install ninja`) 提升并行构建速度
- **GPU 加速**：Metal _(系统原生集成，`llama.cpp` 原生 Metal 后端开箱即用，无需额外安装 SDK)_

#### 2. CMake 配置与工程生成
```bash
# 推荐方式：使用 Ninja (构建速度极快)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 或使用标准 Unix Makefiles
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 或生成 Xcode 解决方案工程
cmake -S . -B build -G Xcode
```

#### 3. 编译与运行主程序
```bash
# 编译主程序 (推荐 Release 配置以获得最佳端侧推理性能，利用多核并行加速)
cmake --build build --config Release --target LinguaAlpaca -j$(sysctl -n hw.ncpu)

# 运行主程序
./build/bin/LinguaAlpaca
# （若使用 Xcode 生成器，产物位于 ./build/bin/Release/LinguaAlpaca）
```
> **提示**：若进行代码调试，可切换为 `-DCMAKE_BUILD_TYPE=Debug` (Ninja/Makefiles) 或 `--config Debug` (Xcode) 编译运行。

#### 4. 运行自动化单元测试
项目集成了 Catch2 单元测试套件，全面覆盖核心配置、语言转换、词典加载与索引、划词提取与几何坐标计算等关键业务逻辑：
```bash
# 编译并运行单元测试
cmake --build build --config Release --target unit_tests -j$(sysctl -n hw.ncpu)
./build/bin/unit_tests
```

#### 5. 独立应用包与 DMG 镜像打包 (Packaging & Release)
项目已内置全自动化的 macOS 自包含打包流水线。打包过程会自动：
- 嵌入实体 `llama-server` 引擎二进制（消除软链接）；
- 递归内嵌 `wxWidgets`、`llama.cpp`、`OpenSSL 3` 等全套动态库至 `Contents/Frameworks/`，并自动通过 `install_name_tool` 完成 `@rpath` 路径重定向（彻底解除对本地开发环境与 Homebrew 路径的依赖）；
- 注入规范的 `Info.plist`（包含中文展示名“译灵驼”、Retina 高分屏支持、系统暗黑外观以及辅助功能/屏幕录制权限声明）；
- 递归完成 Apple Silicon 平台严格要求的 Ad-hoc 代码签名，消除系统 AMFI 门禁校验闪退；
- 制作带 `/Applications` 拖拽安装软链接的标准压缩版 `.dmg` 磁盘镜像。

```bash
# 推荐方式：通过 CMake 构建目标一键打包
cmake --build build --config Release --target package_mac

# 或直接运行打包脚本
./scripts/package_mac.sh build
```

**输出交付物（位于 `build/dist/`）**：
- **`LinguaAlpaca.app`**：完全独立、开箱即用的 macOS 原生应用包（约 89 MB）。
- **`LinguaAlpaca-1.0.3-macOS.dmg`**：体积高度优化的标准分发安装镜像（约 30 MB），可直接对外分发给任何 Mac 用户。

---

## 核心依赖与致谢

- **[wxWidgets 3.3.4](https://www.wxwidgets.org/)**：现代化跨平台 GUI 原生组件框架、Direct2D/GDI+ 渲染与 High-DPI 缩放支持。
- **[llama.cpp](https://github.com/ggerganov/llama.cpp)**：提供超高吞吐量的嵌入式 `llama_server`、CPU/Vulkan GPU 后端推理引擎与多模态 mtmd 视觉架构。
- **[Tencent Hy-MT2](https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF)**：腾讯开源的 1.8B 高性能通用机器翻译大模型。
- **[PaddleOCR-VL](https://huggingface.co/PaddlePaddle/PaddleOCR-VL-1.6-GGUF)**：百度开源的高精度端到端多模态视觉文档解析大模型。
- **[StarDict 词典生态](https://stardict.uber.space/)**：提供庞大丰富的离线双语字典生态与高速索引数据。
- **[nlohmann/json](https://github.com/nlohmann/json)**：现代 C++ 工业级 JSON 序列化与反序列化库。
- **[Catch2](https://github.com/catchorg/Catch2)**：现代化 C++ 单元测试与断言框架。
