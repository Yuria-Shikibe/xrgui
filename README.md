# 介绍

XRGUI(mo_yanXi's Retaine mode GUI)是一个**每帧重绘和更新**的带有**自动**布局的支持多个绘制后端（尽管我只实现了Vulkan)、*（截至目前）不适合用于生产*、特性使用激进（主要为语言特性）、力求轻量化的跨平台和支持所有主流编译器（尽管clang还在ice，我也没试过gcc）的纯粹面向命令式代码编写的**保留式GUI**

* ***你永远不应该动态链接本库***

## [基础页面展示](properties/showcase/runtime_showcase.md)

## Brief
### 主体架构

#### Core
* **GUI Core** 即GUI的核心内容，包含异步控制、绘制接口、布局系统、简单的事件系统等核心内容。这部分内容原则上是后端API无关的

#### Extension/默认实现
* **Image Loader** 提供一个异步不阻塞的图像加载和管理系统，目前针对Vulkan，完全多线程可用。


* **Font/TypeSetting** 文本排版系统，仅提供LTR完全支持和有限的TTB支持，暂不支持RTL和BTT。内嵌一个简单的富文本系统


* **Assets Storage Manager** GUI的资源注册系统，目前暂不保证多线程访问安全，默认在加载时一次加载完毕



* **Compositor** GUI合成器，暂时并未对Buffer相关内容进行测试，提供一个基于内存别名复用的半自动化后处理管线合成处理器


* **Renderer Backend** 基于Vulkan-Mesh Shader-Dynamic Rendering-Descriptor Buffer(将在Descriptor Heap完全可用后迁移到Descriptor Heap)-Compute Graphics Mixed的渲染器，对CPU的压力较小，但是对驱动和硬件的要求较高

#### Backend/默认实现
* 提供一个基于GLFW-Vulkan的默认后端，以处理外设输入和交换链呈现

![structure.drawio.svg](properties/showcase/structure.drawio.svg)

整个GUI系统理论上可以不必跑在主线程，只需要在特定同步点（如输入事件提交、拉取GUI输出命令、等待绘制命令录制、改变尺寸时）阻塞

由于GUI的定位是即时相应，并没有提供复杂的类似事件总线一样的设置。主要类似事件或事件的替代品如下：
* 外部输入事件使用简单的拦截方案进行传递，如果没有被UI元素拦截则回退给外部线程
* 重布局事件在元素内部进行传播和拦截，在更新时自动消费
* 独立更新由元素直接注册，在下一时刻启用/移除独立更新
* GUI和外界的信息交互使用任务队列
* GUI内部的异步任务会挂在特定元素上执行，主要用于异步创建巨大的元素子树
* 永远都是**即时重绘**，但是平铺的绘制命令会在绘制状态改变时重录
* UI元素的Action推送可以从任何线程发起以执行元素相关操作，但是执行都会在UI主线程进行

### 绘制流程

自顶向下的绘制过程描述：
1. 如有需要，GUI重新录制平铺的绘制指令到函数指针级平铺绘制栈
2. 对于需要绘制的GUI树，遍历绘制栈N次，绘制函数将图元指令和状态指令推送到渲染器后端，渲染器会在命令提交后得到M个Layer。
3. M个Layer经由渲染器后处理产生I个Layer
4. 如使用了合成器，使用合成器将I个Layer结合外部数据，绘制到J个Layer上

![draw_structure.drawio.svg](properties/showcase/draw_structure.drawio.svg)

## Dependencies
### Xmake Requirements
* [freetype](https://freetype.org/) 
* [harfbuzz](https://github.com/harfbuzz/harfbuzz)
* [nanosvg](https://github.com/memononen/nanosvg)
* [spirv-reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
* [gtl](https://github.com/greg7mdp/gtl)
* [mimalloc](https://github.com/microsoft/mimalloc)
* [GLFW](https://www.glfw.org/)
* [simdutf(Optional)](https://github.com/simdutf/simdutf)
* [msdfgen](https://github.com/Chlumsky/msdfgen)

### Submodules
* [plf_hive](https://github.com/mattreece/plf_hive) 
* [small_vector](https://github.com/gharveymn/small_vector) 
* [beman/inplace_vector](https://github.com/bemanproject/inplace_vector)
* [stb](https://github.com/nothings/stb)
* [VMA (VulkanMemoryAllocator)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)

### My own libs
* allocator2d
* mo_yanxi_react_flow
* mo_yanxi_vulkan_wrapper
* mo_yanxi_utility

## TODO
* [ ] 文档
* [ ] 更多样例

### 数据流
* [ ] 优化基于大量虚函数的数据流
* [ ] 优化数据流写法，目前的太反人类
* [ ] 进行更多测试
* [ ] 为异步操作提供进度条
* [ ] 多线程调度？

### 提供包管理选项
* [ ] 可选择不同后端，默认Vulkan-GLFW
* [ ] 可选扩展，如文本渲染等（因为这部分产生大量包依赖）
* [ ] 关于模块如何写包我没找到一个好的best practice

### Core
* [ ] 音频支持，至少提供接口
* [ ] 减少虚函数调用量
* [ ] 更多测试
* [ ] 更多基础控件和布局
* [ ] 更多基于事件的操作
* [ ] 支持非硬编码的Key Mapping
* [ ] 视情况实现按需重绘和更新

### Extension
* [ ] 完善compositor

### Style
* [ ] 提供更多基本样式选择

### Render
* [ ] 完成所有基本的几何抗锯齿
* [ ] 完成所有可选贴图指令的UV排列

### Backend
#### Vulkan
* [ ] 对默认渲染器的完好封装，需要等到descriptor heap可用
