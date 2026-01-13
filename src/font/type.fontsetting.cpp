module;

#include <cassert>
#include <hb.h>
#include <vector>

module mo_yanxi.font.typesetting;
import mo_yanxi.font.hb_wrapper;

namespace mo_yanxi::font::typesetting{
const parser global_parser{
	[]{
		parser p;
		apd_default_modifiers(p);
		return p;
	}()
};

const parser global_parser_reserve_token{
	[]{
		parser p;
		apd_default_modifiers(p);
		p.reserve_tokens = true;
		return p;
	}()
};


const parser global_parser_noop{
	[]{
		parser p;
		p.reserve_tokens = true;
		return p;
	}()
};


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

static void push_back_hb(
    layout_unit& unit,
	const parse_context& context,
	const code_point code,
	const unsigned layout_global_index,
    const math::vec2 hb_advance,
    const math::vec2 hb_offset
){
    const layout_index_t column_index = unit.buffer.size();

    // Load glyph. code.code should be a glyph index (with flag)
    auto glyph_obj = context.get_glyph(code.code);

	auto& current = unit.buffer.emplace_back(
		code_point{code.code, code.unit_index},
		layout_abs_pos{{column_index, 0}, layout_global_index},
		std::move(glyph_obj)
	);

	const auto font_region_scale = context.get_current_correction_scale();

	float advance_x = hb_advance.x * font_region_scale.x;

	const auto placementPos = context.get_current_offset().add_x(unit.pen_advance).add(hb_offset * font_region_scale);

	current.region = current.glyph.metrics().place_to(placementPos, font_region_scale);

	current.correct_scale = font_region_scale;
	current.color = context.get_color();


	unit.bound.max_height({
			.width = 0,
			.ascender = placementPos.y - current.region.get_src_y(),
			.descender = current.region.get_end_y() - placementPos.y
		});
	unit.bound.width = std::max(unit.bound.width, current.region.get_end_x());

	unit.pen_advance += advance_x;
}


void layout_unit::push_front(
	const parse_context& context,
	const char_code code,
	const std::optional<char_code> real_code
){
	auto glyph_obj = context.get_glyph(code);

	const bool emptyChar = code == real_code.value_or(code) && (code == U'\0' || code == U'\n');

	const auto font_region_scale = emptyChar ? math::vec2{} : context.get_current_correction_scale();

	const bool was_empty = buffer.empty();
	assert(!was_empty);
	const float old_bound_width = bound.width;

	float advance = glyph_obj.metrics().advance.x * font_region_scale.x;
	const auto placementPos = context.get_current_offset();

	auto region = glyph_obj.metrics().place_to(placementPos, font_region_scale);

	if(emptyChar){
		advance = 0;
		region.set_width(region.width() / 4.0f);
	}

	const auto correct_scale = font_region_scale;

	if(region.get_src_x() < 0){
		advance -= region.get_src_x();
		region.src.x = 0;
	}

	for(auto& existing_glyph : buffer){
		existing_glyph.region.move_x(advance);
		existing_glyph.layout_pos.pos.x += 1;
	}

	buffer.emplace(buffer.begin(),
		code_point{real_code.value_or(code), buffer.front().code.unit_index},
		layout_abs_pos{{0, 0}, buffer.front().layout_pos.index /*Use the same index with the previous front*/},
		std::move(glyph_obj)
	);

	auto& current = buffer.front();
	current.region = region;
	current.correct_scale = correct_scale;
	current.color = context.get_color();

	const float shifted_old_content_width = old_bound_width + advance;
	bound.width = std::max(current.region.get_end_x(), shifted_old_content_width);

	bound.max_height({
			.width = 0, // width 已单独计算
			.ascender = placementPos.y - current.region.get_src_y(),
			.descender = current.region.get_end_y() - placementPos.y
		});

	pen_advance += advance;
}

bool parser::flush(
	parse_context& context, glyph_layout& layout, layout_unit& layout_unit,
	const bool end_line, const bool terminate){

    bool has_retried = false;

    while (true) {
        auto& line = layout.get_row(context.current_row);

        const float last_line_src_y = (context.current_row == 0)
            ? 0.f
            : layout[context.current_row - 1].src.y;

        auto calc_upper_pad = [&](layout_rect& bound) -> float {
            const auto h = context.get_font_size().y;
            if (bound.ascender <= h) bound.ascender = h;
            return func::get_upper_pad(context, layout, bound);
        };

        auto merged_bound = line.bound;
        merged_bound.max_height(layout_unit.bound);
        merged_bound.width = std::max(merged_bound.width, context.row_pen_pos + layout_unit.bound.width);

        const float merged_upper_pad = calc_upper_pad(merged_bound);

        const math::vec2 predicted_captured = layout.captured_size.copy()
            .max_x(merged_bound.width)
            .max_y(last_line_src_y + merged_upper_pad + merged_bound.descender + context.get_line_fixed_spacing());

        const bool size_excessive = predicted_captured.beyond(layout.get_clamp_size());

        if (!terminate && !(end_line && layout_unit.buffer.size() == 1) && size_excessive) {

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

            const float current_upper_pad = calc_upper_pad(line.bound);
            line.src.y = last_line_src_y + current_upper_pad + context.get_line_fixed_spacing();
            layout.captured_size.max_x(line.bound.width).max_y(line.src.y + line.bound.descender);

            if ((layout.policy() & layout_policy::truncate) != layout_policy{}) {
                context.current_row++;
                context.row_pen_pos = 0;
                return false;
            }

            layout_unit.push_front(context, line_feed_character, U'\0');
            context.current_row++;
            context.row_pen_pos = 0;

            has_retried = true;
            continue;
        }

        const layout_index_t last_index = line.size();

        for (auto&& glyph : layout_unit.buffer) {
            glyph.layout_pos.pos = {last_index, context.current_row};
            glyph.region.move_x(context.row_pen_pos);
            line.glyphs.push_back(std::move(glyph));
        }

        context.row_pen_pos += layout_unit.pen_advance;
        line.bound = merged_bound;
        layout_unit.clear();

        line.src.y = last_line_src_y + merged_upper_pad + context.get_line_fixed_spacing();

        if (end_line || terminate) {
            layout.captured_size.max_x(math::min(merged_bound.width, layout.clamp_size.x)).max_y(line.src.y + line.bound.descender);
            context.current_row++;
            context.row_pen_pos = 0;
        }

        return true;
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

    auto hb_buf = create_hb_buffer();

	for(; itr != stl;){
		auto [layout_index, code] = *itr;

		lastTokenItr = func::exec_tokens(layout, context, *this, lastTokenItr, formatted_text, layout_index);

        std::vector<code_point> run_codes;
        run_codes.push_back(code);

        auto current_itr = itr;
        ++current_itr;

        while(current_itr != stl) {
            auto [next_idx, next_code] = *current_itr;

            auto token_range = formatted_text.get_token_group(next_idx, lastTokenItr);
            if (token_range.begin() != token_range.end()) {
                break;
            }

            if (code.code == U'\n' || next_code.code == U'\n') {
                 break;
            }

            run_codes.push_back(next_code);
            ++current_itr;
        }

        itr = current_itr;

        hb_buffer_reset(hb_buf.get());
        for(const auto& c : run_codes) {
            hb_buffer_add(hb_buf.get(), c.code, c.unit_index);
        }
        hb_buffer_guess_segment_properties(hb_buf.get());

        context.get_face().shape(hb_buf.get(), context.get_current_snapped_size());

        unsigned int glyph_count;
        hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hb_buf.get(), &glyph_count);
        hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hb_buf.get(), &glyph_count);

        for(unsigned int i = 0; i < glyph_count; ++i) {
            uint32_t cluster = glyph_info[i].cluster;

             auto found = std::find_if(run_codes.begin(), run_codes.end(), [&](const code_point& p){
                 return p.unit_index == cluster;
             });

             code_point cp_info{U'\0', static_cast<code_point_index>(cluster)};
             unsigned current_global_idx = 0;
             if (found != run_codes.end()) {
                 cp_info = *found;
                 current_global_idx = layout_index + std::distance(run_codes.begin(), found);
             } else {
                 current_global_idx = layout_index;
             }

            float x_advance = glyph_pos[i].x_advance / 64.0f;
            float y_advance = glyph_pos[i].y_advance / 64.0f;
            float x_offset = glyph_pos[i].x_offset / 64.0f;
            float y_offset = glyph_pos[i].y_offset / 64.0f;

            char_code glyph_idx_code = make_glyph_index_code(glyph_info[i].codepoint);

            push_back_hb(unit, context,
                code_point{glyph_idx_code, cp_info.unit_index},
                current_global_idx,
                {x_advance, y_advance},
                {x_offset, y_offset}
            );

            bool is_newline = (cp_info.code == U'\n');

		if(block_line_feed
			&& cp_info.code <= std::numeric_limits<signed char>::max()
			&& std::isalnum(static_cast<unsigned char>(cp_info.code))){
			// Continue
		} else {
                 if(!flush(context, layout, unit, is_newline || cp_info.code == U'\0')){
				if((layout.policy() & layout_policy::truncate) != layout_policy{}){
                        break;
				} else{
					break;
				}
			}
            }
        }

	}

	if(block_line_feed && !unit.buffer.empty()){
		flush(context, layout, unit, false);
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
