![logo.png](properties/assets/images/logo.png)

# XRGUI

XRGUI（mo_yanXi's Retained-mode GUI）是一个实验性的 C++23 模块化保留式 GUI 库。当前实际推荐路径是 Windows + MSVC + Vulkan；默认接入层提供 GLFW + Vulkan 独立应用封装。

## 快速运行

```powershell
git submodule update --init --recursive
xmake quickstart
```

`quickstart` 会配置 MSVC debug 构建，按需生成 shader/icon，运行 `xmake doctor`，构建并启动 `xrgui.hello`。

只检查环境：

```powershell
xmake doctor
```

完整 showcase：

```powershell
xmake -b xrgui.example
xmake run xrgui.example
```

## 文档入口

| 文档 | 内容 |
|------|------|
| [快速上手](properties/showcase/quick_start.md) | 最短运行路径 |
| [GUI 使用指南](properties/showcase/user_guide.md) | 默认应用、元素创建、布局、控件、事件和 native 通信 |
| [项目介绍](PROJECT_INTRO.md) | 项目定位、模块划分、源码入口 |
| [运行展示](properties/showcase/runtime_showcase.md) | 截图和动画 |
| [富文本指南](properties/showcase/rich_text_doc.md) | 富文本 token 语法 |
| [布局速查](properties/showcase/layout_doc.md) | 布局容器使用速查 |
| [绘制流程](properties/showcase/render_spec.md) | GUI 绘制数据流概要 |
| [技术比较与路线图](properties/showcase/gui_library_comparison_and_roadmap.md) | 非 API 调研材料和发展建议 |

API 语义和实现细节优先写在源码中的 `/** */` Doxygen 注释里。文档与实际代码冲突时，以当前代码和示例为准。

## 当前默认目标

| Target | 用途 |
|--------|------|
| `xrgui.hello` | 最小 `default_application` 示例，源码在 `src.hello/main.cpp` |
| `xrgui.example` | 完整 showcase，源码在 `src.examples/` |
| `xrgui.default` | 默认库/后端对象目标 |

## 环境要求

当前阶段建议仅使用 Windows + 最新 MSVC 工具链编译和运行。CI 使用 `windows-latest`、VS 2026 Insider 和 MSVC toolset `14.52`。

必要项：

- Vulkan SDK 1.4+
- Xmake
- Python 3.11+ 或可用的 `tomllib`/`tomli`
- Node.js，用于默认 SVG icon 处理
- Slang compiler，用于默认 shader 编译；已有生成产物时 `xmake doctor` 会给出相应提示

不要默认假设 Windows 上 Clang 可用；项目大量使用 C++ 模块，当前 README 和 CI 都以 MSVC 路径为准。

## 手动构建流程

```powershell
git submodule update --init --recursive
xmake f --toolchain=msvc -m debug -y
xmake gen_slang
xmake gen_icon
xmake -b xrgui.hello
xmake run xrgui.hello
```

Release：

```powershell
xmake f --toolchain=msvc -m release -y
xmake -b xrgui.example
```

本地辅助：

```powershell
xmake switch_mode debug
xmake switch_mode release
```

资源生成是正常构建流程的一部分：

- `xmake gen_slang` 将 `properties/assets_raw/shader/slangs` 编译到 `properties/assets/shader/spv`
- `xmake gen_icon` 将 `properties/assets_raw/icons` 规范化到 `properties/assets_raw/gen/icons`
- 示例构建会根据 `properties/assets_raw/gen/**.svg` 生成 `build/.assets/includes/assets_summary.h`

## 依赖概览

Xmake packages：

- `msdfgen`
- `freetype`
- `harfbuzz`
- `nanosvg`
- `spirv-reflect`
- `gtl`
- `glfw`
- `mimalloc`
- `simdutf`（optional）

Submodules / vendored libs：

- `plf_hive`
- `small_vector`
- `beman/inplace_vector`
- `stb`
- `VulkanMemoryAllocator`
- `allocator2d`
- `mo_yanxi_react_flow`
- `mo_yanxi_vulkan_wrapper`
- `mo_yanxi_utility`

## 项目状态

XRGUI 仍处于实验阶段，API 会继续变化。当前文档整理目标是：用户用法集中在 `properties/showcase/user_guide.md`，复杂 API 语义维护在源码 Doxygen 注释中，避免 Markdown 复制过期实现细节。
