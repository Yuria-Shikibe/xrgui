# XRGUI Markdown Preview

这个示例文件在运行时从 `assets/markdown/preview.md` 读取，然后交给 markdown AST 和现有 GUI / rich text 系统渲染。

## Inline

支持 *emphasis*、**strong**、`code span with { braces }`。

---

这是同一段里的第一行
这是软换行，按 markdown 规则应该和上一行连成同一行显示。

这是硬换行第一行  
这是硬换行第二行。

这是反斜杠硬换行第一行\
这是反斜杠硬换行第二行。

你也可以直接混写 XRGUI 富文本，比如 {color:#7BC6FF}彩色文字{/color}、{b}加粗{/b}、{i}斜体{/i}。

如果你需要显示字面量大括号，请写成 `{{` 和 `}}`，例如：{{NotAToken}}。

## Code Block

```cpp
struct sample {
    std::string text = "{raw block keeps braces}";
};
```

## Table

| Name         | Align | Notes |
|:-------------|:-----:|------:|
| ```Left``` r | Center | Right |
| Rich         | {color:#FFD37B}Inline token{/color} | `raw {code}` |

## Notes

- markdown 负责结构解析
- rich text 负责行内样式
- GUI 布局系统负责块级排版

> 这是一个引用块。
>
> - 引用中的列表项
> - 另一项
>
> ```txt
> quoted code
> ```



1. 有序列表第一项
2. 有序列表第二项
    - 嵌套无序项 A
    - 嵌套无序项 B
        1. 更深一层 1
        2. 更深一层 2
