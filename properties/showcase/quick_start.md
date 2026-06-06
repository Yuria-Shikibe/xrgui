# XRGUI 快速上手

本页只保留最短启动路径。GUI 的完整使用方式集中在 [user_guide.md](user_guide.md)。

## 最快运行

```powershell
git submodule update --init --recursive
xmake quickstart
```

`quickstart` 会配置 MSVC debug 构建，按需生成 shader/icon，运行 `xmake doctor`，构建并启动 `xrgui.hello`。

只检查环境：

```powershell
xmake doctor
```

只构建最小示例：

```powershell
xmake -b xrgui.hello
```

完整 showcase：

```powershell
xmake -b xrgui.example
xmake run xrgui.example
```

## 推荐入口

第一次接入使用 `mo_yanxi.gui.cfg.default_application`，继承后实现 `build_gui(scene, root)`。最小示例见 `src.hello/main.cpp`，集中说明见 [用户指南：推荐入口](user_guide.md#2-推荐入口default_application)。

## 常见错误

- 不要在派生类构造函数里访问 `context()`、`renderer()`、`scene()` 等运行期对象；这些对象在 `run()` 期间才存在。
- `build_gui()` 只创建元素树，不写阻塞循环；逐帧逻辑放到 `before_frame()` 或 `after_frame()`。
- 运行时找不到 shader、font 或图片时，优先通过 `xmake run` 或 `xmake quickstart` 启动，或确认可执行文件目录下已经复制了 `assets/`。
- 第一次构建缺少 `assets_summary.h` 或内置图标时，运行 `xmake gen_icon` 后重新构建。
- 缺少 `.spv` shader 时，运行 `xmake gen_slang`；如果 `slangc` 不在 PATH，可用当前 task 的 `--complier=...` 参数指定路径。
