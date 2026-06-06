# XRGUI 富文本语法

本文是用户侧 token 语法速查。解析模式、恢复规则和 token 分发表的细节维护在源码 Doxygen 注释中：

- `src/font/typesetting.rich_text.tokenized_text.ixx`
- `src/font/typesetting.rich_text.argument.ixx`

## 基本格式

富文本 token 写在 `{}` 中：

```text
{name}
{name:args}
{/name}
```

字面量大括号可以写成 `{{`、`}}`，也可以用反斜杠转义 `\{`、`\}`。反斜杠也可转义 `\\`。

`tokenize_tag` 有三种模式：

| 模式 | 行为 |
|------|------|
| `def` | 默认模式，解析 token、应用转义，并从可见文本中移除 token 字符 |
| `raw` | 原样文本，不解析 token |
| `kep` | 解析 token 并应用转义，但保留原始 token 字符，适合编辑器保持源位置 |

## 数值修改前缀

颜色、字号和偏移支持同一组前缀：

| 前缀 | 含义 |
|------|------|
| 无前缀或 `=` | 绝对设置 |
| `+` | 在当前值上相加 |
| `*` | 在当前值上相乘 |

## Token 列表

### 颜色

名称：`c`、`#`、`color`

```text
{c:#FF0000}红色{/c}
{#:#00FF00}绿色{/#}
{#FFCC66}短写颜色{/color}
{c:*#808080}颜色相乘{/c}
{c:accent}使用 rich_text_look_up_table 中的颜色{/c}
```

`{c}`、`{#}`、`{color}`、`{/c}`、`{/#}`、`{/color}` 都会恢复 fallback 颜色。

### 字号

名称：`s`、`sz`、`size`

```text
{s:24}宽高同为 24 的字号{/s}
{size:24,30}X=24, Y=30{/size}
{sz:*1.5}放大 1.5 倍{/sz}
{s:+2}当前字号加 2{/s}
```

空参数或 slash 形式会恢复字号：`{s}`、`{/s}`、`{/sz}`、`{/size}`。

### 偏移

名称：`off`

```text
{off:10,20}绝对偏移{/off}
{off:+5,-5}相对偏移{/off}
```

`{off}` 和 `{/off}` 恢复偏移。

### 字体

名称：`f`、`font`

```text
{f:Arial}指定字体{/f}
{font:body}使用 rich_text_look_up_table 中的字体别名{/font}
```

`{f}` 或 `{font}` 会设置为 fallback 字体；`{/f}`、`{/font}` 会恢复上一层字体状态。

### 文本样式

```text
{b}粗体{/b}
{i}斜体{/i}
{u}下划线{/u}
```

### 上下标

```text
H{_}2{-}O
E=mc{^}2{-}
```

`{^}` 开启上标，`{_}` 开启下标，`{-}`、`{/^}`、`{/_}` 恢复正常脚本状态。上下标会缩小字号并按当前排版方向调整偏移。

### 文本外框

名称：`wrap`、`w`

```text
{w:b}矩形外框{/w}
{wrap:r}圆角外框{/wrap}
```

当前参数只识别 `b` 和 `r`。空参数或 slash 形式会关闭外框。

### OpenType Feature

名称：`ftr`

参数直接传给 HarfBuzz feature parser：

```text
{ftr:+liga}开启 liga{/ftr}
{ftr:-liga}关闭 liga{/ftr}
{ftr:salt=1}设置 salt=1{/ftr}
```

`{ftr}` 和 `{/ftr}` 恢复 feature 状态。

## 批量恢复

```text
{/2}
{///}
```

`{/2}` 会为最近两个 token 条目生成对应恢复 token。`{///}` 是 slash-only token，会尝试恢复最近三个“可恢复”的 token，跳过无法生成恢复动作的条目。

## 示例

```text
正常文本{c:#FF0000}红色文本{/c}恢复正常。
{s:*1.5}{b}加粗且大 1.5 倍{//}恢复最近两个格式。
H{_}2{-}O 和 E=mc{^}2{-}。
{off:+0,-10}这段文字整体向上浮动 10 像素。{/off}
```
