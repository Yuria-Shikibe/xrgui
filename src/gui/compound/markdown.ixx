export module mo_yanxi.gui.markdown;

import std;
import mo_yanxi.utility;

namespace mo_yanxi::gui::md {

export struct ast_node;
export using node_list = std::vector<ast_node>;

export struct text {
	std::u32string content;
};

export struct code_span {
	std::u32string content;
};

export struct emphasis {
	node_list children;
};

export struct strong_emphasis {
	node_list children;
};

export struct link {
	std::u32string url;
	std::u32string title;
	node_list children;
};

export struct image {
	std::u32string url;
	node_list alt;
};

export struct paragraph {
	node_list children;
};

export struct heading {
	std::uint32_t level;
	node_list children;
};

export struct code_block {
	std::u32string language;
	std::u32string content;
};

export struct list_item {
	node_list blocks;
};

export struct list {
	bool ordered{};
	std::uint32_t start_number{1};
	std::vector<list_item> items;
};

export struct thematic_break {
};

export struct blockquote {
	node_list children;
};

export enum class table_align : std::uint8_t {
	none,
	left,
	center,
	right
};

export struct table_cell {
	node_list children;
};

export struct table {
	std::uint32_t cols{};
	std::uint32_t rows{};
	std::vector<table_align> alignments{};
	std::vector<table_cell> cells{};

	auto get_grid() noexcept {
		return std::mdspan(cells.data(), rows, cols);
	}

	auto get_grid() const noexcept {
		return std::mdspan(cells.data(), rows, cols);
	}

	std::span<table_cell> get_row(std::uint32_t row_idx) {
		if(row_idx >= rows) return {};
		return std::span<table_cell>(cells.data() + row_idx * cols, cols);
	}

	std::span<const table_cell> get_row(std::uint32_t row_idx) const {
		if(row_idx >= rows) return {};
		return std::span<const table_cell>(cells.data() + row_idx * cols, cols);
	}
	};

export using node_variant = std::variant<
	text, code_span, emphasis, strong_emphasis, link, image,
	paragraph, heading, code_block, list, thematic_break, blockquote, table
>;

export struct ast_node {
	std::size_t start_pos;
	node_variant data;
};

struct list_marker_info {
	bool valid{};
	bool ordered{};
	std::uint32_t number{1};
	std::size_t indent{};
	std::size_t marker_width{};
};

export class markdown_parser {
public:
	constexpr explicit markdown_parser(std::u32string_view input) : input_data_(input) {
	}

	constexpr node_list parse() const {
		return parse_blocks(input_data_, 0);
	}

private:
	std::u32string_view input_data_;

	static constexpr bool is_blank_line(std::u32string_view line) noexcept {
		return std::ranges::all_of(line, [](char32_t c) {
			return c == U' ' || c == U'\t' || c == U'\r';
		});
	}

	static constexpr std::uint32_t count_heading_level(std::u32string_view line) noexcept {
		std::uint32_t level = 0;
		while(level < line.size() && line[level] == U'#') {
			level++;
		}
		if(level > 0 && level <= 6 && level < line.size() && line[level] == U' ') {
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
		if(s.empty()) return s;
		std::size_t start = 0;
		while(start < s.size() && (s[start] == U' ' || s[start] == U'\t' || s[start] == U'\r' || s[start] == U'\n')) {
			start++;
		}
		std::size_t end = s.size();
		while(end > start && (s[end - 1] == U' ' || s[end - 1] == U'\t' || s[end - 1] == U'\r' || s[end - 1] == U'\n')) {
			end--;
		}
		return s.substr(start, end - start);
	}

	static constexpr std::u32string normalize_paragraph_line_breaks(std::u32string_view content) {
		std::u32string result;
		result.reserve(content.size());
		for(std::size_t i = 0; i < content.size(); ++i) {
			char32_t c = content[i];
			if(c == U'\r') {
				if(i + 1 < content.size() && content[i + 1] == U'\n') {
					continue;
				}
				c = U'\n';
			}
			if(c == U'\n') {
				std::size_t space_count = 0;
				while(space_count < result.size() && result[result.size() - 1 - space_count] == U' ') {
					++space_count;
				}
				if(!result.empty() && result.back() == U'\\') {
					result.back() = U'\n';
				} else if(space_count >= 2) {
					result.resize(result.size() - space_count);
					result.push_back(U'\n');
				} else {
					result.push_back(U' ');
				}
			} else {
				result.push_back(c);
			}
		}
		return result;
	}

	static constexpr std::size_t count_leading_spaces(std::u32string_view line) noexcept {
		std::size_t count = 0;
		while(count < line.size() && line[count] == U' ') {
			++count;
		}
		return count;
	}

	static constexpr bool is_thematic_break_line(std::u32string_view line) noexcept {
		line = trim_whitespace(line);
		if(line.size() < 3) return false;
		char32_t marker = 0;
		std::size_t count = 0;
		for(char32_t ch : line) {
			if(ch == U' ' || ch == U'\t') continue;
			if(ch != U'-' && ch != U'*' && ch != U'_') return false;
			if(marker == 0) marker = ch;
			if(ch != marker) return false;
			++count;
		}
		return count >= 3;
	}

	static constexpr bool is_table_row(std::u32string_view line) noexcept {
		line = trim_whitespace(line);
		if(line.empty() || line.find(U'|') == std::u32string_view::npos) return false;
		return split_table_cells(line).size() >= 2;
	}

	static constexpr bool is_table_divider_cell(std::u32string_view cell) noexcept {
		cell = trim_whitespace(cell);
		if(cell.empty()) return false;

		if(cell.front() == U':') {
			cell.remove_prefix(1);
		}
		if(!cell.empty() && cell.back() == U':') {
			cell.remove_suffix(1);
		}

		if(cell.size() < 3) return false;
		return std::ranges::all_of(cell, [](char32_t c) {
			return c == U'-';
		});
	}

	static constexpr bool is_table_divider(std::u32string_view line) noexcept {
		line = trim_whitespace(line);
		if(line.empty() || line.find(U'|') == std::u32string_view::npos) return false;

		auto cells = split_table_cells(line);
		if(cells.size() < 2) return false;
		return std::ranges::all_of(cells, [](std::u32string_view cell) {
			return is_table_divider_cell(cell);
		});
	}

	constexpr bool is_table_start(std::u32string_view text_block, std::size_t current_pos) const noexcept {
		std::size_t line_end = text_block.find(U'\n', current_pos);
		if(line_end == std::u32string_view::npos) return false;

		std::u32string_view line1 = text_block.substr(current_pos, line_end - current_pos);
		if(!is_table_row(line1) || is_table_divider(line1)) return false;

		std::size_t pos2 = line_end + 1;
		std::size_t line2_end = text_block.find(U'\n', pos2);
		if(line2_end == std::u32string_view::npos) line2_end = text_block.size();

		std::u32string_view line2 = text_block.substr(pos2, line2_end - pos2);
		if(!is_table_divider(line2)) return false;

		auto header_cells = split_table_cells(line1);
		auto divider_cells = split_table_cells(line2);
		return !header_cells.empty() && header_cells.size() == divider_cells.size();
	}

	static constexpr std::vector<std::u32string_view> split_table_cells(std::u32string_view line) {
		std::vector<std::u32string_view> cells;
		line = trim_whitespace(line);
		if(!line.empty() && line.front() == U'|') line = line.substr(1);
		if(!line.empty() && line.back() == U'|') line = line.substr(0, line.size() - 1);

		std::size_t start = 0;
		while(start < line.size()) {
			std::size_t end = line.find(U'|', start);
			if(end == std::u32string_view::npos) {
				cells.push_back(trim_whitespace(line.substr(start)));
				break;
			}
			cells.push_back(trim_whitespace(line.substr(start, end - start)));
			start = end + 1;
		}
		return cells;
	}

	static constexpr std::u32string_view strip_list_continuation_prefix(std::u32string_view line, const list_marker_info& marker) noexcept {
		const std::size_t leading = count_leading_spaces(line);
		if(leading <= marker.indent) {
			return line.substr(std::min(marker.indent, line.size()));
		}

		const std::size_t removable = std::min(line.size(), marker.indent + std::min(marker.marker_width, leading - marker.indent));
		return line.substr(removable);
	}

	static constexpr std::vector<table_align> parse_table_alignments(const std::vector<std::u32string_view>& cells) {
		std::vector<table_align> alignments;
		alignments.reserve(cells.size());
		for(auto cell : cells) {
			cell = trim_whitespace(cell);
			bool left = !cell.empty() && cell.front() == U':';
			bool right = !cell.empty() && cell.back() == U':';
			if(left && right) {
				alignments.push_back(table_align::center);
			} else if(left) {
				alignments.push_back(table_align::left);
			} else if(right) {
				alignments.push_back(table_align::right);
			} else {
				alignments.push_back(table_align::none);
			}
		}
		return alignments;
	}

	static constexpr list_marker_info parse_list_marker(std::u32string_view line) noexcept {
		list_marker_info info;
		info.indent = count_leading_spaces(line);
		if(info.indent >= line.size()) return info;
		line = line.substr(info.indent);

		if(line.size() >= 2 && (line[0] == U'-' || line[0] == U'*' || line[0] == U'+') && line[1] == U' ') {
			info.valid = true;
			info.ordered = false;
			info.marker_width = 2;
			return info;
		}

		std::size_t pos = 0;
		std::uint32_t number = 0;
		while(pos < line.size() && line[pos] >= U'0' && line[pos] <= U'9') {
			number = number * 10 + static_cast<std::uint32_t>(line[pos] - U'0');
			++pos;
		}
		if(pos > 0 && pos + 1 < line.size() && line[pos] == U'.' && line[pos + 1] == U' ') {
			info.valid = true;
			info.ordered = true;
			info.number = number;
			info.marker_width = pos + 2;
		}
		return info;
	}

	static constexpr std::u32string_view strip_blockquote_marker(std::u32string_view line) noexcept {
		line = trim_left(line);
		if(line.starts_with(U">")) {
			line.remove_prefix(1);
			if(!line.empty() && line.front() == U' ') {
				line.remove_prefix(1);
			}
		}
		return line;
	}

	static constexpr bool is_blockquote_line(std::u32string_view line) noexcept {
		line = trim_left(line);
		return line.starts_with(U">");
	}

	constexpr node_list parse_blocks(std::u32string_view text_block, std::size_t base_pos) const {
		node_list blocks;
		std::size_t current_pos = 0;

		while(current_pos < text_block.size()) {
			std::size_t line_end = text_block.find(U'\n', current_pos);
			if(line_end == std::u32string_view::npos) {
				line_end = text_block.size();
			}

			std::u32string_view line = text_block.substr(current_pos, line_end - current_pos);

			if(is_blank_line(line)) {
				current_pos = std::min(line_end + 1, text_block.size());
				continue;
			}

			std::size_t current_abs_pos = base_pos + current_pos;

			if(auto h_level = count_heading_level(line); h_level > 0) {
				std::u32string_view content = trim_left(line.substr(h_level));
				heading h_node;
				h_node.level = h_level;
				h_node.children = parse_inlines(content, current_abs_pos + (content.data() - line.data()));
				blocks.push_back(ast_node{current_abs_pos, std::move(h_node)});
				current_pos = std::min(line_end + 1, text_block.size());
				continue;
			}

			if(is_thematic_break_line(line)) {
				blocks.push_back(ast_node{current_abs_pos, thematic_break{}});
				current_pos = std::min(line_end + 1, text_block.size());
				continue;
			}

			if(line.starts_with(U"```")) {
				std::u32string_view lang = trim_whitespace(line.substr(3));
				std::size_t block_end = text_block.find(U"\n```", line_end);

				code_block cb_node;
				cb_node.language.assign(lang.begin(), lang.end());

				if(block_end != std::u32string_view::npos) {
					auto content = text_block.substr(line_end + 1, block_end - (line_end + 1));
					cb_node.content.assign(content.begin(), content.end());
					current_pos = std::min(block_end + 4, text_block.size());
				} else {
					auto content = text_block.substr(std::min(line_end + 1, text_block.size()));
					cb_node.content.assign(content.begin(), content.end());
					current_pos = text_block.size();
				}
				blocks.push_back(ast_node{current_abs_pos, std::move(cb_node)});
				continue;
			}

			if(is_blockquote_line(line)) {
				std::size_t scan_pos = current_pos;
				std::size_t quote_start = current_abs_pos;
				std::u32string content;
				bool first = true;

				while(scan_pos < text_block.size()) {
					std::size_t next_line_end = text_block.find(U'\n', scan_pos);
					if(next_line_end == std::u32string_view::npos) next_line_end = text_block.size();
					std::u32string_view scan_line = text_block.substr(scan_pos, next_line_end - scan_pos);
					if(!is_blockquote_line(scan_line) && !is_blank_line(scan_line)) break;

					if(!first) content.push_back(U'\n');
					if(is_blockquote_line(scan_line)) {
						auto stripped = strip_blockquote_marker(scan_line);
						content.append(stripped.begin(), stripped.end());
					}
					first = false;
					scan_pos = std::min(next_line_end + 1, text_block.size());
				}

				blockquote quote_node;
				quote_node.children = parse_blocks(content, quote_start);
				blocks.push_back(ast_node{quote_start, std::move(quote_node)});
				current_pos = scan_pos;
				continue;
			}

			if(auto marker = parse_list_marker(line); marker.valid) {
				list list_node;
				list_node.ordered = marker.ordered;
				list_node.start_number = marker.number;

				std::size_t scan_pos = current_pos;
				while(scan_pos < text_block.size()) {
					std::size_t item_abs_pos = base_pos + scan_pos;
					std::size_t item_line_end = text_block.find(U'\n', scan_pos);
					if(item_line_end == std::u32string_view::npos) item_line_end = text_block.size();
					std::u32string_view item_line = text_block.substr(scan_pos, item_line_end - scan_pos);
					auto item_marker = parse_list_marker(item_line);
					if(!item_marker.valid || item_marker.ordered != list_node.ordered || item_marker.indent != marker.indent) {
						break;
					}

					std::u32string item_content;
					auto first_line = item_line.substr(item_marker.indent + item_marker.marker_width);
					item_content.append(first_line.begin(), first_line.end());

					std::size_t next_pos = std::min(item_line_end + 1, text_block.size());
					while(next_pos < text_block.size()) {
						std::size_t nested_line_end = text_block.find(U'\n', next_pos);
						if(nested_line_end == std::u32string_view::npos) nested_line_end = text_block.size();
						std::u32string_view nested_line = text_block.substr(next_pos, nested_line_end - next_pos);

						if(is_blank_line(nested_line)) {
							item_content.push_back(U'\n');
							item_content.push_back(U'\n');
							next_pos = std::min(nested_line_end + 1, text_block.size());
							continue;
						}

						auto nested_marker = parse_list_marker(nested_line);
						if(nested_marker.valid && nested_marker.indent <= marker.indent) break;
				if(!nested_marker.valid && (count_heading_level(nested_line) > 0 || nested_line.starts_with(U"```") ||
					is_thematic_break_line(nested_line) || is_blockquote_line(nested_line) || is_table_start(text_block, next_pos)))
					break;
				if(!nested_marker.valid && count_leading_spaces(nested_line) <= marker.indent) break;

						item_content.push_back(U'\n');
						std::u32string_view append_line = strip_list_continuation_prefix(nested_line, marker);
						item_content.append(append_line.begin(), append_line.end());
						next_pos = std::min(nested_line_end + 1, text_block.size());
					}

					list_item item;
					item.blocks = parse_blocks(item_content, item_abs_pos + item_marker.indent + item_marker.marker_width);
					list_node.items.push_back(std::move(item));
					scan_pos = next_pos;
				}

				blocks.push_back(ast_node{current_abs_pos, std::move(list_node)});
				current_pos = scan_pos;
				continue;
			}

			if(is_table_start(text_block, current_pos)) {
				table tbl_node;
				bool has_divider = false;
				std::size_t scan_pos = current_pos;

				while(scan_pos < text_block.size()) {
					std::size_t next_line_end = text_block.find(U'\n', scan_pos);
					if(next_line_end == std::u32string_view::npos) next_line_end = text_block.size();

					std::u32string_view scan_line = text_block.substr(scan_pos, next_line_end - scan_pos);
					if(!is_table_row(scan_line)) break;

					if(!has_divider && is_table_divider(scan_line)) {
						has_divider = true;
						auto align_texts = split_table_cells(scan_line);
						tbl_node.alignments = parse_table_alignments(align_texts);
						tbl_node.alignments.resize(tbl_node.cols, table_align::none);
						scan_pos = std::min(next_line_end + 1, text_block.size());
						continue;
					}

					auto cells_text = split_table_cells(scan_line);
					if(!has_divider) {
						tbl_node.cols = static_cast<std::uint32_t>(cells_text.size());
						tbl_node.alignments.resize(tbl_node.cols, table_align::none);
					}

					for(std::uint32_t i = 0; i < tbl_node.cols; ++i) {
						table_cell cell;
						if(i < cells_text.size()) {
							cell.children = parse_inlines(cells_text[i], base_pos + static_cast<std::size_t>(cells_text[i].data() - text_block.data()));
						}
						tbl_node.cells.push_back(std::move(cell));
					}
					++tbl_node.rows;
					scan_pos = std::min(next_line_end + 1, text_block.size());
				}

				blocks.push_back(ast_node{current_abs_pos, std::move(tbl_node)});
				current_pos = scan_pos;
				continue;
			}

			std::size_t para_end = current_pos;
			while(para_end < text_block.size()) {
				std::size_t next_line_end = text_block.find(U'\n', para_end);
				if(next_line_end == std::u32string_view::npos) next_line_end = text_block.size();

				std::u32string_view next_line = text_block.substr(para_end, next_line_end - para_end);
				if(is_blank_line(next_line) || count_heading_level(next_line) > 0 || next_line.starts_with(U"```") ||
					is_thematic_break_line(next_line) || is_blockquote_line(next_line) || parse_list_marker(next_line).valid ||
					is_table_start(text_block, para_end)) {
					break;
				}
				para_end = std::min(next_line_end + 1, text_block.size());
			}

			std::u32string_view para_content = text_block.substr(current_pos, para_end - current_pos);
			paragraph p_node;
			auto normalized = normalize_paragraph_line_breaks(para_content);
			std::u32string_view normalized_view = normalized;
			auto trimmed = trim_whitespace(normalized_view);
			p_node.children = parse_inlines(trimmed, current_abs_pos);
			blocks.push_back(ast_node{current_abs_pos, std::move(p_node)});
			current_pos = para_end;
		}

		return blocks;
	}

	constexpr node_list parse_inlines(std::u32string_view inline_text, std::size_t base_pos) const {
		node_list inlines;
		std::size_t current_pos = 0;
		std::size_t text_start = 0;

			auto flush_text = [&]() {
			if(current_pos > text_start) {
				text t_node;
				std::u32string_view t_content = inline_text.substr(text_start, current_pos - text_start);
				t_node.content.assign(t_content.begin(), t_content.end());
				inlines.push_back(ast_node{base_pos + text_start, std::move(t_node)});
			}
		};

		while(current_pos < inline_text.size()) {
			char32_t c = inline_text[current_pos];
			std::size_t current_abs_pos = base_pos + current_pos;

			if(c == U'`') {
				flush_text();
				std::size_t end_backtick = inline_text.find(U'`', current_pos + 1);
				if(end_backtick != std::u32string_view::npos) {
					code_span cs_node;
					auto content = inline_text.substr(current_pos + 1, end_backtick - current_pos - 1);
					cs_node.content.assign(content.begin(), content.end());
					inlines.push_back(ast_node{current_abs_pos, std::move(cs_node)});
					current_pos = end_backtick + 1;
					text_start = current_pos;
					continue;
				}
			}

			if(c == U'*') {
				flush_text();
				bool is_strong = (current_pos + 1 < inline_text.size() && inline_text[current_pos + 1] == U'*');
				std::u32string_view search_target = is_strong ? U"**" : U"*";
				std::size_t offset = is_strong ? 2 : 1;

				std::size_t end_star = inline_text.find(search_target, current_pos + offset);
				if(end_star != std::u32string_view::npos) {
					std::u32string_view inner_content = inline_text.substr(current_pos + offset, end_star - current_pos - offset);
					if(is_strong) {
						strong_emphasis se_node;
						se_node.children = parse_inlines(inner_content, current_abs_pos + offset);
						inlines.push_back(ast_node{current_abs_pos, std::move(se_node)});
					} else {
						emphasis e_node;
						e_node.children = parse_inlines(inner_content, current_abs_pos + offset);
						inlines.push_back(ast_node{current_abs_pos, std::move(e_node)});
					}
					current_pos = end_star + offset;
					text_start = current_pos;
					continue;
				}
			}

			if(c == U'!' && current_pos + 1 < inline_text.size() && inline_text[current_pos + 1] == U'[') {
				std::size_t end_bracket = inline_text.find(U']', current_pos + 2);
				if(end_bracket != std::u32string_view::npos && end_bracket + 1 < inline_text.size() && inline_text[end_bracket + 1] == U'(') {
					std::size_t end_paren = inline_text.find(U')', end_bracket + 2);
					if(end_paren != std::u32string_view::npos) {
						flush_text();
						image img_node;
						std::u32string_view alt_content = inline_text.substr(current_pos + 2, end_bracket - current_pos - 2);
						img_node.alt = parse_inlines(alt_content, current_abs_pos + 2);
						auto url = inline_text.substr(end_bracket + 2, end_paren - end_bracket - 2);
						img_node.url.assign(url.begin(), url.end());
						inlines.push_back(ast_node{current_abs_pos, std::move(img_node)});
						current_pos = end_paren + 1;
						text_start = current_pos;
						continue;
					}
				}
			}

			if(c == U'[') {
				std::size_t end_bracket = inline_text.find(U']', current_pos + 1);
				if(end_bracket != std::u32string_view::npos && end_bracket + 1 < inline_text.size() && inline_text[end_bracket + 1] == U'(') {
					std::size_t end_paren = inline_text.find(U')', end_bracket + 2);
					if(end_paren != std::u32string_view::npos) {
						flush_text();
						link l_node;
						std::u32string_view title_content = inline_text.substr(current_pos + 1, end_bracket - current_pos - 1);
						l_node.children = parse_inlines(title_content, current_abs_pos + 1);
						auto url = inline_text.substr(end_bracket + 2, end_paren - end_bracket - 2);
						l_node.url.assign(url.begin(), url.end());
						inlines.push_back(ast_node{current_abs_pos, std::move(l_node)});
						current_pos = end_paren + 1;
						text_start = current_pos;
						continue;
					}
				}
			}

			++current_pos;
		}

		flush_text();
		return inlines;
	}
	};
}

namespace mo_yanxi::gui::md {

export class ast_printer {
public:
	static std::string print(const node_list& nodes) {
		std::string result;
		print_nodes(nodes, result, "", true);
		return result;
	}

private:
	static std::string to_utf8(std::u32string_view s32) {
		std::string res;
		for(char32_t c : s32) {
			if(c <= 0x7F) {
				res += static_cast<char>(c);
			} else if(c <= 0x7FF) {
				res += static_cast<char>(0xC0 | ((c >> 6) & 0x1F));
				res += static_cast<char>(0x80 | (c & 0x3F));
			} else if(c <= 0xFFFF) {
				res += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
				res += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				res += static_cast<char>(0x80 | (c & 0x3F));
			} else if(c <= 0x10FFFF) {
				res += static_cast<char>(0xF0 | ((c >> 18) & 0x07));
				res += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				res += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				res += static_cast<char>(0x80 | (c & 0x3F));
			}
		}
		return res;
	}

	static std::string format_excerpt(std::u32string_view s, std::size_t max_len = 30) {
		std::string utf8_str = to_utf8(s);
		size_t pos = 0;
		while((pos = utf8_str.find('\n', pos)) != std::string::npos) {
			utf8_str.replace(pos, 1, "\\n");
			pos += 2;
		}
		if(utf8_str.length() > max_len) {
			return "\"" + utf8_str.substr(0, max_len) + "...\"";
		}
		return "\"" + utf8_str + "\"";
	}

	static void print_nodes(const node_list& nodes, std::string& out, std::string prefix, bool is_root) {
		for(std::size_t i = 0; i < nodes.size(); ++i) {
			print_single_node(nodes[i], out, prefix, i == nodes.size() - 1, is_root);
		}
	}

	static void print_single_node(const ast_node& node, std::string& out, const std::string& prefix, bool is_last, bool is_root) {
		std::string current_prefix = is_root ? "" : (prefix + (is_last ? "└── " : "├── "));
		std::string child_prefix = is_root ? "" : (prefix + (is_last ? "    " : "│   "));
		out += current_prefix;

		std::visit(overload{
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
			[&](const list& n) {
				out += "List [pos: " + std::to_string(node.start_pos) + "] " + (n.ordered ? "Ordered" : "Unordered") + "\n";
				for(std::size_t i = 0; i < n.items.size(); ++i) {
					out += child_prefix + (i == n.items.size() - 1 ? "└── " : "├── ") + "ListItem\n";
					print_nodes(n.items[i].blocks, out, child_prefix + (i == n.items.size() - 1 ? "    " : "│   "), false);
				}
			},
			[&](const thematic_break&) {
				out += "ThematicBreak [pos: " + std::to_string(node.start_pos) + "]\n";
			},
			[&](const blockquote& n) {
				out += "Blockquote [pos: " + std::to_string(node.start_pos) + "]\n";
				print_nodes(n.children, out, child_prefix, false);
			},
			[&](const table& n) {
				out += "Table [pos: " + std::to_string(node.start_pos) + ", cols: " + std::to_string(n.cols) + ", rows: " + std::to_string(n.rows) + "]\n";
				for(std::uint32_t r = 0; r < n.rows; ++r) {
					bool is_last_row = (r == n.rows - 1);
					out += child_prefix + (is_last_row ? "└── " : "├── ") + (r == 0 ? "HeaderRow" : "BodyRow") + "\n";
					std::string row_prefix = child_prefix + (is_last_row ? "    " : "│   ");
					auto row_span = n.get_row(r);
					for(std::uint32_t c = 0; c < row_span.size(); ++c) {
						bool is_last_cell = (c == row_span.size() - 1);
						out += row_prefix + (is_last_cell ? "└── " : "├── ") + "Cell\n";
						std::string cell_prefix = row_prefix + (is_last_cell ? "    " : "│   ");
						for(std::size_t i = 0; i < row_span[c].children.size(); ++i) {
							print_single_node(row_span[c].children[i], out, cell_prefix, i == row_span[c].children.size() - 1, false);
						}
					}
				}
			}
		}, node.data);
	}
};

}
