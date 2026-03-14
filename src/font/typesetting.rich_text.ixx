module;

#include <hb.h>
#include <freetype/freetype.h>
#include <mo_yanxi/adapted_attributes.hpp>


#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <gch/small_vector.hpp>
#endif

export module mo_yanxi.typesetting.rich_text;

export import mo_yanxi.typesetting.util;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <gch/small_vector.hpp>;
#endif

import std;


export import :tokenized_text;
export import :argument;

export import mo_yanxi.font;
import mo_yanxi.math.vector2;
export import mo_yanxi.graphic.color;

import mo_yanxi.utility;


namespace mo_yanxi::typesetting{


export
bool check_token_group_need_another_run(const tokenized_text::token_subrange& range) noexcept{
	return std::ranges::any_of(range, &rich_text_token_argument::need_reset_run);
}


template <typename T>
using stack_of = optional_stack<gch::small_vector<T, 2>>;

export
enum struct context_update_mode{
	all,
	hard_only,
	soft_only
};

export
struct update_param{
	math::vec2 default_font_size;

	float ascender;
	float descender;
	bool is_vertical;
};

export
struct rich_text_context{
private:
	stack_of<math::vec2> history_offset_;
	stack_of<graphic::color> history_color_;
	stack_of<math::vec2> history_size_;
	stack_of<const font::font_family*> history_font_;
	stack_of<unsigned> history_feature_group_count_;

	bool enables_underline_{};

public:
	void clear() noexcept{
		history_offset_.clear();
		history_font_.clear();
		history_color_.clear();
		history_size_.clear();
		history_feature_group_count_.clear();
		enables_underline_ = false;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_underline_enabled() const noexcept{
		return enables_underline_;
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 get_size(math::vec2 default_font_size) const noexcept{
		return history_size_.top(default_font_size);
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 get_offset() const noexcept{
		return history_offset_.top({});
	}

	[[nodiscard]] FORCE_INLINE inline graphic::color get_color() const noexcept{
		return history_color_.top(graphic::colors::white);
	}

	[[nodiscard]] FORCE_INLINE inline const font::font_family& get_font(
		const font::font_family* default_family) const noexcept{
		auto rst = history_font_.top(default_family);
		assert(rst != nullptr);
		return *rst;
	}

	template <std::invocable<hb_feature_t> OnAddFn = overload_def_noop<void>, std::invocable<unsigned> OnEraseFn =
		overload_def_noop<void>>
	void update(
		font::font_manager& manager,
		const update_param& param,
		const tokenized_text::token_subrange& tokens,
		context_update_mode mode = context_update_mode::all,
		OnAddFn&& on_add = {},
		OnEraseFn&& on_erase = {}
	){
		using namespace rich_text_token;

		bool is_contiguous_feature{};
		for(const rich_text_token_argument& token : tokens){
			bool is_hard = token.need_reset_run();
			if(mode == context_update_mode::hard_only && !is_hard) continue;
			if(mode == context_update_mode::soft_only && is_hard) continue;

			std::visit(overload{
					[&](const std::monostate t){
					},
					[&](const set_underline& t){
						enables_underline_ = t.enabled;
					},
					[&](const set_offset& t){
						switch(t.type){
						case setter_type::absolute : history_offset_.push(t.offset);
							break;
						case setter_type::relative_add : history_offset_.push(history_offset_.top({}) + t.offset);
							break;
						case setter_type::relative_mul : history_offset_.push(history_offset_.top({}) * t.offset);
							break;
						default : std::unreachable();
						}
					},
					[&](const set_color& t){
						switch(t.type){
						case setter_type::absolute : history_color_.push(t.color);
							break;
						case setter_type::relative_add : history_color_.push(
								history_color_.top(graphic::colors::white / 2) + t.color);
							break;
						case setter_type::relative_mul : history_color_.push(
								history_color_.top(graphic::colors::white / 2) * t.color);
							break;
						default : std::unreachable();
						}
					},
					[&](const set_size& t){
						switch(t.type){
						case setter_type::absolute : history_size_.push(t.size);
							break;
						case setter_type::relative_add : history_size_.push(
								history_size_.top(param.default_font_size) + t.size);
							break;
						case setter_type::relative_mul : history_size_.push(
								history_size_.top(param.default_font_size) * t.size);
							break;
						default : std::unreachable();
						}
					},
					[&](const set_feature& t){
						if(is_contiguous_feature){
							++history_feature_group_count_.top_ref();
						} else{
							history_feature_group_count_.push(1);
						}

						on_add(t.feature);
					},
					[&](const set_font_by_name& t){
						if(auto ptr = manager.find_family(t.font_name)){
							history_font_.push(ptr);
						} else{
							history_font_.push(manager.get_default_family());
						}
					},
					[&](const set_font_directly& t){
						history_font_.push(t.family ? t.family : manager.get_default_family());
					},
					[&](const set_script& t){
						switch(t.type){
						case script_type::ends :{
							history_offset_.pop();
							history_size_.pop();
							break;
						}
						case script_type::subs :{
							auto size = history_size_.top(param.default_font_size).mul(.6f);
							history_size_.push(size);

							// [Fix] Handle Vertical/Horizontal shifts
							math::vec2 offset{};
							if(param.is_vertical){
								// Vertical Subscript: Shift Left (-X)
								offset.x = -(param.descender + size.y * .2f);
							} else{
								// Horizontal Subscript: Shift Down (+Y)
								offset.y = param.descender + size.y * .2f;
							}
							history_offset_.push(history_offset_.top({}) + offset);
							break;
						}
						case script_type::sups :{
							history_size_.push(history_size_.top(param.default_font_size).mul(.6f));

							// [Fix] Handle Vertical/Horizontal shifts
							math::vec2 offset{};
							if(param.is_vertical){
								// Vertical Superscript: Shift Right (+X)
								offset.x = param.ascender * 0.5f;
							} else{
								// Horizontal Superscript: Shift Up (-Y)
								offset.y = -param.ascender * 0.5f;
							}
							history_offset_.push(history_offset_.top({}) + offset);
							break;
						}
						default : std::unreachable();
						}
					},
					[&](const fallback_offset& t){
						history_offset_.pop();
					},
					[&](const fallback_color& t){
						history_color_.pop();
					},
					[&](const fallback_size& t){
						history_size_.pop();
					},
					[&](const fallback_feature& t){
						on_erase(history_feature_group_count_.pop_and_get().value_or(0));
					},
					[&](const fallback_font& t){
						history_font_.pop();
					},
				}, token.token);


			if(std::holds_alternative<set_feature>(token.token)){
				is_contiguous_feature = true;
			} else{
				is_contiguous_feature = false;
			}
		}
	}
};
}
