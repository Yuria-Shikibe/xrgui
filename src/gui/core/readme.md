# XRGUI Core Notes

Core API 的详细说明应写在相邻源码声明的 `/** */` Doxygen 注释中。用户侧使用方式集中在 `properties/showcase/user_guide.md`，布局使用速查在 `properties/showcase/layout_doc.md`。

## 当前 core 覆盖范围

- 命令式元素树构建
- 保留式元素生命周期
- 每帧 draw + 局部 layout/update 标记
- Scene 输入分发
- Tooltip 和 Overlay
- React Flow 节点挂载
- 元素 task/action 队列
- 布局容器：`sequence`、`table`、`grid`、`scaling_stack`、`head_body`、`split_pane`、`collapser`、`scroll_pane`、`overflow_sequence`、`flipper`
- 基础控件：`button`、`slider`、`progress_bar`、`menu`、`viewport`

## 维护约定

- 新增或改变 public API 时，在声明附近补充 `/** */` Doxygen 注释。
- Markdown 文档只保留使用路径和索引，不复制长段实现说明。
- 文档与代码冲突时，以当前代码和 `src.hello` / `src.examples` 实际使用为准。

## 待整理方向

- 继续减少过期 TODO 列表，改为 issue 或源码 TODO。
- 为更多控件补齐 Doxygen。
- 为异步任务、进度条绑定和 key mapping 增加更小的示例。
