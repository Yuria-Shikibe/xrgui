module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.elem.text_edit_v2;

import std;
export import mo_yanxi.gui.text_edit_core;
export import mo_yanxi.gui.text_render_cache;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;
import mo_yanxi.math;
import mo_yanxi.unicode;

namespace mo_yanxi::gui{
export constexpr inline std::string_view text_edit_key_binding_name{"_text_edit_bind"};

export struct text_edit_key_binding;

enum struct text_edit_change_type : std::uint32_t{
	none = 0,
	text = 1 << 0,
	max_extent = 1 << 1,
	config = 1 << 2,
};

BITMASK_OPS(export, text_edit_change_type);

struct text_layout_result{
	math::vec2 required_extent;
	bool updated;
};

struct caret_cache{
	math::frect caret_rect_{};
	math::frect selection_rects_[3]{};
	unsigned selection_rect_count_{0};
	caret_section last_cached_caret_{};
};

export struct text_edit : elem{
public:
	inline static typesetting::layout_context layout_context{};

protected:
	text_editor_core core_{};
	typesetting::tokenized_text tokenized_text_{};
	typesetting::layout_config layout_config_{};
	typesetting::glyph_layout glyph_layout_{};
	text_render_cache render_cache_{};

	layout::expand_policy expand_policy_{};
	text_edit_change_type change_mark_{};
	bool fit_{};
	bool is_idle_{true};
	bool apply_tokens_{false};

	math::vec2 scale_{1.0f, 1.0f};

	float failed_hint_timer_{};
	float caret_blink_timer_{};

	float on_changed_interval_{30.f};
	float on_changed_timer_{};

	std::u32string hint_text_when_idle_{U">_..."};
	std::size_t maximum_code_points_{std::numeric_limits<std::size_t>::max()};
	mr::heap_uset<char32_t> filter_code_points{mr::get_default_heap_allocator()};
	bool is_code_filter_whitelist_{false};

	caret_cache caret_cache_{};

	struct text_transform_params{
		math::vec2 scale;
		math::vec2 offset_abs;
		math::vec2 offset_local;

		[[nodiscard]] math::vec2 forward_abs(math::vec2 p) const noexcept{ return p * scale + offset_abs; }
		[[nodiscard]] math::vec2 forward_local(math::vec2 p) const noexcept{ return p * scale + offset_local; }

		[[nodiscard]] math::frect forward_abs(const math::frect& r) const noexcept{
			math::vec2 p0 = forward_abs(r.vert_00());
			math::vec2 p1 = forward_abs(r.vert_11());
			return math::frect{
					tags::unchecked, tags::from_vertex,
					{std::min(p0.x, p1.x), std::min(p0.y, p1.y)},
					{std::max(p0.x, p1.x), std::max(p0.y, p1.y)}
				};
		}

		[[nodiscard]] math::frect forward_local(const math::frect& r) const noexcept{
			math::vec2 p0 = forward_local(r.vert_00());
			math::vec2 p1 = forward_local(r.vert_11());
			return math::frect{
					tags::unchecked, tags::from_vertex,
					{std::min(p0.x, p1.x), std::min(p0.y, p1.y)},
					{std::max(p0.x, p1.x), std::max(p0.y, p1.y)}
				};
		}

		[[nodiscard]] math::vec2 inverse_local(math::vec2 p) const noexcept{
			math::vec2 res = p - offset_local;
			if(std::abs(scale.x) > 1e-6f) res.x /= scale.x;
			else res.x = 0.f;
			if(std::abs(scale.y) > 1e-6f) res.y /= scale.y;
			else res.y = 0.f;
			return res;
		}
	};

	[[nodiscard]] text_transform_params get_transform_params() const noexcept{
		const math::vec2 raw_ext = glyph_layout_.extent;
		const math::vec2 abs_scale = scale_.copy().to_abs();
		const math::vec2 trans_ext = raw_ext * abs_scale;
		const math::vec2 reg_ext = get_glyph_draw_extent();

		math::vec2 c_scale = scale_;
		if(fit_ && trans_ext.x > 0.f && trans_ext.y > 0.f){
			c_scale *= (reg_ext / trans_ext);
		}

		// 推导：p_new = (p_old - raw_ext/2) * c_scale + src_pos + reg_ext/2
		// p_new = p_old * c_scale + (src_pos + reg_ext/2 - raw_ext/2 * c_scale)
		math::vec2 offset_abs = get_glyph_src_abs() + reg_ext * 0.5f - raw_ext * 0.5f * c_scale;
		math::vec2 offset_local = get_glyph_src_local() + reg_ext * 0.5f - raw_ext * 0.5f * c_scale;

		return {c_scale, offset_abs, offset_local};
	}

public:
	[[nodiscard]] text_edit(scene& scene, elem* parent)
		: elem(scene, parent){
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
		set_text_internal(hint_text_when_idle_);
	}

	~text_edit() override{
		set_focus(false);
	}

	template <typename StrTy>
		requires std::constructible_from<std::u32string, StrTy&&>
	void set_hint_text(StrTy&& ty){
		hint_text_when_idle_ = std::forward<StrTy>(ty);
		if(is_idle_){
			set_text_internal(hint_text_when_idle_);
		}
	}

	[[nodiscard]] std::u32string_view get_text() const noexcept{
		if(is_idle_) return {};
		return tokenized_text_.get_text();
	}

	[[nodiscard]] bool is_idle() const noexcept{ return is_idle_; }
	[[nodiscard]] bool is_failed() const noexcept{ return failed_hint_timer_ > 0.f; }

#pragma region TypeSetting_Settings
	align::pos text_entire_align{align::pos::top_left};

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{ return expand_policy_; }

	void set_expand_policy(const layout::expand_policy expand_policy){
		if(util::try_modify(expand_policy_, expand_policy)){
			notify_isolated_layout_changed();
		}
	}

	[[nodiscard]] math::vec2 get_scale() const noexcept{ return scale_; }

	void set_scale(const math::vec2 scale){
		if(util::try_modify(scale_, scale)){
			change_mark_ |= text_edit_change_type::max_extent;
			notify_isolated_layout_changed();
		}
	}

	void set_apply_tokens(const bool allow){
		if(util::try_modify(apply_tokens_, allow)){
			change_mark_ |= text_edit_change_type::text;
			notify_isolated_layout_changed();
		}
	}

	void set_typesetting_config(const typesetting::layout_config& config){
		if(util::try_modify(layout_config_, config)){
			change_mark_ |= text_edit_change_type::config;
			notify_isolated_layout_changed();
		}
	}

	void set_fit(bool fit = true){
		if(util::try_modify(fit_, fit)){
			change_mark_ |= text_edit_change_type::max_extent | text_edit_change_type::config;
			notify_isolated_layout_changed();
		}
	}

	[[nodiscard]] std::optional<graphic::color> get_text_color_scl() const noexcept{
		return render_cache_.get_text_color_scl();
	}

	void set_text_color_scl(const std::optional<graphic::color>& text_color_scl){
		if(render_cache_.set_text_color_scl(text_color_scl)){
			render_cache_.update_buffer(glyph_layout_, get_text_draw_color());
		}
	}

	void set_line_align(typesetting::line_alignment align){
		if(render_cache_.set_line_align(align)){
			render_cache_.update_buffer(glyph_layout_, get_text_draw_color());
		}
	}
#pragma endregion

#pragma region EditActions
	template <std::invocable<std::u32string&> Action>
	void apply_edit(Action&& action){
		bool actually_changed = false;
		tokenized_text_.modify(apply_tokens_ ? typesetting::tokenize_tag::kep : typesetting::tokenize_tag::raw,
			[&](std::u32string& text) -> bool{
				std::u32string old_text = text;
				bool result = std::invoke(std::forward<Action>(action), text);
				if(old_text != text){
					actually_changed = true;
				}
				return result;
			});

		change_mark_ |= text_edit_change_type::text;
		notify_isolated_layout_changed();

		if(actually_changed){
			if(on_changed_interval_ > 0.f){
				on_changed_timer_ = on_changed_interval_;
			} else{
				on_changed_timer_ = 0.f;
				on_changed();
			}
		}
	}

	void undo(){ apply_edit([&](std::u32string& text){ return core_.undo(text); }); }
	void redo(){ apply_edit([&](std::u32string& text){ return core_.redo(text); }); }

	void action_do_insert(std::u32string_view str){
		apply_edit([&](std::u32string& text){ return core_.insert_text(text, str); });
	}

	void action_do_delete(){ apply_edit([&](std::u32string& text){ return core_.action_delete(text); }); }
	void action_do_backspace(){ apply_edit([&](std::u32string& text){ return core_.action_backspace(text); }); }

	void action_enter(){
		if(is_character_allowed(U'\n')){
			apply_edit([&](std::u32string& text){ return core_.insert_text(text, U"\n"); });
		} else{
			set_input_invalid();
		}
	}

	void action_tab(){
		if(is_character_allowed(U'\t')){
			apply_edit([&](std::u32string& text){ return core_.insert_text(text, U"\t"); });
		} else{
			set_input_invalid();
		}
	}

	void action_move_left(bool select, bool jump){
		if(jump) core_.action_jump_left(glyph_layout_, tokenized_text_.get_text(), select);
		else core_.action_move_left(tokenized_text_.get_text(), select);
		reset_blink();
	}

	void action_move_right(bool select, bool jump){
		if(jump) core_.action_jump_right(glyph_layout_, tokenized_text_.get_text(), select);
		else core_.action_move_right(tokenized_text_.get_text(), select);
		reset_blink();
	}

	void action_move_up(bool select){
		core_.action_move_up(glyph_layout_, tokenized_text_.get_text(), render_cache_.get_line_align(), select);
		reset_blink();
	}

	void action_move_down(bool select){
		core_.action_move_down(glyph_layout_, tokenized_text_.get_text(), render_cache_.get_line_align(), select);
		reset_blink();
	}

	void action_move_line_begin(bool select){
		core_.action_move_line_begin(glyph_layout_, tokenized_text_.get_text(), select);
		reset_blink();
	}

	void action_move_line_end(bool select){
		core_.action_move_line_end(glyph_layout_, tokenized_text_.get_text(), select);
		reset_blink();
	}

	void action_select_all(){
		core_.action_select_all(tokenized_text_.get_text());
		reset_blink();
	}

	void action_copy() const;
	void action_paste();
	void action_cut();

#pragma endregion

	void set_banned_characters(std::initializer_list<char32_t> chars){
		set_character_filter_mode(false);
		filter_code_points = chars;
	}

	void set_character_filter_mode(bool is_whitelist) noexcept{
		is_code_filter_whitelist_ = is_whitelist;
	}

	[[nodiscard]] bool is_character_allowed(char32_t ch) const noexcept{
		bool contains = filter_code_points.contains(ch);
		return is_code_filter_whitelist_ ? contains : !contains;
	}

	void add_file_banned_characters(){
		set_banned_characters(std::initializer_list{U'<', U'>', U':', U'"', U'/', U'\\', U'|', U'*', U'?'});
	}

	[[nodiscard]] std::size_t get_max_code_points() const noexcept{
		return maximum_code_points_;
	}

	void set_max_code_points(std::size_t max_code_points){
		if(maximum_code_points_ == max_code_points) return;
		maximum_code_points_ = max_code_points;

		if(!is_idle_ && tokenized_text_.get_text().size() > maximum_code_points_){
			apply_edit([this](std::u32string& text){
				text.resize(maximum_code_points_);
				return true;
			});
			core_.action_move_line_end(glyph_layout_, tokenized_text_.get_text(), false);
		}
	}

	void set_on_changed_interval(float interval_ticks) noexcept{
		on_changed_interval_ = interval_ticks;
	}

	virtual void on_changed(){
	}

protected:
	void update_caret_cache();

	graphic::color get_text_draw_color() const noexcept;

	bool is_layout_expired_() const noexcept{
		return change_mark_ != text_edit_change_type::none;
	}

	bool resize_impl(const math::vec2 size) override;
	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override;
	text_layout_result layout_text(math::vec2 bound);

	[[nodiscard]] math::vec2 get_glyph_draw_extent() const noexcept{
		math::vec2 abs_scale = {std::abs(scale_.x), std::abs(scale_.y)};
		math::vec2 base_ext = glyph_layout_.extent * abs_scale;
		if(fit_){
			return mo_yanxi::gui::align::embed_to(align::scale::fit_smaller, base_ext, content_extent());
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


public:
	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;
	void layout_elem() override;
	bool update(float delta_in_ticks) override;

	events::op_afterwards on_drag(const events::drag event) override;

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		if(!event.key.on_release()){
			auto t_params = get_transform_params();
			math::vec2 raw_hit_pos = t_params.inverse_local(event.pos);

			if(core_.action_hit_test(glyph_layout_, tokenized_text_.get_text(), raw_hit_pos, render_cache_.get_line_align(), false)){
				reset_blink();
				return events::op_afterwards::intercepted;
			}
		}
		return events::op_afterwards::fall_through;
	}

	events::op_afterwards on_key_input(const input_handle::key_set key) override;
	events::op_afterwards on_unicode_input(const char32_t val) override;
	events::op_afterwards on_esc() override;

	void update_ime_position() const{
		// if (const auto cmt = get_scene().get_communicator()) {
		//     float fallback = context_.get_config().get_default_font_size().y;
		//     if (fallback < 1.0f) fallback = 16.0f;
		//     auto region = get_caret_local_aabb();
		//     region.move(util::transform_local2scene(*this, math::vec2{}));
		//     cmt->set_ime_cursor_rect(region);
		// }
	}

	void on_last_clicked_changed(bool isFocused) override{
		set_focused_key(isFocused);
		set_focus(isFocused);
	}

private:
	void set_focus(bool keyFocused);
	void set_text_internal(std::u32string_view str);

	void reset_blink(){
		caret_blink_timer_ = 0.f;
	}

	void set_input_invalid(){
		reset_blink();
		failed_hint_timer_ = 10.f;
	}

	void draw_selection_and_caret() const;
	[[nodiscard]] math::frect get_caret_local_aabb() const;
};

void load_default_text_edit_v2_key_binding(text_edit_key_binding& bind);

struct text_edit_key_binding : input_handle::key_mapping<text_edit&>{
	using key_mapping::key_mapping;

	void use_default_setting(){
		load_default_text_edit_v2_key_binding(*this);
	}
};

export struct text_edit_prov : text_edit{
private:
	struct trans{
		static std::u32string_view operator()(const text_edit_prov* src) noexcept{
			assert(src != nullptr);
			return src->get_text();
		}
	};

	react_flow::node_holder<react_flow::provider_cached<const text_edit_prov*, std::u32string_view, trans>> prov_;

public:
	[[nodiscard]] text_edit_prov(scene& scene, elem* parent)
		: text_edit(scene, parent){
		prov_->update_value(this);
	}

	template <typename S>
	auto& get_provider(this S& self) noexcept{
		return self.prov_.node;
	}

	void on_changed() override{
		prov_->pull_and_push(false);
	}

	style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override{
		return style::cursor_style{style::cursor_type::textarea};
	}
};
} // namespace mo_yanxi::gui
