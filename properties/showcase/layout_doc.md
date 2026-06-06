# XRGUI 布局使用速查

本文只说明如何使用布局容器。布局策略、尺寸类别、Cell 和布局通知的实现语义已集中到源码 `/** */` Doxygen 注释中：

- `src/gui/core/layout/policy.ixx`
- `src/gui/core/layout/cell.ixx`
- `src/gui/core/infrastructure/element.ixx`
- `src/gui/core/elements/*.ixx`

完整 GUI 接入请先看 [user_guide.md](user_guide.md)。

## 基础概念

`layout_policy` 决定 major/minor 轴：

| Policy | 含义 |
|--------|------|
| `layout::layout_policy::hori_major` | major 是 X，minor 是 Y；`sequence` 沿 Y 向下排列 |
| `layout::layout_policy::vert_major` | major 是 Y，minor 是 X；`sequence` 沿 X 向右排列 |
| `layout::layout_policy::none` | 不声明方向，由父级或容器映射 |

常用尺寸 builder：

| Builder | 用途 |
|---------|------|
| `cell().set_size(64)` / `set_size({w, h})` | 固定尺寸 |
| `cell().set_width(120)` / `set_height(48)` | 固定单轴尺寸 |
| `cell().set_passive(1.f)` / `set_width_passive(1.f)` | 按权重分享剩余空间 |
| `cell().set_pending()` | 由子元素内容决定尺寸 |
| `cell().set_from_ratio(1.f)` / `set_width_from_scale()` | 按另一轴比例计算 |
| `cell().set_pad(...)` | 设置 cell 内间距 |

`scene.create<T>()` 返回 detached `elem_ptr`。在带 Cell 的容器内，优先用 `create_back()` 或 `emplace_back()`：

```cpp
auto h = seq.emplace_back<gui::label>();
h->set_style();
h->set_text("Label");
h.cell().set_size(48);
```

## sequence

一维线性布局。`hori_major` 常用于竖向列表：每个子元素占满宽度，沿 Y 排列。

```cpp
auto ptr = scene.create<gui::sequence>();
auto& seq = static_cast<gui::sequence&>(root.insert(0, std::move(ptr)));

seq.set_fill_parent({true, true});
seq.set_layout_spec(gui::layout::layout_policy::hori_major);
seq.set_expand_policy(gui::layout::expand_policy::passive);
seq.template_cell.set_pad({8.f, 8.f});

seq.create_back([](gui::label& label) {
    label.set_style();
    label.set_text("Fixed row");
}).cell().set_size(56);

seq.emplace_back<gui::elem>().cell().set_passive(1.f);
```

常用选项：

| API | 说明 |
|-----|------|
| `set_expand_policy(passive)` | 容器尺寸由父级决定 |
| `set_expand_policy(resize_to_fit)` | 容器尝试包住子元素 |
| `set_align_to_tail(true)` | 固定尺寸子元素向尾部对齐 |
| `template_cell` | 后续新增子元素的默认 cell |

## table

二维流式表格，通过 `end_line()` 换行。

```cpp
auto ptr = scene.create<gui::table>();
auto& table = static_cast<gui::table&>(root.insert(0, std::move(ptr)));

table.set_expand_policy(gui::layout::expand_policy::prefer);
table.template_cell.pad.set(8);

table.create_back([](gui::label& label) {
    label.set_style();
    label.set_text("Name");
}).cell().set_width(120);

table.emplace_back<gui::text_edit_prov>().cell().set_pending_weight({false, true});
table.end_line();

table.create_back([](gui::button<gui::label>& button) {
    button.set_style();
    button.set_text("Apply");
}).cell().set_width_passive(1.f);
```

常用 cell 字段：

| 字段/API | 说明 |
|----------|------|
| `cell().set_end_line()` | 当前 cell 后换行 |
| `table.end_line()` | 标记最后一个 cell 后换行 |
| `cell().saturate = true` | 尝试占满当前 line 空间 |
| `cell().unsaturate_cell_align` | cell 在行/列槽内的对齐 |
| `table.set_entire_align(...)` | 整个 table 在内容区域中的对齐 |

## scaling_stack

比例堆叠布局，适合根布局和覆盖层。`region_scale` 是归一化矩形。

```cpp
auto ptr = scene.create<gui::scaling_stack>();
auto& stack = static_cast<gui::scaling_stack&>(root.insert(0, std::move(ptr)));
stack.set_fill_parent({true, true});

stack.emplace_back<gui::elem>().cell().region_scale = {0.f, 0.f, 1.f, 1.f};

auto panel = stack.emplace_back<gui::sequence>();
panel.cell().region_scale = {0.f, 0.f, .3f, 1.f};
panel.cell().region_align = gui::align::pos::left;
```

## head_body

两个子元素的头体布局。head 先分配尺寸，body 使用剩余空间。

```cpp
auto ptr = scene.create<gui::head_body>(gui::layout::layout_policy::vert_major);
auto& page = static_cast<gui::head_body&>(root.insert(0, std::move(ptr)));

page.set_expand_policy(gui::layout::expand_policy::passive);
page.set_head_size(48.f);
page.set_pad(4.f);

page.create_head([](gui::sequence& toolbar) {
    toolbar.set_layout_spec(gui::layout::layout_policy::hori_major);
});

page.create_body([](gui::scroll_pane& pane) {
    pane.create([](gui::table& content) {
        content.set_expand_policy(gui::layout::expand_policy::prefer);
    });
});
```

派生容器：

| 类型 | 说明 |
|------|------|
| `split_pane` | 可拖拽调整 head/body 比例 |
| `collapser` | 可展开/折叠 head/body |
| `menu` | 基于 head/body 的菜单容器 |

## scroll_pane / scroll_adaptor

`scroll_pane` 是默认滚动容器。若内部内容是具体布局类型，使用 `scroll_adaptor<T>`。

```cpp
auto pane = seq.emplace_back<gui::scroll_adaptor<gui::sequence>>();
pane->set_overlay_bar(true);
pane->set_layout_spec(gui::layout::layout_policy::hori_major);

auto& content = pane->get_elem();
content.set_expand_policy(gui::layout::expand_policy::prefer);
content.template_cell.set_pad({4.f, 4.f});
content.emplace_back<gui::elem>().cell().set_size(60);
```

## grid

模板化网格布局。列/行模板通过 `grid_dim_spec` 定义，child cell 的 `extent` 定义占格范围。

```cpp
auto ptr = scene.create<gui::grid>(
    mo_yanxi::math::vector2<gui::grid_dim_spec>{
        gui::grid_uniformed_mastering{4, 200.f, {8, 8}},
        gui::grid_uniformed_passive{3, {4, 4}}
    });
auto& grid = static_cast<gui::grid&>(root.insert(0, std::move(ptr)));

grid.emplace_back<gui::elem>().cell().extent = {
    {.type = gui::grid_extent_type::src_extent, .desc = {0, 2}},
    {.type = gui::grid_extent_type::src_extent, .desc = {0, 1}}
};
```

常用 extent 类型：

| 类型 | 含义 |
|------|------|
| `src_extent` | 起始 + 跨度 |
| `src_dst` | 起始 + 终止 |
| `margin` | 从两端留白后填充 |
| `dst_extent` | 从末端倒推 |

## overflow_sequence

当空间不足时隐藏部分子元素，并可显示溢出按钮。

```cpp
auto toolbar = seq.emplace_back<gui::overflow_sequence>();
toolbar->set_layout_spec(gui::layout::layout_policy::vert_major);
toolbar->template_cell.set_size(120).set_pad({2.f, 2.f});

toolbar->create_overflow_elem([](gui::icon_frame& icon) {
    icon.set_style(gui::style::family_variant::base_only);
    icon.interactivity = gui::interactivity_flag::enabled;
}, gui::assets::builtin::shape_id::more);

toolbar->set_split_index(2);
```

## flipper

只显示 N 个子元素中的当前项，适合简单 tab/页面切换。

```cpp
auto flip = seq.emplace_back<gui::flipper<3>>();
flip->switch_to(0);
```

## 参考

实际示例优先参考：

- `src.hello/main.cpp`
- `src.examples/gui.examples.cpp`
- [user_guide.md](user_guide.md)
