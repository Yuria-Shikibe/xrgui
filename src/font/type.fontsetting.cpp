module;

#include <cassert>
#include "gch/small_vector.hpp"

module mo_yanxi.font.typesetting;

namespace mo_yanxi::font::typesetting{
namespace func{
float get_upper_pad(
	const parse_context& context,
	const glyph_layout& layout,
	const layout_rect region
) noexcept{
	//TODO make it decideable by params
	const auto default_spacing = context.get_line_spacing();
	if(context.get_current_row() > 0){
		const auto& last_line = layout[context.get_current_row() - 1];
		return std::max(region.ascender + last_line.bound.descender, default_spacing);
	}

	return region.ascender;
}
}

void layout_unit::push_back(
	const parse_context& context,
	const code_point code,
	const unsigned layout_global_index,
	const std::optional<char_code> real_code,
	bool termination
){
	const layout_index_t column_index = buffer.size();
	auto& current = buffer.emplace_back(
		code_point{real_code.value_or(code.code), code.unit_index},
		layout_abs_pos{{column_index, 0}, layout_global_index},
		context.get_glyph(code.code)
	);

	bool emptyChar = code.code == real_code && (code.code == U'\0' || code.code == U'\n');

	const auto font_region_scale = (!termination && emptyChar) ? math::vec2{} : context.get_current_correction_scale();
	float advance = current.glyph.metrics().advance.x * font_region_scale.x;


	const auto placementPos = context.get_current_offset().add_x(pen_advance);

	current.region = current.glyph.metrics().place_to(placementPos, font_region_scale);

	if(emptyChar){
		advance = 0;
		current.region.set_width(current.region.width() / 4);
	}

	current.correct_scale = font_region_scale;
	if(current.region.get_src_x() < 0){
		//Fetch for italic line head
		advance -= current.region.get_src_x();
		current.region.src.x = 0;
	}
	current.color = context.get_color();


	bound.max_height({
			.width = 0,
			.ascender = placementPos.y - current.region.get_src_y(),
			.descender = current.region.get_end_y() - placementPos.y
		});
	bound.width = std::max(bound.width, current.region.get_end_x());
	pen_advance += advance;
}

void layout_unit::push_front(
	const parse_context& context,
	const char_code code,
	const std::optional<char_code> real_code
){
	auto glyph_obj = context.get_glyph(code);

	// 2. 检查是否为空字符或换行符 (逻辑参考 push_glyph)
	// prepend 操作通常不作为终止符 (termination=false)，因此直接判断是否为空白占位
	const bool emptyChar = code == real_code.value_or(code) && (code == U'\0' || code == U'\n');

	// 3. 计算缩放比例
	// 如果是空白字符且非强制显示，缩放为 0
	const auto font_region_scale = emptyChar ? math::vec2{} : context.get_current_correction_scale();

	// 4. [关键优化] 保存修改前的状态
	// 在移动 buffer 之前记录旧宽度，用于 O(1) 计算新宽度
	const bool was_empty = buffer.empty();
	assert(!was_empty);
	const float old_bound_width = bound.width;

	// 5. 计算新字形的 Advance (前进量) 和 Region (区域)
	// 插入位置相对于当前 unit 起始点为 0，但需考虑 context 的全局 offset [cite: 54, 118]
	float advance = glyph_obj.metrics().advance.x * font_region_scale.x;
	const auto placementPos = context.get_current_offset();

	auto region = glyph_obj.metrics().place_to(placementPos, font_region_scale);

	if(emptyChar){
		advance = 0;
		// 即使是空字符，通常也保留 1/4 宽度作为逻辑占位 (参考 push_glyph 实现)
		region.set_width(region.width() / 4.0f);
	}

	const auto correct_scale = font_region_scale;

	// 处理斜体字首向左溢出的情况 (src_x < 0)
	// 如果字形向左突出了 bbox，需要增加 advance 来补偿，保证它被包含在单元内
	if(region.get_src_x() < 0){
		advance -= region.get_src_x();
		region.src.x = 0;
	}

	// 6. 将缓冲区中现有的所有字形向右移动
	// 现有的字形都需要加上新字形的 advance，并且索引 +1
	for(auto& existing_glyph : buffer){
		existing_glyph.region.move_x(advance);
		existing_glyph.layout_pos.pos.x += 1;
	}

	// 7. 在头部插入新字形
	// 使用 emplace 在 begin() 处构造，column index 设为 0
	buffer.emplace(buffer.begin(),
		code_point{real_code.value_or(code), buffer.front().code.unit_index},
		layout_abs_pos{{0, 0}, buffer.front().layout_pos.index /*Use the same index with the previous front*/},
		std::move(glyph_obj)
	);

	// 8. 设置新插入元素的属性
	// 注意：emplace 后引用首元素
	auto& current = buffer.front();
	current.region = region;
	current.correct_scale = correct_scale;
	current.color = context.get_color();

	// 9. [优化后] 更新 Layout Unit 的整体宽度
	// 新宽度 = max(新字形的右边界, 旧内容平移后的右边界)
	// 无需遍历 buffer，时间复杂度 O(1)
	const float shifted_old_content_width = old_bound_width + advance;
	bound.width = std::max(current.region.get_end_x(), shifted_old_content_width);

	// 更新高度 (Ascender / Descender) [cite: 15, 118]
	bound.max_height({
			.width = 0, // width 已单独计算
			.ascender = placementPos.y - current.region.get_src_y(),
			.descender = current.region.get_end_y() - placementPos.y
		});

	// 10. 更新笔触总前进量
	pen_advance += advance;
}

bool parser::flush(
	parse_context& context, glyph_layout& layout, layout_unit& layout_unit,
	const bool end_line, const bool terminate){

	// 标记是否已经发生过一次换行重试，替代原来的 'nested' 递归参数
    bool has_retried = false;

    while (true) {
        // 每次循环重新获取当前行引用（因为 context.current_row 可能在上一轮循环改变）
        auto& line = layout.get_row(context.current_row);

        // [Helper 1] 计算相对于上一行的 Y 轴偏移量
        // 如果是换行后的第二次循环，这里获取的就是刚刚结算的那一行
        const float last_line_src_y = (context.current_row == 0)
            ? 0.f
            : layout[context.current_row - 1].src.y;

        // [Helper 2] 上边距计算逻辑
        auto calc_upper_pad = [&](layout_rect& bound) -> float {
            const auto h = context.get_font_size().y;
            if (bound.ascender <= h) bound.ascender = h;
            return func::get_upper_pad(context, layout, bound);
        };

        // 1. 预判合并逻辑 (Predict & Merge)
        auto merged_bound = line.bound;
        merged_bound.max_height(layout_unit.bound);
        // 合并宽度 = max(当前行宽, 当前笔触 + 新单元宽)
        merged_bound.width = std::max(merged_bound.width, context.row_pen_pos + layout_unit.bound.width);

        const float merged_upper_pad = calc_upper_pad(merged_bound);

        // 计算包含当前 buffer 后的整体布局尺寸
        // 注意：Y 轴高度包含 上一行Y + 当前行Pad + 下行高 + 间距
        const math::vec2 predicted_captured = layout.captured_size.copy()
            .max_x(merged_bound.width)
            .max_y(last_line_src_y + merged_upper_pad + merged_bound.descender + context.get_line_fixed_spacing());

        // 2. 检查溢出 (Overflow Check)
        // 只有在非强制结束且预测尺寸超标时才进入处理
        const bool size_excessive = predicted_captured.beyond(layout.get_clamp_size());

        if (!terminate && !(end_line && layout_unit.buffer.size() == 1) && size_excessive) {
            // --- 溢出处理分支 ---

            // 2.1 死局判定 (Dead End)
            // 如果：行是空的 OR 是单个超大字符 OR 已经重试过一次了 -> 判定失败并回滚
            if (line.size() == 0 || (line.is_append_line() && line.size() == 1) || has_retried) {
                layout.pop_line();
                if (context.current_row > 0) {
                    context.current_row--;
                    context.row_pen_pos = layout.get_row(context.current_row).bound.width;
                } else {
                    context.row_pen_pos = 0;
                }
                return false;
            }

            // 2.2 执行换行 (Perform Wrap)

            // 结算当前即将结束的行
            const float current_upper_pad = calc_upper_pad(line.bound);
            line.src.y = last_line_src_y + current_upper_pad + context.get_line_fixed_spacing();
            layout.captured_size.max_x(line.bound.width).max_y(line.src.y + line.bound.descender);

            // 检查截断策略
            if ((layout.policy() & layout_policy::truncate) != layout_policy{}) {
                context.current_row++;
                context.row_pen_pos = 0;
                return false;
            }

            // 插入换行符并准备下一行状态
            layout_unit.push_front(context, line_feed_character, U'\0');
            context.current_row++;
            context.row_pen_pos = 0;

            // 标记重试状态并立即进入下一次循环
            // 下一次循环将尝试把 layout_unit 放入新的空行中
            has_retried = true;
            continue;
        }

        // 3. 提交数据 (Commit)
        // 代码执行到此处说明空间足够，或者处于强制提交模式

        const layout_index_t last_index = line.size();

        // 3.1 移动 Glyph 数据
        for (auto&& glyph : layout_unit.buffer) {
            glyph.layout_pos.pos = {last_index, context.current_row};
            glyph.region.move_x(context.row_pen_pos);
            line.glyphs.push_back(std::move(glyph));
        }

        // 3.2 更新 Context 和 Line 状态
        context.row_pen_pos += layout_unit.pen_advance;
        line.bound = merged_bound;
        layout_unit.clear();

        // 3.3 确定当前行的最终 Y 坐标
        line.src.y = last_line_src_y + merged_upper_pad + context.get_line_fixed_spacing();

        // 3.4 处理强制行尾 (End Line / Terminate)
        if (end_line || terminate) {
            layout.captured_size.max_x(math::min(merged_bound.width, layout.clamp_size.x)).max_y(line.src.y + line.bound.descender);
            context.current_row++;
            context.row_pen_pos = 0;
        }

        return true; // 成功并退出
    }
}

void parser::end_parse(glyph_layout& layout, parse_context& context, const code_point code,
	const layout_index_t idx){
	struct layout_unit append_hint{};

	append_hint.push_back(
		context,
		{0, code.unit_index}, idx, U'\0', true);

	parser::flush(context, layout, append_hint, code.code != U'\n', true);

	//TODO make it spec by user?
	layout.captured_size.add_y(4).min_y(layout.get_clamp_size().y);
}

void parser::operator()(glyph_layout& layout, parse_context context, const tokenized_text& formatted_text) const{
	tokenized_text::token_iterator lastTokenItr = formatted_text.tokens.begin();

	auto view = formatted_text.codes | std::views::enumerate;
	auto itr = view.begin();

	layout_unit unit{};

	const auto stl = std::prev(view.end());

	const bool block_line_feed = (layout.policy() & layout_policy::block_line_feed) != layout_policy{} && (layout.
		policy() & layout_policy::auto_feed_line) == layout_policy{};

	for(; itr != stl; ++itr){
		auto [layout_index, code] = *itr;
		lastTokenItr = func::exec_tokens(layout, context, *this, lastTokenItr, formatted_text, layout_index);
		unit.push_back(context, code, layout_index);

		if(block_line_feed
			&& code.code <= std::numeric_limits<signed char>::max()
			&& std::isalnum(static_cast<unsigned char>(code.code))){
			continue;
		}

		if(!flush(context, layout, unit, code.code == U'\n' || code.code == U'\0')){
			if((layout.policy() & layout_policy::truncate) != layout_policy{}){
				do{
					++itr;
				} while(itr != stl && itr.base()->code != U'\n');
			} else{
				break;
			}
		}
	}

	// [重要补充]：循环结束后，检查 buffer 是否还有残留字符。
	// 如果文本以字母/数字结尾（例如 "Hello"），循环内的逻辑会跳过最后一次 flush，
	// 因此必须在这里强制提交剩余的 buffer。
	if(block_line_feed && !unit.buffer.empty()){
		flush(context, layout, unit, false); // 非换行，非终止（终止由 end_parse 处理）
	}

	if(itr != stl){
		layout.clip = true;
	} else{
		layout.clip = false;
	}

	end_parse(layout, context, *itr.base(), itr - view.begin());

	layout.captured_size.min(layout.get_clamp_size());
}

void parser::operator()(glyph_layout& layout, parse_context&& context) const{
	layout.elements.clear();
	const tokenized_text formatted_text{layout.get_text(), {.reserve = reserve_tokens}};
	this->operator()(layout, std::move(context), formatted_text);
}

void parser::operator()(glyph_layout& layout, const float scale) const{
	layout.elements.clear();
	parse_context context{scale};
	tokenized_text formatted_text{layout.get_text(), {.reserve = reserve_tokens}};

	this->operator()(layout, context, formatted_text);
}

tokenized_text::tokenized_text(const std::string_view string, const token_sentinel sentinel){
	static constexpr auto InvalidPos = std::string_view::npos;
	const auto size = string.size();

	codes.reserve(size + 1);

	enum struct TokenState{
		normal,
		endWaiting,
		signaled
	};

	bool escapingNext{};
	TokenState recordingToken{};
	decltype(string)::size_type tokenRegionBegin{InvalidPos};
	posed_token_argument* currentToken{};

	decltype(string)::size_type off = 0;

scan:
	for(; off < size; ++off){
		const char codeByte = string[off];
		char_code charCode{std::bit_cast<std::uint8_t>(codeByte)};

		if(charCode == '\r') continue;

		if(charCode == '\\' && recordingToken != TokenState::signaled){
			if(!escapingNext){
				escapingNext = true;
				continue;
			} else{
				escapingNext = false;
			}
		}

		if(!escapingNext){
			if(codeByte == sentinel.signal && tokenRegionBegin == InvalidPos){
				if(recordingToken != TokenState::signaled){
					recordingToken = TokenState::signaled;
				} else{
					recordingToken = TokenState::normal;
				}

				if(recordingToken == TokenState::signaled){
					currentToken = &tokens.emplace_back(
						posed_token_argument{static_cast<std::uint32_t>(codes.size())});

					if(!sentinel.reserve) continue;
				}
			}

			if(recordingToken == TokenState::endWaiting){
				if(codeByte == sentinel.begin){
					recordingToken = TokenState::signaled;
					currentToken = &tokens.emplace_back(
						posed_token_argument{static_cast<std::uint32_t>(codes.size())});
				} else{
					recordingToken = TokenState::normal;
				}
			}

			if(recordingToken == TokenState::signaled){
				if(codeByte == sentinel.begin && tokenRegionBegin == InvalidPos){
					tokenRegionBegin = off + 1;
				}

				if(codeByte == sentinel.end){
					if(!currentToken || tokenRegionBegin == InvalidPos){
						tokenRegionBegin = InvalidPos;
						currentToken = nullptr;
						recordingToken = TokenState::normal;
						if(sentinel.reserve) goto record;
						else continue;
					}

					currentToken->data = string.substr(tokenRegionBegin, off - tokenRegionBegin);
					tokenRegionBegin = InvalidPos;
					currentToken = nullptr;
					recordingToken = TokenState::endWaiting;
				}

				if(!sentinel.reserve) continue;
			}
		}

	record:

		const auto codeSize = encode::getUnicodeLength<>(reinterpret_cast<const char&>(charCode));

		if(!escapingNext){
			if(codeSize > 1 && off + codeSize <= size){
				charCode = encode::utf_8_to_32(string.data() + off, codeSize);
			}

			codes.push_back({charCode, static_cast<code_point_index>(off)});
		} else{
			escapingNext = false;
		}

		// if(charCode == '\n') rows++;

		off += codeSize - 1;
	}

	if(!sentinel.reserve && recordingToken == TokenState::signaled){
		tokens.pop_back();

		recordingToken = TokenState::normal;
		off = tokenRegionBegin;
		goto scan;
	}

	if(codes.empty() || codes.back().code != U'\0'){
		codes.push_back({U'\0', static_cast<code_point_index>(string.size())});
		// rows++;
	}


	codes.shrink_to_fit();
}
}
