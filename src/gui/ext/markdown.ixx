export module mo_yanxi.gui.markdown;

import std;

namespace mo_yanxi::gui::md {

struct ast_node;

// 使用 vector 管理子节点，打破 variant 的递归大小计算限制，同时避免为每个节点单独 new/unique_ptr
using node_list = std::vector<ast_node>;

// ==========================================
// AST 节点定义 (Inline Elements)
// ==========================================
struct text {
    std::u32string_view content;
};

struct code_span {
    std::u32string_view content;
};

struct emphasis {
    node_list children;
};

struct strong_emphasis {
    node_list children;
};

struct link {
    std::u32string_view url;
    std::u32string_view title;
    node_list children;
};

struct image {
    std::u32string_view url;
    node_list alt;
};

// ==========================================
// AST 节点定义 (Block Elements)
// ==========================================
struct paragraph {
    node_list children;
};

struct heading {
    std::uint32_t level;
    node_list children;
};

struct code_block {
    std::u32string_view language;
    std::u32string_view content;
};

struct list_item {
    node_list children;
};

struct list {
    bool ordered;
    std::uint32_t start_number;
    std::vector<list_item> items;
};

struct thematic_break {
};

// ==========================================
// 表格相关节点定义
// ==========================================
enum class table_align : std::uint8_t {
	none,
	left,
	center,
	right
};

struct table_cell {
	node_list children;
};

struct table {
	std::uint32_t cols{};
	std::uint32_t rows{};
	// std::uint32_t header_rows = 1; // 默认第一行是表头

	std::vector<table_align> alignments{}; // 长度等于 cols
	std::vector<table_cell> cells{};       // 展平的一维数组，大小为 rows * cols

	// 使用 C++23 mdspan 提供 O(1) 的二维视图访问
	auto get_grid() noexcept {
		return std::mdspan(
			cells.data(), rows, cols
		);
	}

	auto get_grid() const noexcept {
		return std::mdspan(
			cells.data(), rows, cols
		);
	}

	// 使用 C++20 span 提供按行访问
	std::span<table_cell> get_row(std::uint32_t row_idx) {
		if (row_idx >= rows) return {};
		return std::span<table_cell>(cells.data() + row_idx * cols, cols);
	}

	std::span<const table_cell> get_row(std::uint32_t row_idx) const {
		if (row_idx >= rows) return {};
		return std::span<const table_cell>(cells.data() + row_idx * cols, cols);
	}
};

// 核心节点 Variant (已包含新增的 image 和 table)
using node_variant = std::variant<
    text, code_span, emphasis, strong_emphasis, link, image, // 行内元素
    paragraph, heading, code_block, list, thematic_break, table // 块级元素
>;

struct ast_node {
    std::size_t start_pos; // 记录对应于输入字符串视图的起始下标
    node_variant data;
};

// ==========================================
// Markdown 解析器类
// ==========================================
export
class markdown_parser {
public:
    constexpr explicit markdown_parser(std::u32string_view input) : input_data_(input) {
    }

    // 解析入口
    constexpr node_list parse() const {
        return parse_blocks(input_data_);
    }

private:
    std::u32string_view input_data_;

    // 获取某个视图相对于整个 input_data_ 的绝对起始位置
    constexpr std::size_t get_absolute_pos(std::u32string_view sub) const noexcept {
        return static_cast<std::size_t>(sub.data() - input_data_.data());
    }

    // ==========================================
    // 阶段 1: 块级解析 (Block Parsing)
    // ==========================================
    constexpr node_list parse_blocks(std::u32string_view text_block) const {
        node_list blocks;
        std::size_t current_pos = 0;

        while (current_pos < text_block.size()) {
            std::size_t line_end = text_block.find(U'\n', current_pos);
            if (line_end == std::u32string_view::npos) {
                line_end = text_block.size();
            }

            std::u32string_view line = text_block.substr(current_pos, line_end - current_pos);

            // 跳过空行
            if (is_blank_line(line)) {
                current_pos = line_end + 1;
                continue;
            }

            std::size_t current_abs_pos = get_absolute_pos(line);

            // 检查标题 (Headings)
            if (auto h_level = count_heading_level(line); h_level > 0) {
                std::u32string_view content = line.substr(h_level);
                content = trim_left(content);

                heading h_node;
                h_node.level = h_level;
                h_node.children = parse_inlines(content);
                blocks.push_back(ast_node{ current_abs_pos, std::move(h_node) });

                current_pos = line_end + 1;
                continue;
            }

            // 检查代码块 (Fenced Code Blocks)
            if (line.starts_with(U"```")) {
                std::u32string_view lang = trim_whitespace(line.substr(3));
                std::size_t block_end = text_block.find(U"\n```", line_end);

                code_block cb_node;
                cb_node.language = lang;

                if (block_end != std::u32string_view::npos) {
                    cb_node.content = text_block.substr(line_end + 1, block_end - (line_end + 1));
                    current_pos = block_end + 4; // Skip \n```
                } else {
                    // 没有闭合的代码块处理到文件尾
                    cb_node.content = text_block.substr(line_end + 1);
                    current_pos = text_block.size();
                }
                blocks.push_back(ast_node{ current_abs_pos, std::move(cb_node) });
                continue;
            }

            // 检查表格 (Tables)
            // 检查表格 (Tables)
            if (is_table_start(text_block, current_pos)) {
                table tbl_node;
                bool is_header = true;
                bool has_divider = false;
                std::size_t scan_pos = current_pos;

                while (scan_pos < text_block.size()) {
                    std::size_t next_line_end = text_block.find(U'\n', scan_pos);
                    if (next_line_end == std::u32string_view::npos) next_line_end = text_block.size();

                    std::u32string_view scan_line = text_block.substr(scan_pos, next_line_end - scan_pos);

                    if (!is_table_row(scan_line)) {
                        break; // 表格结束
                    }

                    // 处理分隔行，提取对齐信息
                    if (is_header && !has_divider && is_table_divider(scan_line)) {
                        has_divider = true;
                        auto align_texts = split_table_cells(scan_line);
                        tbl_node.alignments = parse_table_alignments(align_texts);
                        // 保证对齐信息的数量和列数严格一致
                        tbl_node.alignments.resize(tbl_node.cols, table_align::none);
                        scan_pos = next_line_end + 1;
                        continue;
                    }

                    auto cells_text = split_table_cells(scan_line);

                    // 如果是表头行，初始化列数并分配内存
                    if (is_header && !has_divider) {
                        tbl_node.cols = static_cast<std::uint32_t>(cells_text.size());
                        tbl_node.alignments.resize(tbl_node.cols, table_align::none);
                        is_header = false;
                    }

                    // 向展平的一维 vector 中填充当前行的单元格
                    // 强制对齐到 tbl_node.cols，多退少补 (Markdown标准行为)
                    for (std::uint32_t i = 0; i < tbl_node.cols; ++i) {
                        table_cell cell;
                        if (i < cells_text.size()) {
                            cell.children = parse_inlines(cells_text[i]);
                        }
                        tbl_node.cells.push_back(std::move(cell));
                    }
                    tbl_node.rows++;

                    scan_pos = next_line_end + 1;
                }

                blocks.push_back(ast_node{ current_abs_pos, std::move(tbl_node) });
                current_pos = scan_pos;
                continue;
            }

            // 默认回退为段落 (Paragraph)
            std::size_t para_end = current_pos;
            while (para_end < text_block.size()) {
                std::size_t next_line_end = text_block.find(U'\n', para_end);
                if (next_line_end == std::u32string_view::npos) next_line_end = text_block.size();

                std::u32string_view next_line = text_block.substr(para_end, next_line_end - para_end);
                if (is_blank_line(next_line) || count_heading_level(next_line) > 0 || 
                    next_line.starts_with(U"```") || is_table_start(text_block, para_end)) {
                    break;
                }
                para_end = next_line_end + 1;
            }

            if (para_end > text_block.size()) para_end = text_block.size();
            
            std::u32string_view para_content = text_block.substr(current_pos, para_end - current_pos);
            paragraph p_node;
            p_node.children = parse_inlines(trim_whitespace(para_content));
            blocks.push_back(ast_node{ current_abs_pos, std::move(p_node) });

            current_pos = para_end;
        }

        return blocks;
    }

    // ==========================================
    // 阶段 2: 行内解析 (Inline Parsing)
    // ==========================================
    constexpr node_list parse_inlines(std::u32string_view inline_text) const {
        node_list inlines;
        std::size_t current_pos = 0;
        std::size_t text_start = 0;

        auto flush_text = [&]() {
            if (current_pos > text_start) {
                text t_node;
                std::u32string_view t_content = inline_text.substr(text_start, current_pos - text_start);
                t_node.content = t_content;
                inlines.push_back(ast_node{ get_absolute_pos(t_content), std::move(t_node) });
            }
        };

        while (current_pos < inline_text.size()) {
            char32_t c = inline_text[current_pos];
            std::size_t current_abs_pos = get_absolute_pos(inline_text) + current_pos;

            // 处理行内代码 `code`
            if (c == U'`') {
                flush_text();
                std::size_t end_backtick = inline_text.find(U'`', current_pos + 1);
                if (end_backtick != std::u32string_view::npos) {
                    code_span cs_node;
                    cs_node.content = inline_text.substr(current_pos + 1, end_backtick - current_pos - 1);
                    inlines.push_back(ast_node{ current_abs_pos, std::move(cs_node) });
                    
                    current_pos = end_backtick + 1;
                    text_start = current_pos;
                    continue;
                }
            }

            // 处理强调 *em* 或 **strong**
            if (c == U'*') {
                flush_text();
                bool is_strong = (current_pos + 1 < inline_text.size() && inline_text[current_pos + 1] == U'*');
                std::u32string_view search_target = is_strong ? U"**" : U"*";
                std::size_t offset = is_strong ? 2 : 1;

                std::size_t end_star = inline_text.find(search_target, current_pos + offset);
                if (end_star != std::u32string_view::npos) {
                    std::u32string_view inner_content = inline_text.substr(current_pos + offset, end_star - current_pos - offset);

                    if (is_strong) {
                        strong_emphasis se_node;
                        se_node.children = parse_inlines(inner_content);
                        inlines.push_back(ast_node{ current_abs_pos, std::move(se_node) });
                    } else {
                        emphasis e_node;
                        e_node.children = parse_inlines(inner_content);
                        inlines.push_back(ast_node{ current_abs_pos, std::move(e_node) });
                    }

                    current_pos = end_star + offset;
                    text_start = current_pos;
                    continue;
                }
            }

            // 处理图片 ![alt](url)
            if (c == U'!' && current_pos + 1 < inline_text.size() && inline_text[current_pos + 1] == U'[') {
                std::size_t end_bracket = inline_text.find(U']', current_pos + 2);
                if (end_bracket != std::u32string_view::npos && end_bracket + 1 < inline_text.size() && inline_text[end_bracket + 1] == U'(') {
                    std::size_t end_paren = inline_text.find(U')', end_bracket + 2);
                    if (end_paren != std::u32string_view::npos) {
                        flush_text();

                        image img_node;
                        std::u32string_view alt_content = inline_text.substr(current_pos + 2, end_bracket - current_pos - 2);
                        img_node.alt = parse_inlines(alt_content);
                        img_node.url = inline_text.substr(end_bracket + 2, end_paren - end_bracket - 2);

                        inlines.push_back(ast_node{ current_abs_pos, std::move(img_node) });
                        
                        current_pos = end_paren + 1;
                        text_start = current_pos;
                        continue;
                    }
                }
            }

            // 处理链接 [title](url)
            if (c == U'[') {
                std::size_t end_bracket = inline_text.find(U']', current_pos + 1);
                if (end_bracket != std::u32string_view::npos && end_bracket + 1 < inline_text.size() && inline_text[end_bracket + 1] == U'(') {
                    std::size_t end_paren = inline_text.find(U')', end_bracket + 2);
                    if (end_paren != std::u32string_view::npos) {
                        flush_text();

                        link l_node;
                        std::u32string_view title_content = inline_text.substr(current_pos + 1, end_bracket - current_pos - 1);
                        l_node.children = parse_inlines(title_content);
                        l_node.url = inline_text.substr(end_bracket + 2, end_paren - end_bracket - 2);

                        inlines.push_back(ast_node{ current_abs_pos, std::move(l_node) });
                        
                        current_pos = end_paren + 1;
                        text_start = current_pos;
                        continue;
                    }
                }
            }

            current_pos++;
        }

        flush_text();
        return inlines;
    }

    // ==========================================
    // 工具函数
    // ==========================================
	static constexpr std::vector<table_align> parse_table_alignments(const std::vector<std::u32string_view>& cells) {
    	std::vector<table_align> alignments;
    	alignments.reserve(cells.size());
    	for (auto cell : cells) {
    		cell = trim_whitespace(cell);
    		bool left = !cell.empty() && cell.front() == U':';
    		bool right = !cell.empty() && cell.back() == U':';
    		if (left && right) {
    			alignments.push_back(table_align::center);
    		} else if (left) {
    			alignments.push_back(table_align::left);
    		} else if (right) {
    			alignments.push_back(table_align::right);
    		} else {
    			alignments.push_back(table_align::none);
    		}
    	}
    	return alignments;
    }

    static constexpr bool is_blank_line(std::u32string_view line) noexcept {
        return std::ranges::all_of(line, [](char32_t c) {
            return c == U' ' || c == U'\t' || c == U'\r';
        });
    }

    static constexpr std::uint32_t count_heading_level(std::u32string_view line) noexcept {
        std::uint32_t level = 0;
        while (level < line.size() && line[level] == U'#') {
            level++;
        }
        if (level > 0 && level <= 6 && level < line.size() && line[level] == U' ') {
            return level;
        }
        return 0;
    }

    static constexpr std::u32string_view trim_left(std::u32string_view s) noexcept {
        auto it = std::ranges::find_if(s, [](char32_t c) {
            return c != U' ' && c != U'\t';
        });
        return s.substr(static_cast<std::size_t>(std::distance(s.begin(), it)));
    }

    static constexpr std::u32string_view trim_whitespace(std::u32string_view s) noexcept {
        if (s.empty()) return s;
        std::size_t start = 0;
        while (start < s.size() && (s[start] == U' ' || s[start] == U'\t' || s[start] == U'\r' || s[start] == U'\n')) {
            start++;
        }
        std::size_t end = s.size();
        while (end > start && (s[end - 1] == U' ' || s[end - 1] == U'\t' || s[end - 1] == U'\r' || s[end - 1] == U'\n')) {
            end--;
        }
        return s.substr(start, end - start);
    }

    // 表格相关辅助函数
    static constexpr bool is_table_row(std::u32string_view line) noexcept {
        line = trim_whitespace(line);
        return !line.empty() && line.find(U'|') != std::u32string_view::npos;
    }

    static constexpr bool is_table_divider(std::u32string_view line) noexcept {
        line = trim_whitespace(line);
        if (line.empty() || line.find(U'|') == std::u32string_view::npos) return false;
        for (char32_t c : line) {
            if (c != U'|' && c != U'-' && c != U':' && c != U' ' && c != U'\t') return false;
        }
        return true;
    }

    constexpr bool is_table_start(std::u32string_view text_block, std::size_t current_pos) const noexcept {
        std::size_t line_end = text_block.find(U'\n', current_pos);
        if (line_end == std::u32string_view::npos) return false;
        
        std::u32string_view line1 = text_block.substr(current_pos, line_end - current_pos);
        if (!is_table_row(line1)) return false;

        std::size_t pos2 = line_end + 1;
        std::size_t line2_end = text_block.find(U'\n', pos2);
        if (line2_end == std::u32string_view::npos) line2_end = text_block.size();
        
        std::u32string_view line2 = text_block.substr(pos2, line2_end - pos2);
        return is_table_divider(line2);
    }

    static constexpr std::vector<std::u32string_view> split_table_cells(std::u32string_view line) {
        std::vector<std::u32string_view> cells;
        line = trim_whitespace(line);
        if (!line.empty() && line.front() == U'|') line = line.substr(1);
        if (!line.empty() && line.back() == U'|') line = line.substr(0, line.size() - 1);

        std::size_t start = 0;
        while (start < line.size()) {
            std::size_t end = line.find(U'|', start);
            if (end == std::u32string_view::npos) {
                cells.push_back(trim_whitespace(line.substr(start)));
                break;
            } else {
                cells.push_back(trim_whitespace(line.substr(start, end - start)));
                start = end + 1;
            }
        }
        return cells;
    }
};
}

namespace mo_yanxi::gui::md {

// 辅助工具：用于 std::visit 的重载解析器
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

export
class ast_printer {
public:
    // 主入口：将 AST 节点列表转换为树状字符串
    static std::string print(const node_list& nodes) {
        std::string result;
        print_nodes(nodes, result, "", true);
        return result;
    }

private:
    // 轻量级 UTF-32 转 UTF-8，方便控制台输出 std::u32string_view
    static std::string to_utf8(std::u32string_view s32) {
        std::string res;
        for (char32_t c : s32) {
            if (c <= 0x7F) {
                res += static_cast<char>(c);
            } else if (c <= 0x7FF) {
                res += static_cast<char>(0xC0 | ((c >> 6) & 0x1F));
                res += static_cast<char>(0x80 | (c & 0x3F));
            } else if (c <= 0xFFFF) {
                res += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
                res += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                res += static_cast<char>(0x80 | (c & 0x3F));
            } else if (c <= 0x10FFFF) {
                res += static_cast<char>(0xF0 | ((c >> 18) & 0x07));
                res += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                res += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                res += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return res;
    }

    // 辅助格式化函数：清理字符串中的换行符，限制显示长度
    static std::string format_excerpt(std::u32string_view s, std::size_t max_len = 30) {
        std::string utf8_str = to_utf8(s);
        // 简单替换换行符为 \n 字面量方便单行显示
        size_t pos = 0;
        while ((pos = utf8_str.find('\n', pos)) != std::string::npos) {
            utf8_str.replace(pos, 1, "\\n");
            pos += 2;
        }
        if (utf8_str.length() > max_len) {
            return "\"" + utf8_str.substr(0, max_len) + "...\"";
        }
        return "\"" + utf8_str + "\"";
    }

    // 递归打印节点列表
    static void print_nodes(const node_list& nodes, std::string& out, std::string prefix, bool is_root) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            bool is_last = (i == nodes.size() - 1);
            print_single_node(nodes[i], out, prefix, is_last, is_root);
        }
    }

    // 打印单个 AST 节点
    static void print_single_node(const ast_node& node, std::string& out, const std::string& prefix, bool is_last, bool is_root) {
        // 构建当前行的前缀线
        std::string current_prefix = is_root ? "" : (prefix + (is_last ? "└── " : "├── "));
        // 构建子节点的前缀线
        std::string child_prefix = is_root ? "" : (prefix + (is_last ? "    " : "│   "));

        out += current_prefix;

        // 使用 std::visit 解析 Variant
        std::visit(overloaded{
            [&](const text& n) {
                out += "Text [pos: " + std::to_string(node.start_pos) + "] " + format_excerpt(n.content) + "\n";
            },
            [&](const code_span& n) {
                out += "CodeSpan [pos: " + std::to_string(node.start_pos) + "] " + format_excerpt(n.content) + "\n";
            },
            [&](const emphasis& n) {
                out += "Emphasis [pos: " + std::to_string(node.start_pos) + "]\n";
                print_nodes(n.children, out, child_prefix, false);
            },
            [&](const strong_emphasis& n) {
                out += "StrongEmphasis [pos: " + std::to_string(node.start_pos) + "]\n";
                print_nodes(n.children, out, child_prefix, false);
            },
            [&](const link& n) {
                out += "Link [pos: " + std::to_string(node.start_pos) + "] url: " + to_utf8(n.url) + "\n";
                print_nodes(n.children, out, child_prefix, false);
            },
            [&](const image& n) {
                out += "Image [pos: " + std::to_string(node.start_pos) + "] url: " + to_utf8(n.url) + "\n";
                print_nodes(n.alt, out, child_prefix, false);
            },
            [&](const paragraph& n) {
                out += "Paragraph [pos: " + std::to_string(node.start_pos) + "]\n";
                print_nodes(n.children, out, child_prefix, false);
            },
            [&](const heading& n) {
                out += "Heading (H" + std::to_string(n.level) + ") [pos: " + std::to_string(node.start_pos) + "]\n";
                print_nodes(n.children, out, child_prefix, false);
            },
            [&](const code_block& n) {
                out += "CodeBlock [pos: " + std::to_string(node.start_pos) + "] lang: " + to_utf8(n.language) + "\n";
                out += child_prefix + "    " + format_excerpt(n.content, 50) + "\n";
            },
        	[&](const table& n) {
	// 顺便打印出表格的行列信息，有助于调试
	out += "Table [pos: " + std::to_string(node.start_pos) +
		   ", cols: " + std::to_string(n.cols) +
		   ", rows: " + std::to_string(n.rows) + "]\n";

	for (std::uint32_t r = 0; r < n.rows; ++r) {
		bool is_last_row = (r == n.rows - 1);
		bool is_header = r == 0;

		// 根据索引判断是表头还是表体
		std::string row_name = is_header ? "HeaderRow" : "BodyRow";
		out += child_prefix + (is_last_row ? "└── " : "├── ") + row_name + "\n";

		std::string row_prefix = child_prefix + (is_last_row ? "    " : "│   ");

		// 使用 C++20 span 获取当前行视图
		auto row_span = n.get_row(r);

		for (std::uint32_t c = 0; c < row_span.size(); ++c) {
			bool is_last_cell = (c == row_span.size() - 1);
			out += row_prefix + (is_last_cell ? "└── " : "├── ") + "Cell";

			// 附加打印对齐信息
			if (c < n.alignments.size()) {
				switch (n.alignments[c]) {
					case table_align::left:   out += " [align: left]"; break;
					case table_align::center: out += " [align: center]"; break;
					case table_align::right:  out += " [align: right]"; break;
					case table_align::none:   break;
				}
			}
			out += "\n";

			std::string cell_prefix = row_prefix + (is_last_cell ? "    " : "│   ");

			// 直接遍历单元格的子节点。假设你外部有一个遍历 children 的方法，例如 print_nodes
			// 如果你的代码结构不同，替换为原本处理 children 的逻辑即可
			const auto& cell = row_span[c];
			for (std::size_t i = 0; i < cell.children.size(); ++i) {
				print_single_node(cell.children[i], out, cell_prefix, i == cell.children.size() - 1, false);
			}
		}
	}
},
            [&](const list& n) {
                out += "List [pos: " + std::to_string(node.start_pos) + "] " + (n.ordered ? "Ordered" : "Unordered") + "\n";
                // 列表项解析暂略，可根据后续实现补充
            },
            [&](const thematic_break&) {
                out += "ThematicBreak [pos: " + std::to_string(node.start_pos) + "]\n";
            }
        }, node.data);
    }

};

}