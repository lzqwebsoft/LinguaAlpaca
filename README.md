# LinguaAlpaca

这是一个使用 CMake 管理的 C++ 项目，集成了 wxWidgets 和 llama.cpp 两个第三方库。

## 目录结构

- `CMakeLists.txt`：顶层 CMake 构建配置
- `src/main.cpp`：示例 wxWidgets 应用程序
- `third_party/wxWidgets/`：wxWidgets 源码或子模块
- `third_party/llama.cpp/`：llama.cpp 源码或子模块
- `build/`：CMake 生成的构建目录

## 快速开始

1. 初始化子模块（如果尚未拉取）：

```bash
git submodule update --init --recursive
```

2.  创建构建目录并配置项目：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
```

3. 构建项目：

```powershell
cmake --build build --config Release
```

> 说明：如果你使用其他生成器，请将 `-G` 参数替换为对应的 Visual Studio 版本或 Ninja 等。

## 依赖说明

- `wxWidgets`：用于图形界面
- `llama.cpp`：用于模型/推理相关功能

## 代码说明

示例 `src/main.cpp` 是一个最小的 wxWidgets 桌面应用程序，包含菜单、状态栏和事件绑定。
