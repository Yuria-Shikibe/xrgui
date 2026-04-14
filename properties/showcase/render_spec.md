## 总览

自顶向下的绘制过程描述：
1. 如有需要，GUI重新录制平铺的绘制指令到函数指针级平铺绘制栈
2. 对于需要绘制的GUI树，遍历绘制栈N次，绘制函数将图元指令和状态指令推送到渲染器后端，渲染器会在命令提交后得到M个Layer。
3. M个Layer经由渲染器后处理产生I个Layer
4. 如使用了合成器，使用合成器将I个Layer结合外部数据，绘制到J个Layer上

![draw_structure.drawio.svg](properties/showcase/draw_structure.drawio.svg)

#### 目前渲染方案并未完全定稿，在Descriptor Heap的支持完备之后会进行一轮改动，计划是使用Descriptor Heap替换现有的Descriptor Buffer

* [ ] 添加内置的Depth/Stencil缓冲区支持

* 理论上你可以替换自己的渲染器后端，或者向默认后端注册你自己写的着色器，只要它与默认的网格指令兼容
* 同理，后处理着色器也可也自行修改

## GUI渲染流程

* 下图展示了绘图命令的大体数据流。除了图上绘制的以外，还支持采样Image以实现像素级别的Mask

![draw_procedure.drawio.svg](draw_procedure.drawio.svg)