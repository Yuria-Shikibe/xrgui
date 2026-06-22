module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>
#include <gch/small_vector.hpp>



export module mo_yanxi.gui.infrastructure:element;

import std;

export import mo_yanxi.gui.util.clamped_size;

export import mo_yanxi.gui.layout.policies;
export import mo_yanxi.gui.flags;
export import mo_yanxi.gui.util;

export import mo_yanxi.gui.style.interface;
export import mo_yanxi.gui.action;
import mo_yanxi.gui.action.queue;

import mo_yanxi.math;
import align;
import mo_yanxi.audio.resources;
import mo_yanxi.gui.sound.manager;

import :events;
import :scene;
import :tooltip_interface;
import :defines;
import :flags;

export import :elem_ptr;
import mo_yanxi.transparent_span;
import mo_yanxi.function_call_stack;


namespace mo_yanxi::gui{
export constexpr inline border_t default_border{8, 8, 8, 8};

export
struct overlay_manager;


export
struct cursor_states{
	float maximum_duration{16.};
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
		constexpr static auto lerp_zero = [](float& v){
			if(v <  0.0015f)v = 0;
		};
		if(pressed){
			math::approach_inplace(time_pressed, maximum_duration, delta_in_ticks);
		} else{
			math::lerp_inplace(time_pressed, 0.f, delta_in_ticks* .075f);
			lerp_zero(time_pressed);
		}

		if(inbound){
			math::approach_inplace(time_inbound, maximum_duration, delta_in_ticks);
		} else{
			math::lerp_inplace(time_inbound, 0.f, delta_in_ticks * .075f);
			lerp_zero(time_inbound);

		}

		if(focused){
			math::approach_inplace(time_focus, maximum_duration, delta_in_ticks);
			math::approach_inplace(time_stagnate, maximum_duration, delta_in_ticks);
			time_tooltip += delta_in_ticks;
		} else{
			math::lerp_inplace(time_focus, 0.f, delta_in_ticks * .075f);
			lerp_zero(time_focus);
			time_tooltip = time_stagnate = 0.f;
		}
	}

	bool check_update_exitable() const noexcept{
		if(inbound || focused || pressed)return false;
		if(time_inbound > 0.f)return false;
		if(time_focus > 0.f) [[unlikely]] return false;
		if(time_stagnate > 0.f) [[unlikely]] return false;
		if(time_pressed > 0.f) [[unlikely]] return false;
		return true;
	}

	[[nodiscard]] float get_factor_of(float cursor_states::* mptr) const noexcept{
		return this->*mptr / maximum_duration;
	}
};

export
using elem_span = transparent_span<elem* const>;

export
struct elem_wrapper{
private:
	struct entry{
		void* flag;
		elem* e;
	};

	union{
		entry etry;
		elem_span span;
	};

	constexpr bool is_span_() const noexcept{
		if consteval{
			auto rst = std::bit_cast<std::array<void*, 2>>(*this);
			return rst[0] != nullptr;
		}else{
			void* p;
			std::memcpy(&p, this, sizeof(void*));
			return p != nullptr;
		}
	}

public:
	[[nodiscard]] explicit(false) elem_wrapper(elem& e) noexcept
		: etry({nullptr, &e}){
	}

	[[nodiscard]] explicit(false) elem_wrapper(elem_span span) noexcept {
		if(span.empty()){
			etry = {};
		}else{
			this->span = span;
		}
	}

	template <std::convertible_to<elem_span> Rng>
	[[nodiscard]] explicit(false) elem_wrapper(Rng& span) noexcept : elem_wrapper(elem_span{span}) {
	}

	constexpr bool empty() const noexcept{
		auto rst = std::bit_cast<std::array<std::uintptr_t, 2>>(*this);
		return rst[1] == 0;
	}

	explicit constexpr operator bool() const noexcept{
		return !empty();
	}

	template <std::invocable<elem&> Fn>
	constexpr void for_each(Fn&& fn) const noexcept(std::is_nothrow_invocable_v<Fn>){
		if(is_span_()){
			for (auto* elem : span){
				assert(elem != nullptr);
				std::invoke(std::forward<Fn>(fn), *elem);
			}
		}else if(etry.e){
			std::invoke(std::forward<Fn>(fn), *etry.e);
		}
	}
};

export
using element_collect_buffer = gch::small_vector<elem_wrapper, 2, mr::unvs_allocator<elem_wrapper>>;

namespace scene_submodule{
struct input;
struct scene_input_dispatcher;
}

export struct elem : tooltip::spawner_general<elem>{
	friend elem_ptr;
	friend elem_ref_access;
	friend scene;
	friend scene_base;
	friend tooltip::tooltip_manager;
	friend overlay_manager;
	friend scene_submodule::input_state;
	friend scene_submodule::scene_input_dispatcher;

private:
	void(*deleter_)(elem*) noexcept = nullptr;
	scene* scene_{};
	elem* parent_{};

	using lifecycle_ref_word = std::uint32_t;
	static constexpr lifecycle_ref_word lifecycle_destroying_bit_ =
		lifecycle_ref_word{1u} << (std::numeric_limits<lifecycle_ref_word>::digits - 1u);
	static constexpr lifecycle_ref_word lifecycle_live_bit_ = lifecycle_destroying_bit_ >> 1u;
	static constexpr lifecycle_ref_word lifecycle_ref_mask_ = lifecycle_live_bit_ - 1u;
	static_assert((lifecycle_destroying_bit_ & lifecycle_live_bit_) == 0u);
	static_assert((lifecycle_destroying_bit_ & lifecycle_ref_mask_) == 0u);
	static_assert((lifecycle_live_bit_ & lifecycle_ref_mask_) == 0u);

	std::atomic<lifecycle_ref_word> lifecycle_ref_{lifecycle_live_bit_};
	bool scene_refs_attached_{true};
	std::stop_source lifetime_stop_source_{};

	clamped_fsize size_{};
	std::optional<math::vec2> preferred_size_{};
	math::vec2 relative_pos_{};
	math::vec2 absolute_pos_{};

	bool is_at_display_stage_{false};
	elem_tree_channel element_channel_{};

private:
	style::target_known_node_ptr<elem> style_{};
public:

	[[nodiscard]] style::target_known_node_ptr<elem> get_elem_default_style() const;
private:

	border_t border_{};
	border_t style_border_cache_{};

	mpsc_action_queue<elem> actions{};

public:
	sound::asset_group_handle sound_group{};
	bool scene_audio_auto_proxy{true};

private:
	[[nodiscard]] audio::audio_asset_handle get_audio_asset_(sound::play_event event) const noexcept{
		if(!sound_group){
			return {};
		}
		return sound_group->get(event);
	}

	[[nodiscard]] bool play_audio_detached(sound::play_event event, audio::play_settings settings = {}) const{
		auto asset = get_audio_asset_(event);
		if(!asset){
			return false;
		}
		const auto resource = asset->load_now();
		if(!resource){
			return false;
		}
		return get_scene().resources().audio_channel().play_detached(resource, std::move(settings));
	}

	[[nodiscard]] audio::playback_control_handle play_audio_controlled(
		sound::play_event event,
		audio::play_settings settings = {},
		audio::playback_control_options options = {}) const{
		auto asset = get_audio_asset_(event);
		if(!asset){
			return {};
		}
		const auto resource = asset->load_now();
		if(!resource){
			return {};
		}
		return get_scene().resources().audio_channel().play_controlled(resource, std::move(settings), options);
	}

	void play_audio_from_scene_proxy(sound::play_event event) const{
		if(scene_audio_auto_proxy){
			get_scene().request_audio_from_scene_proxy(*this, event);
		}
	}

public:
	/**
	 * @brief The maximum extent this element may freely occupy during layout-driven resize.
	 *
	 * A finite axis is the parent-granted allowance: resizing within that value should
	 * not require the parent to acquire a larger size on that axis. An infinite/pending
	 * axis means the parent allows this element to determine that axis from its content
	 * and propagate the resulting size requirement upward.
	 *
	 * This is layout context, not the element's current extent.
	 */
	layout::optional_mastering_extent restriction_extent{layout::pending_size, layout::pending_size};

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

public:
	layout_state layout_state{};
	interactivity_flag interactivity{};


private:
	// Local opacity state. Final subtree opacity is composed at draw time via draw_call_param.
	float propagate_opacity_{1.f};
	float inherent_opacity_{1.f};
	altitude_t layer_altitude_{};

public:
	unsigned _debug_identity{};

	virtual ~elem(){
		if(scene_refs_attached_){
			detach_from_scene();
		}
		mark_destroying_no_external_refs_();
		scene_->decr_ref_count_();
	}

	[[nodiscard]] elem(scene& scene, elem* parent) noexcept;

	elem(const elem& other) = delete;
	elem(elem&& other) noexcept = delete;
	elem& operator=(const elem& other) = delete;
	elem& operator=(elem&& other) noexcept = delete;

public:
	virtual void set_default_appearance();

#pragma region Action
private:
	void push_to_action_queue();
public:

	template <typename E, typename Fn>
		requires (std::invocable<Fn&&, E&> || std::invocable<Fn&&>)
	void post_task(this E& e, Fn&& fn);

	void set_audio_group(sound::asset_group_handle group) noexcept;

	void set_audio_group_assume_synced(sound::asset_group_handle group) noexcept;

	void set_default_audio_group_assume_synced();

	void clear_audio_group() noexcept{
		assert(is_on_scene_thread(get_scene()));
		sound_group = {};
	}

	[[nodiscard]] const sound::asset_group_handle& audio_group() const noexcept{
		return sound_group;
	}

	void set_scene_audio_proxy_enabled(const bool enabled) noexcept{
		scene_audio_auto_proxy = enabled;
	}

	[[nodiscard]] bool scene_audio_proxy_enabled() const noexcept{
		return scene_audio_auto_proxy;
	}

	template <typename E, typename Fn, typename ...Args>
		requires (std::derived_from<std::remove_const_t<E>, elem> && std::invocable<Fn&&, E&, Args&&...>)
	/**
	 * @brief Run a mutation on the owning scene thread.
	 *
	 * If the caller is already on the scene thread, `fn(e, args...)` runs
	 * immediately. Otherwise it is posted to the element's task queue and later
	 * consumed by the scene. Arguments are moved into the posted task.
	 */
	void sync_run(this E& e, Fn&& fn, Args&&... args){
		if(is_on_scene_thread(static_cast<const elem&>(e).get_scene())){
			std::invoke(fn, e, std::forward<Args>(args)...);
		}else{
			if constexpr (sizeof...(Args) > 0){
				e.post_task([f = std::forward<Fn>(fn), ...a = std::forward<Args>(args)](E& v) mutable{
					std::invoke(std::move(f), v, std::move(a)...);
				});
			} else{
				e.post_task(std::forward<Fn>(fn));
			}
		}
	}

	template <std::derived_from<action::action<elem>> ActionType, typename... Args>
		requires (std::constructible_from<ActionType, mr::heap_allocator<>, Args&&...>)
	ActionType& push_action(Args&&... args){
		auto& rst = actions.push_action<ActionType>(get_scene().get_heap_allocator(), std::forward<Args>(args)...);
		push_to_action_queue();
		return rst;
	}

	/**
	 *
	 * @return check if there are any action in queue, the consuming one is excluded
	 */
	bool has_pending_action() const noexcept{
		return !actions.empty();
	}

	/**
	 *
	 * @return check if the element is consuming action, must called on main ui thread!
	 */
	bool has_consuming_action() const noexcept{
		assert(is_on_scene_thread(get_scene()));
		return actions.is_consuming();
	}

#pragma endregion

#pragma region Tooltip

public:
	[[nodiscard]] tooltip::align_config tooltip_get_align_config() const override;

	void create_tooltip(bool fade_in = true, bool below_scene = false);

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

	void drop_tooltip() const;
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

	virtual void on_display_state_changed(bool is_shown, bool is_scene_notified){
		if(util::try_modify(is_at_display_stage_, is_shown) && !is_scene_notified){
			get_scene().notify_display_state_changed(get_channel());
			is_scene_notified = true;
		}

		for (auto child : exposed_children()){
			child->on_display_state_changed(is_shown, is_scene_notified);
		}
	}

	virtual bool decide_is_children_displayable_on_add(elem& elem){
		//TODO spec for menu/overflow seq/collapser or such container that hide children initially
		return is_at_display_stage();
	}

	[[nodiscard]] bool is_at_display_stage() const noexcept{
		return is_at_display_stage_;
	}

	virtual void on_inbound_changed(bool is_inbounded, bool changed){
		cursor_states_.inbound = is_inbounded;
	}

	/**
	 * @brief Called when this element gains or loses cursor focus.
	 */
	virtual void on_focus_changed(bool is_focused){
		cursor_states_.focused = is_focused;
		if(!is_focused && !is_focus_extended_by_mouse())cursor_states_.pressed = false;
	}

	virtual void on_pointer_button(events::event_context& ctx, const events::pointer_button_event& event);
	virtual void on_pointer_drag(events::event_context& ctx, const events::pointer_drag_event& event);
	virtual void on_pointer_move(events::event_context& ctx, const events::pointer_move_event& event);
	virtual void on_wheel(events::event_context& ctx, const events::wheel_event& event);
	virtual void on_key(events::event_context& ctx, const events::key_event& event);
	virtual void on_text(events::event_context& ctx, const events::text_event& event);
	virtual void on_ime(events::event_context& ctx, const events::ime_event& event);

	/**
	 * @brief Handle cursor movement after hit testing and focus updates.
	 */
	virtual void on_cursor_moved(const events::pointer_move_event& event){
		cursor_states_.time_stagnate = 0;
		if(tooltip_create_config.use_stagnate_time && !event.local_delta().equals({})){
			cursor_states_.time_tooltip = 0.;
			on_tooltip_drop();
		}
	}

	/**
	 * @brief Handle the scene ESC chain.
	 *
	 * Scene-level ESC handling first gives tooltips and overlays a chance to
	 * close, then calls this through the focused element's parent chain.
	 */
	virtual events::dispatch_result on_esc(){
		return events::dispatch_result::unhandled;
	}

#pragma endregion

#pragma region Draw
public:

	const style::target_known_node_ptr<elem>& get_style() const noexcept{
		return style_;
	}

	void set_style(style::target_known_node_ptr<elem>&& style);

	void set_style_assume_synced(style::target_known_node_ptr<elem>&& style);

	void set_style_assume_synced(style::family_variant v);

	void set_style_assume_synced() noexcept;

	void set_style(style::family_variant v);

	void set_style() noexcept;

protected:
	template <typename S, std::invocable<const S&, draw_recorder&> Fn>
	void record_drawer_draw_context(this const S& self, draw_recorder& call_stack_builder, Fn&& fn){
		call_stack_builder.push_call_enter(self, [](const S& s, const draw_call_param& p) static -> draw_call_param {
			const rect bound = s.bound_abs();
				return {
					.current_subject = p.draw_bound.overlap_exclusive(bound) ? &s : nullptr,
					.draw_bound = bound,
					.opacity_scl = p.opacity_scl * s.get_local_draw_opacity()
				};
			});

		std::invoke(std::forward<Fn>(fn), self, call_stack_builder);

		call_stack_builder.push_call_leave();
	}

	template <typename S, std::invocable<const S&, draw_recorder&> Fn>
	void record_content_drawer_draw_context(this const S& self, draw_recorder& call_stack_builder, Fn&& fn){
		call_stack_builder.push_call_enter(self, [](const S& s, const draw_call_param& p) static -> draw_call_param {
			const rect bound = s.content_bound_abs();
				return {
					.current_subject = p.draw_bound.overlap_exclusive(bound) ? &s : nullptr,
					.draw_bound = bound,
					.opacity_scl = p.opacity_scl * s.get_local_draw_opacity()
				};
			});

		std::invoke(std::forward<Fn>(fn), self, call_stack_builder);

		call_stack_builder.push_call_leave();
	}

public:
	virtual void record_draw_layer(draw_recorder& call_stack_builder) const{
		if(style_){
			record_drawer_draw_context(call_stack_builder, [](const elem& e, draw_recorder& s){
				style::draw_record(e.style_, s);
			});
		}
	}

protected:
	virtual void on_opacity_changed(float previous){
	}

	FORCE_INLINE void draw_style_impl(math::frect region, fx::layer_param param, float inheritedOpacityScl) const{
		style::draw_direct(style_, style::typed_draw_param<elem>{
			.param = draw_call_param{
				.current_subject = this,
				.draw_bound = region,
				.opacity_scl = inheritedOpacityScl * get_local_draw_opacity()
			},
			.immut_args = {
				.layer = param
			}
		});
	}

public:
	FORCE_INLINE void draw_style(math::frect region, fx::layer_param param, float inheritedOpacityScl = 1.f) const{
		draw_style_impl(region, param, inheritedOpacityScl);
	}

	FORCE_INLINE void draw_style(fx::layer_param param, float inheritedOpacityScl = 1.f) const{
		draw_style_impl(bound_abs(), param, inheritedOpacityScl);
	}

#pragma endregion

#pragma region Behavior
public:

	virtual bool update(float delta_in_ticks);

	bool update_action(float delta_in_ticks){
		return actions.update_action(delta_in_ticks, *this);
	}

public:

	void detach_from_scene() noexcept;
	void detach_from_scene_recursively() noexcept{
		for_each_collected_children([](elem& e){
			e.detach_from_scene_recursively();
		});
		detach_from_scene();
	}

	void clear_scene_references() noexcept{
		detach_from_scene();
	}

	void clear_scene_references_recursively() noexcept{
		detach_from_scene_recursively();
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
	FORCE_INLINE inline std::optional<math::vec2> pre_acquire_size_no_border_clip(const layout::optional_mastering_extent extent){
		return pre_acquire_size_impl(extent).transform([&, this](const math::vec2 v){
			return size_.clamp(v + border_extent()).min(extent.potential_extent());
		});
	}

	FORCE_INLINE inline std::optional<math::vec2> pre_acquire_size(const layout::optional_mastering_extent extent){
		return pre_acquire_size_impl(clip_border_from(extent, border_extent())).transform([&, this](const math::vec2 v){
			assert(!v.is_NaN());
			return size_.clamp(v + border_extent()).min(extent.potential_extent());
		});
	}

	[[nodiscard]] virtual layout::layout_policy get_layout_policy() const noexcept{
		return layout::layout_policy::none;
	}

	bool propagate_layout_policy(const layout::layout_policy layout_policy_of_parent){
		if(!set_layout_policy_impl(layout::layout_policy_setting{layout_policy_of_parent})){
			return false;
		}

		auto policy_to_children = get_layout_policy();
		if(policy_to_children == layout::layout_policy::none){
			policy_to_children = layout_policy_of_parent;
		}

		for(auto&& collect_child : collect_children()){
			collect_child.for_each([policy_to_children](elem& e){
				e.propagate_layout_policy(policy_to_children);
			});
		}
		return true;
	}

	bool set_layout_spec(const layout::layout_specifier specifier){
		return set_layout_policy_impl(layout::layout_policy_setting{specifier});
	}

	bool set_layout_spec(const layout::directional_layout_specifier specifier){
		return set_layout_policy_impl(layout::layout_policy_setting{specifier});
	}

	bool set_layout_spec(const layout::layout_policy specifier){
		return set_layout_policy_impl(layout::layout_policy_setting{layout::layout_specifier::fixed(specifier)});
	}

	//TODO responsibility chain to notify one?

	/**
	 * @brief Mark this element, ancestors, and/or children dirty for layout.
	 *
	 * `propagation` selects local, upper, lower, or forced upper propagation. A
	 * parent with `layout_state.intercept_lower_to_isolated` redirects child
	 * changes into the scene's isolated layout queue instead of propagating
	 * farther upward.
	 */
	void notify_layout_changed(propagate_mask propagation);

	/**
	 * @brief Mark this element for scene-level isolated layout update.
	 */
	void notify_isolated_layout_changed();

protected:
	/**
	 * @brief Accept or reject a layout policy update.
	 *
	 * Containers override this to store fixed or mapped policies. The default
	 * implementation accepts raw policies and ignores specifier mappings.
	 */
	virtual bool set_layout_policy_impl(const layout::layout_policy_setting setting){
		return setting.is_policy();
	}

	void propagate_layout_policy_to_children() const{
		auto policy_to_children = get_layout_policy();
		if(policy_to_children == layout::layout_policy::none){
			return;
		}
		for(auto&& collect_child : collect_children()){
			collect_child.for_each([policy_to_children](elem& e){
				e.propagate_layout_policy(policy_to_children);
			});
		}
	}

	virtual std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept{
		return get_layout_policy();
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
	template <std::invocable<elem&> Fn>
	void for_each_collected_children(Fn&& fn){
		for (auto&& elem_wrapper : collect_children()){
			elem_wrapper.for_each([&](elem& e){
				e.for_each_collected_children(fn);
			});
		}
	}

	virtual element_collect_buffer collect_children() const{
		element_collect_buffer buf;
		auto c = exposed_children();
		if(!c.empty())buf.push_back(c);
		return buf;
	}

	[[nodiscard]] virtual elem_span exposed_children() const noexcept{
		return {};
	}

	[[nodiscard]] bool has_exposed_children() const noexcept{
		return !exposed_children().empty();
	}

	virtual bool set_scaling(math::vec2 scl) noexcept {
		assert(!scl.is_NaN());
		if(!util::try_modify(context_scaling_, scl))return false;
		notify_isolated_layout_changed();

		if(propagate_scaling_){
			for(auto&& collect_child : collect_children()){
				collect_child.for_each([](elem& e){
					e.set_scaling(e.is_root_element() ? vec2{1, 1} : e.parent()->get_scaling());
				});
			}

			layout_state.notify_children_changed();
		}
		return true;
	}


	virtual bool update_abs_src(math::vec2 parent_content_src) noexcept;
#pragma endregion

#pragma region Transform
protected:
	virtual void transform_to_local_space_impl(std::span<math::vec2> where_relative_in_parent) const noexcept{
		for(auto& pos : where_relative_in_parent){
			pos -= relative_pos_;
		}
	}

	virtual void transform_from_local_space_impl(std::span<math::vec2> where_relative_in_local) const noexcept{
		for(auto& pos : where_relative_in_local){
			pos += relative_pos_;
		}
	}

public:
	void transform_to_local_space(std::span<math::vec2> where_relative_in_parent) const noexcept{
		transform_to_local_space_impl(where_relative_in_parent);
	}

	void transform_from_local_space(std::span<math::vec2> where_relative_in_local) const noexcept{
		transform_from_local_space_impl(where_relative_in_local);
	}

	void transform_to_local_space(math::vec2& where_relative_in_parent) const noexcept{
		transform_to_local_space(std::span{&where_relative_in_parent, 1});
	}

	void transform_from_local_space(math::vec2& where_relative_in_local) const noexcept{
		transform_from_local_space(std::span{&where_relative_in_local, 1});
	}

	virtual void transform_to_content_space(std::span<math::vec2> where_relative_in_parent) const noexcept{
		transform_to_local_space(where_relative_in_parent);
		const auto offset = content_src_offset();
		for(auto& pos : where_relative_in_parent){
			pos -= offset;
		}
	}

	virtual void transform_from_content_space(std::span<math::vec2> where_relative_in_child) const noexcept{
		const auto offset = content_src_offset();
		for(auto& pos : where_relative_in_child){
			pos += offset;
		}
		transform_from_local_space(where_relative_in_child);
	}

	[[nodiscard]] virtual math::vec2 transform_to_content_space(math::vec2 where_relative_in_parent) const noexcept{
		transform_to_content_space(std::span{&where_relative_in_parent, 1});
		return where_relative_in_parent;
	}

	[[nodiscard]] virtual math::vec2 transform_from_content_space(math::vec2 where_relative_in_child) const noexcept{
		transform_from_content_space(std::span{&where_relative_in_child, 1});
		return where_relative_in_child;
	}

	[[nodiscard]] bool contains(math::vec2 pos_relative) const noexcept;

	[[nodiscard]] bool contains_self(math::vec2 pos_relative, float margin) const noexcept;

protected:
	[[nodiscard]] virtual bool parent_contain_constrain(math::vec2 pos_relative) const noexcept;
public:

#pragma endregion

#pragma region State
	elem_tree_channel get_channel() const noexcept{
		auto p = parent();
		auto channel = element_channel_;
		while(channel == elem_tree_channel::deduced && p){
			channel = p->get_channel();
			p = p->parent();
		}
		return channel;
	}

	[[nodiscard]] bool is_focused_scroll() const noexcept;
	[[nodiscard]] bool is_focused_key() const noexcept;

	[[nodiscard]] bool is_focused() const noexcept;
	[[nodiscard]] bool is_inbounded() const noexcept;

	void set_focused_scroll(bool focus) noexcept;
	void set_focused_key(bool focus) noexcept;

	virtual void on_focus_key_changed(bool isFocused){

	}

	virtual void on_last_clicked_changed(bool isFocused){

	}

	virtual bool set_toggled(bool isToggled){
		const bool was_toggled = toggled;
		if(util::try_modify(toggled, isToggled)){
			get_scene().record_state_audio_delta(
				*this,
				sound::state_family::toggle,
				was_toggled,
				isToggled);
			return true;
		}
		return false;
	}

	virtual bool set_disabled(bool isDisabled){
		const bool was_disabled = disabled;
		if(util::try_modify(disabled, isDisabled)){
			get_scene().record_state_audio_delta(
				*this,
				sound::state_family::disabled,
				was_disabled,
				isDisabled);
			return true;
		}
		return false;
	}

	virtual style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept{
		return {style::cursor_type::regular, style::cursor_decoration_type::none};
	}

#pragma endregion

#pragma region Trivial_Getter_Setters
public:
	style::style_tree_manager& get_style_tree_manager() const noexcept{
		return get_scene().resources().style_tree_manager;
	}

	sound::manager& get_sound_manager() const noexcept{
		return get_scene().resources().sound_manager;
	}

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

	[[nodiscard]] bool is_live() const noexcept{
		return elem::is_lifecycle_live_(lifecycle_ref_.load(std::memory_order_acquire));
	}

	[[nodiscard]] bool is_detached() const noexcept{
		return elem::is_lifecycle_detached_(lifecycle_ref_.load(std::memory_order_acquire));
	}

	[[nodiscard]] bool is_destroying() const noexcept{
		return elem::is_lifecycle_destroying_(lifecycle_ref_.load(std::memory_order_acquire));
	}

	template <std::derived_from<elem> T = elem>
	[[nodiscard]] elem_ref<T> ref(){
		if constexpr (std::same_as<T, elem>){
			return elem_ref<T>{*this};
		}else{
			return elem_ref<T>{dynamic_cast<T&>(*this)};
		}
	}

	[[nodiscard]] std::stop_token lifetime_stop_token() const noexcept{
		return lifetime_stop_source_.get_token();
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

	template <std::derived_from<elem> T, bool unchecked = false>
	[[nodiscard]] FORCE_INLINE T& parent_ref() const noexcept{
		assert(parent_ != nullptr);
		if constexpr (!unchecked && !std::same_as<T, elem>){
			return dynamic_cast<T&>(*parent_);
		}else{
#ifndef NDEBUG
			return dynamic_cast<T&>(*parent_);
#else
			return static_cast<T&>(*parent_);
#endif

		}
	}

	[[nodiscard]] FORCE_INLINE inline vec2 content_extent() const noexcept{
		const auto [w, h] = size_.get_size();
		const auto [bw, bh] = border_extent();
		return {std::fdim(w, bw), std::fdim(h, bh)};
	}

	[[nodiscard]] FORCE_INLINE inline float content_width() const noexcept{
		const auto w = size_.get_width();
		const auto bw = border_.width() + style_border_cache_.width();
		return std::fdim(w, bw);
	}

	[[nodiscard]] FORCE_INLINE inline float content_height() const noexcept{
		const auto v = size_.get_height();
		const auto bv = border_.height() + style_border_cache_.height();
		return std::fdim(v, bv);
	}

	[[nodiscard]] FORCE_INLINE inline vec2 extent() const noexcept{
		return size_.get_size();
	}

	[[nodiscard]] FORCE_INLINE inline const clamped_fsize& extent_raw() const noexcept{
		return size_;
	}

	FORCE_INLINE inline bool set_max_extent(math::vec2 ext) noexcept{
		if(size_.set_maximum_size(ext)){
			this->resize(extent());
			return true;
		}
		return false;
	}

	FORCE_INLINE inline bool set_min_extent(math::vec2 ext) noexcept{
		if(size_.set_minimum_size(ext)){
			this->resize(extent());
			return true;
		}
		return false;
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

	[[nodiscard]] FORCE_INLINE inline align::spacing border() const noexcept{
		return border_ + style_border_cache_;
	}

	[[nodiscard]] FORCE_INLINE inline vec2 border_extent() const noexcept{
		return border_.extent() + style_border_cache_.extent();
	}

	void restrict_child(elem& child) const{
		child.restriction_extent = clip_border_from(restriction_extent, border_extent());
		child.resize(content_extent());
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 content_src_offset() const noexcept{
		return border_.top_lft() + style_border_cache_.top_lft();
	}

	[[nodiscard]] FORCE_INLINE inline rect clip_to_content_bound(rect region) const noexcept{
		return rect{tags::unchecked, tags::from_extent, region.src + content_src_offset(), region.extent().fdim(border_extent())};
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

	void set_self_border(align::spacing border) {
		if(util::try_modify(border_, border)){
			notify_layout_changed(propagate_mask::lower);
		}
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
		if((interactivity & interactivity_flag::self_interactable) == interactivity_flag{})return false;
		return true;
	}

	[[nodiscard]] FORCE_INLINE inline bool touch_propagate_blocked() const noexcept{
		return (interactivity & interactivity_flag::ppgt_interactable) == interactivity_flag{};
	}

	[[nodiscard]] FORCE_INLINE inline float get_local_draw_opacity() const noexcept{
		return propagate_opacity_ * inherent_opacity_;
	}

	[[nodiscard]] float get_propagate_opacity() const noexcept{
		return propagate_opacity_;
	}

	FORCE_INLINE inline void set_propagate_opacity(const float val) noexcept{
		const auto prev = get_local_draw_opacity();
		if(util::try_modify(propagate_opacity_, val)){
			on_opacity_changed(prev);
		}
	}

	FORCE_INLINE inline void set_opacity(const float val) noexcept{
		const auto prev = get_local_draw_opacity();
		if(util::try_modify(inherent_opacity_, val)){
			on_opacity_changed(prev);
		}
	}

	FORCE_INLINE inline void set_children_opacity_with_scl(const float scl) noexcept{
		for(const auto& element : collect_children()){
			element.for_each(std::bind_back(&elem::set_propagate_opacity, scl));
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
			return extent.fdim(border_extent());
		});
	}

	[[nodiscard]] FORCE_INLINE inline altitude_t get_altitude() const noexcept{
		return layer_altitude_;
	}

#pragma endregion

protected:

	[[nodiscard]] auto* get_memory_resource() const noexcept{
		return scene_->get_memory_resource();
	}

public:

	template <typename T = std::byte>
	auto get_heap_allocator() const noexcept{
		return scene_->get_heap_allocator<T>();
	}

	void relocate_scene(scene& target_scene) noexcept;

protected:
	void relocate_self_scene(scene& target_scene) noexcept;

private:
	[[nodiscard]] static constexpr lifecycle_ref_word lifecycle_ref_count_(const lifecycle_ref_word state) noexcept{
		return state & lifecycle_ref_mask_;
	}

	[[nodiscard]] static constexpr bool is_lifecycle_live_(const lifecycle_ref_word state) noexcept{
		return (state & (lifecycle_destroying_bit_ | lifecycle_live_bit_)) == lifecycle_live_bit_;
	}

	[[nodiscard]] static constexpr bool is_lifecycle_detached_(const lifecycle_ref_word state) noexcept{
		return (state & (lifecycle_destroying_bit_ | lifecycle_live_bit_)) == 0u;
	}

	[[nodiscard]] static constexpr bool is_lifecycle_destroying_(const lifecycle_ref_word state) noexcept{
		return (state & lifecycle_destroying_bit_) != 0u;
	}

	void mark_destroying_no_external_refs_() noexcept;
	[[nodiscard]] bool try_retain_external_ref_live_() noexcept;
	void retain_external_ref_existing_() noexcept;
	void release_external_ref_() noexcept;
	[[nodiscard]] bool has_external_refs_() const noexcept{
		return elem::lifecycle_ref_count_(lifecycle_ref_.load(std::memory_order_acquire)) != 0u;
	}

private:
	void update_altitude_(altitude_t height);

	void init_altitude_(altitude_t height);

};


namespace util{
export
FORCE_INLINE constexpr bool is_draw_param_valid(const elem& s, const draw_call_param& p) noexcept {
	if(s.invisible) return false;
	if(!p.draw_bound.overlap_inclusive(s.bound_abs())) return false;
	return true;
}

export
FORCE_INLINE constexpr float get_final_draw_opacity(const elem& s, const draw_call_param& p) noexcept{
	return p.opacity_scl * s.get_local_draw_opacity();
}

export
[[nodiscard]] math::vec2 select_prefer_extent(bool is_prefer, math::vec2 current, std::optional<math::vec2> preferred){
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
	if(current->interactivity == interactivity_flag::disabled)return;

	if(!current->contains_self(cursorPos, 0)){
		return;
	}

	selected.push_back(current);

	if(current->touch_propagate_blocked() || !current->has_exposed_children()) return;

	auto transformed = current->transform_to_content_space(cursorPos);

	for(const auto& child : current->exposed_children()/* | std::views::reverse*/){
		if(!child->is_visible())continue;
		util::dfs_record_inbound_element<Container>(transformed, selected, child);

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
layout::optional_mastering_extent get_fill_parent_restriction(
	const math::vec2 boundSize,
	const math::bool2 fillparent = {true, true},
	const math::bool2 expansion_mask = {false, false}){
	// For filled axes, the child is bounded by the parent content size. For non-filled
	// axes, expansion_mask selects whether the child may resolve that axis freely and
	// propagate the requirement upward (pending) or remains bounded by boundSize.
	const auto [fx, fy] = fillparent;
	layout::optional_mastering_extent restriction_extent;


	if(fx) restriction_extent.set_width(boundSize.x);
	else{
		if(expansion_mask.x){
			restriction_extent.set_width_pending();
		} else{
			restriction_extent.set_width(boundSize.x);
		}
	}

	if(fy) restriction_extent.set_height(boundSize.y);
	else{
		if(expansion_mask.y){
			restriction_extent.set_height_pending();
		} else{
			restriction_extent.set_height(boundSize.y);
		}
	}

	return restriction_extent;
}

export
bool set_fill_parent(
	elem& item,
	const math::vec2 boundSize,
	const math::bool2 mask = {true, true},
	const math::bool2 expansion_mask = {false, false},
	const propagate_mask direction_mask = propagate_mask::lower){
	const auto [fx, fy] = item.get_fill_parent() && mask;

	item.restriction_extent = get_fill_parent_restriction(boundSize, {fx, fy}, expansion_mask);

	const auto [ox, oy] = item.extent();
	return item.resize({
		                   fx ? boundSize.x : ox,
		                   fy ? boundSize.y : oy
	                   }, direction_mask);
}

export
unsigned inline get_nest_depth(const elem* where) noexcept{
	return where ? where->get_altitude() : 0;
}

void inline helper_transform_scene2content(const elem* where, std::span<math::vec2> inPos) noexcept{
	std::array<const elem*, 16> elem_buffer;
	std::size_t elem_count = 0;

	auto current = where;
	for(; current && elem_count < elem_buffer.size(); current = current->parent()){
		elem_buffer[elem_count++] = current;
	}

	if(current){
		helper_transform_scene2content(current, inPos);
	}

	while(elem_count != 0){
		elem_buffer[--elem_count]->transform_to_content_space(inPos);
	}
}

export
[[nodiscard]] math::vec2 inline helper_transform_scene2content(const elem* where, math::vec2 inPos) noexcept{
	helper_transform_scene2content(where, std::span{&inPos, 1});
	return inPos;
}

export
void inline transform_scene2local(const elem& where, std::span<math::vec2> inPos) noexcept{
	helper_transform_scene2content(where.parent(), inPos);
	where.transform_to_local_space(inPos);
}

export
[[nodiscard]] math::vec2 inline transform_scene2local(const elem& where, math::vec2 inPos) noexcept{
	transform_scene2local(where, std::span{&inPos, 1});
	return inPos;
}

export
void inline transform_scene2local(std::span<const elem* const> elems, std::span<math::vec2> inPos) noexcept{
	auto cur = elems.begin();
	const auto end = elems.end();
	if(cur == end) return;
	const auto prev = std::prev(end);

	for(; cur != prev; ++cur){
		(*cur)->transform_to_content_space(inPos);
	}

	(*cur)->transform_to_local_space(inPos);
}

export
[[nodiscard]] math::vec2 inline transform_scene2local(std::span<const elem* const> elems, math::vec2 inPos) noexcept{
	transform_scene2local(elems, std::span{&inPos, 1});
	return inPos;
}
export
void inline transform_local2scene(const elem& where, std::span<math::vec2> inPos) noexcept{
	where.transform_from_local_space(inPos);

	auto parent = where.parent();
	while(parent){
		parent->transform_from_content_space(inPos);
		parent = parent->parent();
	}
}

export
[[nodiscard]] math::vec2 inline transform_local2scene(const elem& where, math::vec2 inPos) noexcept{
	transform_local2scene(where, std::span{&inPos, 1});
	return inPos;
}

export
void inline transform_current2parent(const elem& where, std::span<math::vec2> pos_in_local_space) noexcept{
	where.transform_from_local_space(pos_in_local_space);
	if(auto p = where.parent()){
		p->transform_from_content_space(pos_in_local_space);
		p->transform_to_local_space(pos_in_local_space);
	}
}

export
[[nodiscard]] math::vec2 inline transform_current2parent(const elem& where, math::vec2 pos_in_local_space) noexcept{
	transform_current2parent(where, std::span{&pos_in_local_space, 1});
	return pos_in_local_space;
}

events::dispatch_result thoroughly_esc(elem* where) noexcept;

FORCE_INLINE inline events::dispatch_result thoroughly_esc(elem& where) noexcept{
	return thoroughly_esc(std::addressof(where));
}

export
template <typename T>
FORCE_INLINE constexpr bool contains(math::vector2<T> pos, math::vector2<T> extent) noexcept{
	return pos.x >= 0 && pos.x < extent.x && pos.y >= 0 && pos.y < extent.y;
}

export
template <typename T>
FORCE_INLINE constexpr bool contains(math::vector2<T> pos, math::vector2<T> extent, math::vector2<T> margin) noexcept{
	return pos.x >= -margin.x && pos.x < extent.x + margin.x && pos.y >= -margin.y && pos.y < extent.y + margin.y;
}

export
template <std::unsigned_integral T>
FORCE_INLINE constexpr bool contains(math::vector2<T> pos, math::vector2<T> extent) noexcept{
	return pos.x < extent.x && pos.y < extent.y;
}


export
void update_insert(elem& e, update_channel channel){
	e.get_scene().insert_update(e, channel);
}

export
void update_erase(const elem& e, update_channel channel){
	e.get_scene().erase_update(&e, channel);
}

export
void sync_elem_tree(elem& to_join, scene& target_scene) noexcept{
	auto& scene = to_join.get_scene();
	to_join.relocate_scene(target_scene);
	target_scene.join(std::move(scene));
	to_join.get_scene().notify_display_state_changed(to_join.get_channel());
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
}

#pragma region IMPL
namespace mo_yanxi::gui{
mr::heap_allocator<elem> elem_ptr::alloc_of(const scene& s) noexcept{
	return s.get_heap_allocator<elem>();
}

mr::heap_allocator<elem> elem_ptr::alloc_of(const elem* ptr) noexcept{
	return alloc_of(ptr->get_scene());
}

void elem_ptr::set_deleter(elem* element, void (*p)(elem*) noexcept) noexcept{
	element->deleter_ = p;
}

void elem_ptr::delete_elem(elem* ptr) noexcept{
	ptr->get_scene().retire_elem(ptr);
}


template <std::derived_from<elem> T>
void elem_ptr::dynamic_init(T& ptr){

	ptr.sync_run([](T& elem){
		elem.T::set_default_appearance();
	});
}



namespace action{
export
template <std::derived_from<elem> T, std::invocable<T&> Fn>
struct elem_runnable_action final : action<elem>{
	ADAPTED_NO_UNIQUE_ADDRESS Fn fn;

	template <typename F>
	[[nodiscard]] explicit elem_runnable_action(const mr::heap_allocator<>& allocator, F&& fn, float delay = 0)
		: action(allocator, delay), fn(std::forward<F>(fn)){
	}

protected:
	void end(elem& elem) override{
		std::invoke(fn, elem_cast<T, false>(elem));
	}
};

export
template <std::derived_from<elem> E, std::invocable<E&> Fn>
void push_runnable_action(E& e, Fn&& fn, float delay = 0){
	static_cast<elem&>(e).push_action<elem_runnable_action<E, std::decay_t<Fn>>>(std::forward<Fn>(fn), delay);
}

}


export
template <typename E, typename T, typename Fn>
	requires (
		std::derived_from<std::remove_const_t<E>, elem>
		&& std::is_invocable_r_v<T, Fn, E&>
		&& std::is_assignable_v<std::invoke_result_t<T E::*, E&>, std::invoke_result_t<Fn, E&>>)
T post_sync_assign(E& e, T E::* mptr, Fn&& fn){
	if(is_on_scene_thread(static_cast<const elem&>(e).get_scene())){
		return std::invoke_r<T>(fn, e);
	}else{
		e.post_task([mptr, f = std::forward<Fn>(fn)](E& e){
			std::invoke(mptr, e) = std::invoke_r<T>(f, e);
		});
		return T{};
	}
}


namespace events{
std::span<elem* const> event_context::descendants_to_target() const noexcept{
	const auto route_path = path();
	auto itr = std::ranges::find(route_path, current());
	// for(std::size_t i = 0; i < route_path.size(); ++i){
	// 	if(begin[i] == current()){
	// 		return {begin + i + 1, route_path.size() - i - 1};
	// 	}
	// }
	if(itr != route_path.end())[[likely]]{
		++itr;
	}
	return {itr, route_path.end()};
}

math::vec2 pointer_button_event::get_content_pos(const event_context& ctx, const elem& elem) const noexcept{
	if(ctx.current() == std::addressof(elem)){
		return local_pos - elem.content_src_offset();
	}
	return util::transform_scene2local(elem, scene_pos) - elem.content_src_offset();
}

bool pointer_button_event::within_elem(const event_context& ctx, const elem& elem, float margin) const noexcept{
	auto p = ctx.current() == std::addressof(elem)
		? local_pos
		: util::transform_scene2local(elem, scene_pos);
	p.x += margin;
	p.y += margin;
	return p.axis_greater(0, 0) && p.axis_less(elem.extent().add(margin * 2));
}
}
}
#pragma endregion
