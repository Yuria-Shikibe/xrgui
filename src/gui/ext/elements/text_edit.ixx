module;

#include <cassert>

export module mo_yanxi.gui.elem.text_edit;

export import mo_yanxi.gui.elem.label;
export import mo_yanxi.gui.elem.text_holder;

import mo_yanxi.gui.alloc;
import mo_yanxi.font.typesetting;
import mo_yanxi.graphic.color;
import mo_yanxi.math;
import mo_yanxi.encode;
import std;
import mo_yanxi.char_filter;
import mo_yanxi.history_stack;

namespace mo_yanxi::gui{
constexpr bool caresAbout(const font::char_code code) noexcept{
	if(code > std::numeric_limits<std::uint8_t>::max()){
		return true;
	}
	return std::isalpha(code);
}

using Layout = font::typesetting::glyph_layout;

struct caret_identity{
	font::typesetting::layout_index_t src, dst{};

	auto length() const noexcept{
		return math::dst_safe(dst, src);
	}
};

enum class text_edit_type{
	insert, del, replace
};

struct text_edit_delta {
	text_edit_type type{};
	caret_identity caret_state{};
	mr::heap_string::size_type position{};
	mr::heap_string text_before{};
	mr::heap_string text_after{};
};

struct caret_range{
	font::typesetting::layout_abs_pos src{};
	font::typesetting::layout_abs_pos dst{};

	void set_from_identity(caret_identity identity) noexcept {
		src.index = identity.src;
		dst.index = identity.dst;
		dst.pos = src.pos = math::vectors::constant2<unsigned>::max_vec2;
	}

	[[nodiscard]] constexpr auto ordered() const noexcept{
		auto [min, max] = std::minmax(src, dst, less);
		return caret_range{min, max};
	}

	constexpr static bool less(const font::typesetting::layout_abs_pos l,
		const font::typesetting::layout_abs_pos r) noexcept{
		if(l.index == r.index){
			if(l.pos.y == r.pos.y) return l.pos.x < r.pos.x;
			return l.pos.y < r.pos.y;
		}

		return l.index < r.index;
	}

	[[nodiscard]] bool hasRegion() const noexcept{
		return src != dst;
	}

	[[nodiscard]] bool includedLine(const Layout& layout) const noexcept{
		if(src.pos.y != dst.pos.y) return false;

		const auto [min, max] = std::minmax(src.pos.x, dst.pos.x);
		if(min != 0) return false;
		if(layout[dst.pos.y].size() != max + 1) return false;
		return true;
	}

	[[nodiscard]] const font::typesetting::glyph_elem* getPrevGlyph(const Layout& layout) const noexcept{
		if(dst.index == 0) return nullptr;

		if(dst.pos.x == 0){
			return &layout[dst.pos.y - 1].glyphs.back();
		} else{
			return &layout[dst.pos.y][dst.pos.x - 1];
		}
	}

	[[nodiscard]] const font::typesetting::glyph_elem* getNextGlyph(const Layout& layout) const noexcept{
		if(layout.is_end(dst.pos)) return nullptr;

		auto& row = layout[dst.pos.y];

		if(dst.pos.x == row.size() - 1){
			return &layout[dst.pos.y + 1].glyphs.front();
		} else{
			return &row[dst.pos.x + 1];
		}
	}

	[[nodiscard]] std::string_view getSelection(const Layout& layout) const noexcept{
		if(hasRegion()){
			auto sorted = ordered();
			const auto from = layout.at(sorted.src.pos).code.unit_index;
			const auto to = layout.at(sorted.dst.pos).code.unit_index;

			const std::string_view sv{layout.get_text()};
			return sv.substr(from, to - from);
		} else{
			return "";
		}
	}

	explicit(false) operator caret_identity() const noexcept{
		return {src.index, dst.index};
	}

	bool operator==(const caret_range&) const noexcept = default;
};

struct caret_section{
	std::string_view text;
	std::size_t pos;

	explicit operator bool() const noexcept{
		return !text.empty();
	}
};

struct caret{
	static constexpr float Stroke = 3;
	static constexpr float BlinkCycle = 60;
	static constexpr float BlinkThreshold = BlinkCycle * .5f;

	caret_range range{};
	float blink{};

	[[nodiscard]] caret() = default;

	[[nodiscard]] explicit caret(const Layout& layout, const font::typesetting::layout_pos_t begin)
	: range({begin}, {begin}){
		auto idx = layout.at(begin).layout_pos.index;
		range.src.index = idx;
		range.dst.index = idx;
	}

	[[nodiscard]] explicit caret(const caret_range& pos)
	: range(pos){
	}

	[[nodiscard]] constexpr bool should_blink() const noexcept{
		return blink < BlinkThreshold;
	}

	void to_lower_line(const Layout& layout, const bool append) noexcept{
		toLineAt(layout, range.dst.pos.y + 1, append);
	}

	void to_upper_line(const Layout& layout, const bool append) noexcept{
		if(range.dst.pos.y > 0){
			toLineAt(layout, range.dst.pos.y - 1, append);
		}
	}

private:
	auto delete_range(Layout& layout) const{
		math::section rng{layout.at(range.src.pos).code.unit_index, layout.at(range.dst.pos).code.unit_index};
		rng = rng.get_ordered();
		const auto len = rng.length();
		if(len){
			layout.text.erase(layout.text.begin() + rng.from, layout.text.begin() + rng.to);
		}
		return len;
	}

public:
	[[nodiscard]] caret_section get_selected(const Layout& layout) const noexcept{
		math::section rng{layout.at(range.src.pos).code.unit_index, layout.at(range.dst.pos).code.unit_index};
		rng = rng.get_ordered();
		const auto len = rng.length();
		return {std::string_view{layout.text}.substr(rng.from, len), rng.from};
	}


	bool delete_at(Layout& layout) noexcept{
		if(delete_range(layout)){
			const auto min = std::min(range.src, range.dst, caret_range::less);
			mergeTo(min, false);
			return true;
		} else{
			mergeTo(range.dst, false);
			const auto idx = layout.at(range.dst.pos).code.unit_index;
			if(layout.get_text().size() <= idx) return false;
			auto code = layout.get_text()[idx];
			if(code == 0) return false;
			const auto len = encode::getUnicodeLength(layout.get_text()[idx]);

			if(len){
				layout.text.erase(idx, len);

				if(range.dst.pos.y > 0){
					auto& lastLine = layout[range.dst.pos.y - 1];
					if(lastLine.is_truncated_line() && !layout[range.dst.pos.y].has_valid_before(range.dst.pos.x)){
						mergeTo(layout, lastLine.size() - 1, range.dst.pos.y - 1, false);
					}
				}

				return true;
			}
		}

		return false;
	}

	bool backspace_at(Layout& layout) noexcept{
		if(delete_range(layout)){
			const auto min = std::min(range.src, range.dst, caret_range::less);
			mergeTo(min, false);
			return true;
		} else{
			const auto idx = layout.at(range.dst.pos).code.unit_index;
			if(idx == 0) return false;
			const auto end = layout.text.begin() + idx;
			const auto begin = encode::gotoUnicodeHead(std::ranges::prev(end));

			assert(begin != end);
			layout.text.erase(begin, end);

			if(range.dst.pos.y > 0){
				auto& lastLine = layout[range.dst.pos.y - 1];
				if(lastLine.is_truncated_line() && !layout[range.dst.pos.y].has_valid_before(range.dst.pos.x)){
					mergeTo(layout, static_cast<int>(lastLine.size()) - 1, range.dst.pos.y - 1, false);
					advance(layout, 1, false);
				} else{
					to_left(layout, false);
				}
			} else{
				to_left(layout, false);
			}

			return true;
		}
	}

	void select_all(const Layout& layout){
		range.src = {0, 0, 0};
		range.dst.pos.y = layout.row_size() - 1;
		range.dst.pos.x = layout.rows().back().size() - 1;
		range.dst.index = layout.at(range.dst.pos).layout_pos.index;
	}

	void to_right(const Layout& layout, const bool append, const bool skipAllPadder = false) noexcept{
		if(range.hasRegion() && !append){
			const auto max = std::max(range.src, range.dst, caret_range::less);
			mergeTo(max, false);
			return;
		}

		if(skipAllPadder){
			while(!layout.is_end(range.dst.pos) && layout.at(range.dst.pos).code.code == 0){
				toRight_impl(layout, append);
			}
		} else{
			if(!layout.is_end(range.dst.pos) && layout.at(range.dst.pos).code.code == 0){
				toRight_impl(layout, append);
			}
		}


		toRight_impl(layout, append);
	}

	void to_left(const Layout& layout, const bool append) noexcept{
		if(range.hasRegion() && !append){
			const auto min = std::min(range.src, range.dst, caret_range::less);
			mergeTo(min, false);
			return;
		}

		toLeft_impl(layout, append);

		if(layout.at(range.dst.pos).code.code == 0){
			toLeft_impl(layout, append);
		}
	}

	void to_left_jump(const Layout& layout, const bool append) noexcept{
		if(range.dst.index != 0 && !caresAbout(layout.at(range.dst.pos).code.code)){
			to_left(layout, append);
		}

		auto* last{range.getPrevGlyph(layout)};

		while(last){
			if(!caresAbout(last->code.code)) break;
			to_left(layout, append);
			last = range.getPrevGlyph(layout);
		}

		while(last){
			if(caresAbout(last->code.code)) break;
			to_left(layout, append);
			last = range.getPrevGlyph(layout);
		}
	}

	void to_right_jump(const Layout& layout, const bool append) noexcept{
		auto* next{range.getNextGlyph(layout)};

		while(next){
			if(!caresAbout(next->code.code)) break;
			to_right(layout, append);
			next = range.getNextGlyph(layout);
		}

		while(next){
			if(caresAbout(next->code.code)) break;
			to_right(layout, append);
			next = range.getNextGlyph(layout);
		}
	}

	std::size_t insert_at(Layout& layout, std::string_view buffer){
		if(range.hasRegion()){
			auto sorted = range.ordered();
			const auto from = layout.text.begin() + layout.at(sorted.src.pos).code.unit_index;
			const auto to = layout.text.begin() + layout.at(sorted.dst.pos).code.unit_index;

			layout.text.replace_with_range(from, to, buffer);
			mergeTo(sorted.src, false);
		} else{
			mergeTo(range.dst, false);
			const auto index = layout.at(range.dst.pos).code.unit_index;
			const auto where = layout.text.begin() + index;
			layout.text.insert_range(where, buffer);
		}

		return buffer.size();
	}

	void update(Layout& layout, const float delta) noexcept{
		blink += delta;
		if(blink > BlinkCycle){
			blink = 0;
		}
	}

	bool check_index(const Layout& layout) noexcept{
		auto original = this->range;
		const bool hasRegion = range.hasRegion();

		if(!layout.contains(range.dst.pos) || layout.at(range.dst.pos).index() != range.dst.index){
			if(const auto elem = layout.find_valid_elem(range.dst.index)){
				range.dst = elem->layout_pos;
			}
		}

		if(!hasRegion){
			range.src = range.dst;
			return original != this->range;
		}

		if(!layout.contains(range.src.pos) || layout.at(range.src.pos).index() != range.src.index){
			if(const auto elem = layout.find_valid_elem(range.src.index)){
				range.src = elem->layout_pos;
			}
		}
		return original != this->range;
	}

	void to_line_end(const Layout& layout, const bool append){
		if(layout.rows().empty()) return;

		auto line = layout.rows().begin() + range.dst.pos.y;
		while(line != layout.rows().end() && line->is_truncated_line()){
			++line;
			if(line == layout.rows().end()){
				--line;
				break;
			}
		}

		mergeTo(layout, line->size() - 1, line - layout.rows().begin(), append);
	}

	void to_line_begin(const Layout& layout, const bool append){
		auto line = layout.rows().begin() + range.dst.pos.y;
		while(line->is_append_line() && line != layout.rows().begin()){
			--line;
		}

		mergeTo(layout, 0, line - layout.rows().begin(), append);
	}

	void advance(const Layout& layout, int adv, const bool append){
		range.dst.index += adv;
		if(!append) range.src = range.dst;
	}

private:
	void toRight_impl(const Layout& layout, const bool append) noexcept{
		auto& line = layout[range.dst.pos.y];
		if(line.size() <= range.dst.pos.x + 1){
			// line end, to next line
			if(layout.row_size() <= range.dst.pos.y + 1){
				//no next line
				tryMerge(append);
			} else{
				mergeTo(layout, 0, range.dst.pos.y + 1, append);
			}
		} else{
			mergeTo(layout, range.dst.pos.x + 1, range.dst.pos.y, append);
		}
	}

	void toLeft_impl(const Layout& layout, const bool append) noexcept{
		if(range.dst.pos.x == 0){
			// line end, to next line
			if(range.dst.pos.y == 0){
				//no next line
				tryMerge(append);
			} else{
				auto& nextLine = layout[range.dst.pos.y - 1];
				mergeTo(layout, math::max<int>(nextLine.size() - 1, 0), range.dst.pos.y - 1, append);
			}
		} else{
			mergeTo(layout, range.dst.pos.x - 1, range.dst.pos.y, append);
		}
	}

	void toLineAt(const Layout& layout,
		const font::typesetting::layout_index_t nextRowIndex,
		const bool append) noexcept{
		if(layout.row_size() <= nextRowIndex) return;

		auto& lastLine = layout[range.dst.pos.y];
		const auto curPos = lastLine[range.dst.pos.x].region.src;

		auto& nextLine = layout[nextRowIndex];
		const auto next = nextLine.line_nearest(curPos.x + lastLine.src.x);
		if(next == nextLine.glyphs.end()){
			mergeTo(layout, math::max<int>(nextLine.size() - 1, 0), nextRowIndex, append);
		} else{
			mergeTo(next->layout_pos, append);
		}
	}

	void mergeTo(const font::typesetting::layout_abs_pos where, const bool append){
		this->mergeTo(where.pos.x, where.pos.y, where.index, append);
	}

	void mergeTo(const std::integral auto x, const std::integral auto y, const unsigned index, const bool append){
		range.dst = {
				static_cast<font::typesetting::layout_pos_t::value_type>(x),
				static_cast<font::typesetting::layout_pos_t::value_type>(y), index
			};
		if(!append) range.src = range.dst;
		blink = 0;
	}

	void mergeTo(const Layout& layout, const std::integral auto x, const std::integral auto y, const bool append){
		this->mergeTo(x, y, layout.at({
				static_cast<font::typesetting::layout_pos_t::value_type>(x),
				static_cast<font::typesetting::layout_pos_t::value_type>(y)
			}).layout_pos.index, append);
	}

	void tryMerge(const bool append){
		if(!append) range.src = range.dst;
		blink = 0;
	}
};

class edit_history {
private:
    mo_yanxi::procedure_history_stack<text_edit_delta, std::deque<text_edit_delta, mr::heap_allocator<text_edit_delta>>> history_{};

public:
    [[nodiscard]] edit_history() = default;

    [[nodiscard]] edit_history(const std::size_t capacity, const mr::heap_allocator<text_edit_delta>& alloc) : history_(capacity, alloc) {}

private:
    static void apply_delta(std::string& target, const text_edit_delta& delta, caret_range& caret){
    	switch(delta.type){
        case text_edit_type::insert :{
            target.insert(delta.position, delta.text_after);
        	if(delta.caret_state.length() == 0){
        		const unsigned len = delta.text_after.size();
        		caret.set_from_identity({
					delta.caret_state.src + len, delta.caret_state.dst + len
				});
        	}else{
        		caret.set_from_identity(delta.caret_state);
        	}
            return;
        }
        case text_edit_type::del :{
            target.erase(delta.position, delta.text_before.size());
        	caret.set_from_identity(delta.caret_state);

            return;
        }
        case text_edit_type::replace :{
            target.replace(delta.position, delta.text_before.size(), delta.text_after);
        	caret.set_from_identity(delta.caret_state);
            return;
        }
        default : std::unreachable();
        }
    }

    static void revert_delta(std::string& target, const text_edit_delta& delta, caret_range& caret) {
        switch(delta.type){
        case text_edit_type::insert :{
            target.erase(delta.position, delta.text_after.size());
        	caret.set_from_identity(delta.caret_state);
            return;
        }
        case text_edit_type::del :{
	        target.insert(delta.position, delta.text_before);
        	caret.set_from_identity(delta.caret_state);

            return;
        }
        case text_edit_type::replace :{
            target.replace(delta.position, delta.text_after.size(), delta.text_before);
        	caret.set_from_identity(delta.caret_state);
            return;
        }
        default : std::unreachable();
        }
    }

public:
    void commit_insertion(const std::string_view content, const caret_identity caret, const std::size_t where, const std::string_view text){
    	assert(where <= content.size());
        text_edit_delta op{
            .type = text_edit_type::insert,
        	.caret_state = caret,
            .position = where,
            .text_after = mr::heap_string{text, history_.get_base().get_allocator()}
        };
    	history_.push(std::move(op));
    }

    void commit_delete(const std::string_view content, const caret_identity caret, const std::size_t where, const std::size_t length){
    	assert(where < content.size() && where + length <= content.size());
        text_edit_delta op{
            .type = text_edit_type::del,
        	.caret_state = caret,
            .position = where,
            .text_before = mr::heap_string{content.substr(where, length), history_.get_base().get_allocator()},
        };
    	history_.push(std::move(op));
    }

    void commit_replace(const std::string_view content, const caret_identity caret, const std::size_t where, const std::size_t length, const std::string_view inserted){
    	assert(where < content.size() && where + length <= content.size());
        text_edit_delta op{
            .type = text_edit_type::replace,
        	.caret_state = caret,
            .position = where,
            .text_before = mr::heap_string{content.substr(where, length), history_.get_base().get_allocator()},
            .text_after = mr::heap_string{inserted, history_.get_base().get_allocator()}
        };
    	history_.push(std::move(op));
    }

    bool undo(std::string& content, caret_range& caret) {
        if (auto* op = history_.to_prev()){
            revert_delta(content, *op, caret);
        	return true;
        }
    	return false;
    }

    bool redo(std::string& content, caret_range& caret) {
        if(auto* op = history_.to_next()){
            apply_delta(content, *op, caret);
        	return true;
        }
    	return false;
    }

	void pop(){
	    if(history_.to_prev()){
	    	history_.truncate();
	    }
    }
};



export
struct text_edit : label{
	[[nodiscard]] text_edit(scene& scene, elem* parent)
	: label(scene, parent){
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
		parser = &font::typesetting::global_parser_reserve_token;
	}

protected:
	std::string hint_text_when_idle_{">_..."};
	bool is_idle_{};
	float failed_hint_timer_{};
	std::size_t maximum_units_{std::numeric_limits<std::size_t>::max()};
	mr::heap_uset<font::char_code> prohibited_codes{get_heap_allocator<>()};

	edit_history edit_history_{64, get_heap_allocator<text_edit_delta>()};
	std::optional<caret> caret_{};
	std::basic_string<char32_t, std::char_traits<char32_t>, mr::heap_allocator<char32_t>> buffer{get_heap_allocator<char32_t>()};
	using edit_string_prov_node = react_flow::provider_cached<std::string_view>;
	edit_string_prov_node* string_prov_node_{};

	bool update(float delta_in_ticks) override{
		if(!label::update(delta_in_ticks)) return false;

		if(failed_hint_timer_ > 0.f){
			failed_hint_timer_ -= delta_in_ticks;
		}

		if(!is_focused_key()){
			caret_ = std::nullopt;
		}

		if(caret_){
			if(is_idle_){
				glyph_layout.text.clear();
				layout_text_then_resume({});
				is_idle_ = false;
			}

			if(!buffer.empty()){
				action_do_insert(encode::utf_32_to_8(buffer), buffer.size());
			}

			caret_->update(glyph_layout, delta_in_ticks);
		} else{
			if(!hint_text_when_idle_.empty() && glyph_layout.text.empty() && !is_idle_){
				is_idle_ = true;
				set_text(hint_text_when_idle_);
			}
		}

		buffer.clear();

		return true;
	}

	void layout_elem() override{
		label::layout_elem();
		if(caret_) caret_->check_index(glyph_layout);
	}

#pragma region Events
	events::op_afterwards on_drag(const events::drag event) override{
		const auto layoutSrc = this->get_layout_pos(event.src);
		if(!layoutSrc) return events::op_afterwards::intercepted;

		const auto layoutDst = this->get_layout_pos(event.dst);
		if(!layoutDst) return events::op_afterwards::intercepted;

		this->caret_ = caret{
				{
					{layoutSrc.value(), this->glyph_layout.at(layoutSrc.value()).index()},
					{layoutDst.value(), this->glyph_layout.at(layoutDst.value()).index()}
				}
			};

		this->set_focus(true);

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		if(!event.key.on_release()){
			auto layoutPos = get_layout_pos(event.pos);

			caret_ = layoutPos.transform([this](const font::typesetting::layout_pos_t pos){
				return caret{glyph_layout, pos};
			});

			if(caret_){
				set_focus(true);
			}
		}

		return elem::on_click(event, aboves);
	}

	events::op_afterwards on_key_input(const input_handle::key_set key) override{
		//Controlled by key binding
		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_unicode_input(const char32_t val) override{
		if(prohibited_codes.contains(val)){
			set_input_invalid();
		}else{
			buffer.push_back(val);
		}

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_esc() override{
		if(elem::on_esc() == events::op_afterwards::fall_through){
			if(caret_){
				caret_ = std::nullopt;
				set_focus(false);
			}
		}

		return events::op_afterwards::intercepted;
	}

#pragma endregion

	void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override;


	void layout_text_then_resume(const caret_range pos){
		text_expired = true;
		layout_elem();
		if(glyph_layout.contains(pos.dst.pos) && glyph_layout.contains(pos.src.pos)){
			if(!caret_){
				caret_.emplace(pos);
			} else{
				caret_->range = pos;
			}
		}
	}

	graphic::color get_text_draw_color() const noexcept override{
		if(is_idle()){
			return graphic::colors::light_gray.copy().mul_a(.65f * get_draw_opacity());
		}else{
			return label::get_text_draw_color();
		}
	}

private:
	void set_focus(bool keyFocused);

public:

	void undo(){
		if(!caret_)return;
		if(edit_history_.undo(glyph_layout.text, caret_->range)){
			notify_text_changed();
			if(string_prov_node_)string_prov_node_->update_value(get_text());
		}
	}

	void redo(){
		if(!caret_)return;
		if(edit_history_.redo(glyph_layout.text, caret_->range)){
			notify_text_changed();
			if(string_prov_node_)string_prov_node_->update_value(get_text());
		}
	}

	void add_file_banned_characters(){
		prohibited_codes.insert_range(std::initializer_list{U'<', U'>', U':', U'"', U'/', U'\\', U'|', U'*', U'?'});
	}

	void set_banned_characters(std::initializer_list<char32_t> chars){
		prohibited_codes = chars;
	}

	[[nodiscard]] std::string_view get_text() const noexcept override{
		if(is_idle()){
			return std::string_view{};
		}

		return label::get_text();
	}

	[[nodiscard]] bool is_idle() const noexcept{
		return is_idle_;
	}

	[[nodiscard]] bool is_failed() const noexcept{
		return failed_hint_timer_ > 0.f;
	}

	template <typename StrTy>
		requires (std::constructible_from<std::string, StrTy&&>)
	void set_hint_text(StrTy&& ty){
		if constexpr (std::equality_comparable_with<StrTy&&, std::string>){
			if(hint_text_when_idle_ == ty){
				return;
			}
		}

		hint_text_when_idle_ = std::forward<StrTy>(ty);
		if(is_idle()){
			notify_text_changed();
		}
	}

	edit_string_prov_node& set_as_string_prov(react_flow::propagate_behavior propagate_type = react_flow::propagate_behavior::pulse){
		if(!string_prov_node_){
			string_prov_node_ = &get_scene().request_react_node<edit_string_prov_node>(*this, propagate_type);
			string_prov_node_->update_value(get_text());
		}

		return *string_prov_node_;
	}

#pragma region KeyAction

	void action_do_insert(std::string_view string, const std::size_t advance){
		if(string.size() + get_text().size() >= maximum_units_){
			set_input_invalid();
			return;
		}

		caret_->check_index(glyph_layout);
		if(const auto sel = caret_->get_selected(glyph_layout)){
			edit_history_.commit_replace(get_text(), caret_->range, sel.pos, sel.text.size(), string);
		}else{
			edit_history_.commit_insertion(get_text(), caret_->range, sel.pos, string);
		}

		caret_->insert_at(glyph_layout, std::move(string));
		caret_->advance(glyph_layout, advance, false);

		notify_text_changed();
		if(string_prov_node_)string_prov_node_->update_value(get_text());
	}

	void action_do_delete(){
		if(!caret_) return;

		if(const auto sel = caret_->get_selected(glyph_layout)){
			edit_history_.commit_delete(get_text(), caret_->range, sel.pos, sel.text.size());
		}
		if(caret_->delete_at(glyph_layout)){
			notify_text_changed();
			if(string_prov_node_)string_prov_node_->update_value(get_text());
		}
	}

	void action_do_backspace(){
		if(!caret_) return;

		if(const auto sel = caret_->get_selected(glyph_layout)){
			edit_history_.commit_delete(get_text(), caret_->range, sel.pos, sel.text.size());
		}
		if(caret_->backspace_at(glyph_layout)){
			notify_text_changed();
			if(string_prov_node_)string_prov_node_->update_value(get_text());
		}
	}

	void action_move_right(bool select, bool jump) {
		if (!caret_) return;
		if (jump) {
			caret_->to_right_jump(glyph_layout, select);
		} else {
			caret_->to_right(glyph_layout, select);
		}
	}

	void action_move_left(bool select, bool jump) {
		if (!caret_) return;
		if (jump) {
			caret_->to_left_jump(glyph_layout, select);
		} else {
			caret_->to_left(glyph_layout, select);
		}
	}

    void action_move_right(bool select) noexcept {
        if (!caret_) return;
		caret_->to_right(glyph_layout, select);
    }

    void action_jump_right(bool select) noexcept {
        if (!caret_) return;
		caret_->to_right_jump(glyph_layout, select);
    }

    void action_move_left(bool select) noexcept {
        if (!caret_) return;
		caret_->to_left(glyph_layout, select);
    }

    void action_jump_left(bool select) noexcept {
        if (!caret_) return;
		caret_->to_left_jump(glyph_layout, select);
    }

    void action_move_up(bool select) noexcept {
        if (!caret_) return;
        caret_->to_upper_line(glyph_layout, select);
    }

    void action_move_down(bool select) noexcept {
        if (!caret_) return;
        caret_->to_lower_line(glyph_layout, select);
    }

    void action_move_line_begin(bool select) noexcept {
        if (!caret_) return;
        caret_->to_line_begin(glyph_layout, select);
    }

    void action_move_line_end(bool select) noexcept {
        if (!caret_) return;
        caret_->to_line_end(glyph_layout, select);
    }

    void action_select_all() noexcept {
        if (!caret_) return;
        caret_->select_all(glyph_layout);
    }

    void action_copy() const{
        if (!caret_ || !caret_->range.hasRegion()) return;
        if (const auto cmt = get_scene().get_communicator()) {
            cmt->set_clipboard(caret_->range.getSelection(glyph_layout));
        }
    }

    void action_paste() {
        if (!caret_) return;
        // Ctrl+V logic
        if (const auto cmt = get_scene().get_communicator()) {
            const auto str = cmt->get_clipboard();
            action_do_insert(str, encode::count_code_points(str));
        }
    }

    void action_cut() {
        if (!caret_ || !caret_->range.hasRegion()) return;
        // Copy then Delete
        if (const auto cmt = get_scene().get_communicator()) {
            cmt->set_clipboard(caret_->range.getSelection(glyph_layout));
        }
        action_do_delete();
    }

    void action_enter() {
        if (!prohibited_codes.contains(U'\n')) buffer.push_back(U'\n');
    }

    void action_tab() {
        if (!prohibited_codes.contains(U'\t')) buffer.push_back(U'\t');
    }

#pragma endregion


protected:
	text_layout_result layout_text(math::vec2 bound) override {
		auto rst = label::layout_text(bound);
		if(rst.updated){
			if(glyph_layout.is_clipped()){
				undo();
				edit_history_.pop();
				set_input_invalid();
			}
			if(caret_)caret_->check_index(glyph_layout);
		}
		return rst;
	}

	void set_input_invalid(){
		failed_hint_timer_ = 10.f;
	}

	void set_input_valid(){
		failed_hint_timer_ = 0.;
	}

public:
	~text_edit() override{
		set_focus(false);
	}
};


export
struct text_edit_key_binding : input_handle::key_mapping<text_edit&>{
	using key_mapping::key_mapping;
};


void load_default_text_edit_key_binding(text_edit_key_binding& bind);
}
