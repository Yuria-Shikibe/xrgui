
# TODO LIST

## GUI设计

* [x] 命令式绘制
* [x] 保留式
* [x] 每帧重绘
* [x] 响应式/半自动布局
* [x] 数据流
* [x] 事件管理
* [x] Tooltip
* [x] Overlay

## 数据流
* [ ] 添加时钟脉冲系统
* [ ] 添加更多易用接口


## 组件

### GUI基本组件

* [x] List(Sequence)
* [x] Table
* [x] Grid
* [x] ScrollPane
* [x] ProgressBar(1D) 
* [ ] ProgressBar(ND) 
* [x] Label
* [ ] ImageFrame
* [ ] InputArea

* [x] Slider
* [ ] InputBox
* [ ] CheckBox

### GUI复合组件
* [x] Menu
* [x] Collapser

## 光标样式
* [ ] 自定义样式 
* [ ] 根据元素和可交互状况切换样式 
* [ ] 扩展样式 

## Style
* 一键Style切换？
* [ ] Style管理
* [ ] 调色盘
* [ ] 默认提供的圆角Style

## 默认渲染后端
* [ ] SlideLine渲染
* [ ] 带谓词的抗锯齿
* 临近阴影？

## 待决
* 无限滚动元素？
* 书页布局？
* 可调二分布局？
* 处理ScrollPane滚动时，应该改变子元素绘制时的projection矩阵，然后修改传入的cursor位置；还是直接变换子元素原始坐标？
* I18N
* I18N字体排版
* Markdown渲染
* 音效控制
* 提供深度值
* 如何对非SDF绘制进行抗锯齿
* 圆角Scissor
* 随屏幕缩放
* 根据PPI自动控制尺寸？
* scaling是否控制margin，boarder和padding?

## 待整理
* [ ] 有些元素的layout policy是非横即竖的，不能填none，要强硬这块的类型安全
* [ ] 访问控制是一坨史
* [ ] 命名很尴尬，得改
* [ ] 数据流的接口太难用了
* [ ] 原生支持进度条绑定有进度的异步任务
* [ ] rect_ortho的构造函数目前是反直觉的