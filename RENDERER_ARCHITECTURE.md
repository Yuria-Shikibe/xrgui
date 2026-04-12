# Yanxi 渲染器架构详尽解析

本文档详尽说明了现有渲染器的运作机制、管线控制以及图形绘制的核心原理。

## 0. 抽象层级

Yanxi 渲染器在架构上被严格地分为了前端逻辑（Frontend）、指令中间层（Instruction Batching）与 Vulkan 后端（Backend）三个主要层级：

1. **前端层（Frontend）**：
   位于 `src/gui/core/draw/gui.renderer.frontend.ixx`。它与具体的后端 API 完全解耦，面向 GUI 组件提供直接的绘制接口。
   - **坐标与视口管理**：负责处理 `layer_viewport`、本地到屏幕的矩阵变换（`math::mat3`）、以及图层裁剪（`scissor`）。
   - **指令分发**：提供类型安全的 `push()` 与 `update_state()` 接口，将绘制指令（如矩形、多边形、线条）和状态序列化，并通过 `batch_backend_interface` 推送给中间层。

2. **指令中间层（Instruction Batching）**：
   位于 `src/graphic/instruction_draw/` 目录下（如 `batch.frontend.ixx`，`batch.common.ixx`）。
   - **指令收集**：使用 `draw_list_context` (即 `batch_host`) 收集指令。指令被序列化为连续的 `instruction_head`（带类型、长度、顶点和图元数）和 Payload 二进制流。
   - **状态与断点追踪**：使用 `state_tracker` 和 `binary_diff_trace`（二进制增量追踪）来维护当前的渲染状态。一旦检测到打断合并的非幂等操作（如切换混合模式、管线、触发后处理 Blit），就会自动切分出 Draw Call 组，插入 `state_transition` 断点。
   - **数据布局**：管理 UBO（Uniform Buffer）及长期存在的图元数据（Sustained Data/Volatile Data），自动生成 `uniform_update` 伪指令。

3. **Vulkan 后端层（Backend）**：
   位于 `src.backends/vulkan/` 以及 `batch.backend.ixx`。
   - **资源管理**：通过 `renderer_v2` 统筹全局附件（`attachment_manager`）、管线实例（`pipeline_manager`）和三缓冲资源。
   - **后端执行引擎**：`batch_vulkan_executor` 解析中间层生成的逻辑 Draw Call，进行内存重排与拍平（Flattening），上传指令与描述符后调用 Vulkan 的绘制/计算命令。

---

## 1. 管线管理

后端的管线生命周期和切换由管理器统一控制：

- **图形与计算管线管理器**：
  `graphic_pipeline_manager` 和 `compute_pipeline_manager` 分别负责处理图形渲染与后处理管线。初始化时通过描述性的 `graphic_pipeline_create_config` 批量构建 Vulkan Pipeline 对象。

- **描述符缓冲区（Descriptor Buffers）**：
  项目弃用了传统的 Descriptor Set，转而全面使用较新的 `VK_EXT_descriptor_buffer` 扩展。`descriptor_slots` 与 `dynamic_descriptor_buffer` 管理绑定信息，大幅减少了每帧更新描述符产生的 CPU 开销和驱动同步负担。

- **动态渲染（Dynamic Rendering）**：
  `attachment_manager` 维护了 Draw Attachments（颜色附件）、Blit Attachments（后处理计算目标）和 MSAA 附件。在绘制循环中，渲染器无需显式使用 Render Pass，而是根据 `gui::fx::render_target_mask` 动态配置并在必要时自动开始/结束 `Rendering`。

---

## 2. 后处理（计算管线）部分

系统不使用全屏三角形来进行后处理（如模糊、抗锯齿、颜色变换），而是将其抽象为 **计算管线（Compute Shader）驱动的 Blit 操作**。

- **触发机制**：
  在前端推送 `blit_config` 时，会被 `state_tracker` 捕获并作为一个管线断点（Breakpoint）。

- **执行与屏障逻辑（Barriers）**：
  在 `renderer_v2` 执行录制时，如果遇到 `state_type::blit`：
  1. 当前如果正处于 Graphic 渲染通道中，会先被刷新并结束（`vkCmdEndRendering`）。
  2. 根据 Blit 配置文件（`inout`）计算输入输出图像的依赖。利用 `vk::cmd::dependency_gen` 自动插入并应用 Image Memory Barrier，将输入从 Color Attachment 转换为 General 布局用于 Compute 读取。
  3. 绑定 Compute Pipeline、描述符与 Push Constant（通常传递偏移量和尺寸），调用 `vkCmdDispatch`。

- **状态跃迁**：
  修改后的输出附件会被标记为 `blit_sync_state::pending_barrier`（待决状态）。如果在下一个 Draw Call 前没有其他 Blit 冲突，这个同步依赖会在切换回图形管线时通过底层的渲染阶段天然消化，从而最大化 GPU 的并行效率。

---

## 3. 图形绘制（图形管线）部分

本框架最大的特点是**抛弃了传统的顶点着色器（Vertex Shader）驱动，全面转向 Mesh Shader (`vkCmdDrawMeshTasksEXT`)**。

- **纯 Mesh Shader 驱动**：
  几何图形并不是由 CPU 预先生成顶点并上传到 Vertex Buffer 的，CPU 仅提供轻量级的、语义化的指令（如 `rectangle`, `poly`, `line` 的结构体参数）。

- **Task & Mesh Workgroup 限制**：
  在系统启动时，框架会通过 `query_hardware_limits` 获取 GPU 的 Mesh Shader 限制（如每个 Workgroup 最大顶点数 `maxMeshOutputVertices`），中间层利用这些信息来合并或拆分图元，以填满每个 Mesh Workgroup 的容量（如 `VERTEX_PER_MESH_LIMIT = 64`）。

- **基于 Draw Call 的分发**：
  后端遍历 `batch_host` 中合法的 `submit_groups_`（按断点拆分的一系列指令集），绑定对应的 Descriptor Buffer，通过 `vkCmdDrawMeshTasksEXT` 执行。每一个提交组对应一次 Draw Call。

---

## 4. 图形绘制的原理、方案与管线状态更新

### A. 动态即时生成顶点（On-the-fly Geometry Generation）
渲染器的核心运行逻辑是"指令解析器"思想：
1. **指令上传**：CPU 生成的不同长度的 Instruction Payload 流被追加在统一的 SSBO Storage Buffer 中。
2. **Resolve Info（线程映射解算）**：`instruction_resolve_info` 将这些紧凑的指令解包为扁平化的数组，映射到具体的 GPU 线程。
   - `group_dispatch_info_v3` 存储每个 Workgroup 的总图元/顶点信息。
   - `thread_resolve_info` 记录了第 `N` 个顶点是哪种类型（如 `triangle` 或 `line`），以及该指令在 Payload Buffer 中的绝对字节偏移（`payload_offset`）和局部 Skip 数。
3. **GPU 并行解析**：在 Mesh Shader 的每次执行中，各个线程读取自己负责的 `thread_resolve_info`，从 Storage Buffer 取出指令数据结构，并基于数学公式原地计算出对应顶点的坐标、颜色和 UV，最后写入到 Mesh 阶段输出中送入光栅化。这极大降低了 CPU 到 GPU 的内存带宽需求。

### B. 管线状态（State）在绘制过程中的设置与跟踪
1. **状态缓冲（State Tracker）**：
   前端执行状态设置（如裁剪区 `set_scissor`，混合模式 `set_color_blend_equation`）。`state_tracker` 结构通过高效的原地比较和覆盖机制检测状态是否发生了真实改变（Dirty）。如果改变，则终止当前的 Instruction Batching 块。
2. **Volatile 与 Sustained 数据**：
   频繁变动的 Uniform 级别数据（如变换矩阵）被称为 Volatile Info，长期的如材质贴图参数为 Sustained Info。当数据发生折叠（Collapse）时，中间层会在指令流中注入一条隐式的 `uniform_update` 指令。
3. **断点消费（Breakpoint Processing）**：
   在后端的 `renderer_v2::record_ctx_::record` 循环中，若到达断点索引处，会调用 `process_breakpoints_`：
   - 如果是 `set_scissor` 或 `set_viewport`，会在下一次 Graphic API 绑定时更新相应的动态状态。
   - 如果是 `set_color_blend_enable` 或写入掩码变化，在渲染器上层会更新绑定的 Pipeline Index 进而更换 Pipeline 对象。
   - 如果包含 Push Constant 数据，则立即调用 `vkCmdPushConstants`。
   由于在同一 Draw Call 块内所有顶点的状态是一致的，系统能够非常安全地进行大批量的几何数据合并提交，实现了接近极致的批处理（Batching）性能。