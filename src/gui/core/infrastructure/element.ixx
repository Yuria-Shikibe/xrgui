module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.infrastructure:element;

export import mo_yanxi.gui.util.clamped_size;

export import mo_yanxi.gui.layout.policies;
export import mo_yanxi.gui.flags;
export import mo_yanxi.gui.util;

export import mo_yanxi.gui.style.interface;
export import mo_yanxi.gui.action;

import mo_yanxi.math;
import align;

import :events;
import :scene;
import :tooltip_interface;
import :type_def;
import :update_flag;

export import :elem_ptr;


namespace mo_yanxi::gui{

export constexpr inline boarder default_boarder{8, 8, 8, 8};

namespace style{

export struct elem_style_drawer : style_drawer<elem>{
	using style_drawer::style_drawer;

	[[nodiscard]] virtual boarder get_boarder() const noexcept{
		return {};
	}
};

export struct debug_elem_drawer final : elem_style_drawer{
	[[nodiscard]] constexpr debug_elem_drawer() : elem_style_drawer(tags::persistent, layer_draw_until<2>){
	}

	boarder get_boarder() const noexcept override{
		return default_boarder;
	}

	void draw_layer_impl(const elem& element, math::frect region, float opacityScl, gfx_config::layer_param layer_param) const override;

	void draw(const elem& element, rect region, float opacityScl) const;

	void draw_background(const elem& element, math::frect region, float opacityScl) const;
};

export struct empty_drawer final : elem_style_drawer{
	[[nodiscard]] constexpr empty_drawer() : elem_style_drawer(tags::persistent, layer_top_only){}

	void draw_layer_impl(
		const elem& element,
		math::frect region,
		float opacityScl,
		gfx_config::layer_param layer_param) const override{

	}
};


using elem_style_ptr = referenced_ptr<const elem_style_drawer>;

export constexpr inline debug_elem_drawer debug_style;
export constexpr inline empty_drawer empty_style;

export inline const elem_style_drawer* global_default_style_drawer{};

export inline const elem_style_drawer* get_default_style_drawer() noexcept{
	return global_default_style_drawer == nullptr ? &debug_style : global_default_style_drawer;
}

}

export
[[nodiscard]] layout::stated_extent clip_boarder_from(layout::stated_extent extent, const math::vec2 boarder_extent) noexcept{
	if(extent.width.mastering()){extent.width.value = std::fdim(extent.width.value, boarder_extent.x);}
	if(extent.height.mastering()){extent.height.value = std::fdim(extent.height.value, boarder_extent.y);}

	return extent;
}

export
[[nodiscard]] layout::optional_mastering_extent clip_boarder_from(layout::optional_mastering_extent extent, const math::vec2 boarder_extent) noexcept{
	auto [dx, dy] = extent.get_mastering();
	if(dx)extent.set_width(std::fdim(extent.potential_width(), boarder_extent.x));
	if(dy)extent.set_height(std::fdim(extent.potential_height(), boarder_extent.y));

	return extent;
}

export
struct cursor_states{
	float maximum_duration{60.};
	/**
	 * @brief in tick
	 */
	float time_inbound{};
	float time_focus{};
	float time_stagnate{};
	float time_pressed{};

	float time_tooltip{};

	bool inbound{};
	bool focused{};
	bool pressed{};

	void quit_focus() noexcept{
		focused = pressed = false;
	}

	void update_press(const input_handle::key_set k) noexcept{
		switch(k.action){
		case input_handle::act::press :
			pressed = true;
			break;
		default : pressed = false;
		}
	}

	void update(const float delta_in_ticks) noexcept {
		if(pressed){
			math::approach_inplace(time_pressed, maximum_duration, delta_in_ticks);
		} else{
			math::lerp_inplace(time_pressed, 0.f, delta_in_ticks* .075f);
		}

		if(inbound){
			math::approach_inplace(time_inbound, maximum_duration, delta_in_ticks);
		} else{
			math::lerp_inplace(time_inbound, 0.f, delta_in_ticks * .075f);
		}

		if(focused){
			math::approach_inplace(time_focus, maximum_duration, delta_in_ticks);
			math::approach_inplace(time_stagnate, maximum_duration, delta_in_ticks);
			time_tooltip += delta_in_ticks;
		} else{
			math::lerp_inplace(time_focus, 0.f, delta_in_ticks * .075f);
			time_tooltip = time_stagnate = 0.f;
		}
	}

	bool check_update_exitable() const noexcept{
		if(inbound || focused || pressed)return false;
		if(time_inbound > 0.f)return false;
		if(time_focus > 0.f) [[unlikely]] return false;
		if(time_stagnate > 0.f) [[unlikely]] return false;
		if(time_pressed > 0.f) [[unlikely]] return false;
		if(time_tooltip > 0.f) [[unlikely]] return false;
		return true;
	}

	[[nodiscard]] float get_factor_of(float cursor_states::* mptr) const noexcept{
		return this->*mptr / maximum_duration;
	}
};

export struct elem : tooltip::spawner_general<elem>{
	friend elem_ptr;
	friend scene;

private:
	void(*deleter_)(elem*) noexcept = nullptr;
	scene* scene_{};
	elem* parent_{};

	clamped_fsize size_{};
	std::optional<math::vec2> preferred_size_{};
	math::vec2 relative_pos_{};
	math::vec2 absolute_pos_{};
	boarder boarder_{};

public:
	style::elem_style_ptr style{style::get_default_style_drawer()};

private:
	boarder style_boarder_cache_{style ? style->get_boarder() : gui::boarder{}};

	//TODO make it async?
	std::deque<action::action_ptr<elem>, mr::heap_allocator<action::action_ptr<elem>>> actions{scene_->get_heap_allocator()};

public:
	layout::optional_mastering_extent restriction_extent{};

protected:
	cursor_states cursor_states_{};

	math::bool2 fill_parent_{};
	bool extend_focus_until_mouse_drop{};
	bool propagate_scaling_{true};

	math::vec2 inherent_scaling_{1.f, 1.f};
	math::vec2 context_scaling_{1.f, 1.f};

	//TODO using bit flags?
	bool toggled{};
	bool disabled{};
public:
	bool invisible{};
	bool sleep{};

	update_flag update_flag{};

	// bool is_transparent_in_inbound_filter{};

private:
	//TODO direct access
	// bool has_scene_direct_access{};

public:
	layout_state layout_state{};
	interactivity_flag interactivity{};


private:
	float context_opacity_{1.f};
	float inherent_opacity_{1.f};
	altitude_t layer_altitude_{};

public:
	virtual ~elem(){
		clear_scene_references();
	}

	[[nodiscard]] elem(scene& scene, elem* parent) noexcept :
		scene_(std::addressof(scene)),
		parent_(parent){
		init_altitude_(parent_ ? parent_->layer_altitude_ + 1 : 0);
	}

	elem(const elem& other) = delete;
	elem(elem&& other) noexcept = delete;
	elem& operator=(const elem& other) = delete;
	elem& operator=(elem&& other) noexcept = delete;

#pragma region Action

public:
	template <std::derived_from<action::action<elem>> ActionType, typename... Args>
		requires (std::constructible_from<ActionType, mr::heap_allocator<>, Args&&...>)
	ActionType& push_action(Args&&... args){
		auto& ptr = actions.emplace_back(std::in_place_type<ActionType>, get_scene().get_heap_allocator(), std::forward<Args>(args)...);
		auto& ref = static_cast<ActionType&>(*ptr);
		return ref;
	}


#pragma endregion

#pragma region Tooltip

public:
	[[nodiscard]] tooltip::align_config tooltip_get_align_config() const override;

	[[nodiscard]] bool tooltip_spawner_contains(math::vec2 cursorPos) const noexcept override;

	[[nodiscard]] bool tooltip_should_build(math::vec2 cursorPos) const noexcept override{
		return true
			and has_tooltip_builder()
			and tooltip_create_config.auto_build()
			and cursor_states_.time_tooltip > tooltip_create_config.min_hover_time;
	}

	[[nodiscard]] bool tooltip_should_maintain(math::vec2 cursorPos) const override{
		assert(tooltip_handle.handle);
		return !tooltip_create_config.auto_release || tooltip_handle.handle->is_focused_key();
	}

protected:
	[[nodiscard]] scene& tooltip_get_scene() const noexcept final{
		return get_scene();
	}

	void tooltip_on_drop_behavior_impl() override{
		cursor_states_.time_tooltip = -10.f;
	}

public:
#pragma endregion

#pragma region Event

	virtual void on_inbound_changed(bool is_inbounded, bool changed){
		cursor_states_.inbound = is_inbounded;
	}

	virtual void on_focus_changed(bool is_focused){
		cursor_states_.focused = is_focused;
		if(!is_focused && !is_focus_extended_by_mouse())cursor_states_.pressed = false;
	}

	virtual events::op_afterwards on_key_input(const input_handle::key_set key){
		return events::op_afterwards::fall_through;
	}

	virtual events::op_afterwards on_unicode_input(const char32_t val){
		return events::op_afterwards::fall_through;
	}

	virtual events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves){
		if(!is_interactable()){
			return events::op_afterwards::fall_through;
		}else if(is_focused()){
			cursor_states_.update_press(event.key);
		}

		return events::op_afterwards::fall_through;
	}

	virtual events::op_afterwards on_drag(const events::drag event){
		if(!is_interactable()){
			return events::op_afterwards::fall_through;
		}
		return events::op_afterwards::intercepted;
	}


	virtual events::op_afterwards on_cursor_moved(const events::cursor_move event){
		cursor_states_.time_stagnate = 0;
		if(tooltip_create_config.use_stagnate_time && !event.delta().equals({})){
			cursor_states_.time_tooltip = 0.;
			tooltip_notify_drop();
		}
		return events::op_afterwards::fall_through;
	}

	virtual events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves){
		return events::op_afterwards::fall_through;
	}

	virtual events::op_afterwards on_esc(){
		return events::op_afterwards::fall_through;
	}

#pragma endregion

#pragma region Draw
public:
	// void try_draw(const rect clipSpace) const{
	// 	if(invisible) return;
	// 	//TODO fix this
	// 	if(!clipSpace.overlap_inclusive(bound_abs())) return;
	// 	draw(clipSpace);
	// }
	//
	// void try_draw_background(const rect clipSpace) const{
	// 	if(invisible) return;
	// 	if(!clipSpace.overlap_inclusive(bound_abs())) return;
	// 	draw_background(clipSpace);
	// }

	// void draw(const rect clipSpace) const{
	// 	draw_content_impl(clipSpace);
	// }
	//
	// void draw_background(const rect clipSpace) const{
	// 	draw_background_impl(clipSpace);
	// }

	void set_style(const style::elem_style_drawer& style) noexcept{
		this->style = std::addressof(style);
		style_boarder_cache_ = style.get_boarder();
	}

	void set_style() noexcept{
		this->style = nullptr;
		style_boarder_cache_ = {};
	}

	virtual void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const;


	FORCE_INLINE void try_draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const{
		if(invisible) return;
		if(!clipSpace.overlap_inclusive(bound_abs())) return;
		draw_layer(clipSpace, param);
	}

protected:
	virtual void on_opacity_changed(float previous){
	}

	FORCE_INLINE void draw_style(gfx_config::layer_param param) const{
		if(style)style->draw_layer(*this, bound_abs(), get_draw_opacity(), param);
	}
	// void draw_style_background() const;

	// virtual void draw_background_impl(const rect clipSpace) const{
	// 	draw_style_background();
	// }
	//
	// virtual void draw_content_impl(const rect clipSpace) const{
	// 	draw_style();
	// }

public:

#pragma endregion

#pragma region Behavior
public:

	virtual bool update(float delta_in_ticks);

protected:
	void propagate_update_requirement_since_self(bool required){
		auto last = this;
		auto cur = parent();
		update_requirement_set_result cur_rst{true, required};
		while(cur){
			if((cur_rst = cur->update_flag.set_child_mark_update_changed(last, cur_rst.is_required()))){
				last = cur;
				cur = cur->parent();
			}else{
				break;
			}
		}
	}

	void clear_children_update_required( elem* children_of_self) noexcept{
		if(children_of_self->update_flag.is_update_required()){
			if(const auto rst = update_flag.set_child_mark_update_changed(children_of_self, false)){
				propagate_update_requirement_since_self(rst.is_required());
			}
		}
	}
public:
	void set_update_required(update_channel channel, update_channel mask = update_channel{~0U}) noexcept{
		if(const auto rst = update_flag.set_self_update_required(channel, mask)){
			propagate_update_requirement_since_self(rst.is_required());
		}
	}
	void set_update_disabled(update_channel channel) noexcept{
		set_update_required({}, channel);
	}

	void clear_scene_references() noexcept;
	void clear_scene_references_recursively() noexcept{
		clear_scene_references();

		for (auto && child : children()){
			child->clear_scene_references_recursively();
		}
	}

	void require_scene_cursor_update() const noexcept{
		get_scene().request_cursor_update();
	}

#pragma endregion

#pragma region Layout
protected:

	/**
	 * @brief pre get the size of this elem if none/one side of extent is known
	 *
	 * *recommended* to be const
	 *
	 * @param extent : any tag of the length should be within {mastering, external}
	 * @return expected CONTENT size, or nullopt
	 */
	virtual std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent){
		return std::nullopt;
	}

public:
	FORCE_INLINE inline std::optional<math::vec2> pre_acquire_size_no_boarder_clip(const layout::optional_mastering_extent extent){
		return pre_acquire_size_impl(extent).transform([&, this](const math::vec2 v){
			return size_.clamp(v + boarder_extent()).min(extent.potential_extent());
		});
	}

	FORCE_INLINE inline std::optional<math::vec2> pre_acquire_size(const layout::optional_mastering_extent extent){
		return pre_acquire_size_impl(clip_boarder_from(extent, boarder_extent())).transform([&, this](const math::vec2 v){
			return size_.clamp(v + boarder_extent()).min(extent.potential_extent());
		});
	}

	//TODO responsibility chain to notify one?

	void notify_layout_changed(propagate_mask propagation);

	void notify_isolated_layout_changed();

protected:
	virtual std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept{
		return std::nullopt;
	}

public:
	[[nodiscard]] FORCE_INLINE inline static std::optional<layout::layout_policy> search_layout_policy(const elem* from, bool allowNone) noexcept{
		auto ptr = from;
		while(true){
			if(!ptr){
				return std::nullopt;
			}
			if(auto p = ptr->search_layout_policy_getter_impl()){
				if(allowNone || p.value() != layout::layout_policy::none)return p;
			}
			ptr = ptr->parent();
		}
	}

	[[nodiscard]] FORCE_INLINE inline std::optional<layout::layout_policy> search_parent_layout_policy(bool allowNone) const noexcept{
		return search_layout_policy(parent(), allowNone);
	}

protected:
	virtual bool resize_impl(const math::vec2 size){

		if(size_.set_size(size)){
			notify_layout_changed(propagate_mask::all);
			return true;
		}
		return false;
	}

public:
	FORCE_INLINE inline bool resize(const math::vec2 size, propagate_mask temp_mask){
		const auto last = layout_state.inherent_broadcast_mask;
		layout_state.inherent_broadcast_mask &= temp_mask;
		const auto rst = resize_impl(size);
		layout_state.inherent_broadcast_mask = last;
		return rst;
	}

	FORCE_INLINE inline bool resize(const math::vec2 size){
		return resize_impl(size);
	}

	virtual void layout_elem(){
		layout_state.clear();
	}

	FORCE_INLINE inline bool try_layout(){
		if(layout_state.any_lower_changed()){
			layout_elem();
			return true;
		}
		return false;
	}


#pragma endregion

#pragma region Group
public:

	[[nodiscard]] virtual std::span<const elem_ptr> children() const noexcept{
		return {};
	}

	[[nodiscard]] bool has_children() const noexcept{
		return !children().empty();
	}

	virtual bool set_scaling(math::vec2 scl) noexcept {
		assert(!scl.is_NaN());
		if(!util::try_modify(context_scaling_, scl))return false;
		context_scaling_ = scl;
		layout_state.notify_self_changed();
		auto c = children();
		if(!c.empty() && propagate_scaling_){
			layout_state.notify_children_changed();
			auto s = get_scaling();
			for (auto && elem : c){
				elem->set_scaling(s);
			}
		}
		return true;
	}


	virtual bool update_abs_src(math::vec2 parent_content_src) noexcept;
#pragma endregion

#pragma region Transform
public:
	[[nodiscard]] virtual math::vec2 transform_to_content_space(math::vec2 where_relative_in_parent) const noexcept{
		return where_relative_in_parent - content_src_offset() - relative_pos_;
	}

	[[nodiscard]] virtual math::vec2 transform_from_content_space(math::vec2 where_relative_in_child) const noexcept{
		return where_relative_in_child + content_src_offset() + relative_pos_;
	}

	[[nodiscard]] bool contains(math::vec2 pos_relative) const noexcept;

	[[nodiscard]] bool contains_self(math::vec2 pos_relative, float margin) const noexcept;

protected:
	[[nodiscard]] virtual bool parent_contain_constrain(math::vec2 pos_relative) const noexcept;
public:

#pragma endregion

#pragma region State

	[[nodiscard]] bool is_focused_scroll() const noexcept;
	[[nodiscard]] bool is_focused_key() const noexcept;

	[[nodiscard]] bool is_focused() const noexcept;
	[[nodiscard]] bool is_inbounded() const noexcept;

	void set_focused_scroll(bool focus) noexcept;
	void set_focused_key(bool focus) noexcept;

	virtual void on_focus_key_changed(bool isFocused){

	}

	virtual bool set_toggled(bool isToggled){
		return util::try_modify(toggled, isToggled);
	}

	virtual bool set_disabled(bool isDisabled){
		return util::try_modify(disabled, isDisabled);
	}

#pragma endregion

#pragma region Trivial_Getter_Setters
public:
	[[nodiscard]] FORCE_INLINE inline const cursor_states& cursor_state() const noexcept{
		return cursor_states_;
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 get_scaling() const noexcept{
		return context_scaling_ * inherent_scaling_;
	}

	[[nodiscard]] FORCE_INLINE inline math::bool2 get_fill_parent() const noexcept{
		return fill_parent_;
	}

	FORCE_INLINE inline void set_fill_parent(math::bool2 f, propagate_mask notify = propagate_mask::force_upper) noexcept{
		fill_parent_ = f;
		notify_layout_changed(notify);
	}

	[[nodiscard]] FORCE_INLINE inline bool is_toggled() const noexcept{ return toggled; }

	[[nodiscard]] FORCE_INLINE inline bool is_visible() const noexcept{ return !invisible; }

	[[nodiscard]] FORCE_INLINE inline bool is_sleep() const noexcept{ return sleep; }

	[[nodiscard]] FORCE_INLINE inline bool is_disabled() const noexcept{ return disabled; }


	[[nodiscard]] FORCE_INLINE inline bool is_focus_extended_by_mouse() const noexcept{
		return extend_focus_until_mouse_drop;
	}

	FORCE_INLINE inline void set_focus_extended_by_mouse(bool b) noexcept{
		extend_focus_until_mouse_drop = b;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_root_element() const noexcept{
		return parent_ == nullptr;
	}

	FORCE_INLINE inline elem* set_parent(elem* const parent) noexcept{
		auto rst = std::exchange(parent_, parent);
		update_altitude_(parent_ ? parent_->layer_altitude_ + 1 : 0);
		return rst;
	}

	[[nodiscard]] FORCE_INLINE inline renderer_frontend& renderer() const noexcept{
		return get_scene().renderer();
	}

	[[nodiscard]] FORCE_INLINE inline scene& get_scene() const noexcept{
		return *scene_;
	}

	[[nodiscard]] FORCE_INLINE inline elem* parent() const noexcept{
		return parent_;
	}

	template <std::derived_from<elem> T, bool unchecked = false>
	[[nodiscard]] FORCE_INLINE T* parent() const noexcept{
		if constexpr (!unchecked && !std::same_as<T, elem>){
			return dynamic_cast<T*>(parent_);
		}else{
			return static_cast<T*>(parent_);
		}
	}

	[[nodiscard]] FORCE_INLINE inline vec2 content_extent() const noexcept{
		const auto [w, h] = size_.get_size();
		const auto [bw, bh] = boarder_extent();
		return {std::fdim(w, bw), std::fdim(h, bh)};
	}

	[[nodiscard]] FORCE_INLINE inline float content_width() const noexcept{
		const auto w = size_.get_width();
		const auto bw = boarder_.width() + style_boarder_cache_.width();
		return std::fdim(w, bw);
	}

	[[nodiscard]] FORCE_INLINE inline float content_height() const noexcept{
		const auto v = size_.get_height();
		const auto bv = boarder_.height() + style_boarder_cache_.height();
		return std::fdim(v, bv);
	}

	[[nodiscard]] FORCE_INLINE inline vec2 extent() const noexcept{
		return size_.get_size();
	}

	[[nodiscard]] FORCE_INLINE inline rect bound_abs() const noexcept{
		return rect{tags::unchecked, tags::from_extent, pos_abs(), extent()};
	}

	[[nodiscard]] FORCE_INLINE inline rect bound_rel() const noexcept{
		return rect{tags::unchecked, tags::from_extent, pos_rel(), extent()};
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 pos_abs() const noexcept{
		return absolute_pos_;
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 pos_rel() const noexcept{
		return relative_pos_;
	}

	[[nodiscard]] FORCE_INLINE inline align::spacing boarder() const noexcept{
		return boarder_ + style_boarder_cache_;
	}

	[[nodiscard]] FORCE_INLINE inline vec2 boarder_extent() const noexcept{
		return boarder_.extent() + style_boarder_cache_.extent();
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 content_src_offset() const noexcept{
		return boarder_.top_lft() + style_boarder_cache_.top_lft();
	}

	[[nodiscard]] FORCE_INLINE inline rect clip_to_content_bound(rect region) const noexcept{
		return rect{tags::unchecked, tags::from_extent, region.src + content_src_offset(), region.extent().fdim(boarder_extent())};
	}

	[[nodiscard]] FORCE_INLINE inline rect content_bound_abs() const noexcept{
		return clip_to_content_bound(bound_abs());
	}

	[[nodiscard]] FORCE_INLINE inline rect content_bound_rel() const noexcept{
		return clip_to_content_bound(bound_rel());
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 content_src_pos_abs() const noexcept{
		return pos_abs() + content_src_offset();
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 content_src_pos_rel() const noexcept{
		return pos_rel() + content_src_offset();
	}

	FORCE_INLINE inline bool set_rel_pos(math::vec2 p) noexcept{
		return util::try_modify(relative_pos_, p);
	}

	FORCE_INLINE inline bool set_rel_pos(math::vec2 p, float lerp_alpha) noexcept{
		if(lerp_alpha <= 0)return false;
		const auto approch = p - relative_pos_;
		if(approch.is_zero(std::numeric_limits<float>::epsilon() * 16) || lerp_alpha >= 1.f){
			relative_pos_ = p;
			return false;
		}else{
			relative_pos_ = math::fma(approch, lerp_alpha, relative_pos_);
			return true;
		}
	}


	[[nodiscard]] FORCE_INLINE inline elem* root_parent() const noexcept{
		elem* cur = parent();
		if(!cur) return nullptr;

		while(true){
			if(const auto next = cur->parent()){
				cur = next;
			} else{
				break;
			}
		}
		return cur;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_interactable() const noexcept{
		if(invisible) return false;
		if(disabled) return false;
		if(touch_blocked())return false;
		return true;
	}

	// [[nodiscard]] constexpr bool ignore_inbound() const noexcept{
	// 	return is_transparent_in_inbound_filter;
	// }

	[[nodiscard]] FORCE_INLINE inline bool touch_blocked() const noexcept{
		return interactivity == interactivity_flag::disabled || interactivity == interactivity_flag::intercept;
	}

	[[nodiscard]] FORCE_INLINE inline float get_draw_opacity() const noexcept{
		return context_opacity_ * inherent_opacity_;
	}

	FORCE_INLINE inline void update_context_opacity(const float val) noexcept{
		const auto prev = get_draw_opacity();
		if(util::try_modify(context_opacity_, val)){
			on_opacity_changed(prev);
			for(const auto& element : children()){
				element->update_context_opacity(get_draw_opacity());
			}
		}
	}

	FORCE_INLINE inline void set_opacity(const float val) noexcept{
		const auto prev = get_draw_opacity();
		if(util::try_modify(inherent_opacity_, val)){
			on_opacity_changed(prev);
			for(const auto& element : children()){
				element->update_context_opacity(get_draw_opacity());
			}
		}
	}

	FORCE_INLINE inline bool set_prefer_extent(math::vec2 extent) noexcept{
		return util::try_modify(preferred_size_, size_.clamp(extent));
	}

	FORCE_INLINE inline bool set_prefer_extent_to_current() noexcept{
		return util::try_modify(preferred_size_, extent());
	}

	[[nodiscard]] FORCE_INLINE inline std::optional<vec2> get_prefer_extent() const noexcept{
		return preferred_size_;
	}

	[[nodiscard]] FORCE_INLINE inline std::optional<vec2> get_prefer_content_extent() const noexcept{
		return preferred_size_.transform([this](math::vec2 extent){
			return extent.fdim(boarder_extent());
		});
	}

	[[nodiscard]] FORCE_INLINE inline altitude_t get_altitude() const noexcept{
		return layer_altitude_;
	}

#pragma endregion

public:
	template <typename T, typename S, typename ...Args>
	T& request_react_node(this S& self, Args&& ...args){
		return self.get_scene().template request_react_node<T>(self, std::forward<Args>(args)...);
	}

	template <typename T, typename S>
		requires (std::derived_from<std::remove_cvref_t<T>, react_flow::node>)
	T& request_react_node(this S& self, T&& args){
		return self.get_scene().template request_react_node<T>(self, std::forward<T>(args));
	}

protected:
	template <typename T = std::byte>
	auto get_heap_allocator() const noexcept{
		return scene_->get_heap_allocator<T>();
	}

	[[nodiscard]] auto* get_memory_resource() const noexcept{
		return scene_->get_memory_resource();
	}

	template <typename S, typename T, typename ...Args>
	T& request_and_cache_node(this S& self, T* S::* cache, Args&& ...args){
		if(self.*cache){
			return *(self.*cache);
		}else{
			auto& node = self.template request_react_node<T>(self, std::forward<Args>(args)...);
			self.*cache = std::addressof(node);
			return node;
		}
	}

private:
	void update_altitude_(altitude_t height);

	void init_altitude_(altitude_t height);

	void relocate_scene_(struct scene* scene) noexcept;
};

namespace util{

export
math::vec2 select_prefer_extent(bool is_prefer, math::vec2 current, std::optional<math::vec2> preferred){
	if(is_prefer){
		if(const auto pref = preferred)current.max(*pref);
	}

	return current;
}

export
template <typename Container>
void dfs_record_inbound_element(
	math::vec2 cursorPos,
	Container& selected,
	elem* current){
	if(current->is_disabled() || current->interactivity == interactivity_flag::disabled)return;

	if(!current->contains_self(cursorPos, 0)){
		return;
	}

	selected.push_back(current);

	if(current->touch_blocked() || !current->has_children()) return;

	auto transformed = current->transform_to_content_space(cursorPos);

	for(const auto& child : current->children()/* | std::views::reverse*/){
		if(!child->is_visible())continue;
		util::dfs_record_inbound_element<Container>(transformed, selected, child.get());

		//TODO better inbound shadow, maybe dialog system instead of add to root
		if(child->interactivity == interactivity_flag::intercept && child->get_fill_parent().x && child->get_fill_parent().y){
			break;
		}
	}
}

template <typename Alloc = std::allocator<elem*>>
std::vector<elem*, std::remove_cvref_t<Alloc>> dfs_find_deepest_element(elem* root, math::vec2 cursorPos, Alloc&& alloc = {}){
	std::vector<elem*, std::remove_cvref_t<Alloc>> rst{std::forward<Alloc>(alloc)};

	util::dfs_record_inbound_element(cursorPos, rst, root);

	return rst;
}


export
bool set_fill_parent(
	elem& item,
	const math::vec2 boundSize,
	const math::bool2 mask = {true, true},
	const math::bool2 expansion_mask = {false, false},
	const propagate_mask direction_mask = propagate_mask::lower){
	const auto [fx, fy] = item.get_fill_parent() && mask;
	if(!fx && !fy) return false;

	const auto [ox, oy] = item.extent();

	if(fx) item.restriction_extent.set_width(boundSize.x);
	else{
		if(expansion_mask.x){
			item.restriction_extent.set_width_pending();
		} else{
			item.restriction_extent.set_width(boundSize.x);
		}
	}

	if(fy) item.restriction_extent.set_height(boundSize.y);
	else{
		if(expansion_mask.y){
			item.restriction_extent.set_height_pending();
		} else{
			item.restriction_extent.set_height(boundSize.y);
		}
	}

	return item.resize({
						   fx ? boundSize.x : ox,
						   fy ? boundSize.y : oy
					   }, direction_mask);
}

export
unsigned inline get_nest_depth(const elem* where) noexcept{
	unsigned cur{};
	while(where){
		where = where->parent();
		cur++;
	}
	return cur;
}

math::vec2 inline helper_transform_scene2content(const elem* where, math::vec2 inPos) noexcept{
	if(where){
		return where->transform_to_content_space(helper_transform_scene2content(where->parent(), inPos));
	}else{
		return inPos;
	}
}

export
[[nodiscard]] math::vec2 inline transform_scene2local(const elem& where, math::vec2 inPos) noexcept{
	const auto position_in_current_relative_space = helper_transform_scene2content(where.parent(), inPos);
	return position_in_current_relative_space - where.pos_rel();
}

export
[[nodiscard]] math::vec2 inline transform_scene2local(std::span<const elem* const> elems, math::vec2 inPos) noexcept{
	auto cur = elems.begin();
	const auto end = elems.end();
	if(cur == end) return inPos;
	const auto prev = --elems.end();

	math::vec2 pos = inPos;
	for(; cur != prev; ++cur){
		pos = (*cur)->transform_to_content_space(pos);
	}

	return pos - (*cur)->pos_rel();
}
export
[[nodiscard]] math::vec2 inline transform_local2scene(const elem& where, math::vec2 inPos) noexcept{
	inPos += where.pos_rel();

	auto parent = where.parent();
	while(parent){
		inPos = parent->transform_from_content_space(inPos);
		parent = parent->parent();
	}

	return inPos;
}

export
[[nodiscard]] math::vec2 inline transform_current2parent(const elem& where, math::vec2 pos_in_local_space) noexcept{
	pos_in_local_space += where.pos_rel();
	if(auto p = where.parent()){
		pos_in_local_space = p->transform_from_content_space(pos_in_local_space) - p->pos_rel();
	}

	return pos_in_local_space;
}

// export
// [[nodiscard]] math::vec2 inline transform_from_root_to_current(const elem& where, math::vec2 inPos) noexcept{
// 	return where.transform_to_content_space([&]{
// 		if(auto p = where.parent()){
// 			return transform_from_root_to_current(*p, inPos);
// 		} else{
// 			return inPos;
// 		}
// 	}());
// }
//
// /**
//  *
//  * @param where target element
//  * @param inPos position in scene space
//  * @return position in target CONTENT space
//  */
// export
// [[nodiscard]] FORCE_INLINE inline math::vec2 transform_from_root_to_current(const elem* where, math::vec2 inPos) noexcept{
// 	if(!where) return inPos;
// 	return where->transform_to_content_space(transform_from_root_to_current(where->parent(), inPos));
// }
//
//
// /**
//  *
//  * @param where target element
//  * @param inPos position in scene space
//  * @return position in target CONTENT space
//  */
// export
// [[nodiscard]] FORCE_INLINE inline math::vec2 transform_from_root_to_current(std::span<const elem* const> range, math::vec2 inPos) noexcept{
// 	for (auto elem : range){
// 		assert(elem);
// 		inPos = elem->transform_to_content_space(inPos);
// 	}
// 	return inPos;
// }
//
// export
// [[nodiscard]] FORCE_INLINE inline math::vec2 transform_from_current_to_root(const elem* where, math::vec2 pos_in_children) noexcept{
// 	while(where){
// 		pos_in_children = where->transform_from_content_space(pos_in_children);
// 		where = where->parent();
// 	}
// 	return pos_in_children;
// }
//
// export
// [[nodiscard]] FORCE_INLINE inline math::vec2 transform_from_current_to_root(std::span<const elem* const> range, math::vec2 inPos) noexcept{
// 	for (auto elem : range){
// 		assert(elem);
// 		inPos = elem->transform_from_content_space(inPos);
// 	}
// 	return inPos;
// }

events::op_afterwards thoroughly_esc(elem* where) noexcept;

FORCE_INLINE inline events::op_afterwards thoroughly_esc(elem& where) noexcept{
	return thoroughly_esc(std::addressof(where));
}


}

export
template <std::derived_from<elem> D, bool unchecked = false, std::derived_from<elem> B>
D& elem_cast(B& b) noexcept(unchecked || std::same_as<D, elem>){
	if constexpr (unchecked || std::same_as<D, elem>){
		return static_cast<D&>(b);
	}else{
		return dynamic_cast<D&>(b);
	}
}

export
template <std::derived_from<elem> D, bool unchecked = false, std::derived_from<elem> B>
const D& elem_cast(const B& b) noexcept(unchecked || std::same_as<D, elem>){
	if constexpr (unchecked || std::same_as<D, elem>){
		return static_cast<const D&>(b);
	}else{
		return dynamic_cast<const D&>(b);
	}
}

mr::heap_allocator<elem> elem_ptr::alloc_of(const scene& s) noexcept{
	return s.get_heap_allocator<elem>();
}

mr::heap_allocator<elem> elem_ptr::alloc_of(const elem* ptr) noexcept{
	return alloc_of(ptr->get_scene());
}

void elem_ptr::set_deleter(elem* element, void(* p)(elem*) noexcept) noexcept{
	element->deleter_ = p;
}

void elem_ptr::delete_elem(elem* ptr) noexcept{
	ptr->deleter_(ptr);
}


}


namespace mo_yanxi::gui::events{
math::vec2 click::get_content_pos(const elem& elem) const noexcept{
	return pos - elem.content_src_offset();
}

bool click::within_elem(const elem& elem, float margin) const noexcept{
	auto p = pos;
	p.x += margin;
	p.y += margin;
	return p.axis_greater(0, 0) && p.axis_less(elem.extent().add(margin * 2));
}

}
