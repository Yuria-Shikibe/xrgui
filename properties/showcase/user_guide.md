# XRGUI GUI 使用指南

本文档是 GUI 使用方式的集中入口。更细的实现语义应查看源码中的 `/** */` Doxygen 注释；这里优先保留能按当前代码使用的入口、构建模式和常用控件写法。

## 1. 快速运行

默认支持路径是 Windows + MSVC + Vulkan：

```powershell
git submodule update --init --recursive
xmake quickstart
```

`quickstart` 会配置 MSVC debug 构建，按需生成 shader/icon，运行 `xmake doctor`，构建并启动 `xrgui.hello`。

常用命令：

```powershell
xmake doctor
xmake -b xrgui.hello
xmake run xrgui.hello
xmake -b xrgui.example
xmake run xrgui.example
```

`xrgui.hello` 是最小 GUI 接入示例，源码在 `src.hello/main.cpp`。`xrgui.example` 是完整 showcase，包含 compositor、后处理、markdown、CSV、复杂控件和演示页面，不建议作为第一次接入时的最小模板。

## 2. 推荐入口：default_application

最小独立应用继承 `mo_yanxi::gui::cfg::default_application`，只需要实现 `build_gui()`：

```cpp
import std;

import mo_yanxi.gui.cfg.default_application;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.text_edit;

struct hello_app : mo_yanxi::gui::cfg::default_application {
    using default_application::default_application;

private:
    int click_count_{};
    mo_yanxi::gui::label* counter_label_{};

    void build_gui(mo_yanxi::gui::scene& scene,
                   mo_yanxi::gui::loose_group& root) override {
        namespace gui = mo_yanxi::gui;

        auto content = scene.create<gui::sequence>();
        auto& column = static_cast<gui::sequence&>(root.insert(0, std::move(content)));

        column.set_fill_parent({true, true});
        column.set_layout_spec(gui::layout::layout_policy::hori_major);
        column.set_self_border(gui::border_t{}.set(24));
        column.set_style();
        column.template_cell.set_size(56);
        column.template_cell.set_pad({8.f, 8.f});

        column.create_back([](gui::label& label) {
            label.set_style();
            label.set_fit_type(gui::label_fit_type::scl);
            label.text_entire_align = gui::align::pos::center_left;
            label.set_text("XRGUI Hello");
        }).cell().set_size(72);

        column.create_back([this](gui::label& label) {
            counter_label_ = &label;
            label.set_style(gui::style::family_variant::base_only);
            label.set_fit_type(gui::label_fit_type::scl);
            label.text_entire_align = gui::align::pos::center_left;
            label.set_text("Clicks: 0");
        });

        column.create_back([this](gui::button<gui::label>& button) {
            button.set_style(gui::style::family_variant::accent);
            button.set_fit_type(gui::label_fit_type::scl);
            button.text_entire_align = gui::align::pos::center;
            button.set_text("Click");
            button.set_button_callback([this] {
                ++click_count_;
                if (counter_label_ != nullptr) {
                    counter_label_->set_text(std::format("Clicks: {}", click_count_));
                }
            });
        }).cell().set_size(60);

        auto slider = column.emplace_back<gui::slider1d_with_output>();
        slider->set_style();
        slider->set_smooth_drag(true);
        slider->bar_handle_extent = {40.f};
        slider.cell().set_size(52);

        auto input = column.emplace_back<gui::text_edit_prov>();
        input->set_style(gui::style::family_variant::base_only);
        input->set_hint_text(U"Type here");
        input->set_on_changed_interval(30.f);
        input.cell().set_size(96);
    }
};

int main(int argc, char** argv) {
    return mo_yanxi::gui::cfg::run_default_application<hello_app>(
        argc, argv, {.app_name = "XRGUI Hello"});
}
```

`default_application` 负责初始化 platform、FreeType、GLFW、Vulkan context、默认 renderer、image atlas、font manager、scene、main loop、native communicator 和默认资源。派生类构造函数中不要访问 `context()`、`renderer()`、`scene()` 等运行期对象；这些对象在 `run()` 期间才存在。

可重写钩子：

| 钩子 | 调用时机 |
|------|----------|
| `build_gui(scene, root)` | 场景建好后调用一次，用于创建元素树 |
| `before_frame(delta)` | 每帧 layout/draw 前调用 |
| `after_frame()` | 每帧 draw/record 后调用 |
| `on_unhandled_input(event)` | 输入未被 UI 拦截时调用 |

## 3. 创建元素与容器

`scene.create<T>()` 返回 detached `elem_ptr`。它适合先创建再插入普通 group 或 root：

```cpp
auto ptr = scene.create<gui::sequence>();
auto& seq = static_cast<gui::sequence&>(root.insert(0, std::move(ptr)));
```

在带 Cell 的容器中，优先使用 `create_back()` 或 `emplace_back()`。它们返回 `create_handle<Elem, Cell>`，可以同时访问元素和 cell：

```cpp
auto title = seq.create_back([](gui::label& label) {
    label.set_style();
    label.set_text("Title");
});
title.cell().set_size(72);

auto slider = seq.emplace_back<gui::slider1d_with_output>();
slider->set_smooth_drag(true);
slider.cell().set_size(52);
```

核心规则：

| API | 用途 |
|-----|------|
| `scene.create<T>()` | 创建 detached 元素，返回 `elem_ptr` |
| `basic_group::insert()/push_back()` | 把 `elem_ptr` 接到普通 group |
| `container.create_back(lambda)` | 创建、初始化并插入元素，返回 handle |
| `container.emplace_back<T>(args...)` | 构造并插入元素，返回 handle |
| `handle->...` 或 `handle.elem()` | 访问元素 |
| `handle.cell()` | 访问容器 cell 元数据 |

## 4. 布局基础

`layout_policy` 选择 major/minor 轴：

| Policy | 含义 |
|--------|------|
| `layout::layout_policy::hori_major` | major 是 X。`sequence` 中每个子元素获得可用宽度，沿 Y 方向向下排列 |
| `layout::layout_policy::vert_major` | major 是 Y。`sequence` 中每个子元素获得可用高度，沿 X 方向向右排列 |
| `layout::layout_policy::none` | 不声明方向，由容器或父级映射处理 |

Cell 尺寸类别：

| 类别 | Builder | 含义 |
|------|---------|------|
| `mastering` | `set_size()` / `set_width()` / `set_height()` | 固定像素尺寸 |
| `passive` | `set_passive()` / `set_width_passive()` | 按权重分享剩余空间 |
| `pending` | `set_pending()` | 询问子元素内容尺寸 |
| `scaling` | `set_from_ratio()` / `set_width_from_scale()` | 按另一轴比例计算 |

## 5. 常用布局

### sequence

```cpp
auto column = scene.create<gui::sequence>();
auto& seq = static_cast<gui::sequence&>(root.insert(0, std::move(column)));

seq.set_fill_parent({true, true});
seq.set_layout_spec(gui::layout::layout_policy::hori_major);
seq.set_expand_policy(gui::layout::expand_policy::passive);
seq.template_cell.set_pad({8.f, 8.f});

seq.emplace_back<gui::elem>().cell().set_size(48);
seq.emplace_back<gui::elem>().cell().set_passive(1.f);
seq.create_back([](gui::label& label) {
    label.set_style();
    label.set_text("content sized");
}).cell().set_pending();
```

### table

```cpp
auto table_ptr = scene.create<gui::table>();
auto& table = static_cast<gui::table&>(root.insert(0, std::move(table_ptr)));

table.set_expand_policy(gui::layout::expand_policy::prefer);
table.template_cell.pad.set(8);

table.create_back([](gui::label& label) {
    label.set_style();
    label.set_text("Name");
}).cell().set_width(120);

table.emplace_back<gui::text_edit_prov>().cell().set_pending_weight({false, true});
table.end_line();
```

### scaling_stack

`scaling_stack` 常用于全屏分层。每个 child 的 `region_scale` 是父内容区域的归一化矩形。

```cpp
auto stack_ptr = scene.create<gui::scaling_stack>();
auto& stack = static_cast<gui::scaling_stack&>(root.insert(0, std::move(stack_ptr)));
stack.set_fill_parent({true, true});

stack.emplace_back<gui::elem>().cell().region_scale = {0.f, 0.f, 1.f, 1.f};

auto panel = stack.emplace_back<gui::sequence>();
panel.cell().region_scale = {0.f, 0.f, .35f, 1.f};
panel.cell().region_align = gui::align::pos::left;
```

### head_body / split_pane / collapser

```cpp
auto page_ptr = scene.create<gui::head_body>(gui::layout::layout_policy::vert_major);
auto& page = static_cast<gui::head_body&>(root.insert(0, std::move(page_ptr)));
page.set_expand_policy(gui::layout::expand_policy::passive);
page.set_head_size(48.f);
page.set_pad(4.f);

page.create_head([](gui::sequence& toolbar) {
    toolbar.set_layout_spec(gui::layout::layout_policy::hori_major);
    toolbar.template_cell.set_pad({4.f, 4.f});
});

page.create_body([](gui::scroll_pane& pane) {
    pane.create([](gui::table& body) {
        body.set_expand_policy(gui::layout::expand_policy::prefer);
    });
});
```

`split_pane` 和 `collapser` 都基于 head/body 结构。完整演示见 `src.examples/gui.examples.cpp` 中的 `"collapsers"`、`"drag/label"` 页面。

### scroll_pane / scroll_adaptor

```cpp
auto pane = seq.emplace_back<gui::scroll_adaptor<gui::sequence>>();
pane->set_overlay_bar(true);

auto& content = pane->get_elem();
content.set_layout_spec(gui::layout::layout_policy::hori_major);
content.set_expand_policy(gui::layout::expand_policy::prefer);
content.template_cell.set_pad({4.f, 4.f});
```

`scroll_pane` 是 `scroll_adaptor<elem_ptr>` 的别名；如果内容本身是一个具体布局容器，使用 `scroll_adaptor<sequence>`、`scroll_adaptor<table>` 等更方便。

## 6. 常用控件

### label

```cpp
auto label = seq.create_back([](gui::label& label) {
    label.set_style();
    label.set_fit_type(gui::label_fit_type::scl);
    label.text_entire_align = gui::align::pos::center_left;
    label.set_text("Hello");
});
```

### button

```cpp
seq.create_back([](gui::button<gui::label>& button) {
    button.set_style(gui::style::family_variant::accent);
    button.set_text("Apply");
    button.set_button_callback([] {
        // release callback
    });
});
```

### slider

```cpp
auto slider = seq.emplace_back<gui::slider1d_with_output>();
slider->set_style();
slider->set_smooth_drag(true);
slider->set_progress(.5f);

auto& progress_provider = slider->get_provider();
```

`slider1d_with_output::get_provider()` 和 `slider2d_with_output::get_provider()` 暴露 react-flow provider。复合控件 `cpd::named_slider` 额外提供 `get_slider_provider()`。

### text_edit

```cpp
auto input = seq.emplace_back<gui::text_edit_prov>();
input->set_style(gui::style::family_variant::base_only);
input->set_hint_text(U"Type here");
input->set_on_changed_interval(30.f);

auto& text_provider = input->get_provider();
```

## 7. 事件处理

自定义元素继承 `elem` 或某个控件，重写当前实际事件钩子：

```cpp
struct my_elem : mo_yanxi::gui::elem {
    using elem::elem;

    mo_yanxi::gui::events::op_afterwards on_click(
        mo_yanxi::gui::events::click event,
        std::span<mo_yanxi::gui::elem* const> aboves) override {
        if (event.key.on_release()) {
            return mo_yanxi::gui::events::op_afterwards::intercepted;
        }
        return mo_yanxi::gui::events::op_afterwards::fall_through;
    }

    mo_yanxi::gui::events::op_afterwards on_key_input(
        mo_yanxi::input_handle::key_set key) override {
        return mo_yanxi::gui::events::op_afterwards::fall_through;
    }

    mo_yanxi::gui::events::op_afterwards on_unicode_input(char32_t codepoint) override {
        return mo_yanxi::gui::events::op_afterwards::fall_through;
    }
};
```

常用返回值：

| 返回值 | 含义 |
|--------|------|
| `events::op_afterwards::intercepted` | 事件已处理，停止传播 |
| `events::op_afterwards::fall_through` | 放行给下一个候选元素或外部 handler |

常用事件钩子：

| Hook | 说明 |
|------|------|
| `on_click(event, aboves)` | 鼠标按下/释放 |
| `on_drag(event)` | UI 捕获鼠标后的拖动 |
| `on_scroll(event, aboves)` | 滚轮 |
| `on_cursor_moved(event)` | 光标移动 |
| `on_key_input(key)` | 按键 |
| `on_unicode_input(codepoint)` | 文本输入 |
| `on_esc()` | ESC 关闭链 |

## 8. React Flow 与跨线程更新

元素拥有的节点：

```cpp
auto& listener = mo_yanxi::react_flow::attach(
    *label,
    mo_yanxi::react_flow::make_listener([&](float value) {
        label->set_text(std::format("{:.2f}", value));
    }));

listener.connect_predecessor(slider->get_provider());
```

场景独立节点：

```cpp
auto& provider = mo_yanxi::react_flow::attach(
    scene,
    mo_yanxi::react_flow::make_provider<float>(0.5f));
```

从非 GUI 线程修改元素时使用 `elem::sync_run()` 或 `post_task()`，不要直接访问控件状态：

```cpp
my_label.sync_run([](gui::label& label) {
    label.set_text("Updated on GUI thread");
});
```

## 9. Native 通信

GUI 线程不要直接访问窗口对象、剪贴板、IME 或系统光标。通过 `scene.get_communicator()` 发起 native 请求：

```cpp
scene.get_communicator()->set_clipboard("text");
scene.get_communicator()->set_native_cursor_visibility(false);

scene.get_communicator()->request_clipboard(my_edit, [](gui::text_edit& edit, std::string text) {
    // owner-bound callback; edit is still live here
});
```

默认 GLFW 后端在场景资源上安装 communicator，并通过 `window_thread_dispatcher` 把真正的 native 调用放到窗口线程执行。

## 10. 参考入口

| 文件 | 内容 |
|------|------|
| `src.hello/main.cpp` | 最小 `default_application` 示例 |
| `gui.config/default/default_application.ixx` | 默认应用公开 API 与 Doxygen 注释 |
| `src/gui/core/layout/policy.ixx` | layout policy、size category、runtime restriction |
| `src/gui/core/layout/cell.ixx` | Cell builder API |
| `src/gui/core/infrastructure/element.ixx` | 元素事件、布局通知、线程调度 API |
| `src/gui/core/infrastructure/scene.ixx` | scene、native communicator、overlay、task queue API |
| `src.examples/gui.examples.cpp` | 完整 showcase 的实际页面构建代码 |
| `properties/showcase/rich_text_doc.md` | 富文本语法 |
| `properties/showcase/runtime_showcase.md` | 运行效果截图 |
