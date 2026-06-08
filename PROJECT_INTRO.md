# XRGUI 项目介绍

XRGUI（mo_yanXi's Retained-mode GUI）是一个实验性的 C++23 模块化保留式 GUI 库。当前实际支持和验证重点是 Windows + MSVC + Vulkan；默认窗口接入使用 GLFW + Vulkan，最小可运行入口是 `xrgui.hello`。

本文只保留项目级概要。GUI 的使用方式集中在 `properties/showcase/user_guide.md`；布局、scene、element、默认应用等细节请优先查看对应源码中的 `/** */` Doxygen 注释。

## 主要定位

- 保留式 GUI，面向命令式 C++ 构建元素树。
- GUI core 不拥有窗口，默认配置层提供 GLFW + Vulkan 独立应用封装。
- 元素自带布局、事件、样式和绘制入口。
- 默认渲染后端基于 Vulkan，使用抽象绘制指令和 GPU 侧解析/绘制流程。
- GUI 可作为应用的一部分运行，并通过队列与窗口线程、渲染提交线程交换任务。

## 当前推荐使用路径

最小运行：

```powershell
git submodule update --init --recursive
xmake quickstart
```

最小应用代码从 `src.hello/main.cpp` 开始；完整 GUI 使用说明见：

- `properties/showcase/quick_start.md`
- `properties/showcase/user_guide.md`

完整 showcase 目标是 `xrgui.example`，源码入口为 `src.examples/main.cpp` 和 `src.examples/gui.examples.cpp`。

## 架构分层

| 层 | 位置 | 职责 |
|----|------|------|
| GUI Core | `src/gui/core/` | 元素、scene、布局、事件、样式、绘制前端 |
| GUI Ext | `src/gui/ext/` | 文本、图像、输入框、资源管理等扩展元素 |
| Compound | `src/gui/compound/` | 复合控件，如拾色器、文件选择器、数据表格 |
| Default Config | `gui.config/default/` | 默认应用封装、默认样式、字体、内置资源 |
| Backends | `src.backends/` | 通用 main loop、Vulkan renderer、GLFW 窗口适配 |
| Examples | `src.hello/`, `src.examples/` | 最小示例和完整 showcase |

## 关键源码入口

| 文件 | 内容 |
|------|------|
| `gui.config/default/default_application.ixx` | 独立应用推荐入口 |
| `src/gui/core/infrastructure/scene.ixx` | scene、任务队列、overlay、native communicator |
| `src/gui/core/infrastructure/element.ixx` | 元素基类、事件钩子、布局通知、线程调度 |
| `src/gui/core/layout/policy.ixx` | layout policy、size category、runtime restriction |
| `src/gui/core/layout/cell.ixx` | Cell 类型和 builder API |
| `src/gui/core/elements/` | 基础布局容器和控件 |
| `src/gui/ext/elements/` | 文本、图片、复选框等扩展控件 |
| `src.examples/gui.examples.cpp` | showcase 中各页面的实际使用代码 |

## 文档索引

| 文档 | 用途 |
|------|------|
| `README.md` | 项目概览、卖点、快速入口 |
| `docs/build-and-development.md` | 环境、手动构建、生成资产、目标和依赖 |
| `properties/showcase/quick_start.md` | 最短运行路径 |
| `properties/showcase/user_guide.md` | 集中的 GUI 使用指南 |
| `properties/showcase/runtime_showcase.md` | 运行效果截图 |
| `properties/showcase/layout_doc.md` | 布局容器使用速查 |
| `properties/showcase/rich_text_doc.md` | 富文本语法 |
| `properties/showcase/render_spec.md` | 渲染流程概要 |
| `properties/showcase/gui_library_comparison_and_roadmap.md` | 非 API 调研材料和发展建议 |

## 状态说明

项目仍处于实验阶段，API 会继续变化。文档与代码冲突时，以当前实际代码和示例为准；面向 API 的详细说明应维护在源码 Doxygen 注释中，避免在 Markdown 中复制实现细节。
