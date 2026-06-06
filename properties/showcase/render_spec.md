# XRGUI 绘制流程概要

本文只保留面向使用者的绘制数据流。具体数据结构、descriptor buffer 录制、GPU resolve、状态 diff 和 attachment 同步语义已放到源码 `/** */` Doxygen 注释中：

- `src/graphic/instruction_draw/recorder.ixx`
- `src/graphic/instruction_draw/binary_trace.ixx`
- `src/graphic/instruction_draw/record_context.ixx`
- `src/graphic/image/image_view_registry.ixx`
- `src.backends/vulkan/renderer.components.ixx`
- `src.backends/vulkan/renderer.ixx`

## 总览

当前默认 Vulkan 后端的高层流程：

1. GUI 元素和样式把抽象绘制指令写入 draw record。
2. 前端按状态和 pass 组织指令，交给 Vulkan batch backend。
3. 如有需要，compute resolver 将抽象指令解析成 GPU 侧几何/索引/元数据。
4. renderer 录制 graphics pass，把结果绘制到 GUI draw attachments。
5. 可选 blit/post-process/compositor pass 继续处理这些 attachments。

![draw_structure.drawio.svg](draw_structure.drawio.svg)

## 用户侧关注点

- 普通 GUI 代码只需要通过元素、样式和控件发出绘制；不要直接依赖内部 draw record 格式。
- 当前命令录制路径使用 Vulkan descriptor buffer；Descriptor Heap 相关代码还不是用户 API 或接入契约。
- 自定义 renderer backend 或 shader 时，应与当前抽象绘制指令、image registry 和 state/pipeline 配置兼容。
- 自定义后处理更适合从 compositor/pass 层接入，而不是绕过 GUI renderer 的 attachment 生命周期。

## GUI 渲染流程图

下图展示绘图命令的大体数据流。图中未完整展开 mask、image sampling、post-process 和 compositor 的所有分支；实现细节以源码注释为准。

![draw_procedure.drawio.svg](draw_procedure.drawio.svg)
