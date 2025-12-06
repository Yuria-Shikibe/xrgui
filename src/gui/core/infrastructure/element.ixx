module;

#include <cassert>

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
export import :elem_ptr;


namespace mo_yanxi::gui{
export using boarder = align::spacing;

export constexpr inline boarder default_boarder{8, 8, 8, 8};

namespace style{

export struct elem_style_drawer : style_drawer<elem>{
	using style_drawer::style_drawer;

	[[nodiscard]] virtual boarder get_boarder() const noexcept{
		return {};
	}

};

export struct debug_elem_drawer final : elem_style_drawer{
	[[nodiscard]] constexpr debug_elem_drawer() : elem_style_drawer(tags::persistent){
	}

	boarder get_boarder() const noexcept override{
		return default_boarder;
	}

	void draw(const elem& element, rect region, float opacityScl) const override;
};

export struct empty_drawer final : elem_style_drawer{
	[[nodiscard]] constexpr empty_drawer() : elem_style_drawer(tags::persistent){}

	void draw(const elem& element, rect region, float opacityScl) const override{
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
	boarder style_boarder_cache_{style::get_default_style_drawer()->get_boarder()};
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

	// bool is_transparent_in_inbound_filter{};

private:
	//TODO direct access
	bool has_scene_direct_access{};

public:
	layout_state layout_state{};
	interactivity_flag interactivity{};

	style::elem_style_ptr style{style::get_default_style_drawer()};

private:
	float context_opacity_{1.f};
	float inherent_opacity_{1.f};

public:
	virtual ~elem(){
		clear_scene_references();
	}

	[[nodiscard]] elem(scene& scene, elem* parent) noexcept : scene_(std::addressof(scene)), parent_(parent){

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
	[[nodiscard]] struct scene& tooltip_get_scene() const noexcept override{
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
	void try_draw(const rect clipSpace) const{
		if(invisible) return;
		if(!clipSpace.overlap_inclusive(bound_abs())) return;
		draw(clipSpace);
	}

	void draw(const rect clipSpace) const{
		draw_content(clipSpace);
	}

	void set_style(const style::elem_style_drawer& style) noexcept{
		this->style = std::addressof(style);
		style_boarder_cache_ = style.get_boarder();
	}

	void set_style() noexcept{
		this->style = nullptr;
		style_boarder_cache_ = {};
	}

protected:
	virtual void on_opacity_changed(float previous){

	}

	void draw_background() const;

	virtual void draw_content(const rect clipSpace) const{
		draw_background();
	}
public:

#pragma endregion

#pragma region Behavior
public:

	virtual bool update(float delta_in_ticks);

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
	std::optional<math::vec2> pre_acquire_size_no_boarder_clip(const layout::optional_mastering_extent extent);

	std::optional<math::vec2> pre_acquire_size(const layout::optional_mastering_extent extent);

	//TODO responsibility chain to notify one?

	void notify_layout_changed(propagate_mask propagation);

	void notify_isolated_layout_changed();

protected:
	virtual std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept{
		return std::nullopt;
	}

public:

	[[nodiscard]] static std::optional<layout::layout_policy> search_layout_policy(const elem* from, bool allowNone) noexcept{
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

	[[nodiscard]] std::optional<layout::layout_policy> search_parent_layout_policy(bool allowNone) const noexcept{
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
	bool resize(const math::vec2 size, propagate_mask temp_mask){
		const auto last = layout_state.inherent_broadcast_mask;
		layout_state.inherent_broadcast_mask &= temp_mask;
		const auto rst = resize_impl(size);
		layout_state.inherent_broadcast_mask = last;
		return rst;
	}

	bool resize(const math::vec2 size){
		return resize_impl(size);
	}

	virtual void layout_elem(){
		layout_state.clear();
	}

	bool try_layout(){
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
	[[nodiscard]] virtual math::vec2 transform_to_children(math::vec2 where_relative_in_parent) const noexcept{
		return where_relative_in_parent - content_src_offset() - relative_pos_;
	}

	[[nodiscard]] virtual math::vec2 transform_from_children(math::vec2 where_relative_in_child) const noexcept{
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
	[[nodiscard]] const cursor_states& cursor_state() const noexcept{
		return cursor_states_;
	}

	[[nodiscard]] math::vec2 get_scaling() const noexcept{
		return context_scaling_ * inherent_scaling_;
	}

	[[nodiscard]] math::bool2 get_fill_parent() const noexcept{
		return fill_parent_;
	}

	void set_fill_parent(math::bool2 f, propagate_mask notify = propagate_mask::force_upper) noexcept{
		fill_parent_ = f;
		notify_layout_changed(notify);
	}

	[[nodiscard]] constexpr bool is_toggled() const noexcept{ return toggled; }

	[[nodiscard]] constexpr bool is_visible() const noexcept{ return !invisible; }

	[[nodiscard]] constexpr bool is_sleep() const noexcept{ return sleep; }

	[[nodiscard]] constexpr bool is_disabled() const noexcept{ return disabled; }


	[[nodiscard]] bool is_focus_extended_by_mouse() const noexcept{
		return extend_focus_until_mouse_drop;
	}

	void set_focus_extended_by_mouse(bool b) noexcept{
		extend_focus_until_mouse_drop = b;
	}

	[[nodiscard]] bool is_root_element() const noexcept{
		return parent_ == nullptr;
	}

	elem* set_parent(elem* const parent) noexcept{
		return std::exchange(parent_, parent);
	}

	[[nodiscard]] scene& get_scene() const noexcept{
		return *scene_;
	}

	[[nodiscard]] elem* parent() const noexcept{
		return parent_;
	}

	template <std::derived_from<elem> T, bool unchecked = false>
	[[nodiscard]] T* parent() const noexcept{
		if constexpr (!unchecked && !std::same_as<T, elem>){
			return dynamic_cast<T*>(parent_);
		}else{
			return static_cast<T*>(parent_);
		}
	}

	[[nodiscard]] vec2 content_extent() const noexcept{
		const auto [w, h] = size_.get_size();
		const auto [bw, bh] = boarder_extent();
		return {std::fdim(w, bw), std::fdim(h, bh)};
	}

	[[nodiscard]] float content_width() const noexcept{
		const auto w = size_.get_width();
		const auto bw = boarder_.width() + style_boarder_cache_.width();
		return std::fdim(w, bw);
	}

	[[nodiscard]] float content_height() const noexcept{
		const auto v = size_.get_height();
		const auto bv = boarder_.height() + style_boarder_cache_.height();
		return std::fdim(v, bv);
	}

	[[nodiscard]] vec2 extent() const noexcept{
		return size_.get_size();
	}

	[[nodiscard]] rect bound_abs() const noexcept{
		return rect{tags::unchecked, tags::from_extent, pos_abs(), extent()};
	}

	[[nodiscard]] rect bound_rel() const noexcept{
		return rect{tags::unchecked, tags::from_extent, pos_rel(), extent()};
	}

	[[nodiscard]] math::vec2 pos_abs() const noexcept{
		return absolute_pos_;
	}

	[[nodiscard]] math::vec2 pos_rel() const noexcept{
		return relative_pos_;
	}

	[[nodiscard]] constexpr align::spacing boarder() const noexcept{
		return boarder_ + style_boarder_cache_;
	}

	[[nodiscard]] constexpr vec2 boarder_extent() const noexcept{
		return boarder_.extent() + style_boarder_cache_.extent();
	}

	[[nodiscard]] constexpr math::vec2 content_src_offset() const noexcept{
		return boarder_.top_lft() + style_boarder_cache_.top_lft();
	}

	[[nodiscard]] rect clip_to_content_bound(rect region) const noexcept{
		return rect{tags::unchecked, tags::from_extent, region.src + content_src_offset(), region.extent().fdim(boarder_extent())};
	}

	[[nodiscard]] rect content_bound_abs() const noexcept{
		return clip_to_content_bound(bound_abs());
	}

	[[nodiscard]] rect content_bound_rel() const noexcept{
		return clip_to_content_bound(bound_rel());
	}

	[[nodiscard]] constexpr math::vec2 content_src_pos_abs() const noexcept{
		return pos_abs() + content_src_offset();
	}

	[[nodiscard]] constexpr math::vec2 content_src_pos_rel() const noexcept{
		return pos_rel() + content_src_offset();
	}

	constexpr void set_rel_pos(math::vec2 p) noexcept{
		relative_pos_ = p;
	}

	constexpr void set_rel_pos(math::vec2 p, float lerp_alpha) noexcept{
		if(lerp_alpha <= 0)return;
		const auto approch = p - relative_pos_;
		if(approch.is_zero(std::numeric_limits<float>::epsilon() * 16) || lerp_alpha >= 1.f){
			relative_pos_ = p;
		}else{
			relative_pos_ = math::fma(approch, lerp_alpha, relative_pos_);
		}
	}


	[[nodiscard]] elem* root_parent() const noexcept{
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

	[[nodiscard]] constexpr bool is_interactable() const noexcept{
		if(invisible) return false;
		if(disabled) return false;
		if(touch_blocked())return false;
		return true;
	}

	// [[nodiscard]] constexpr bool ignore_inbound() const noexcept{
	// 	return is_transparent_in_inbound_filter;
	// }

	[[nodiscard]] constexpr bool touch_blocked() const noexcept{
		return interactivity == interactivity_flag::disabled || interactivity == interactivity_flag::intercept;
	}

	[[nodiscard]] float get_draw_opacity() const noexcept{
		return context_opacity_ * inherent_opacity_;
	}

	void update_context_opacity(const float val) noexcept{
		const auto prev = get_draw_opacity();
		if(util::try_modify(context_opacity_, val)){
			on_opacity_changed(prev);
			for(const auto& element : children()){
				element->update_context_opacity(get_draw_opacity());
			}
		}
	}

	void set_opacity(const float val) noexcept{
		const auto prev = get_draw_opacity();
		if(util::try_modify(inherent_opacity_, val)){
			on_opacity_changed(prev);
			for(const auto& element : children()){
				element->update_context_opacity(get_draw_opacity());
			}
		}
	}

	bool set_prefer_extent(math::vec2 extent) noexcept{
		return util::try_modify(preferred_size_, size_.clamp(extent));
	}

	bool set_prefer_extent_to_current() noexcept{
		return util::try_modify(preferred_size_, extent());
	}

	[[nodiscard]] std::optional<vec2> get_prefer_extent() const noexcept{
		return preferred_size_;
	}

	[[nodiscard]] std::optional<vec2> get_prefer_content_extent() const noexcept{
		return preferred_size_.transform([this](math::vec2 extent){
			return extent.fdim(boarder_extent());
		});
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
	void reset_scene(struct scene* scene) noexcept;
};

namespace util{

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

	//TODO transform?
	auto transformed = current->transform_to_children(cursorPos);

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
[[nodiscard]] math::vec2 transform_from_root_to_current(const elem& where, math::vec2 inPos) noexcept{
	return where.transform_to_children([&]{
		if(auto p = where.parent()){
			return transform_from_root_to_current(*p, inPos);
		} else{
			return inPos;
		}
	}());
}

export
[[nodiscard]] math::vec2 transform_from_root_to_current(const elem* where, math::vec2 inPos) noexcept{
	if(!where) return inPos;
	return where->transform_to_children(transform_from_root_to_current(where->parent(), inPos));
}
export
[[nodiscard]] math::vec2 transform_from_root_to_current(std::span<const elem* const> range, math::vec2 inPos) noexcept{
	for (auto elem : range){
		assert(elem);
		inPos = elem->transform_to_children(inPos);
	}
	return inPos;
}

export
[[nodiscard]] math::vec2 transform_from_current_to_root(const elem* where, math::vec2 pos_in_children) noexcept{
	while(where){
		pos_in_children = where->transform_from_children(pos_in_children);
		where = where->parent();
	}
	return pos_in_children;
}

export
[[nodiscard]] math::vec2 transform_from_current_to_root(std::span<const elem* const> range, math::vec2 inPos) noexcept{
	for (auto elem : range){
		assert(elem);
		inPos = elem->transform_from_children(inPos);
	}
	return inPos;
}

events::op_afterwards thoroughly_esc(elem* where) noexcept;

events::op_afterwards thoroughly_esc(elem& where) noexcept{
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
bool click::within_elem(const elem& elem, float margin) const noexcept{
	auto p = pos + elem.boarder().bot_lft();
	p.x += margin;
	p.y += margin;
	return p.axis_greater(0, 0) && p.axis_less(elem.extent().add(margin * 2));
}
}
