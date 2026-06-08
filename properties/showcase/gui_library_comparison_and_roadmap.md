# XRGUI 与常见 C/C++ GUI 库对比及发展建议

> 调研日期：2026-06-04  
> 目标：从代码实现、性能、易用程度、库能力、生态和许可证等维度，对比 XRGUI 与常见 C/C++ GUI 库，并给出本库后续发展建议。
>  
> 注：本文是调研和路线图材料，不是 API 文档。实际使用方式以 `README.md`、`properties/showcase/user_guide.md` 和源码 Doxygen 注释为准。

---

## 1. 总体结论

XRGUI 当前不应被定位为 Qt、wxWidgets 或 GTK 的通用替代品。它更接近一个面向高性能桌面程序、渲染引擎、可视化工具和复杂内部工具的 **Vulkan 保留式 UI 组件**：不拥有窗口、不强制运行在主线程、输出到渲染附件，并把布局、样式、异步任务和 GPU 绘制指令组织进自己的元素树。

与主流库相比，XRGUI 的核心优势是：

- **保留式元素树 + 每帧即时响应**：比 Dear ImGui/Nuklear 更适合长期存在、结构复杂、需要自动布局和异步更新的 UI。
- **渲染管线深度可控**：默认后端直接服务 Vulkan Image、Compute Shader、后处理和合成器，更适合嵌入已有渲染管线。
- **C++ 类型系统驱动样式与布局**：样式树、Palette、布局策略、Cell 元数据都在 C++ 内表达，避免外部样式表解析和字符串协议。
- **线程和异步设计更激进**：GUI 可独立线程运行，输入、native 通信、元素异步任务、Action 队列和数据流系统已经纳入架构。

同时，XRGUI 当前最大短板也很明确：

- **接入门槛高**：C++23 模块、MSVC、Vulkan SDK、Slang、Python、Node.js、xmake、生成资源流程共同抬高了首次运行成本。
- **平台与后端覆盖不足**：当前实际建议路径是 Windows + 最新 MSVC + Vulkan；这与 Qt/wxWidgets/GTK/JUCE 的跨平台成熟度差距很大。
- **控件、文本、可访问性和国际化仍不完整**：基础控件、富文本、文本输入已有基础，但与 Qt/GTK/JUCE 的生产级文本编辑、IME、RTL、辅助功能、平台对话框等能力仍有距离。
- **生态与验证弱**：缺少官方包、稳定 API 边界、回归测试、截图测试和跨 GPU 性能基准；虽然已有 `xrgui.hello`，但第一小时体验仍需要继续打磨。

因此，后续建议不是追求“比 Qt 更全”，而是先把 XRGUI 做成 **高性能渲染应用中的保留式 GUI 选择**：比 Dear ImGui 更结构化，比 RmlUi 更 C++ 类型安全，比 Qt Quick 更容易嵌入自有 Vulkan 管线。

---

## 2. XRGUI 当前基线

| 维度 | 当前状态 |
| --- | --- |
| 编程模型 | C++23 模块；命令式构建；保留式元素树；每帧重绘并支持局部重录 |
| 渲染模型 | 默认 Vulkan 后端；抽象绘制指令；Compute Shader 生成顶点/索引；后处理合成器 |
| 窗口模型 | 核心不拥有窗口；默认提供 GLFW + Vulkan 后端 |
| 布局 | 元素内建布局能力；序列、表格、网格、flex-wrap、缩放栈、split pane、scroll pane、collapser 等 |
| 样式 | C++ 样式组合树；状态化 Palette；样式族/变体；Nine-Patch；样式指标反馈布局 |
| 输入与线程 | 外部事件队列；多焦点模型；GUI 线程可独立运行；native 通信通过 dispatcher 回到窗口线程 |
| 文本 | FreeType/HarfBuzz/MSDF 路线；富文本和文本输入已有基础；LTR 支持较完整，RTL/Bidi 仍需补齐 |
| 控件 | 按钮、选择按钮、翻板、进度条、拖动条、滚动面板、菜单、复选框、图像框、标签、文本输入、拾色器、文件选择、Markdown/CSV 展示等 |
| 许可证 | BSD-2-Clause |
| 成熟度 | 实验阶段；当前建议仅 Windows + 最新 MSVC + Vulkan |

本库内部文档中给出的性能目标是：在 RTX 4080 Laptop、2K 分辨率场景下，纯 UI 绘制到单个颜色附件约 0.1ms，含样例后处理和合成器总计约 1ms。这个数字说明 XRGUI 的渲染路径具备竞争力，但还不能直接等同于跨设备、跨驱动、跨场景的基准结论；后续需要可复现 benchmark。

---

## 3. 横向对比矩阵

### 3.1 实现模型与性能取向

| 库 | 实现模型 | 渲染/绘制路径 | 性能取向 | 与 XRGUI 的关系 |
| --- | --- | --- | --- | --- |
| Qt Widgets | `QObject/QWidget` 对象树、Signal/Slot、`QLayout` | 平台窗口 + QPainter/Backing Store/样式系统 | 通用桌面稳定性、完整控件和平台服务 | Qt Widgets 更成熟完整；XRGUI 更适合嵌入 Vulkan 渲染附件 |
| Qt Quick | QML/对象树 + Qt Quick Scene Graph | Scene Graph 经 RHI 映射到 OpenGL/Vulkan/Metal/D3D | GPU 加速、批处理、跨平台产品 UI | Qt Quick 是 XRGUI 最接近的成熟对标；XRGUI 更底层、更 C++、更偏自有渲染管线 |
| Dear ImGui | Immediate Mode；每帧代码声明 UI | 输出顶点缓冲和 draw command list，由后端渲染 | 快速工具、低接入成本、实时调试 | XRGUI 应学习其后端边界、示例和接入体验；保留式布局是 XRGUI 的差异点 |
| Nuklear | ANSI C single-header Immediate Mode | 输出抽象 draw command；宿主翻译到任意后端 | 极小依赖、固定内存、可嵌入 | Nuklear 更轻；XRGUI 能力更复杂但接入成本高得多 |
| wxWidgets | C++ 包装平台原生控件 | 调用 Win32/Cocoa/GTK 等原生控件 | 原生外观、稳定桌面应用 | wxWidgets 赢在 native 和成熟；XRGUI 赢在自绘 GPU UI 与渲染集成 |
| GTK/gtkmm | C/GObject 对象系统；gtkmm C++ wrapper | GDK 窗口抽象 + GSK rendering abstraction；CSS 节点 | Linux/GNOME 生态、GTK4 GPU 渲染 | GTK 更像完整平台工具包；XRGUI 是自有渲染层组件 |
| FLTK | C++ Widget 树、简单事件循环、坐标式布局 | 自绘控件；支持 OpenGL 窗口 | 小、静态链接友好、简单 | FLTK 是轻量桌面控件库；XRGUI 是高端 Vulkan 自绘库 |
| LVGL | C 对象树、样式、事件、invalid area | draw buffer/tiled rendering/flush callback；可接 2D GPU | MCU/嵌入式、低内存、显示驱动适配 | LVGL 的脏区和显示驱动模型值得借鉴；目标硬件与 XRGUI 相反 |
| RmlUi | RML/RCSS DOM + C++ 接口 | CompileGeometry + RenderInterface；应用自带 renderer | HTML/CSS 工作流、游戏/引擎 UI | RmlUi 赢在内容工作流；XRGUI 赢在 C++ 类型安全和自有 Vulkan 管线 |
| JUCE | C++ Component 树；音频/插件框架 | 自绘 Component；平台/插件 wrapper；Direct2D 等路径 | 音频应用、插件格式、DSP/GUI 一体 | JUCE 的专长是音频和插件；XRGUI 暂不应竞争这一垂直生态 |
| NanoGUI | C++/Python 小型 Widget 库 | OpenGL/GLES/Metal/WebGL，基于 GLFW/NanoVG | 科研/可视化的小型 UI | NanoGUI 易集成但能力有限；XRGUI 更重、更面向复杂 UI |
| libui-ng | C API 包装平台原生控件 | Win32/Cocoa/GTK 原生控件 | 小型 native GUI | libui-ng 仍是 mid-alpha；XRGUI 不是 native wrapper 方向 |

### 3.2 能力与易用性

| 库 | 控件覆盖 | 布局/样式 | 文本/I18N/可访问性 | 上手难度 | 典型适用场景 |
| --- | --- | --- | --- | --- | --- |
| Qt | 极强 | Widgets 布局 + QML/Qt Quick + Designer | 极强 | 中到高，生态完整但体系大 | 商业桌面、嵌入式 Linux、移动、复杂产品 |
| Dear ImGui | 工具控件强，终端用户控件弱 | 简单布局、Docking 分支、扩展生态 | 官方明确不支持完整 I18N 和可访问性 | 很低 | 游戏引擎工具、调试器、编辑器、实时可视化 |
| wxWidgets | 桌面常用控件强 | Sizer 布局、native look | 借助平台原生能力 | 中 | 原生桌面应用 |
| GTK/gtkmm | Linux/GNOME 控件强 | CSS 节点、LayoutManager、Builder | GTK/Pango/AT-SPI 生态强 | C/gtkmm 学习曲线中到高 | Linux 桌面、跨平台开源应用 |
| FLTK | 常用控件中等 | 简单 resize/layout；FLUID 生成 C++ | 基础 Unicode/UTF-8，复杂文本较弱 | 低到中 | 小工具、教学、轻量桌面程序 |
| LVGL | 嵌入式控件强 | Flex/Grid，CSS 启发样式 | 嵌入式范围内较强 | 中，显示/输入驱动适配成本高 | MCU、仪表、HMI、嵌入式 Linux |
| Nuklear | 基础 immediate 控件完整 | 行/列布局、手写控制 | UTF-8 基础，复杂文本弱 | 低，后端需自己接 | C 项目、小型工具、嵌入式/引擎 overlay |
| RmlUi | 文档/表单/窗口类 UI 强 | HTML/CSS 风格；DOM/样式表 | 取决于后端和字体输入接口 | 中，需接 Render/System/File 接口 | 游戏 UI、引擎 UI、设计师参与的界面 |
| JUCE | 音频相关 GUI 强 | Component、FlexBox/Grid、LookAndFeel | 音频产品需求覆盖较强 | 中，音频生态友好 | DAW、插件、音频工具 |
| XRGUI | 基础控件和复杂展示已有雏形 | 响应式布局、C++ 样式树、GPU 指令 | LTR/富文本已有基础，RTL/辅助功能待补 | 高 | Vulkan 工具、渲染应用、自有引擎 UI、复杂内部工具 |

---

## 4. 分库深入分析

### 4.1 Qt

**代码实现。** Qt 有两条主要 GUI 路线：Qt Widgets 和 Qt Quick。Qt Widgets 以 `QObject/QWidget` 对象树、Signal/Slot、事件处理虚函数和 `QLayout` 管理器为核心；Qt Quick 则使用 QML 对象树和 Scene Graph，把可渲染节点保留在图形场景中，并经 RHI 适配 OpenGL、Vulkan、Metal、Direct3D。Qt 的实现层包含元对象系统、资源系统、平台抽象、Designer/Creator 工具链和大量平台服务。

**性能。** Qt Widgets 的性能优势主要来自成熟控件、平台优化和更新合并；Qt Quick 的性能优势来自 Scene Graph、批处理和保留节点。与 XRGUI 相比，Qt Quick 的成熟度和平台覆盖更强，但它是完整应用框架，不是为“把 UI 画到宿主 Vulkan Image 上并交给宿主合成器”这个场景设计的。

**易用程度。** Qt 对新应用非常友好：安装器、文档、Designer、Creator、CMake 支持完整。但它的体系也大：MOC、Signal/Slot、QML、对象生命周期、许可证和部署都需要学习。XRGUI 目前在首次运行上明显更难，但对于已有 Vulkan 渲染框架，XRGUI 的底层边界更直接。

**库能力。** Qt 在控件、文本、输入法、国际化、无障碍、平台对话框、网络、数据库、WebEngine、图表、测试等方面远超 XRGUI。XRGUI 不应短期竞争这些完整能力，应专注“高性能渲染内嵌 UI”。

**对 XRGUI 的启发。**

- 需要明确区分“核心 GUI”和“默认应用框架/示例后端”，避免核心被窗口系统绑死。
- 需要公开稳定的布局、事件、渲染和资源生命周期规则。
- 需要把 Qt Quick Scene Graph 的 retained batching 思路作为长期对标，但保持 XRGUI 的 Vulkan 附件输出优势。

### 4.2 Dear ImGui

**代码实现。** Dear ImGui 是 Immediate Mode。用户每帧调用 `ImGui::Button`、`ImGui::SliderFloat` 等函数声明 UI，库内部维护最小必要状态，最终输出优化后的顶点缓冲、索引缓冲和 draw command list。平台输入和渲染后端分离，官方仓库维护 Win32/GLFW/SDL + D3D/OpenGL/Vulkan/Metal/WebGPU 等大量 backend。

**性能。** Dear ImGui 的强项不是“逐控件立即调用 GPU”，而是每帧构建 UI 后输出少量 draw list，让宿主在自己的渲染管线里绘制。它对 CPU 和内存非常克制，特别适合调试工具和数据可视化。XRGUI 的 GPU Compute 顶点生成与 retained command 重录在复杂、长期存在的 UI 中可能更有上限，但需要真实 benchmark 证明。

**易用程度。** Dear ImGui 接入成本极低：加入少数源文件，接标准 backend，上传字体纹理，喂输入，渲染 textured triangles。XRGUI 目前与它差距最大的不是控件能力，而是“第一小时体验”。如果新用户不能快速跑出一个可交互窗口，后续性能和架构优势很难被验证。

**库能力。** Dear ImGui 的工具控件、表格、Docking 分支、plot/node/text editor 等扩展生态很强，但官方明确它不是面向普通终端用户的完整高阶 GUI，也不支持完整 RTL、Bidi、文本 shaping 和 accessibility。XRGUI 的保留式布局、富文本、异步任务和样式系统可以在“长期使用的复杂工具界面”上形成差异。

**对 XRGUI 的启发。**

- 学习它的 backend 文档格式：平台输入、渲染器、纹理、剪裁、坐标、生命周期必须能被一页文档讲清。
- 提供极小可运行样例，不要求用户理解全部 style、compositor、react_flow。
- 对外宣传时避免和 Dear ImGui 比“谁更快”，应比“谁更适合复杂持久 UI”。

### 4.3 Nuklear

**代码实现。** Nuklear 是 ANSI C single-header immediate-mode GUI。它没有窗口、没有 OS 调用、没有默认渲染后端，输入被写入 `nk_context`，UI 函数把矩形、线、圆、文本等命令写入 command buffer，宿主遍历命令或转换为 vertex buffer。

**性能。** Nuklear 的极限优势在依赖、内存和可移植性。它可以固定内存运行，按模块裁剪，不隐藏全局状态。XRGUI 在性能目标上更偏现代 GPU 和复杂 UI，而 Nuklear 更适合小型 C 项目或资源受限场景。

**易用程度。** 单头文件是巨大优势，但 Nuklear 的后端、字体、内存选项和每帧清理协议需要用户理解。XRGUI 不能也不必变成 single-header，但可以学习“核心边界小、后端责任清晰、没有隐式 OS 依赖”的接口文档。

**库能力。** Nuklear 的基础控件足够多，但复杂布局、文本、异步、长期状态和大规模 UI 管理不是它的主要目标。XRGUI 的 retained element + layout tree 正好补齐这类需求。

### 4.4 wxWidgets

**代码实现。** wxWidgets 是 C++ 跨平台 GUI 框架，核心策略是用统一 C++ API 包装各平台原生控件和服务。Windows 下接 Win32，macOS 下接 Cocoa，Linux/Unix 常见路径接 GTK。事件系统通过 event table 或 `Bind` 绑定处理函数，布局主要靠 sizer。

**性能。** 对普通桌面 UI，wxWidgets 借用平台原生控件，性能和行为通常接近原生。它不需要自己实现文本编辑、无障碍和平台对话框的全部细节。XRGUI 则是完全自绘，能够统一视觉和接入 GPU pipeline，但必须自己承担文本、输入法、可访问性和平台习惯。

**易用程度。** wxWidgets 的优势是原生桌面应用开发路径成熟，许可证对商业闭源友好。但它的 API 风格偏传统 C++，跨平台差异和 native 控件限制会影响自定义外观。XRGUI 在视觉一致性和渲染控制上强，但做 native 桌面应用不划算。

**库能力。** wxWidgets 有丰富平台服务：clipboard、drag/drop、image、HTML、printing、threading、network 等。XRGUI 当前的库能力集中在 UI 渲染和交互，平台服务需要外部宿主或 backend 补齐。

### 4.5 GTK / gtkmm

**代码实现。** GTK 是 C/GObject 对象系统上的 widget toolkit。GTK4 通过 GDK 抽象窗口系统，通过 GSK 把 widget 生成的绘制节点交给 OpenGL、Vulkan、Cairo 等 renderer。gtkmm 是官方 C++ wrapper，提供类型安全回调、继承式自定义 widget 和 C++ 内存管理。

**性能。** GTK4 的 GSK 路线已经从传统 CPU 绘制转向 scene/render node 抽象，现代 Linux 桌面上很成熟。XRGUI 与 GTK 的差异在于：GTK 是桌面应用 toolkit，XRGUI 是渲染组件；GTK 关注平台集成和 accessibility，XRGUI 关注宿主渲染管线控制。

**易用程度。** 在 Linux/GNOME 环境中，GTK/gtkmm 有生态优势；在 Windows/macOS 上部署和外观一致性通常比 Qt 更有摩擦。XRGUI 当前 Windows/MSVC 路线反而更明确，但跨平台远不成熟。

**库能力。** GTK 在 CSS、Builder、Pango 文本、列表/树、对话框、accessibility 和桌面集成上明显强于 XRGUI。XRGUI 不应直接模仿 GTK 的平台栈，而应借鉴 GTK4 的 CSS node/render node 分层思想，完善 style metrics 和渲染节点调试工具。

### 4.6 FLTK

**代码实现。** FLTK 是轻量 C++ widget toolkit。控件通过 `Fl_Widget` 派生，事件循环以 `Fl::run()` / `Fl::wait()` 为核心，自定义绘制通常重写 `draw()`，事件处理重写 `handle()`。它支持 OpenGL 窗口，且许可证允许静态链接。

**性能。** FLTK 的优势是小、快、低依赖，适合简单桌面工具和教学项目。它没有 Qt/GTK 那样庞大的平台服务，也没有 XRGUI 这种 GPU Compute 指令系统。对小 UI，FLTK 的轻量性比 XRGUI 更实际。

**易用程度。** FLTK 上手比 XRGUI 低很多：创建 widget、show window、进入 event loop 即可。代价是现代视觉、复杂布局、复杂文本和高级控件都需要额外工作。

**库能力。** XRGUI 的布局、样式和 GPU 自绘能力更强；FLTK 的价值在于提醒 XRGUI：小样例、静态/源码集成、明确事件循环，比宏大架构更影响采用率。

### 4.7 LVGL

**代码实现。** LVGL 是 C 写的嵌入式 GUI 库，有对象树、样式、事件、Flex/Grid 布局和大量 widget。显示端通过 draw buffer 和 flush callback 接入设备；渲染可按 invalid area/tile 分块刷新，也可接多种 2D GPU/draw unit。

**性能。** LVGL 的性能目标是 MCU/嵌入式设备：少内存、局部刷新、显示控制器约束、SPI/RGB 屏吞吐。XRGUI 的性能目标是桌面 GPU：Vulkan、Compute、附件、后处理。两者不在同一硬件层竞争。

**易用程度。** LVGL 对嵌入式开发者友好，但显示驱动、触摸输入、内存、RTOS 调度都需要配置。XRGUI 对桌面 Vulkan 开发者更自然，但通用 C++ 用户上手更难。

**库能力。** LVGL 的 Flex/Grid、样式、控件、嵌入式工具链和 GUI editor 生态值得关注。XRGUI 可以借鉴它的 invalid area、tile、draw task 统计和配置化裁剪，而不是学习其 C API 风格。

### 4.8 RmlUi

**代码实现。** RmlUi 用 RML/RCSS 构建 DOM 和样式树，布局后通过 `RenderInterface` 把几何、纹理、clip、layer、filter 等请求交给应用。应用必须提供 render/system/file/font/text input 等接口中的关键部分。

**性能。** RmlUi 的性能取决于应用 renderer 和 DOM/CSS 布局复杂度。它的优势不是最小 CPU 开销，而是把 HTML/CSS 类工作流带进游戏和 C++ 应用，降低非程序员参与 UI 内容的成本。

**易用程度。** 对熟悉 HTML/CSS 的团队，RmlUi 比 XRGUI 的纯 C++ 样式树更容易组织内容；对追求类型安全、避免运行时解析、希望所有 UI 都可被 C++ 重构工具追踪的团队，XRGUI 更合适。

**库能力。** RmlUi 在文档结构、样式表、数据绑定、插件、DOM 事件、视觉测试和 renderer 接口文档上非常值得 XRGUI 学习。XRGUI 不一定要引入 HTML/CSS，但应提供可选的描述式层或 theme token，以降低复杂界面的重复 C++ 样板。

### 4.9 JUCE

**代码实现。** JUCE 是 C++ 应用框架，最强领域是音频应用和插件。它用 `Component` 树组织 GUI，支持自绘、LookAndFeel、FlexBox/Grid、动画、文本渲染和多平台封装，同时提供 VST/VST3/AU/AUv3/AAX/LV2 等插件格式 wrapper、DSP、音频 IO 和工具链。

**性能。** JUCE 的 GUI 性能服务于音频产品：实时线程安全、插件宿主约束、跨 DAW/OS 一致性。XRGUI 的 GPU pipeline 对音频插件并不自然，因为插件 UI 受宿主窗口、平台图形 API 和分发格式限制。

**易用程度。** 对音频开发者，JUCE 比 XRGUI 直接得多；对 Vulkan 可视化工具，JUCE 的音频/插件生态反而是负担。

**库能力。** XRGUI 暂不应竞争 JUCE 的音频垂直能力。若未来考虑音频或插件，只应提供“可嵌入的渲染 UI 层”，而不是复制 JUCE 的插件生态。

### 4.10 NanoGUI、Ultimate++、libui-ng 等补充库

NanoGUI 是 OpenGL/GLES/Metal/WebGL 路线的小型 C++/Python widget 库，适合科研、渲染 demo 和中小可视化工具；Ultimate++ 是 BSD-2-Clause 的 C++ RAD 框架，集成 GUI、IDE、SQL、网络等；libui-ng 是 C API native control wrapper，但官方状态仍是 mid-alpha。

这些库对 XRGUI 的启发主要是：

- NanoGUI 说明“小而完整的可视化工具 UI”也有稳定需求，XRGUI showcase 应提供类似规模的清晰样例。
- Ultimate++ 说明工具链和 IDE/Designer 能显著提高采用率，但这不是 XRGUI 近期优先级。
- libui-ng 说明 native wrapper 的方向已经拥挤，XRGUI 不应把目标改成原生控件库。

---

## 5. XRGUI 的优势与劣势

### 5.1 相对优势

1. **渲染集成能力强。** Qt/wx/GTK/JUCE 通常以应用窗口为中心，Dear ImGui/Nuklear/RmlUi 虽然可接入自有 renderer，但 XRGUI 从设计上就输出到附件并允许宿主安排 GPU 任务。
2. **复杂 UI 的结构化潜力。** Immediate-mode 库在短生命周期工具上极强，但复杂树、异步加载、局部布局更新和持久状态会逐渐复杂；XRGUI 的 retained model 更适合这类场景。
3. **C++ 类型安全样式系统。** RmlUi/GTK/Qt Quick 的样式和布局有强工作流优势，但运行时解析、字符串选择器和样式调试会带来另一类成本。XRGUI 可以把“可重构、可静态检查”的 C++ 样式作为卖点。
4. **线程模型有差异化。** 很多传统 GUI 要求主线程操作 UI；XRGUI 的独立 GUI 线程和 native dispatcher 设计有利于渲染应用或工具引擎分离。
5. **GPU 指令系统有上限。** 抽象图元、批处理、Compute 解析、MSDF、遮罩和合成器让 XRGUI 有机会在高分辨率复杂 UI 中形成性能优势。

### 5.2 相对劣势

1. **首次接入成本过高。** 与 Dear ImGui 的“加入几个源文件 + backend”相比，XRGUI 的工具链和生成步骤太多。
2. **平台风险。** 当前 MSVC/Windows/Vulkan 是事实支持路径，不能对外暗示已有 Qt 级跨平台能力。
3. **生产能力缺口。** 可访问性、IME、RTL/Bidi、剪贴板边界、文件/系统对话框、焦点和键盘导航规范、错误恢复、自动化测试都需要补。
4. **控件覆盖仍不稳定。** 对复杂桌面应用，树、表格虚拟化、属性面板、dock/tab、菜单栏、工具栏、列表选择、表单验证、对话框体系都需要系统化。
5. **缺少可复现性能证据。** 单张 Nsight 图很有价值，但还需要固定场景、跨 GPU、跨驱动、CPU/GPU 分离的 benchmark。
6. **API 稳定性与命名。** `scene`、`elem`、layout cell、style、renderer frontend 和 native communicator 的公开边界仍需持续收敛；这会直接影响外部采用。

---

## 6. 发展建议

### 6.1 定位建议

XRGUI 的推荐定位：

> 面向高性能桌面渲染应用和工具引擎的 C++23 保留式 GUI 库，默认提供 Vulkan 后端，支持自动布局、样式树、异步任务和 GPU 驱动绘制，可作为宿主渲染管线中的 UI 组件。

不建议的定位：

- 不要说“替代 Qt”。Qt 是完整应用平台，XRGUI 目前不是。
- 不要说“比 Dear ImGui 更快”。应说“比 immediate-mode 更适合复杂持久 UI，并保留接入渲染管线的能力”。
- 不要过早承诺移动端、所有编译器、所有图形 API。
- 不要把默认 Vulkan 后端和 GUI Core 混成一个不可拆整体。

### 6.2 P0：近期必须补齐

1. **最小可运行样例。** 维护现有 `xrgui.hello`，保持它只展示窗口、按钮、滑条、文本输入和简单布局，不启用复杂 showcase。
2. **后端接入文档。** 用 Dear ImGui backend 文档的粒度说明：输入事件、纹理、字体、剪裁、坐标、DPI、生命周期、同步点、渲染附件、线程限制。
3. **构建失败诊断。** 继续完善 `xmake doctor` 和文档，针对 MSVC、Vulkan SDK、Slang、Python、Node、`gen_icon`、`gen_slang`、submodule、assets summary 逐项给出错误现象和修复。
4. **Benchmark 协议。** 固定 3-5 个场景：空场景、1000 控件、长文本/富文本、表格滚动、遮罩/后处理。输出 CPU layout、CPU record、GPU resolve、GPU draw、compositor 时间。
5. **截图/布局回归测试。** 用固定窗口尺寸和主题生成 golden image；布局用纯 CPU 单元测试覆盖 size_category、pending、grid span、scroll、split pane。
6. **API 边界清理。** 先稳定 `scene`、`elem`、`renderer_frontend`、`native_communicator`、layout cell、style manager 的公开接口，不急着扩展新控件。

### 6.3 P1：形成可用产品力

1. **文本输入生产化。** 完整 IME、剪贴板、撤销/重做、选择、光标移动、组合字符、字体 fallback、Bidi/RTL 策略。
2. **键盘导航和焦点规范。** Tab 顺序、快捷键映射、ESC 链、默认按钮、菜单键、disabled/hidden 行为需要文档和测试。
3. **控件体系补全。** 优先补：输入框、复选框/单选、下拉框、树、虚拟列表、虚拟表格、tab、dock、菜单栏、工具栏、属性编辑器、modal/dialog。
4. **主题与样式 token。** 保持 C++ 样式树，但提供一层 theme token/JSON 或代码生成入口，让用户无需改 C++ 模板即可换色、间距、圆角和字体。
5. **资源和字体管理。** 明确资源热更新、图像异步加载、字体 atlas、MSDF 参数、DPI scaling 的生命周期。
6. **文档分层。** README 保持短；`user_guide.md` 集中 GUI 使用；布局、富文本、渲染流程保留速查；复杂 API 语义维护在源码 Doxygen 注释中。

### 6.4 P2：扩大适用面

1. **Linux + Vulkan 路径。** 不必一开始支持所有平台，但 Linux + Vulkan + GLFW 是最自然的第二目标。
2. **编译器现实策略。** 先声明 MSVC 为主支持；跟踪 Clang/GCC C++ modules 问题；不要让跨编译器承诺拖慢核心稳定。
3. **可选后端抽象。** 长期可以考虑 D3D12/WebGPU/软件或 Skia 类 fallback，但必须先把 Vulkan backend 的接口固化。
4. **可视化调试器。** 类似浏览器 inspector：元素树、布局框、dirty 状态、style metrics、draw commands、GPU layer。
5. **可选声明式层。** 不是替代 C++ API，而是为大型 UI 提供数据描述/代码生成，目标是降低重复代码，不引入完整 CSS/HTML 复杂度。
6. **生态接入。** 提供 xmake 模板、CMake bridge、vcpkg/conan 说明、submodule 模板和“从源码静态构建”的最佳实践。

---

## 7. 可执行路线图

| 阶段 | 目标 | 交付物 |
| --- | --- | --- |
| 0-1 个月 | 降低首次运行成本 | 维护 `xrgui.hello`、构建诊断、backend 接入图、常见错误文档 |
| 1-2 个月 | 建立可信性能叙事 | benchmark harness、Nsight/RenderDoc 采样说明、截图回归 |
| 2-4 个月 | 稳定核心 API | scene/elem/layout/style/render/native communicator 文档和测试 |
| 4-6 个月 | 补齐工具型 UI 能力 | 虚拟表格、树、tab/dock、属性面板、输入体系 |
| 6-12 个月 | 扩大平台和生态 | Linux Vulkan、包管理模板、可视化 inspector、主题 token |

---

## 8. 资料来源

### XRGUI 本地资料

- [README](../../README.md)
- [PROJECT_INTRO](../../PROJECT_INTRO.md)
- [用户指南](user_guide.md)
- [运行时展示](runtime_showcase.md)
- [布局文档](layout_doc.md)
- [渲染流程文档](render_spec.md)
- [富文本文档](rich_text_doc.md)

### 外部资料

- Qt: [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html), [Layout Management](https://doc.qt.io/qt-6/layout.html), [Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html), [Qt Quick Scene Graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html), [Supported Platforms](https://doc.qt.io/qt-6/supported-platforms.html), [Qt LGPL/GPL obligations](https://www.qt.io/licensing/open-source-lgpl-obligations)
- Dear ImGui: [README](https://github.com/ocornut/imgui), [Backends](https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md), [FAQ](https://github.com/ocornut/imgui/blob/master/docs/FAQ.md)
- wxWidgets: [Overview](https://wxwidgets.org/about/), [License](https://wxwidgets.org/about/licence/), [GitHub](https://github.com/wxWidgets/wxWidgets)
- GTK/gtkmm: [GTK Architecture](https://www.gtk.org/docs/architecture/index), [GTK 4 docs](https://docs.gtk.org/gtk4/), [CSS in GTK](https://docs.gtk.org/gtk4/css-overview.html), [gtkmm](https://gtkmm.gnome.org/)
- FLTK: [Official site](https://www.fltk.org/), [FLTK Basics](https://fltk.gitlab.io/fltk/basics.html), [Drawing](https://fltk.gitlab.io/fltk/drawing.html), [Events](https://fltk.gitlab.io/fltk/events.html)
- LVGL: [Layouts](https://docs.lvgl.io/master/common-widget-features/layouts/overview.html), [Display](https://docs.lvgl.io/master/main-modules/display/index.html), [Draw Pipeline](https://docs.lvgl.io/master/main-modules/draw/draw_pipeline.html)
- Nuklear: [README](https://github.com/Immediate-Mode-UI/Nuklear), [Drawing docs](https://immediate-mode-ui.github.io/Nuklear/Drawing.html)
- RmlUi: [README](https://github.com/mikke89/RmlUi), [Custom interfaces](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces.html), [Render interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html), [Integration](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html)
- JUCE: [Home](https://juce.com/), [Features](https://juce.com/juce/features/), [GitHub](https://github.com/juce-framework/JUCE), [License](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
- NanoGUI: [GitHub](https://github.com/mitsuba-renderer/nanogui)
- Ultimate++: [GitHub](https://github.com/ultimatepp/ultimatepp)
- libui-ng: [GitHub](https://github.com/libui-ng/libui-ng)
