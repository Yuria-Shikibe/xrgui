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
export import :tokenized_text_view;
export import :argument;

export import mo_yanxi.font;
import mo_yanxi.math.vector2;
export import mo_yanxi.graphic.color;

import mo_yanxi.utility;


namespace mo_yanxi::typesetting{


export
constexpr bool check_token_group_need_another_run(const tokenized_text_view::token_span& range) noexcept{
	return std::ranges::any_of(range, &rich_text_token_argument::need_reset_run);
}


template <typename T>
using stack_of = optional_stack<gch::small_vector<T>>;

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
struct rich_text_fallback_style {
	math::vec2 offset{};
	graphic::color color{graphic::colors::white};
	const font::font_family* family{nullptr}; // 若为空，稍后使用 manager 的 default
	gch::small_vector<hb_feature_t> features{}; // 初始全局 OpenType 特性
	bool enables_underline{false};
	bool enables_italic{false};
	bool enables_bold{false};
	rich_text_token::wrap_frame_type wrap_frame_type;

	constexpr friend bool operator==(const rich_text_fallback_style& lhs, const rich_text_fallback_style& rhs) noexcept {
		return lhs.offset == rhs.offset
			&& lhs.color == rhs.color
			&& lhs.family == rhs.family
			&& lhs.features == rhs.features
			&& lhs.enables_underline == rhs.enables_underline
			&& lhs.enables_italic == rhs.enables_italic
			&& lhs.enables_bold == rhs.enables_bold;
	}


	// constexpr bool operator==(const rich_text_fallback_style&) const noexcept = default;
};

enum struct enable_type : std::uint8_t{
	no_spec,
	disabled,
	enabled
};

export
struct rich_text_context{
private:
	stack_of<math::vec2> history_offset_;
	stack_of<graphic::color> history_color_;
	stack_of<math::vec2> history_size_;
	stack_of<const font::font_family*> history_font_;
	stack_of<unsigned> history_feature_group_count_;

	// 使用 optional 记录下划线状态。只有 std::nullopt 时才采用 fallback
	enable_type enables_underline_{};
	enable_type enables_bold_{};
	enable_type enables_italic_{};
	rich_text_token::set_wrap_frame wrap_frame_state_{rich_text_token::wrap_frame_type::invalid};


public:
	void clear() noexcept{
		history_offset_.clear();
		history_font_.clear();
		history_color_.clear();
		history_size_.clear();
		history_feature_group_count_.clear();
		enables_underline_ = {};
		enables_bold_ = {};
		enables_italic_ = {};
		wrap_frame_state_ = {rich_text_token::wrap_frame_type::invalid};
	}

	rich_text_token::set_wrap_frame get_wrap_frame_state(const rich_text_fallback_style& fallback) const noexcept{
		return wrap_frame_state_.type == rich_text_token::wrap_frame_type::invalid ? rich_text_token::set_wrap_frame{fallback.wrap_frame_type} : wrap_frame_state_;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_underline_enabled(const rich_text_fallback_style& fallback) const noexcept{
		return enables_underline_ == enable_type::no_spec ? fallback.enables_underline : enables_underline_ == enable_type::enabled;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_italic_enabled(const rich_text_fallback_style& fallback) const noexcept{
		return enables_italic_ == enable_type::no_spec ? fallback.enables_italic : enables_italic_ == enable_type::enabled;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_bold_enabled(const rich_text_fallback_style& fallback) const noexcept{
		return enables_bold_ == enable_type::no_spec ? fallback.enables_bold : enables_bold_ == enable_type::enabled;
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 get_size(math::vec2 default_font_size) const noexcept{
		// 字体基础尺寸通常依然通过排版全局属性控制，如有需要也可以加进 fallback
		return history_size_.top(default_font_size);
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 get_offset(const rich_text_fallback_style& fallback) const noexcept{
		return history_offset_.top(fallback.offset);
	}

	[[nodiscard]] FORCE_INLINE inline graphic::color get_color(const rich_text_fallback_style& fallback) const noexcept{
		return history_color_.top(fallback.color);
	}

	[[nodiscard]] FORCE_INLINE inline const font::font_family& get_font(
		const rich_text_fallback_style& fallback,
		const font::font_family* default_family) const noexcept{
		auto rst = history_font_.top(fallback.family ? fallback.family : default_family);
		assert(rst != nullptr);
		return *rst;
	}

	template <std::invocable<hb_feature_t> OnAddFn = overload_def_noop<void>, std::invocable<unsigned> OnEraseFn =
		overload_def_noop<void>>
	void update(
		font::font_manager& manager,
		const update_param& param,
		const rich_text_fallback_style& fallback, // 增加 fallback 参数
		const tokenized_text_view::token_span& tokens,
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
						enables_underline_ = t.enabled ? enable_type::enabled : enable_type::disabled;
					},
					[&](const set_italic& t){
						enables_italic_ = t.enabled ? enable_type::enabled : enable_type::disabled;
					},
					[&](const set_bold& t){
						enables_bold_ = t.enabled ? enable_type::enabled : enable_type::disabled;
					},
					[&](const set_wrap_frame& t){
						wrap_frame_state_ = t;
					},
					[&](const set_offset& t){
						switch(t.type){
						case setter_type::absolute : history_offset_.push(t.offset);
							break;
						case setter_type::relative_add : history_offset_.push(history_offset_.top(fallback.offset) + t.offset);
							break;
						case setter_type::relative_mul : history_offset_.push(history_offset_.top(fallback.offset) * t.offset);
							break;
						default : std::unreachable();
						}
					},
					[&](const set_color& t){
						switch(t.type){
						case setter_type::absolute : history_color_.push(t.color);
							break;
						case setter_type::relative_add : history_color_.push(
								history_color_.top(fallback.color) + t.color);
							break;
						case setter_type::relative_mul : history_color_.push(
								history_color_.top(fallback.color) * t.color);
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
