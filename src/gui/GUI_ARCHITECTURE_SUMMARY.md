# GUI 系统架构与机制学习总结

本文档是对当前仓库 `src/gui` 目录下 GUI 框架设计的系统性总结。该 GUI 框架采用了**保留式树状结构**（Retained-Mode Tree）、**响应式布局协商**、**每帧重绘**（结合按需绘制标记）、以及**基于数据流（React Flow）**的状态管理等现代 GUI 设计理念。

---

## 1. 事件相关 API 与机制

事件系统负责捕捉底层输入并分发给正确的 UI 元素，其核心机制包括捕获、拦截与冒泡。

### 1.1 事件抽象与分发
* **结构体定义**：事件被抽象为诸如 `click`, `scroll`, `cursor_move`, `drag` 及其它组合输入（定义在 `mo_yanxi.gui.infrastructure:events` 中）。
* **冒泡与拦截机制**：事件的回调（如 `on_click`, `on_drag`, `on_key_input`, `on_scroll`）通常返回一个枚举值 `events::op_afterwards`：
  * `op_afterwards::fall_through`：表示当前元素未完全消耗该事件，事件可以继续传递。
  * `op_afterwards::intercepted`：表示事件已被当前元素消耗，阻断事件的进一步传递。
* **光标与交互状态**：每个元素内部持有一个 `cursor_states` 对象。它记录了鼠标进入 (`inbound`)、聚焦 (`focused`)、按下 (`pressed`) 等状态及其持续时间。这不仅用于逻辑判断，还能为动画（如悬浮高亮、点击反馈的插值动画）提供时间系数。

### 1.2 输入映射系统 (Key Mapping)
底层输入系统由 `mo_yanxi.input_handle`（特别是 `input_manager`, `key_mapping`, `key_binding`）驱动。
* **绑定机制**：使用 `key_mapping_interface` 及其子类，支持将特定按键（如 `mouse::left`, `key::enter`）与状态（`press`, `release`, `repeat`）和修饰键组合绑定到回调函数上。
* **数据注入**：`key_mapping<ParamTy...>` 允许在绑定事件时将特定的上下文（Context）自动注入给回调函数，如注入对当前 UI 元素的引用，使输入回调能够直接操作该元素。

---

## 2. 布局相关 API、机制与策略

布局系统采用的是**父子协商式**的半自动响应布局，在渲染帧外根据脏标记触发更新。

### 2.1 核心概念与数据结构
* **布局策略 (`layout_policy`)**：用于规定容器内元素的主导排列方向：
  * `hori_major`（水平主导）、`vert_major`（垂直主导）、`none`（无强制方向）。
  * 布局策略支持子元素通过 `search_parent_layout_policy` 向上查询，以适应父容器的流向。
* **伸展策略 (`expand_policy`)**：
  * `resize_to_fit`：元素强制缩放以填满可用空间。
  * `passive`：元素被动接受大小，不主动抢占多余空间。
  * `prefer`：元素尝试达到其理想内容大小。
* **尺寸意图 (`stated_size` / `stated_extent`)**：
  * **被动 (`passive`)**：根据内容或子元素自然撑开。
  * **主导 (`mastering`)**：具有硬性的固定尺寸（像素大小）。
  * **缩放/比例 (`scaling`)**：按照父容器分配的空间比例调整。
  * **待定 (`pending`)**：向外部（父容器）索求可用空间。
* **布局限制 (`optional_mastering_extent`)**：用于父元素向子元素传递允许的最大空间，未指定宽高的部分使用 `pending_size` (Infinity) 表示可以无限延伸。

### 2.2 布局协商过程
框架内的布局是一个严格自底向上（预获取）和自顶向下（分配）的过程：
1. **策略查询与约束建立**：子元素通过读取父级的 `layout_policy`（若是单向流容器）和传递下来的 `optional_mastering_extent` 确定自己在各轴上的约束。
2. **获取期望尺寸 (`pre_acquire_size`)**：在缺乏父级硬性约束（即 `pending`）时，子元素会通过内部逻辑（例如文字长度、子容器累计）计算并向上汇报自身期望的尺寸。
3. **父容器决断**：父容器收集所有子元素的期望尺寸，结合自身的硬性限制（如果有）和 `stated_extent` 策略，计算出每个子元素最终应分配的空间。
4. **分配实际尺寸 (`resize`)**：父元素调用子元素的 `resize(vec2)` 或利用 `basic_cell` 应用分配的区域。如果元素实际大小改变，框架会修改其 `layout_state`，触发重新计算其相对/绝对坐标，并按需标记自身及父子节点的更新。
5. **定位与变换**：
  * 子元素维护自身的相对位置 (`relative_pos_`)。
  * 组合元素的绝对位置 (`absolute_pos_`) 则是由场景层级转换而来（`update_abs_src`），以确保光标拾取（Hit Testing）的快速计算。

### 2.3 Cell 辅助布局 (Layout Cells)
为了简化类似网格 (Grid)、列表 (Sequence) 的分配，框架提供了 `basic_cell`, `mastering_cell` 等辅助工具。这些工具包含了元素的对齐方式（`align::pos`）、尺寸限制、边距（`margin` / `pad`）等属性，帮助组装器将元素“放入”相应的网格或列表行中。

---

## 3. 绘制相关 API 与机制

绘制系统是将保留在内存中的 UI 树转化给底层 Vulkan 渲染后端的全过程，支持命令式推入和状态批处理。

### 3.1 基于状态的重绘标记机制
虽然 GUI 默认在每一帧构建绘制指令，但存在优化机制：
* **`draw_flag`**：维护了自身的渲染标记。通过 `set_draw_required` 和层层向上的 `propagate_draw_requirement_since_self`，实现了按需更新、裁剪绘制以及针对调试的 `debug_count` 可视化。
* 只有当元素可见（`is_visible`）、且其绝对包围盒与当前裁剪区（`clipSpace`）相交时，才调用真正的 `draw_layer` 动作。

### 3.2 绘制流与渲染后端前端 (`renderer_frontend`)
* **多图层绘制 (`layer_param`)**：元素可以在不同的图层级上绘制（如底板层、内容层、悬浮层）。`style_config::used_layer` 决定了该样式参与的绘制层次。
* **命令式推入**：在 `draw_layer_impl` 等实现内部，使用 `renderer.push(...)` 将基本指令（如 `poly`, `poly_partial`, `line_node`, `parametric_curve`）和状态更新推入批处理后端。
* **视口与裁剪 (`Guard` 模式)**：
  * 利用 `viewport_guard` 和 `scissor_guard` 通过 RAII 压入和弹出视口和裁剪区域。
  * `renderer_frontend` 会维护一个变换栈（包含从局部元素坐标系到屏幕像素坐标系的矩阵乘法），确保每个元素发出的顶点都在正确的裁剪区和屏幕坐标下。

### 3.3 样式管理器 (`style_manager`) 与调色板 (`palette`)
* **样式抽离**：元素的绘制逻辑大部分被解耦在 `style_drawer<T>` 中。通过这种多态（或函数指针）方式，同一元素可以在不修改逻辑的前提下应用 `debug_style` 或其他主题。
* **颜色与混合**：定义了如 `palette` 和 `color_blend_mode`，用于计算元素处于禁用（disable）、悬浮（focus）、按下（press）、切换（toggled）状态时的颜色变化，并且自动处理颜色的乘法与混合模式（如预乘Alpha、正片叠底、滤色等）。

---

## 4. 其它工具相关 API 与机制

除了基础的布局和渲染，该系统集成了多种高级功能模块，提升了交互体验和拓展性。

### 4.1 数据流机制 (React Flow)
框架内嵌了基于节点的响应式数据流机制（位于 `mo_yanxi.react_flow`）。
* UI 元素能够直接申请（`request_react_node`，`request_embedded_react_node`）作为数据流的节点，监听数值的变化。
* 当底层数据变更时，UI 可以响应式地更新，而无需手动编写回调或每帧轮询，天然支持如滑动条、进度条等控件的值绑定。

### 4.2 Tooltip 系统
所有的元素默认继承于 `tooltip::spawner_general<elem>`。
* 只要配置了 `tooltip_create_config`（包含最小悬停时间、是否自动构建），当光标在元素内悬停（`time_tooltip`）超过阈值时，就会触发 Tooltip 弹出。
* 开发者可以通过覆盖虚函数（如 `tooltip_should_build`, `tooltip_should_maintain`）精确控制 Tooltip 的生命周期。

### 4.3 其它视觉工具：边缘抗锯齿与线条生成 (Fringe & Compound)
* **`fringe.ixx`**：对于不使用 MSDF 而需要抗锯齿的 SDF 形状，提供了对 `poly`, `curve` 的外部拓展方法（如 `poly_fringe_only`, `curve_with_cap`），利用线宽的细微扩张并在外边缘设置透明色（Alpha=0）以产生柔和的平滑渐变效果。
* **`compound.ixx`**：提供了虚线 (`dash_line`) 等复杂的图案线条拆分生成算法。

---

## 5. 各元素的职责

整个 UI 系统的组织通过类层次结构明确了不同对象的职责。

* **`scene` (场景管理器)**：
  * 是整个 GUI 环境的上下文入口，持有堆分配器（`heap_allocator`）、渲染前端（`renderer_frontend`）、样式管理器（`style_manager`）、以及数据流管理器（`react_flow::manager`）。
  * **状态记录机制**：`scene` 在内部维护了一系列集合（集成了双缓冲 `double_buffer` 和扁平集合 `linear_flat_set`）来追踪活跃元素，避免全树遍历：
    * `inbounds_`：记录当前鼠标悬浮（Inbound）的元素层级栈。
    * `focus_scroll_` / `focus_cursor_` / `focus_key_`：记录当前占据滚动、鼠标、键盘焦点的特定元素。
    * `active_update_elems` / `action_active_elems_`：记录当前帧需要执行逻辑更新或包含排队 Action 的活跃元素集合。
    * `independent_layouts_`：记录孤立发生的布局变更，方便在下一帧进行集中计算。
  * 负责分发最顶级的输入事件、运行全局的更新循环（`update` 和 `draw`）、管理全局焦点以及弹出层（Overlay/Tooltip）。
* **`elem` (UI 元素基类)**：
  * 所有可交互和可绘制对象的基类。
  * **生命周期与拓扑**：持有父节点指针和（可变的）相对位置及绝对位置、持有多层高度（`altitude_t`）以判定重叠排序。
  * **绘制与更新树**：持有 `draw_flag` 与 `update_flag` 实现自底向上/自顶向下的脏标记检查，决定子树是否需遍历。
  * **交互响应**：实现了鼠标的进入退出、拖拽、点击等的通用状态变迁，记录状态供样式调取。
* **组件类（衍生自 `elem`）**：
  * 框架包含了诸如 `group` (基础容器), `gui.sequence` (列表/垂直布局), `gui.scroll_pane` (滚动窗), `gui.split_pane` (分割面板), `gui.grid` (网格) 等元素。这些元素重写了 `layout_elem` 或 `resize_impl` 来处理具体的尺寸分配和子元素管理规则。
* **样式类 (`style_drawer_base` 及其衍生)**：
  * 彻底解耦于业务逻辑的类。只通过拿取元素内部的只读状态（大小、交互状态 `cursor_state`），往 `renderer_frontend` 塞入顶点或指令以产生可视化的结果。

## 总结
该 GUI 库通过保留模式构建元素树，结合严格的响应式布局协商流程解决复杂的动态排版问题。它将视觉表现通过 `style_drawer` 解耦，将输入与交互映射通过 `key_mapping` 解耦，并引入数据流（React Flow）来响应业务数据的变更。整体上呈现出一个结构清晰、扩展性极强、专注于渲染性能和现代状态管理的 UI 解决方案。