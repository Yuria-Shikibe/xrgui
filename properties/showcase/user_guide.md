# XRGUI 用户指南

> XRGUI 是一个**每帧重绘+按需局部更新**、内置**自动布局**、基于 **Vulkan** 渲染后端的**保留式 GUI** 库。采用 C++23 模块编写。

---

## 1. 设计理念

XRGUI 被设计为**应用程序的一个组件**，而非完整的应用框架：

- **不拥有窗口** — 窗口由您创建和管理
- **不运行于主线程** — GUI 线程独立运行，主线程负责输入和渲染提交
- **只输出到附件** — 绘制结果输出到 Vulkan Image
- **即时重绘** — 每帧重绘，但扁平绘制指令仅在状态变化时重录
- **纯命令式** — 所有 UI 通过 C++ 代码构建
- **永远从源码构建** — 不支持动态链接

### 典型集成模式

```
[主线程]                     [GUI线程]
   |                            |
   输入事件 ----------------> 消费事件
   放行GUI -----------------> 布局+绘制
   等待完成 <---------------- 录制完成
   提交GPU命令                  |
   呈现到屏幕                   |
```

---

## 2. 快速开始

### 环境要求

| 依赖 | 用途 |
|------|------|
| MSVC 14.52+ (VS 2026) | 编译器 |
| Vulkan SDK 1.4+ | 渲染后端 |
| xmake | 构建系统 |
| slangc | Shader编译 |
| Python 3.11+ | 构建脚本 |
| Node.js | SVG图标处理 |

### 构建步骤

```bash
# 1. 初始化子模块
git submodule update --init --recursive

# 2. 配置
xmake f --toolchain=msvc -y

# 3. 生成资源（必须，不可跳过）
xmake gen_slang      # 编译 Slang Shader -> SPIR-V
xmake gen_icon       # 处理 SVG 图标

# 4. 运行示例
xmake run xrgui.example
```

### 模式切换

```bash
xmake switch_mode debug    # Debug模式
xmake switch_mode release  # Release模式
```

---

## 3. 架构概览

```
应用层 ---- 创建窗口、注入输入、提交绘制命令
  |
  |- GUI线程 ---- scene -> 元素树 -> 布局 -> 绘制指令
  |                  |
  |                  |- react_flow (数据流)
  |                  |- style_tree (样式)
  |                  -- renderer_frontend (绘制接口)
  |
  |- 渲染后端 (Vulkan)
  |   |- 图形管线 (basic draw, outline SDF, mask)
  |   |- 计算管线 (blit, blend, inverse)
  |   -- 指令解析 (compute resolve)
  |
  -- 合成器 (Compositor)
      |- 高光提取
      |- Bloom
      |- 背景模糊
      -- HDR->SDR 色调映射
```

核心分层：

| 层 | 位置 | 职责 |
|----|------|------|
| GUI Core | src/gui/core/ | 元素、布局、绘制接口、事件、样式 |
| Extension | src/gui/ext/, src/gui/compound/ | 文本编辑、富文本、拾色器等 |
| Backend | src.backends/ | Vulkan渲染器、GLFW窗口、主循环 |
| Default Config | gui.config/default/ | 默认样式、图标、字体配置 |

---

## 4. 核心概念

### Scene 与 Elem

**Scene** 是UI的根容器，拥有所有元素、管理渲染和输入分发：

```cpp
using namespace mo_yanxi::gui;

// 获取全局 manager
auto& ui_mgr = gui::global::manager;

// 创建场景资源
auto& res = ui_mgr.add_scene_resources("my_scene");

// 创建场景
auto result = ui_mgr.add_scene<example_scene, loose_group>(
    "my_scene",
    res,
    true,                          // 是否异步创建根
    std::move(renderer_frontend)   // 渲染器前端
);

auto& scene = result.scene;
auto& root = result.root_group;   // 根元素组
```

**Elem** 是所有UI元素的基类。每个 elem 具备：
- 布局能力（尺寸、位置）
- 绘制能力（通过 renderer_frontend）
- 事件响应（鼠标、键盘、滚动）
- 样式引用
- 异步任务投递

```cpp
// 从 scene 创建元素
auto e = scene.create<some_element>(constructor_args...);

// 获取元素引用
some_element& elem_ref = *e;
```

### 元素生命周期

元素通过 `elem_ptr` 引用计数智能指针管理，与所属 scene 绑定：

```cpp
{
    auto ptr = scene.create<elem>();   // 引用计数 = 1
    auto ptr2 = ptr;                  // 引用计数 = 2
} // 元素被删除
```

### 线程模型

主线程和GUI线程独立运行：

```
主线程                    GUI线程 (xrgui ui thread)
  |                          |
  |  push事件到全局队列      |
  |  放行 (permit) ------>  | 消费事件、布局、绘制
  |  等待完成 <-----------  | 设置完成信号
  |  提交GPU                 |
  |  重置状态               |
```

关键 API：

```cpp
// 主线程：推送帧时间戳和事件
gui::global::event_queue.push_frame_split(delta_time);

// 主线程：放行 GUI 线程执行一帧
main_loop.permit_burst();

// 主线程：等待 GUI 线程完成
main_loop.wait_term();

// GUI 线程：消费事件
global::consume_current_input(scene, unhandled_handler);

// GUI 线程：执行布局
scene.layout();

// GUI 线程：执行绘制
scene.draw();
```

**异步任务**：在GUI线程之外执行耗时操作：

```cpp
util::post_elem_async_task(my_elem, [](MyElem& e) {
    // 在异步工作线程执行
    auto data = heavy_computation();
    return elem_async_yield_task{
        e,
        [data](MyElem& e, scene& s) {
            return process(data);
        },
        [](MyElem& e, scene& s, auto&& r) {
            // 回到 GUI 线程应用结果
            e.apply(r);
        }
    };
});
```

### 数据流 (React Flow)

内置于 scene 中的响应式数据流图，用于在元素之间传递数据：

```cpp
// 生产者节点
auto& provider = scene.request_independent_react_node(
    react_flow::make_provider<float>(0.5f)
);

// 消费者节点
auto& listener = scene.request_independent_react_node(
    react_flow::make_listener([](float val) {
        update_something(val);
    })
);

// 连接
listener.connect_predecessor(provider);

// 变换节点
auto& transformer = scene.request_independent_react_node(
    react_flow::make_transformer([](float val) {
        return std::lerp(0.0f, 100.0f, val);
    })
);

// 链式连接
react_flow::connect_chain(provider, transformer, listener);

// 主动推送
provider.push_value(0.75f);
```

常用节点类型：

| 节点 | 用途 |
|------|------|
| `provider<T>` | 数据生产者，可推送值 |
| `listener` | 数据消费者，接收回调 |
| `transformer` | 值转换器 |
| `terminal` | 终端节点，需重写 on_update |

---

## 5. 构建界面

### 创建元素

所有元素通过 `scene.create<T>()` 创建：

```cpp
// 基础创建
auto e = scene.create<elem>();

// 带构造参数
auto btn = scene.create<button<direct_label>>(callback);

// 用 lambda 初始化
auto label = scene.create<gui::label>([](gui::label& l) {
    l.set_text("Hello World");
    l.set_style();
});
```

### 根布局

使用 `scaling_stack` 填充整个窗口：

```cpp
auto stack = scene.create<scaling_stack>();
stack->set_fill_parent({true, true});

// 添加全屏子元素
auto child = stack->emplace_back<my_element>();
child.cell().region_scale = {0, 0, 1, 1};
```

分层叠加：

```cpp
// 背景层
auto bg = stack->emplace_back<elem>();
bg.cell().region_scale = {0, 0, 1, 1};

// 侧边栏（左侧20%）
auto sidebar = stack->emplace_back<sidebar>();
sidebar.cell().region_scale = {0, 0, 0.2f, 1};

// 主区域（右侧80%）
auto main = stack->emplace_back<main_content>();
main.cell().region_scale = {0.2f, 0, 0.8f, 1};
```

---

## 6. 布局系统

### 6.1 核心概念

**布局策略**：

| 策略 | 含义 |
|------|------|
| `layout_policy::none` | 无布局关系，从父级继承 |
| `layout_policy::hori_major` | 主轴水平 |
| `layout_policy::vert_major` | 主轴垂直 |

**尺寸类别**：

| 类别 | 含义 | 使用场景 |
|------|------|----------|
| `size_category::mastering` | 固定像素值 | 固定宽/高元素 |
| `size_category::passive` | 按权重占剩余空间 | 弹性布局 |
| `size_category::scaling` | 基于另一维度计算 | 保持宽高比 |
| `size_category::pending` | 延迟查询子元素 | 自适应内容 |

**扩展策略**：

| 策略 | 含义 |
|------|------|
| `expand_policy::resize_to_fit` | 容器随子元素缩放 |
| `expand_policy::passive` | 容器固定，子元素受约束 |
| `expand_policy::prefer` | 类似 resize_to_fit，但有上限 |

**对齐** (`align::pos`)：

```cpp
align::pos::top_left   align::pos::center   align::pos::left
align::pos::right       align::pos::top       align::pos::bottom
```

### 6.2 sequence -- 线性布局

类似 CSS Flexbox：

```cpp
// 水平排列
auto& seq = scene.create<sequence>(layout_policy::hori_major);
seq.set_expand_policy(layout::expand_policy::passive);
seq.template_cell.set_pad({8, 8});

// 固定宽度
auto e1 = seq.emplace_back<elem>();
e1.cell().set_size(100);   // mastering 100px

// 自适应宽度
auto e2 = seq.emplace_back<elem>();
e2.cell().set_pending();

// 弹性占用剩余空间
auto e3 = seq.emplace_back<elem>();
e3.cell().set_passive(1.0f);

// 垂直排列
auto& vseq = scene.create<sequence>(layout_policy::vert_major);
vseq.template_cell.set_pad({4, 8});  // {主轴间距, 交叉轴间距}
```

### 6.3 table -- 表格布局

通过 `end_line()` 换行，自动计算行列：

```cpp
auto& t = scene.create<table>();
t.set_expand_policy(layout::expand_policy::prefer);
t.template_cell.pad.set(8);

// 第一行：标签+输入框
t.emplace_back<label>().cell().set_width(100);
t.emplace_back<text_input>().cell().set_pending_weight({false, true});
t.end_line();  // 换行！

// 第二行：占满整行的分隔线
auto sep = t.emplace_back<row_separator>();
sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
sep.cell().set_end_line();

// 第三行：两个等宽按钮
t.emplace_back<button>().cell().set_width_passive(1);
t.emplace_back<button>().cell().set_width_passive(1);
t.end_line();
```

单元格 Builder API：

```cpp
cell.set_size({w, h})           // mastering 固定尺寸
cell.set_width(100)             // 宽度 mastering
cell.set_height(80)             // 高度 mastering
cell.set_pending()              // pending
cell.set_pending_weight({x,y})  // pending+权重
cell.set_width_passive(1.0f)    // 被动宽度+权重
cell.saturate = true            // 拉伸至整行
cell.set_end_line()             // 换行
```

### 6.4 grid -- 网格布局

类似 CSS Grid Template，预定义列/行模板：

```cpp
auto& g = scene.create<grid>(
    math::vector2<grid_dim_spec>{
        grid_uniformed_mastering{6, 300.f, {4, 4}},  // 6列固定300px
        grid_uniformed_passive{8, {4, 4}}              // 8行等分
    }
);

// 占据列0起跨2列；行0起跨1行
g.emplace_back<elem>().cell().extent = {
    {.type = src_extent, .desc = {0, 2}},
    {.type = src_extent, .desc = {0, 1}}
};

// 列margin[1..end-1]（左右各留1列空白）
g.emplace_back<elem>().cell().extent = {
    {.type = margin, .desc = {1, 1}},
    {.type = src_extent, .desc = {5, 1}}
};
```

grid 跨度类型：

| 类型 | 含义 |
|------|------|
| `src_extent` | (起始, 跨度) |
| `src_dst` | (起始, 结束) |
| `margin` | 留白后填充 |
| `dst_extent` | 从末尾倒推 |

列/行模板：

| 模板 | 描述 |
|------|------|
| `grid_uniformed_mastering` | 等宽固定列 |
| `grid_uniformed_passive` | 等宽弹性列 |
| `grid_uniformed_scaling` | 等比例列 |
| `grid_mixed` | 混合类型列 |

### 6.5 head_body / split_pane / collapser

**head_body** -- 双面板布局：

```cpp
auto& hb = scene.create<head_body>(layout_policy::vert_major);
hb.set_expand_policy(layout::expand_policy::passive);
hb.set_head_size(48);    // 头部48px
hb.set_pad(4);           // 头体间距

hb.create_head([](sequence& toolbar) {
    toolbar.set_layout_policy(layout_policy::hori_major);
    toolbar.emplace_back<button>().cell().set_size({32, 32});
});
hb.create_body([](scroll_pane& content) { /* 内容 */ });
```

**split_pane** -- 可拖拽分隔面板：

```cpp
auto& sp = scene.create<split_pane>(layout_policy::hori_major);
sp.set_split_pos(0.3f);  // 头部占30%
sp.set_min_margin({50, 50});

sp.create_head([](tree_view& tv) { /* ... */ });
sp.create_body([](content& c)    { /* ... */ });
```

**collapser** -- 可折叠面板：

```cpp
auto& cp = scene.create<collapser>();
cp.set_expand_cond(collapser_expand_cond::inbound); // 悬停展开
cp.emplace_head<elem>();
cp.set_head_size(50);
cp.create_body([](sequence& body) { /* ... */ });
```

| 触发条件 | 含义 |
|----------|------|
| `click` | 点击head区域切换 |
| `inbound` | 鼠标悬停时展开 |
| `focus` | 获得焦点时展开 |
| `pressed` | 鼠标按下时展开 |

### 6.6 scaling_stack -- 比例堆叠

子元素按 `region_scale` 比例占据空间：

```cpp
auto& stack = scene.create<scaling_stack>();
auto child = stack.emplace_back<elem>();
child.cell().region_scale = {0.1f, 0.1f, 0.9f, 0.9f}; // 10%->90%
```

### 6.7 scroll_pane -- 滚动面板

```cpp
auto& pane = scene.create<scroll_adaptor<sequence>>();
pane.set_overlay_bar(true);  // 覆盖式滚动条
auto& seq = pane.get_elem();
seq.set_expand_policy(layout::expand_policy::prefer);

for (int i = 0; i < 100; ++i) {
    seq.emplace_back<elem>().cell().set_size(60);
}
```

### 6.8 overflow_sequence -- 流溢序列

超出空间时隐藏部分子元素，显示溢出指示器：

```cpp
auto& seq = scene.create<overflow_sequence>();
seq.set_layout_policy(layout_policy::hori_major);
seq.set_split_index(5);  // 前5个始终可见
seq.create_overflow_elem([](icon_frame& btn) {
    btn.interactivity = interactivity_flag::enabled;
}, assets::builtin::shape_id::more);
```

### 6.9 flipper -- 选项卡

```cpp
auto& flip = scene.create<flipper<3>>();
flip.switch_to(0);  // 切换到第一个
```

---

## 7. 元素参考

### 容器/布局元素

| 元素 | 说明 |
|------|------|
| `scaling_stack` | 比例缩放堆叠，常用作根布局 |
| `sequence` | 一维线性排列（类似Flexbox） |
| `overflow_sequence` | 带溢出处理的sequence |
| `table` | 换行标记的二维网格布局 |
| `grid` | 模板化网格布局（类似CSS Grid） |
| `head_body` | 头-体双面板 |
| `split_pane` | 可拖拽分隔线头体面板 |
| `collapser` | 可折叠/展开面板 |
| `flipper<N>` | 选项卡切换 |
| `scroll_pane` | 滚动面板 |
| `loose_group` | 无布局约束组 |
| `menu` | 菜单容器 |

### 基础控件

| 控件 | 说明 |
|------|------|
| `button<T>` | 通用按钮 |
| `slider1d` / `slider2d` | 一维/二维拖动条 |
| `progress_bar` | 进度条（支持环形） |
| `check_box` | 复选框 |
| `dispersed_value_selector` | 离散值选择按钮 |

### 文本与输入

| 控件 | 说明 |
|------|------|
| `label` | 多行文本标签，支持富文本 |
| `direct_label` | 轻量文本标签（按钮内部） |
| `text_edit` | 文本输入框 |

### 展示类

| 控件 | 说明 |
|------|------|
| `viewport` | 2D摄像机视口（可缩放/平移） |
| `image_frame` | 图像框 |
| `icon_frame` | 图标框 |

### 复合控件 (compound)

| 控件 | 说明 |
|------|------|
| `named_slider` | 带名称标签的滑块 |
| `click_collapser` | 点击展开的折叠面板 |
| `color_picker` | 拾色器 |
| `precise_color_picker` | 精确拾色器 |
| `file_selector` | 文件选择对话框 |
| `data_table` | CSV数据表格 |
| `numeric_input_area` | 数值输入区域 |

### 使用示例

**按钮**：

```cpp
auto& btn = scene.create<button<direct_label>>();
btn.elem().set_tokenized_text({"Click Me"});
btn.set_button_callback([](direct_label& e) {
    // 点击回调
});
```

**滑块**：

```cpp
auto& slider = scene.create<slider1d_with_output>();
slider->set_smooth_drag(true);
slider->set_progress(0.5f);
auto& provider = slider->get_slider_provider(); // react_flow节点
```

**复选框**：

```cpp
auto& cb = scene.create<check_box>();
cb->icons[1].components.color = {graphic::colors::pale_green};

auto& listener = scene.request_embedded_react_node(
    react_flow::make_listener([&](bool checked) { /* ... */ })
);
listener.connect_predecessor(cb->get_prov());
```

**进度条（环形）**：

```cpp
auto& prog = scene.create<progress_bar>();
prog->set_style(style::make_ring_progress_style(32));
prog->set_progress_state(progress_state::approach_smooth);
```

**弹窗/对话框 (Overlay)**：

```cpp
scene.create_overlay({
    .extent = {
        {layout::size_category::passive, .4f},  // 宽度40%
        {layout::size_category::scaling, 1.f}
    },
    .align = align::pos::center,
}, [](table& overlay) {
    overlay.emplace_back<elem>().cell().set_size({200, 100});
});

scene.close_overlay(overlay_elem);  // 关闭弹窗
```

---

## 8. 输入处理

### 事件流

```
主线程 push -> 全局队列 -> consume_current_input
-> scene事件方法 -> 命中测试 -> elem回调
```

### 元素事件回调

重写 elem 虚函数：

```cpp
struct my_elem : elem {
    events::op_afterwards on_click(
        const events::click event,
        std::span<elem* const> aboves) override {
        if (event.key.on_release()) {
            return events::op_afterwards::intercepted; // 拦截
        }
        return events::op_afterwards::fall_through;    // 放行
    }

    void on_inbound_changed(bool is_inbound) override {
        // 鼠标进入/离开
    }

    events::op_afterwards on_key(
        const input_handle::key_set key) override { /* ... */ }

    events::op_afterwards on_unicode(char32_t c) override { /* ... */ }

    events::op_afterwards on_scroll(math::vec2 scroll) override { /* ... */ }

    bool update(float delta_in_ticks) override {
        return false; // true=需要重绘
    }
};
```

### 交互控制

```cpp
e.interactivity = interactivity_flag::enabled;  // 启用
e.interactivity = interactivity_flag::none;      // 禁用
```

### Tooltip

```cpp
elem.set_tooltip_state({
    .layout_info = tooltip::align_meta{
        .follow = tooltip::anchor_type::owner,
        .attach_point_spawner = align::pos::top_left,
        .attach_point_tooltip = align::pos::top_right,
    },
}, [](table& tooltip) {
    tooltip.emplace_back<label>().cell().set_size({200, 60});
});
```

---

## 9. 样式系统

通过 `style::family_variant` 选择预设样式：

```cpp
elem.set_style();                              // 默认
elem.set_style(style::family_variant::general); // 通用
elem.set_style(style::family_variant::solid);   // 实心
elem.set_style(style::family_variant::base_only); // 仅基础
elem.set_style(style::family_variant::edge_only); // 仅边框
elem.set_style(style::family_variant::accent);    // 强调色
elem.set_style(style::family_variant::accepted);  // 接受色(绿)
elem.set_style(style::family_variant::warning);   // 警告色(黄)
elem.set_style(style::family_variant::invalid);   // 错误色(红)
```

边框和toggle：

```cpp
elem.set_self_boarder(gui::boarder{}.set(8));  // 四边8px
elem.set_toggled(true);   // active样式
elem.set_toggled(false);  // 正常样式
```

---

## 10. 富文本

在字符串中嵌入指令控制格式，指令使用花括号包裹：

| 指令 | 示例 | 说明 |
|------|------|------|
| 颜色 | `{#:#FF0000}红色{/}` | 设置文本颜色 |
| 字号 | `{s:24}` 或 `{s:*1.5}` | 绝对/相对 |
| 粗体 | `{b}加粗{/b}` | 开关 |
| 斜体 | `{i}斜体{/i}` | 开关 |
| 下划线 | `{u}下划线{/u}` | 开关 |
| 字体 | `{f:Arial}` | 切换字体 |
| 上标 | `{^}上标{-}` | 上标 |
| 下标 | `{_}下标{-}` | 下标 |
| 偏移 | `{off:+0,-10}` | 位置微调 |
| 外框 | `{w:b}` 或 `{w:r}` | 矩形/圆角框 |
| OpenType | `{ftr:+liga}` | 字体特性 |

属性修改器：`=`(绝对) / `+`(相对增加) / `*`(相对乘算)

批量还原：`{/2}` 撤销最近2个指令，`{///}` 撤销所有指令

```cpp
auto& label = scene.create<gui::label>();
label->set_text(R"(
    {s:*1.5}{b}标题{//}
    {#color1}主要文本{/}
    H{_}2{-}O 是水
    {#:#FF0000}红色{/color}
)");

// 原始模式（不解析指令）
label->set_tokenizer_tag(typesetting::tokenize_tag::raw);
```

转义：`{{` 输出 `{`，`}}` 输出 `}`

> 完整富文本文档：[rich_text_doc.md](rich_text_doc.md)

---

## 11. 渲染与合成

### 绘制接口 (renderer_frontend)

采用 push/pop 状态机管理渲染状态：

```cpp
auto& r = scene.renderer();

// 设置渲染状态
r.update_state(fx::pipeline_config{.pipeline_index = gpip::idx::def});
r.update_state(fx::push_constant{gpip::default_draw_constants{...}});
r.update_state(fx::blend::pma::standard);

// 设置裁剪/视口
r.update_state(r.get_full_screen_scissor());
r.update_state(r.get_full_screen_viewport());

// 提交绘制指令
r << fx::rect_aabb{
    .v00 = {x, y},
    .v11 = {x + w, y + h},
    .vert_color = {graphic::colors::white}
};

// 临时状态切换（RAII）
{
    state_guard g{r, fx::blend::pma::additive};
    r << fx::circle{...};
} // 自动恢复
```

常用绘制原语：

| 原语 | 用途 |
|------|------|
| `rect_aabb` | 矩形 |
| `poly` | 多边形/圆 |
| `poly_partial` | 弧形/环形 |
| `parametric_curve` | B-样条曲线 |
| `triangle` | 三角形 |
| `nine_patch_draw_vert_color` | 九宫格绘制 |
| `line_segments` | 折线 |

抗锯齿 (Fringe)：

```cpp
rx << fx::fringe::poly(my_circle, fringe_size);
rx << fx::fringe::poly_partial_with_cap(my_arc, cap_s, edge_s, edge_s);
rx << fx::fringe::curve(curve);
```

### 合成器 (Compositor)

后处理管线：

```cpp
compositor::manager manager{};

auto& input = manager.add_external_resource(...);
auto& highlight = manager.add_pass<...>(meta);
auto& bloom = manager.add_pass<compositor::bloom_pass>(bloom_meta);
auto& tonemap = manager.add_pass<compositor::post_process_pass_with_ubo<tonemap_args>>(h2s_meta);

// 连接依赖
highlight.id()->add_input({{input, 0}});
bloom.pass.add_dep({highlight.id(), 0, 0});
tonemap.id()->add_dep({merge.id(), 0, 0});

manager.sort();
```

---

## 12. 完整示例

以下是一个最小完整应用的骨架：

```cpp
// 1. 初始化
platform::initialize();
font::initialize();
backend::glfw::initialize();

// 2. Vulkan上下文
backend::vulkan::context ctx{appInfo};

// 3. 创建渲染器
auto renderer = backend::vulkan::renderer{renderer_create_info{ ... }};

// 4. 初始化GUI
gui::global::initialize();
gui::global::initialize_assets_manager();

// 5. 创建场景
auto& res = gui::global::manager.add_scene_resources("main");
auto result = gui::global::manager.add_scene<...>(
    "main", res, true, renderer.create_frontend());
auto& scene = result.scene;

// 6. 构建UI
auto stack = scene.create<scaling_stack>();
stack->set_fill_parent({true, true});

auto& body = scene.create<head_body>(layout_policy::vert_major);
body.set_head_size(48);
body.create_head([](sequence& toolbar) { /* ... */ });
body.create_body([](scroll_pane& pane) { /* ... */ });

// 7. 主循环
backend::application_timer timer{};
while (!ctx.window().should_close()) {
    ctx.window().poll_events();
    timer.fetch_time();

    gui::global::event_queue.push_frame_split(timer.global_delta());
    main_loop.permit_burst();
    main_loop.wait_term();

    vk::cmd::submit_command(ctx.graphic_queue(), cmd_bufs, fence);
    ctx.flush();
    main_loop.reset_term();
}

// 8. 清理
main_loop.join();
gui::global::terminate();
backend::glfw::terminate();
font::terminate();
platform::terminate();
```

---

## 13. 扩展阅读

| 文档 | 内容 |
|------|------|
| [runtime_showcase.md](runtime_showcase.md) | 运行时界面截图 |
| [layout_doc.md](layout_doc.md) | 布局系统详细文档 |
| [render_spec.md](render_spec.md) | 渲染流程说明 |
| [rich_text_doc.md](rich_text_doc.md) | 富文本完整指南 |
| [layout_spec.drawio.svg](layout_spec.drawio.svg) | 布局架构图 |
| [structure.drawio.svg](structure.drawio.svg) | 整体架构图 |
| [draw_procedure.drawio.svg](draw_procedure.drawio.svg) | 绘制流程图 |
| [draw_structure.drawio.svg](draw_structure.drawio.svg) | 绘制结构图 |

---

> 库目前处于实验阶段，API可能会变化。有问题请提交Issue。
