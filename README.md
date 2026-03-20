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

D:.
├───compound
│   │   color_picker.ixx
│   │   
│   └───ext
│           named_slider.ixx
│           
├───core
│   │   clamped_size.ixx
│   │   flags.ixx
│   │   gui.alloc.ixx
│   │   gui.global.ixx
│   │   obervable_value.ixx
│   │   readme.md
│   │   ui.util.ixx
│   │   
│   ├───action
│   │       gui.action.elem.ixx
│   │       gui.action.ixx
│   │       gui.action.queue.ixx
│   │       
│   ├───draw
│   │       compound.ixx
│   │       fringe.ixx
│   │       gui.draw_config.ixx
│   │       gui.fx.ixx
│   │       gui.renderer.frontend.ixx
│   │       style_manager.ixx
│   │       trail.ixx
│   │       
│   ├───elements
│   │       group.ixx
│   │       gui.collapser.ixx
│   │       gui.dispersed_value_selector.ixx
│   │       gui.grid.ixx
│   │       gui.menu.ixx
│   │       gui.progress_bar.ixx
│   │       gui.scaling_stack.ixx
│   │       gui.scroll_pane.ixx
│   │       gui.sequence.ixx
│   │       gui.slider.ixx
│   │       gui.split_pane.ixx
│   │       gui.table.ixx
│   │       gui.universal_group.ixx
│   │       gui.viewport.ixx
│   │       head_body_elem.ixx
│   │       
│   ├───impl
│   │       collapser.cpp
│   │       cursor.cpp
│   │       element.cpp
│   │       gui.sequence.cpp
│   │       overlay_manager.cpp
│   │       progress_bar.cpp
│   │       scene.cpp
│   │       scroll_pane.cpp
│   │       slider.cpp
│   │       table.cpp
│   │       tooltip_manager.cpp
│   │       
│   ├───infrastructure
│   │       cursor.ixx
│   │       element.ixx
│   │       elem_ptr.ixx
│   │       events.ixx
│   │       flags.ixx
│   │       infrastructure.ixx
│   │       overlay_manager.ixx
│   │       scene.ixx
│   │       tooltip_interface.ixx
│   │       tooltip_manager.ixx
│   │       type_def.ixx
│   │       ui_manager.ixx
│   │       
│   ├───input
│   │       input_handle.ixx
│   │       key_binding.ixx
│   │       key_constants.ixx
│   │       key_mapping_manager.ixx
│   │       
│   ├───layout
│   │       cell.ixx
│   │       policy.ixx
│   │       
│   ├───misc
│   │       gui.slider_logic.ixx
│   │       
│   └───style
│           style.config.ixx
│           style.interface.ixx
│           style.palette.ixx
│           
└───ext
│   instruction.extension.ixx
│   text_edit_core.ixx
│   text_render_cache.cpp
│   text_render_cache.ixx
│   
├───elements
│       async_label.ixx
│       check_box.ixx
│       image_frame.ixx
│       label.ixx
│       text_edit_v2.ixx
│       text_holder.ixx
│       
├───impl
│       check_box.cpp
│       gui.assets.cpp
│       gui.resource.manager.cpp
│       image_regions.cpp
│       label.cpp
│       text_edit_v2.cpp
│       text_holder.cpp
│       
├───resource_manage
│       gui.assets.ixx
│       gui.drawable.derives.ixx
│       gui.drawable.ixx
│       gui.resource.manager.ixx
│       image_regions.ixx
│       
└───style
round_square.ixx