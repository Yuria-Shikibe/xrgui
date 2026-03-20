module;

#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.gui.elem.label;

import std;
export import mo_yanxi.typesetting;
export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.font;
export import mo_yanxi.typesetting.rich_text;
export import mo_yanxi.graphic.draw.instruction.recorder;
export import mo_yanxi.gui.text_render_cache;
import mo_yanxi.concurrent.atomic_shared_mutex;
import mo_yanxi.math;
import mo_yanxi.math.matrix3;
import mo_yanxi.unicode;

namespace mo_yanxi::gui{
struct text_layout_result{
	math::vec2 required_extent;
	bool updated;
};

enum struct change_type : std::uint32_t{
	none = 0,
	text = 1 << 0,
	max_extent = 1 << 1,
	config = 1 << 2,
};

BITMASK_OPS(export, change_type);

export enum struct text_rotation : std::uint8_t{
	deg_0 = 0,
	deg_90,
	deg_180,
	deg_270
};

export struct text_transform_config{
	text_rotation rotation{text_rotation::deg_0};
	bool flip_x{false};
	bool flip_y{false};

	constexpr bool is_vertical() const noexcept{
		return rotation == text_rotation::deg_90 || rotation == text_rotation::deg_270;
	}
};

export struct label;
export struct direct_label;

export struct label_text_prov : react_flow::terminal<std::string>{
	label_text_prov() = default;

	explicit label_text_prov(label& label)
		: terminal(react_flow::propagate_type::eager), label(&label){
	}

protected:
	label* label;
	void on_update(react_flow::data_carrier<std::string>& data) override;
};

export struct direct_label_text_prov : react_flow::terminal<typesetting::tokenized_text>{
	direct_label_text_prov() = default;

	explicit direct_label_text_prov(direct_label& label)
		: terminal(react_flow::propagate_type::eager), label(&label){
	}

protected:
	direct_label* label;
	void on_update(react_flow::data_carrier<typesetting::tokenized_text>& data) override;
};

//TODO provide a convenient constructor

export struct direct_label : elem{
protected:
	typesetting::tokenized_text tokenized_text_{};
private:
	typesetting::glyph_layout glyph_layout_{};
	typesetting::layout_config layout_config_{};
	typesetting::layout_context context_{std::in_place};

	text_render_cache render_cache_{};
	text_transform_config transform_config_{};

	layout::expand_policy expand_policy_{};

protected:
	change_type change_mark_{};

private:

	bool fit_{};

	bool is_layout_expired_() const noexcept{
		return change_mark_ != change_type{};
	}

public:
	math::vec2 max_fit_scale_bound{math::vectors::constant2<float>::inf_positive_vec2};
	align::pos text_entire_align{align::pos::top_left};

	using elem::elem;

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void set_expand_policy(const layout::expand_policy expand_policy){
		if(util::try_modify(expand_policy_, expand_policy)){
			notify_isolated_layout_changed();
		}
	}

#pragma region LabelSetter
	const typesetting::tokenized_text& get_tokenized_text() const noexcept{
		return tokenized_text_;
	}

	// 开放给外部或派生类直接设置 tokenized_text_
	void set_tokenized_text(typesetting::tokenized_text&& tokenized_text){
		if(util::try_modify(tokenized_text_, std::move(tokenized_text))){
			change_mark_ |= change_type::text;
			notify_isolated_layout_changed();
		}
	}

	void set_tokenized_text(const typesetting::tokenized_text& tokenized_text){
		if(util::try_modify(tokenized_text_, tokenized_text)){
			change_mark_ |= change_type::text;
			notify_isolated_layout_changed();
		}
	}

	void set_tokenized_text_quiet(typesetting::tokenized_text&& tokenized_text){
		tokenized_text_ = std::move(tokenized_text);
	}

	[[nodiscard]] std::optional<graphic::color> get_text_color_scl() const noexcept{
		return render_cache_.get_text_color_scl();
	}

	void set_text_color_scl(const std::optional<graphic::color>& text_color_scl){
		if(render_cache_.set_text_color_scl(text_color_scl)){
			render_cache_.update_buffer(glyph_layout_, render_cache_.get_draw_color(get_draw_opacity(), is_disabled()));
		}
	}

	[[nodiscard]] text_transform_config get_transform_config() const noexcept{
		return transform_config_;
	}

	void set_transform_config(const text_transform_config& config){
		const bool rot_changed_axis = transform_config_.is_vertical() != config.is_vertical();
		transform_config_ = config;

		if(rot_changed_axis){
			change_mark_ |= change_type::max_extent;
			notify_isolated_layout_changed();
		}
	}

	void set_typesetting_config(const typesetting::layout_config& config){
		if(util::try_modify(layout_config_, config)){
			change_mark_ |= change_type::config;
			notify_isolated_layout_changed();
		}
	}

	void set_fit(bool fit = true){
		if(util::try_modify(fit_, fit)){
			change_mark_ |= change_type::max_extent | change_type::config;
			notify_isolated_layout_changed();
		}
	}
#pragma endregion

	void layout_elem() override{
		elem::layout_elem();
		if(is_layout_expired_()){
			auto maxSz = restriction_extent.potential_extent();

			const auto resutlSz = layout_text(maxSz.fdim(boarder_extent()));
			if(resutlSz.updated && !resutlSz.required_extent.equals(content_extent())){
				notify_layout_changed(propagate_mask::force_upper);
			}
		}
	}

	bool set_disabled(bool isDisabled) override{
		if(elem::set_disabled(isDisabled)){
			render_cache_.update_buffer(glyph_layout_, render_cache_.get_draw_color(get_draw_opacity(), is_disabled()));
			return true;
		}
		return false;
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;

protected:
	void on_opacity_changed(float previous) override{
		render_cache_.update_buffer(glyph_layout_, render_cache_.get_draw_color(get_draw_opacity(), is_disabled()));
	}

	bool resize_impl(const math::vec2 size) override{
		if(elem::resize_impl(size)){
			layout_text(content_extent());
			return true;
		}
		return false;
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		if(get_expand_policy() == layout::expand_policy::passive){
			return std::nullopt;
		}

		if(!fit_){
			const auto text_size = layout_text(extent.potential_extent());
			extent.apply(text_size.required_extent);
		}

		const auto ext = extent.potential_extent().inf_to0();
		return util::select_prefer_extent(get_expand_policy() == layout::expand_policy::prefer, ext,
			get_prefer_content_extent());
	}


	[[nodiscard]] math::vec2 get_glyph_draw_extent() const noexcept{
		math::vec2 base_ext = glyph_layout_.extent;

		if(transform_config_.is_vertical()){
			base_ext.swap_xy();
		}

		if(fit_){
			return mo_yanxi::gui::align::embed_to(align::scale::fit_smaller, base_ext,
				content_extent().min(max_fit_scale_bound));
		} else{
			return base_ext;
		}
	}

	[[nodiscard]] math::vec2 get_glyph_src_local() const noexcept{
		const auto sz = get_glyph_draw_extent();

		return mo_yanxi::gui::align::get_offset_of(text_entire_align, sz, rect{content_extent()});
	}

	[[nodiscard]] math::vec2 get_glyph_src_abs() const noexcept{
		return get_glyph_src_local() + content_src_pos_abs();
	}

	[[nodiscard]] math::vec2 get_glyph_src_rel() const noexcept{
		return get_glyph_src_local() + content_src_pos_rel();
	}

	text_layout_result layout_text(math::vec2 bound){
		if(!fit_ && bound.area() < 32.0f) return {};

		math::vec2 local_bound = bound;

		const bool swap_axes = transform_config_.is_vertical();
		if(swap_axes){
			local_bound.swap_xy();
		}

		if(fit_){
			if((change_mark_ & change_type::max_extent) != change_type{}){
				if(layout_config_.set_max_extent(mo_yanxi::math::vectors::constant2<float>::inf_positive_vec2)){
				} else{
					change_mark_ &= ~change_type::max_extent;
				}
			}

			if(is_layout_expired_()){
				if(layout_config_.set_max_extent(mo_yanxi::math::vectors::constant2<float>::inf_positive_vec2) ||
					((change_mark_ & change_type::config) != change_type{}) || ((change_mark_ & change_type::text) !=
						change_type{})){
					context_.layout(tokenized_text_, layout_config_, glyph_layout_);
					render_cache_.update_buffer(glyph_layout_,
						render_cache_.get_draw_color(get_draw_opacity(), is_disabled()));
					change_mark_ = change_type::none;

					math::vec2 res_ext = glyph_layout_.extent;
					if(swap_axes) res_ext = {res_ext.y, res_ext.x};
					return {res_ext, true};
				}
				change_mark_ = change_type::none;

			}
		} else if(layout_config_.set_max_extent(local_bound) || is_layout_expired_()){
			context_.layout(tokenized_text_, layout_config_, glyph_layout_);
			render_cache_.update_buffer(glyph_layout_, render_cache_.get_draw_color(get_draw_opacity(), is_disabled()));
			change_mark_ = change_type::none;

			math::vec2 res_ext = glyph_layout_.extent;

			if(swap_axes) res_ext = {res_ext.y, res_ext.x};
			return {res_ext, true};
		}

		math::vec2 res_ext = glyph_layout_.extent;
		if(swap_axes) res_ext = {res_ext.y, res_ext.x};
		return {res_ext, false};
	}

	void draw_text() const;
};

export struct label : direct_label {
private:
	std::basic_string<char, std::char_traits<char>, mr::heap_allocator<char>> raw_string_{get_heap_allocator<char>()};
	typesetting::tokenize_tag tokenize_tag_{typesetting::tokenize_tag::def};

	template <typename StrTy>
	void update_tokenized_text_(StrTy&& str) {
		tokenized_text_.reset(std::forward<StrTy>(str), tokenize_tag_);
		change_mark_ |= change_type::text;
		notify_isolated_layout_changed();
	}

public:
	using direct_label::direct_label;

	void set_tokenized_text(typesetting::tokenized_text&& tokenized_text) = delete;
	void set_tokenized_text(const typesetting::tokenized_text& tokenized_text) = delete;
	void set_tokenized_text_quiet(typesetting::tokenized_text&& tokenized_text) = delete;

	template <typename StrTy>
		requires (std::constructible_from<std::string_view, const StrTy&>)
	void set_text(StrTy&& str){
		if(raw_string_ != std::string_view{std::as_const(str)}){
			raw_string_ = std::forward<StrTy>(str);
			update_tokenized_text_(raw_string_);
		}
	}

	bool set_text_quiet(std::string_view text){
		if(util::try_modify(raw_string_, text)){
			raw_string_ = text;
			update_tokenized_text_(raw_string_);
			return true;
		}
		return false;
	}

	void set_tokenizer_tag(const typesetting::tokenize_tag tag){
		if(util::try_modify(tokenize_tag_, tag)){
			update_tokenized_text_(raw_string_);
		}
	}

	[[nodiscard]] std::string_view get_text() const noexcept{
		return raw_string_;
	}
};

} // namespace mo_yanxi::gui