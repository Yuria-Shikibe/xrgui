# XRGUI Element Layout System

> 响应式/半自动布局（Reactive / Semi-Automatic Layout）
>
> 布局系统内建于元素树中——**每个元素都具备布局能力**，不存在独立的"Layout Manager"。
> 布局行为分散在各容器元素中，通过位掩码传播系统实现脏标记与增量更新。

---

## 1. 架构概览

### 1.1 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| **布局策略** | `src/gui/core/layout/policy.ixx` | 方向策略、尺寸类别、约束定义 |
| **单元格** | `src/gui/core/layout/cell.ixx` | 容器中子元素的定位与尺寸元数据 |
| **对齐** | `src/align.ixx` | 位掩码对齐系统、间距 (padding) |
| **元素基类** | `src/gui/core/infrastructure/element.ixx` | 布局虚方法 (548-691行) |
| **标志** | `src/gui/core/flags.ixx` | `layout_state`、`propagate_mask` |
| **工具** | `src/gui/core/ui.util.ixx` | `update_layout_policy_setting`、`set_fill_parent` |

### 1.2 布局执行流程

```
1. resize(size)
   └─ resize_impl() → clamped_fsize clamp → notify_layout_changed(propagate_mask)

2. notify_layout_changed(propagation)
   ├─ local 位 → 标记自身 dirty
   ├─ super 位 + 父可接收 → 通知父 children_changed
   │    └─ 若父 intercept_lower_to_isolated → 停止向上传播，走 isolated 路径
   └─ child 位 + 可广播 → 通知子 parent_changed

3. try_layout()
   └─ layout_state.any_lower_changed() → layout_elem()（虚方法）
        └─ 容器实现：计算可用空间 → 分配 cell 尺寸 → cell.apply() → 递归布局

4. pre_acquire_size(extent)
   └─ 预查询：给定部分尺寸约束时，元素期望的尺寸（用于 pending 尺寸解析）
```

---

## 2. 布局策略系统 (Layout Policy)

### 2.1 `layout_policy` — 布局方向

```cpp
// src/gui/core/layout/policy.ixx:29
enum class layout_policy : std::uint8_t{
    none,         // 无布局关系
    hori_major,   // 主轴水平
    vert_major,   // 主轴垂直
};
```

### 2.2 `layout_specifier` — 布局方向映射器

```cpp
// src/gui/core/layout/policy.ixx:36
struct layout_specifier{
    // 打包编码：self(2bit) + none_map(2bit) + hori_major_map(2bit) + vert_major_map(2bit) = 8 bit
    layout_policy self();            // 自身策略
    layout_policy map_none();        // 父为 none 时的映射
    layout_policy map_hori_major();  // 父为 hori_major 时的映射
    layout_policy map_vert_major();  // 父为 vert_major 时的映射
    layout_policy resolve(layout_policy parent_policy); // 解析后的实际策略

    // 工厂方法
    static layout_specifier fixed(layout_policy);   // 所有映射固定
    static layout_specifier identity();              // naive 继承
    static layout_specifier transpose();             // 转置继承（h/v 互换）
};
```

当一个元素自身 policy 为 `none` 时，通过 `resolve()` 查询映射表决定实际方向。这使得元素可以从父级继承布局方向而无须显式声明。

### 2.3 `directional_layout_specifier` — 方向性布局策略

用于 `sequence`、`head_body` 等必须有方向的容器。所有映射值必须是 `hori_major` 或 `vert_major`（不允许 `none`）。

### 2.4 `layout_policy_setting` — 策略设置（tag union）

可存储原始 `layout_policy` 值或完整 `layout_specifier` 的压缩值。

### 2.5 策略传播

- `elem::propagate_layout_policy(parent_policy)` — 自顶向下传播
- `elem::search_layout_policy(from, allowNone)` — 向上查找最近的非 `none` 策略
- 容器的 `set_layout_policy_impl(setting)` 被调用来接受策略变更

---

## 3. 尺寸系统 (Size System)

### 3.1 `size_category` — 尺寸类别

```cpp
// src/gui/core/layout/policy.ixx:330
enum class size_category{
    passive,   // 被动：占据剩余空间，按权重分配
    scaling,   // 缩放：基于另一个维度计算（如 height = width × ratio）
    mastering, // 主导：固定像素值
    pending,   // 待定：延迟到查询子元素 pre_acquire_size() 时确定
};
```

### 3.2 `stated_size` — 声明的尺寸

```cpp
// src/gui/core/layout/policy.ixx:337
struct stated_size{
    size_category type;
    float value;  // size(mastering) 或 weight(passive) 或 ratio(scaling)
};
```

`try_promote_by(other)` — 合并两个尺寸声明（取更严格的约束）。

### 3.3 `stated_extent` — 二维尺寸声明

```cpp
// src/gui/core/layout/policy.ixx:406
struct stated_extent{
    stated_size width, height;
};
```

### 3.4 `optional_mastering_extent` — 运行时尺寸约束

```cpp
// src/gui/core/layout/policy.ixx:479
struct optional_mastering_extent{
    // 每个维度要么是具体值(mastering)，要么是 pending_size (=inf)
    // pending_size 表示"该维度未定，请查询子元素"
    float width_, height_;
};
```

每个元素都有 `restriction_extent`（公开成员），用于节流子元素的可用空间。

### 3.5 `expand_policy` — 容器尺寸策略

```cpp
// src/gui/core/layout/policy.ixx:324
enum class expand_policy{
    resize_to_fit, // 容器自动缩放以容纳子元素
    passive,       // 容器固定大小，子元素受约束
    prefer,        // 类似 resize_to_fit，同时尊重 preferred_size 上限
};
```

### 3.6 `clamped_size<float>` — 尺寸钳制

每个元素都有 `clamped_fsize`（min/max size），用于钳制最终尺寸。定义于 `src/gui/core/clamped_size.ixx`。

---

## 4. 对齐与间距 (Alignment & Spacing)

### 4.1 `align::pos` — 位掩码对齐

```cpp
// src/align.ixx:182
enum class pos : unsigned char{
    none,
    left = 0b0001,   right = 0b0010,   center_x = 0b0100,
    top  = 0b1000,   bottom = 0b0001'0000, center_y = 0b0010'0000,
    top_left = top | left, center = center_y | center_x, ...
};
```

关键函数：`get_offset_of(align, internal_size, external_rect)` — 计算子元素在区域内的偏移量。

### 4.2 `align::spacing` (= `padding2d<float>`)

```cpp
// src/align.ixx:180
using spacing = padding2d<float>;
// 成员：left, right, bottom, top
// 方法：width(), height(), extent(), set(), set_hori(), set_vert(), scl()
```

每个元素有 `boarder_` — 元素的边框/内边距，在 `resize` 时从可用空间中扣除。

### 4.3 `padding1d<T>`

```cpp
// src/align.ixx:34
template <typename T>
struct padding1d{ T pre; T post; };
// length() = pre + post
```

用于一维布局（如 `sequence` 中的 cell padding）。

---

## 5. 单元格系统 (Cell System)

Cell 是"元素 + 布局元数据"的组合体，描述子元素在容器中的位置和尺寸。

### 5.1 `basic_cell` — 基础单元格

```cpp
// src/gui/core/layout/cell.ixx:44
struct basic_cell{
    rect allocated_region{};                // 分配的矩形区域
    align::pos unsaturate_cell_elem_align;  // 区域内对齐（默认 center）
    vec2 scaling{1, 1};                    // 每格缩放
    align::spacing margin{};                // 格边距

    vec2 get_relative_src(vec2 actual_extent);  // 计算子元素相对位置
    void apply_to(elem& group, elem& elem, optional_mastering_extent restriction);
    bool update_relative_src(elem& elem, vec2 parent_content_src, float lerp_alpha = 1.0f);
};
```

`apply_to()` 的核心逻辑：
1. 设置子元素的 `context_scaling`
2. 分配的区域减去 margin 后填入 restriction_extent
3. 调用 `resize()` 和 `try_layout()`

### 5.2 `partial_mastering_cell` — 一维单元格（用于 `sequence`）

```cpp
// src/gui/core/layout/cell.ixx:108
struct partial_mastering_cell : basic_cell{
    stated_size stated_size;         // 一维尺寸声明
    align::padding1d<float> pad;     // 一维内边距

    auto& set_size(float);           // mastering 尺寸
    auto& set_pending();             // pending 尺寸
    auto& set_passive(float weight); // passive + 权重
    auto& set_from_ratio(float);     // scaling + 比例
    auto& set_pad(padding1d);
};
```

### 5.3 `mastering_cell` — 二维单元格（用于 `table`）

```cpp
// src/gui/core/layout/cell.ixx:144
struct mastering_cell : basic_cell{
    stated_extent stated_extent;     // 二维尺寸声明
    align::spacing pad;              // 二维内边距
    align::pos unsaturate_cell_align; // 单元格内对齐
    bool end_line;                    // 换行标记
    bool saturate;                    // 拉伸至整行

    // Builder API:
    auto& set_size(vec2);
    auto& set_width(float);
    auto& set_height(float);
    auto& set_width_passive(float weight);
    auto& set_height_passive(float weight);
    auto& set_pending();
    auto& set_pending_weight(vec2);
    auto& set_end_line(bool = true);
    auto& set_pad(align::spacing);
};
```

### 5.4 `grid_cell` — 网格单元格（用于 `grid`）

```cpp
// src/gui/core/elements/gui.grid.ixx:44
struct grid_cell : basic_cell{
    math::vector2<grid_capture_size> extent;
    // grid_capture_size 描述 grid 中单元格的占据范围
};
```

`grid_capture_size` 支持四种范围类型：
- `src_extent` — (起始列, 跨列数)
- `src_dst` — (起始列, 终止列)
- `margin` — (左空白列, 右空白列)
- `dst_extent` — (起始列=grid_size - desc[0] - desc[1], 跨列数)

回退策略：`shrink_or_hide`（收缩）或 `hide`（隐藏重叠区域）。

---

## 6. 布局通知传播系统

### 6.1 `propagate_mask` — 传播方向位掩码

```cpp
// src/gui/core/flags.ixx:11
enum struct propagate_mask : std::uint8_t{
    none  = 0,
    local = 1 << 0,  // 标记自身
    super = 1 << 1,  // 通知父级
    child = 1 << 2,  // 通知子级
    force_upper = 1 << 3, // 强制跨 isolate 向上传播
    all   = local | super | child,
    lower = local | child,
    upper = local | super,
};
```

### 6.2 `layout_state` — 每个元素的布局状态

```cpp
// src/gui/core/flags.ixx:37
struct layout_state{
    propagate_mask context_accept_mask;          // 上下文允许的接收方向
    propagate_mask inherent_accept_mask;          // 元素固有的接收方向
    propagate_mask inherent_broadcast_mask;       // 广播方向
    bool intercept_lower_to_isolated{false};      // 拦截下级变更，走 isolated 路径

    // internal flags: children_changed, parent_changed, local_changed

    bool any_lower_changed();   // local_changed || children_changed
    void notify_self_changed();  // 若 local 可接受则标记
    bool notify_children_changed();
    bool notify_parent_changed();
    void clear();
};
```

### 6.3 两种通知路径

1. **树传播路径（Tree Propagation）**：`notify_layout_changed(propagation)` — 按 `propagate_mask` 位掩码向上/向下传播脏标记
2. **隔离路径（Isolated）**：`notify_isolated_layout_changed()` — 当 `intercept_lower_to_isolated` 为 true 时，子元素的变更不会向上传播，而是在这层被截断并添加到场景的 `isolated_layout_update` 列表

---

## 7. 元素基类的布局虚方法

```cpp
// src/gui/core/infrastructure/element.ixx:548-691

// 预查询尺寸：给定部分约束，返回期望的 content size（或 nullopt）
virtual std::optional<vec2> pre_acquire_size_impl(optional_mastering_extent extent);

// 获取/设置布局策略
virtual layout_policy get_layout_policy() const noexcept;    // 默认返回 none
virtual bool set_layout_policy_impl(layout_policy_setting);  // 容器的实现可能更复杂
bool set_layout_spec(layout_specifier);                      // 公开 API

// 策略传播
bool propagate_layout_policy(layout_policy parent_policy);

// 尺寸变更
virtual bool resize_impl(vec2 size);  // 钳制 → 通知 → 返回是否变化
bool resize(vec2 size, propagate_mask mask);

// 布局执行
virtual void layout_elem();  // 默认仅 clear layout_state
bool try_layout();           // 脏则调用 layout_elem()
```

### `fill_parent` 属性

每个元素有 `fill_parent_`（`math::bool2`）——当属性为 true 的维度上，父元素 resize 时会自动将子元素 resize 到相应维度。

### 元素 Scaling 系统

```
effective_scaling = inherent_scaling_ × context_scaling_
```
- `inherent_scaling_` — 元素自身的缩放
- `context_scaling_` — 从父元素 cell 传播的缩放
- `set_scaling(scl)` — 设置 context scaling，触发通知

---

## 8. 布局容器类型

### 8.1 容器层级

```
elem
 ├─ basic_group                              (src/.../group.ixx:18)
 │   ├─ loose_group                          (group.ixx:221)
 │   │    └─ 无布局关系，仅 fill_parent
 │   └─ universal_group<Cell, Adaptor>       (gui.universal_group.ixx:165)
 │        ├─ sequence                        (gui.sequence.ixx:17)
 │        │    └─ overflow_sequence          (gui.overflow_sequence.ixx:17)
 │        ├─ table                           (gui.table.ixx:158)
 │        ├─ grid                            (gui.grid.ixx:385)
 │        └─ scaling_stack                   (gui.scaling_stack.ixx:8)
 ├─ head_body_base                           (head_body_elem.ixx:15)
 │    └─ head_body                           (head_body_elem.ixx:345)
 │         ├─ head_body_no_invariant         (head_body_elem.ixx:492)
 │         ├─ split_pane                     (gui.split_pane.ixx:16)
 │         └─ collapser                      (gui.collapser.ixx:35)
 ├─ flipper<N>                               (gui.flipper.ixx:11)
 └─ scroll_adaptor_base                      (gui.scroll_pane.ixx:33)
      └─ scroll_adaptor<Item, Interface>     (gui.scroll_pane.ixx:390)
```

---

### 8.2 `sequence` — 一维线性布局（Flex）

**文件**: `src/gui/core/elements/gui.sequence.ixx:17`

等同于 CSS Flexbox 的核心概念。通过 `layout_policy` 参数化方向。

**核心特性**:
- `layout_policy`: 主轴方向（`hori_major` / `vert_major`）
- `expand_policy`: `resize_to_fit` / `passive` / `prefer`
- `align_to_tail`: 子元素是否对齐到尾部
- `template_cell`: 新子元素的默认 cell 模板

**尺寸分配算法**:
1. 收集所有子元素的 `stated_size`
2. `mastering` 优先分配固定尺寸（乘以 scaling）
3. `pending` 查询 `pre_acquire_size()` 获取实际尺寸
4. `scaling` 基于主轴尺寸计算
5. 剩余空间按 `passive` 权重分配
6. 更新子元素的 `allocated_region` 和位置偏移

**使用示例**:
```cpp
auto& seq = scene.create<sequence>(layout_policy::hori_major);
seq.set_expand_policy(layout::expand_policy::passive);
seq.template_cell.set_pad({8, 8});

// 添加 mastering 尺寸的子元素
auto hdl = seq.emplace_back<some_elem>();
hdl.cell().set_size(200);          // 固定 200px
hdl.cell().unsaturate_cell_align = align::pos::center; // 居中对齐

// 添加 pending 尺寸的子元素
seq.emplace_back<some_elem>().cell().set_pending();

// 添加 passive 权重的子元素
seq.emplace_back<some_elem>().cell().set_passive(1.0f);
```

---

### 8.3 `table` — 基于换行标记的网格布局

**文件**: `src/gui/core/elements/gui.table.ixx:158`

类似 CSS Grid + Flow 的结合。通过 `end_line()` 标记换行，自动计算行列分布。

**核心特性**:
- `layout_policy`: 主轴方向
- `expand_policy`: `resize_to_fit` / `passive` / `prefer`
- `entire_align`: 整个 table 在父级中的对齐方式
- `end_line()`: 标记当前 cell 为行尾，开始新行
- `set_edge_pad(padding)`: 自动为边缘 cell 设置 padding
- `template_cell.pad.set(4)`: 默认 cell padding

**网格缓存**:
`update_grid_cache()` 遍历 cells，按 `end_line` 重新计算 `grid_row_counts_` 和 `max_major_size_`。

**单元格配置** (Builder 模式):
```cpp
table.emplace_back<elem>().cell().set_width(120);               // 固定宽度
table.emplace_back<elem>().cell().set_width_passive(2.0f);      // 被动宽度 + 权重
table.emplace_back<elem>().cell().set_pending();                 // pending 宽度
table.emplace_back<elem>().cell().set_pending_weight({true, true}); // pending + 权重
table.emplace_back<elem>().cell().saturate = true;               // 拉伸至整行
table.end_line();                                                 // 换行
```

**使用示例**:
```cpp
auto& t = scene.create<table>();
t.set_expand_policy(layout::expand_policy::prefer);
t.template_cell.pad.set(8);

// 第一行：label + input
t.emplace_back<label>().cell().set_width(100);
t.emplace_back<text_input>().cell().set_pending_weight({false, true});
t.end_line();

// 第二行：一个占据全行的 separator
t.emplace_back<separator>().cell().set_height(20).saturate = true;
t.end_line();

// 第三行：button + button
t.emplace_back<button>().cell().set_width_passive(1);
t.emplace_back<button>().cell().set_width_passive(1);
t.end_line();
```

---

### 8.4 `grid` — 模板化网格布局

**文件**: `src/gui/core/elements/gui.grid.ixx:385`

类似 CSS Grid（template columns/rows）。通过预定义的列/行模板和 `grid_capture_size` 控制子元素跨格。

**核心特性**:
- `extent_spec_`: `math::vector2<grid_dim_spec>` — 列 × 行模板
- `expand_policy`: `resize_to_fit` / `passive` / `prefer`
- 重叠检测与回退（shrink_or_hide / hide）
- `set_has_smooth_pos_animation(true)`: 平滑位置动画

**列/行模板类型**:
| 模板 | 描述 |
|------|------|
| `grid_uniformed_mastering{count, size, pad}` | 等间距主导列 |
| `grid_uniformed_passive{count, pad}` | 等间距被动列 |
| `grid_uniformed_scaling{count, ratio, pad}` | 等间距缩放列 |
| `grid_all_mastering` | 变间距主导列 |
| `grid_all_passive` | 变间距被动列 |
| `grid_all_scaling` | 变间距缩放列 |
| `grid_mixed` | 混合类型列 |

**网格跨度 (grid_capture_size)**:
```cpp
struct grid_capture_size{
    fallback_strategy fall;     // shrink_or_hide / hide
    grid_extent_type type;      // src_extent / src_dst / margin / dst_extent
    std::uint16_t desc[2];
};
```

**使用示例**:
```cpp
auto& g = scene.create<grid>(
    math::vector2<grid_dim_spec>{
        grid_uniformed_mastering{6, 300.f, {4, 4}},  // 6列，每列300px
        grid_uniformed_passive{8, {4, 4}}              // 8行，等分
    }
);

// 占据 (col:0, row:0) 跨2列1行
g.emplace_back<elem>().cell().extent = {
    {.type = src_extent, .desc = {0, 2}},
    {.type = src_extent, .desc = {0, 1}}
};

// 占据列 margin [1, 4] (留左右各1列空白)
g.emplace_back<elem>().cell().extent = {
    {.type = margin, .desc = {1, 1}},
    {.type = src_extent, .desc = {5, 1}}
};
```

---

### 8.5 `scaling_stack` — 比例缩放堆叠

**文件**: `src/gui/core/elements/gui.scaling_stack.ixx:8`

所有子元素按 `region_scale` 比例占据父元素空间，是 XRGUI 最基础的根布局。

**核心特性**:
- 每个子元素通过 `scaled_cell` 中的 `region_scale`（rect）定义其区域比例
- `region_align`: 区域对齐方式
- 隔离变化：子元素布局变更不向上传播

**使用示例**:
```cpp
auto& stack = scene.create<scaling_stack>();
stack.set_fill_parent({true, true});

// 全屏填充
stack.emplace_back<elem>().cell().region_scale = {0, 0, 1, 1};
```

---

### 8.6 `head_body` — 头-体双面板布局

**文件**: `src/gui/core/elements/head_body_elem.ixx:345`

精确支持两个子元素（head + body），head 先分配尺寸，body 占据剩余空间。

**核心特性**:
- `layout_policy`: 布局方向（head 在上/左，body 在下/右）
- `expand_policy`: `resize_to_fit` / `passive` / `prefer`
- `set_head_size(stated_size)`: head 子元素的尺寸声明
- `set_body_size(stated_size)`: body 子元素的尺寸声明
- `set_pad(float)`: head 和 body 之间的间距
- `transpose_head_and_body_`: 交换 head/body 位置

**尺寸分配**:
- 如果 head 是 `mastering`，先分配 head，body 得到剩余空间
- 如果 head 是 `pending`，查询 head 的 `pre_acquire_size()`
- 如果 head 是 `passive`，与 body 按权重分配
- 如果 head 是 `scaling`，基于主轴尺寸计算

**变体**:
- `head_body_no_invariant` — 无约束变体（允许运行时更换子元素）
- `split_pane` — 可拖拽分隔线
- `collapser` — 可折叠/展开面板

---

### 8.7 `split_pane` — 可拖拽分隔面板

**文件**: `src/gui/core/elements/gui.split_pane.ixx:16`

继承自 `head_body_no_invariant`。在 head/body 之间渲染一条可拖拽的分隔线。

**核心特性**:
- `set_split_pos(float)`: 设置分隔位置（0.0~1.0）
- `set_min_margin({min_head, min_body})`: 最小边距约束
- 分隔线交互：hover 时显示高亮分隔线，drag 时实时调整比率
- 拖拽动画：enter/drag/exit 状态机 + 透明度渐变动画
- 内部通过 `set_head_size(passive, ratio)` + `set_body_size(passive, 1-ratio)` 实现

---

### 8.8 `collapser` — 可折叠面板

**文件**: `src/gui/core/elements/gui.collapser.ixx:35`

继承自 `head_body_base`。根据交互状态在展开/折叠之间过渡。

**展开触发条件**:
```cpp
enum struct collapser_expand_cond{
    click,    // 点击 head 区域时切换
    inbound,  // 鼠标悬停时展开
    focus,    // 获得焦点时展开
    pressed,  // 鼠标按下时展开
};
```

**核心特性**:
- `settings.expand_enter_spacing`: 展开起始间距
- `settings.expand_exit_spacing`: 折叠起始间距
- `settings.expand_speed`: 展开/折叠速度
- `util::delayed_animator<float>`: 动画引擎
- `set_update_opacity_during_expand(true)`: 展开过程中更新子元素透明度
- 禁止 passive 尺寸：collapser 中 head/body 必须为 mastering/pending

---

### 8.9 `flipper<N>` — 选项卡切换

**文件**: `src/gui/core/elements/gui.flipper.ixx:11`

运行时在 N 个子元素中切换显示仅一个。

**核心特性**:
- `switch_to(index)`: 切换到指定子元素
- `get_current_active()`: 获取当前活跃子元素
- `expand_policy`: `resize_to_fit` / `passive` / `prefer`

---

### 8.10 `scroll_adaptor` / `scroll_pane` — 滚动面板

**文件**: `src/gui/core/elements/gui.scroll_pane.ixx:33-903`

支持单向滚动内容的面板，使用裁剪变换实现。

**核心特性**:
- `layout_policy`: `hori_major`（水平滚动）或 `vert_major`（垂直滚动）
- `sensitivity_mode`: `absolute`（绝对灵敏度）或 `proportional`（比例灵敏度）
- `scroll_scale`: 滚动比例/像素
- `scroll_bar_stroke_`: 滚动条宽度
- `overlay_scroll_bars_`: 覆盖式滚动条（浮于内容之上）
- `force_hori_scroll_enabled_` / `force_vert_scroll_enabled_`: 强制启用滚动

**滚动条渲染**:
- 根据 `scroll_progress_at(scroll_.temp)` 计算条位置
- `bar_hori_length()` / `bar_vert_length()` 计算条长度（在内容范围内按比例缩放）
- overlay 模式下有淡入/淡出动画

**`scroll_adaptor<Item, Interface>`** 是泛型实现，支持：
- `elem_ptr`（默认 `scroll_pane`）
- 任意值类型 + 自定义 adaptor interface

**使用**:
```cpp
// 默认用法
auto& pane = scene.create<scroll_pane>();
pane.emplace<some_component>(...);

// 包裹 sequence 的 scroll pane
auto& pane = scene.create<scroll_adaptor<sequence>>();
pane.get_elem().set_layout_policy(layout_policy::vert_major);
pane.get_elem().template_cell.set_pad({4, 4});
pane.get_elem().emplace_back<some_component>();
```

---

### 8.11 `overflow_sequence` — 流溢序列

**文件**: `src/gui/core/elements/gui.overflow_sequence.ixx:17`

扩展 `sequence`：当子元素总和超出可用空间时，将部分子元素隐藏（scissor clipping），并可配合 `overflow_elem` 显示"溢出"指示器。

**核心特性**:
- `split_index_`: 在何处分割可见/隐藏元素
- `overflow_elem_`: 溢出指示器元素（如"..."折叠按钮）
- `requires_scissor_`: 是否需要 scissor 裁剪
- 可见性管理：非暴露元素调用 `on_display_state_changed(false)`
- 暴露元素有 `alpha_ctx_fade_in_action` 淡入动画

---

### 8.12 `loose_group` — 无布局约束组

**文件**: `src/gui/core/elements/group.ixx:221`

子元素之间无布局关系，仅通过 `fill_parent` 占据父元素空间。不关心子元素的尺寸变化。

```cpp
auto& g = scene.create<loose_group>();
g.emplace_back<elem>(); // 自由定位
```

---

## 9. 布局关键辅助函数

### 9.1 策略 → 维度映射

```cpp
// src/gui/core/layout/policy.ixx:615-683

// 根据 policy 获取 padding 字段指针
get_pad_ptr(policy) → {major_src, major_dst, minor_src, minor_dst}

// 根据 policy 获取 extent 字段指针
get_extent_ptr<T>(policy) → {major_field, minor_field}

// 根据 policy 获取 vec2 字段指针
get_vec_ptr<T>(policy) → {major_field, minor_field}

// 获取填充 extent
get_pad_extent(policy, boarder) → {major_pad, minor_pad}

// 转置布局方向
transpose_layout(policy)  // hori↔vert
```

### 9.2 混合布局方向（Transpose）

```cpp
// 需求：父级水平排列，但子容器需要垂直布局
container.set_layout_spec(layout_specifier::transpose());
// container 自身无策略，当父为 hori_major 时 → vert_major
```

### 9.3 边框裁剪

```cpp
clip_boarder_from(extent, boarder_extent)  // 从约束中扣除边框
```

---

## 10. 完整示例

### 10.1 根布局 — scaling_stack 填满窗口

```cpp
auto root = scene.create<scaling_stack>();
root.set_fill_parent({true, true});
root.emplace_back<elem>().cell().region_scale = {0, 0, 1, 1};
```

### 10.2 带工具栏的垂直布局

```cpp
auto& page = scene.create<head_body>(layout_policy::vert_major);
page.set_expand_policy(layout::expand_policy::passive);
page.set_head_size(48.f);                    // 工具栏 48px
page.set_pad(4.f);

// Head: 工具栏
page.emplace_head([](sequence& toolbar){
    toolbar.set_layout_policy(layout_policy::hori_major);
    toolbar.template_cell.set_pad({4, 4});
    toolbar.emplace_back<button>().cell().set_size({32, 32});
    toolbar.emplace_back<button>().cell().set_size({32, 32});
});

// Body: 可滚动的 table
page.create_body([](scroll_pane& pane){
    pane.create([](table& t){
        t.set_expand_policy(layout::expand_policy::prefer);
        t.template_cell.pad.set(8);

        t.emplace_back<label>().cell().set_width(100);
        t.emplace_back<text_input>().cell().set_pending_weight({false, true});
        t.end_line();
        // ...
    });
});
```

### 10.3 Grid 布局

```cpp
auto& g = scene.create<grid>(
    math::vector2<grid_dim_spec>{
        grid_uniformed_mastering{4, 200.f, {8, 8}},  // 4 列 200px
        grid_uniformed_passive{3, {4, 4}}              // 3 行等分
    }
);

// 左上角大块 (col:0-2, row:0-2)
g.emplace_back<big_block>().cell().extent = {
    {.type = src_extent, .desc = {0, 3}},  // 跨3列
    {.type = src_extent, .desc = {0, 2}}   // 跨2行
};

// 右侧长条 (col:3, row:0-3)
g.emplace_back<side_bar>().cell().extent = {
    {.type = src_extent, .desc = {3, 1}},  // 1列
    {.type = src_extent, .desc = {0, 3}}   // 跨3行
};
```

### 10.4 带 overflow 的水平工具栏

```cpp
auto& toolbar = scene.create<overflow_sequence>();
toolbar.set_layout_policy(layout_policy::hori_major);
toolbar.template_cell.set_pad({4, 4});

for(auto& item : items){
    toolbar.emplace_back<tool_button>(item).cell().set_size(32);
}

toolbar.set_split_index(5);   // 前 5 个始终可见
toolbar.create_overflow_elem([](overflow_button& btn){ /* ... */ });
```

### 10.5 嵌套 split_pane

```cpp
auto& outer = scene.create<split_pane>(layout_policy::hor_major);
outer.set_split_pos(0.3f);

outer.create_head([](split_pane& inner){
    inner.set_split_pos(0.4f);
    inner.emplace_head<tree_view>();
    inner.emplace_body<properties_panel>();
});

outer.create_body([](scroll_pane& pane){
    pane.create([](table& t){ /* 主内容区 */ });
});
```

---

## 11. 与样式系统的交互

元素通过 `style_tree_metrics` 获取样式驱动的 `inset`（内边距）：

```cpp
// src/gui/core/style/style.tree.interface.ixx:149
struct style_tree_metrics{
    align::spacing inset{};  // 样式驱动的 border/inset
};
```

元素的 `boarder_` 由样式 `inset` 合并而来（取每边最大值）。

---

## 12. 关键枚举速查

| 枚举 | 值 | 用途 |
|------|----|------|
| `layout_policy` | `none`, `hori_major`, `vert_major` | 布局方向 |
| `size_category` | `passive`, `scaling`, `mastering`, `pending` | 尺寸分配策略 |
| `expand_policy` | `resize_to_fit`, `passive`, `prefer` | 容器适应策略 |
| `propagate_mask` | `none`, `local`, `super`, `child`, `force_upper`, `all` | 布局变化传播 |
| `grid_extent_type` | `src_extent`, `src_dst`, `margin`, `dst_extent` | Grid 跨度类型 |
| `collapser_expand_cond` | `click`, `inbound`, `focus`, `pressed` | Collapser 触发条件 |
| `scroll_pane_mode` | `absolute`, `proportional` | 滚动灵敏度模式 |

---

## 13. 参考文件

| 文件 | 内容 |
|------|------|
| `src/gui/core/layout/policy.ixx` | 策略、尺寸类别、约束 |
| `src/gui/core/layout/cell.ixx` | 单元格类型定义 |
| `src/align.ixx` | 对齐位掩码、间距 |
| `src/gui/core/flags.ixx` | 传播掩码、layout_state |
| `src/gui/core/infrastructure/element.ixx` | 元素基类布局虚方法 |
| `src/gui/core/elements/group.ixx` | basic_group、loose_group |
| `src/gui/core/elements/gui.universal_group.ixx` | universal_group 模板 |
| `src/gui/core/elements/gui.sequence.ixx` | 一维线性布局 |
| `src/gui/core/elements/gui.overflow_sequence.ixx` | 流溢序列 |
| `src/gui/core/elements/gui.table.ixx` | 换行网格布局 |
| `src/gui/core/elements/gui.grid.ixx` | 模板化网格布局 |
| `src/gui/core/elements/gui.scaling_stack.ixx` | 比例缩放堆叠 |
| `src/gui/core/elements/head_body_elem.ixx` | 头体双面板 |
| `src/gui/core/elements/gui.split_pane.ixx` | 可拖拽分隔面板 |
| `src/gui/core/elements/gui.collapser.ixx` | 可折叠面板 |
| `src/gui/core/elements/gui.flipper.ixx` | 选项卡切换 |
| `src/gui/core/elements/gui.scroll_pane.ixx` | 滚动面板 |
| `src/gui/core/clamped_size.ixx` | 尺寸钳制 |
| `src.examples/gui.examples.cpp` | 实际使用示例 |
