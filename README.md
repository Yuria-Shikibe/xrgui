# 介绍

XRGUI(mo_yanXi's Retaine mode GUI)是一个**每帧重绘和更新**的带有**自动**布局的支持多个绘制后端（尽管我只实现了Vulkan)、*（截至目前）不适合用于生产*、特性使用激进（主要为语言特性）、力求轻量化的跨平台和支持所有主流编译器（尽管clang还在ice，我也没试过gcc）的纯粹面向命令式代码编写的**保留式GUI**

* ***你永远不应该动态链接本库***

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
* [ ] 提供基于HB的平凡字体排版
* [ ] 完善compositor

### Style
* [ ] 完善光标绘制
* [ ] 提供更多基本样式选择
* [ ] Nine Patch

### Render
* [ ] 完成所有基本的几何抗锯齿
* [ ] 完成所有可选贴图指令的UV排列

### Backend
#### Vulkan
* [ ] 对默认渲染器的完好封装

| 123123                      |   |   |   |   |   |
|:----------------------------|---|---|---|---|---|
| 123123                      |   |   |   |   |   |
| aaaaaaaaaaaaaaaaaaaaaaaaaaa |   |   |   |   |   |
|                             |   |   |   |   |   |
|                             |   |   |   |   |   |

export

template <typename EnumTy, typename DrawInfoTy>

    requires (std::is_enum_v<EnumTy>)

struct alignas(instr_required_align) generic_instruction_head{

    EnumTy type;

    std::uint32_t payload_size;

    dispatch_info_payload<DrawInfoTy> payload;

};



export struct draw_payload{

    std::uint32_t vertex_count;

    std::uint32_t primitive_count;

};



export using instruction_head = generic_instruction_head<instr_type, draw_payload>;



我有一个将slang中的instruction resolve的寻址移动到CPU阶段，避免在GPU中阻塞查找的方案

1. timeline机制还需要生效，每个指令所处的timeline可能不同（但一定是非递减的）

2. 每个线程处理N（总数/组线程数）个顶点

3. 注意当前的skip vertices/primitives机制（这是mesh shader限制输出顶点数产生的必要限制）

4. 尽量避免增大带宽开销

5. 不要修改instruction_head

以下为完整计划：


### 【上下文交接文档】基于 CPU 状态机模拟的 Mesh Shader 指令拍平方案

#### 1. 背景与核心目标
目前的渲染管线中，前端会将2D/UI绘制指令连续压入（Push）到变长的内存块中。过去，GPU 在 Mesh Shader 中需要使用 `while(true)` 循环遍历指令头（Instruction Heads）来寻找当前线程对应的顶点和图元偏移，这导致了严重的 Warp Divergence（线程分歧）和性能瓶颈。
**当前的核心目标：** 彻底剥离 GPU 端的循环查找逻辑。保持前端（Frontend/Common）结构**完全不变**，将状态机推演功能迁移到 Vulkan 后端（Backend）的上传阶段（Upload Dispatch）。由 CPU 预先计算好每个顶点的绝对映射，GPU 只需进行 O(1) 的数组读取。

#### 2. 当前既有方案（已实现的架构）

**2.1 核心数据结构与打包 (C++ & Slang 共享)**
为了严格控制显存带宽并保证 16 字节对齐，设计了 `vertex_resolve_info` 结构体：
* `packed_type_timeline`: `uint32`。低16位存指令类型 (`instruction_type`)，高16位存该指令相对 Timeline 的索引。
* `payload_offset`: `uint32`。该指令在 Payload Buffer 中的绝对字节偏移。
* `packed_skips`: `uint32`。低16位存 `vtx_skip`（当前顶点在该指令内部的局部纯净索引）；高16位存 `prm_skip`（该顶点生成的图元在当前 Mesh Group 中的本地绝对索引，如果该顶点不负责生成图元，则写入 `0xFFFF` 作为哨兵值）。
* `payload_size`: `uint32`。该指令的 Payload 大小，替代 Padding，供动态长度指令使用。

**2.2 Vulkan 后端模拟器 (`batch.backend.vulkan.ixx`)**
在后端的 `upload_dispatch` 中，CPU 遍历前端生成的 Draw List，重演 GPU 原本的解析过程：
1.  **四大 Buffer 布局：** 分离并构建了 `buffer_dispatch_info` (组信息)、`buffer_resolve_infos` (拍平的顶点映射，每组严格 64 个)、`buffer_timelines` (独立状态戳) 和 `buffer_instruction` (指令数据)。
2.  **状态机推演：** 使用双层循环。外层遍历每一组的 64 个线程（`target_index`），内层遍历当前组相关的 `instruction_heads`。
3.  **精确跳过与图元判定：**
    * 通过 `skipped_vertices` 和 `skipped_primitives` 累加，定位当前线程属于哪一条具体指令。
    * 使用原生的 `vtx_skip = local_skip`（已彻底移除 `patch_index` 的污染，保证着色器数组访问安全）。
    * 图元计数逻辑改为严谨的 `vtx_count < 3 ? 0 : vtx_count - 2` 进行推演。
    * 对于满足 `local_skip > 1` 的合法顶点，分配确切的图元索引 (`idc_idx`)；否则标记为 `0xFFFF`。

**2.3 GPU 侧极简解析 (`mesh_instruction_lib.slang` & `vert.slang`)**
* **消除循环：** `resolve_instruction_v3` 函数中，每个线程直接通过 `global_vtx_base + target_index` O(1) 读取 `vertex_resolve_info`。
* **按需输出：** 所有的图元解析器（如 Quad, Rectangle, Line 等）统一使用 `if (param.generates_primitive())`（即检查 `prm_skip != 0xFFFF`）来决定是否向 `primits` 和 `indices` 数组中写入数据。
* 不再需要传递 `instruction_heads`，Timeline 从独立的 Buffer 根据预计算的偏移获取。

---
