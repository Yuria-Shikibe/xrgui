module;

#include <cassert>
#include <complex.h>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.elem.scroll_pane;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;
import mo_yanxi.math.matrix3;
import mo_yanxi.cond_exist;

import mo_yanxi.snap_shot;
import mo_yanxi.math;
import std;
import mo_yanxi.gui.util.animator;

namespace mo_yanxi::gui{
export
enum class scroll_pane_mode : std::uint8_t {
	absolute,
	proportional
};

namespace style{
export
struct scroll_pane_bar_drawer;
}

export
//TODO reduce protected fields
struct scroll_adaptor_base : elem{
public:
	scroll_pane_mode sensitivity_mode{scroll_pane_mode::absolute};
	float scroll_velocity_sensitivity{1.f};
	float scroll_scale{55.0f};

	void set_scroll_mode(scroll_pane_mode mode, float sensitivity){
		sensitivity_mode = mode;
		scroll_scale = sensitivity;
	}

	void set_scroll_mode(scroll_pane_mode mode){
		set_scroll_mode(mode, mode == scroll_pane_mode::proportional ? .1f : 55.f);
	}

protected:
	float scroll_bar_stroke_{20.0f};

	math::vec2 scroll_velocity_{};
	math::vec2 scroll_target_velocity_{};
	snap_shot<math::vec2> scroll_{};
	math::vec2 item_extent_cache_{};
	math::vec2 saved_scroll_ratio_{};

	layout::layout_specifier layout_policy_{layout::layout_specifier::fixed(layout::layout_policy::hori_major)};
	bool bar_caps_size{true};
	bool force_hori_scroll_enabled_{false};
	bool force_vert_scroll_enabled_{false};

	math::vec2 last_local_cursor_pos_{};
	bool overlay_scroll_bars_{false};
	bool scroll_changed_in_update_{false};

	util::animator<float, util::anime_dynamic_spec_v<float>, 0.f, util::anime_dynamic_spec_v<float>> bar_animator_{};


public:
	bool draw_track_if_locked{true};

private:
	enum class overlay_bar_state : std::uint8_t {
		hidden,
		fading_in,
		active,
		cooling_down,
		fading_out
	};
	overlay_bar_state overlay_state_{overlay_bar_state::hidden};

	referenced_ptr<const style::scroll_pane_bar_drawer> init_drawer_();

	referenced_ptr<const style::scroll_pane_bar_drawer> drawer{init_drawer_()};

public:
	float fade_delay_ticks{60.0f * 1.5f};
	float fade_duration_ticks{60.0f * 0.5f};


	[[nodiscard]] scroll_adaptor_base(scene& scene, elem* parent, const layout::layout_specifier policy)
		: elem(scene, parent), layout_policy_{policy}{
		interactivity = interactivity_flag::enabled;

		extend_focus_until_mouse_drop = true;
		layout_state.intercept_lower_to_isolated = true;
	}

	void set_overlay_bar(bool enable){
		if(util::try_modify(overlay_scroll_bars_, enable)){
			notify_isolated_layout_changed();
		}
	}

	// 新增获取当前进度（透明度）的快捷函数
	[[nodiscard]] float get_bar_opacity() const noexcept {
		return overlay_scroll_bars_ ? bar_animator_.get_progress() : 1.0f;
	}

	[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept override{
		return layout_policy_.self();
	}

	[[nodiscard]] layout::layout_specifier get_layout_specifier() const noexcept{
		return layout_policy_;
	}

	[[nodiscard]] float get_scroll_bar_stroke() const noexcept{
		return scroll_bar_stroke_;
	}

	void set_scroll_bar_stroke(const float scroll_bar_stroke){
		if(util::try_modify(scroll_bar_stroke_, scroll_bar_stroke)){
			notify_isolated_layout_changed();
		}
	}

#pragma region Event
	events::op_afterwards on_cursor_moved(const events::cursor_move event) override{
		last_local_cursor_pos_ = event.dst - content_src_offset();
		if(overlay_scroll_bars_
			&& (is_hori_scroll_enabled() || is_vert_scroll_enabled())
			&& is_pos_in_bar_section(last_local_cursor_pos_)){
			util::update_insert(*this, update_channel::draw);
		}

		return elem::on_cursor_moved(event);
	}

	events::op_afterwards on_scroll(const events::scroll e, std::span<elem* const> aboves) override;

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		if(event.key.action == input_handle::act::release){
			scroll_.apply();
			saved_scroll_ratio_ = scroll_progress_at(scroll_.base);
		}

		return elem::on_click(event, aboves);
	}

	events::op_afterwards on_drag(const events::drag e) override;
#pragma endregion




	[[nodiscard]] bool parent_contain_constrain(math::vec2 relative_pos) const noexcept final{
		return rect{tags::from_extent, content_src_pos_rel(), get_viewport_extent()}.contains_loose(relative_pos) &&
			elem::parent_contain_constrain(relative_pos);
	}

public:
	bool set_layout_policy_impl(const layout::layout_policy_setting setting) override{
		return util::update_layout_policy_setting(
			setting,
			layout_policy_,
			[this]{ return util::layout_policy_or_none(search_parent_layout_policy(true)); },
			[](const layout::layout_policy parent_policy, const layout::layout_specifier specifier){
				return specifier.cache_from(parent_policy);
			},
			[this](const layout::layout_policy policy){
				return layout_policy_.clear_self().cache_from(policy);
			},
			[this](const auto&){
				notify_isolated_layout_changed();
			}
		);
	}

	void on_inbound_changed(bool is_inbounded, bool changed) override{
		elem::on_inbound_changed(is_inbounded, changed);
		set_focused_scroll(is_inbounded);
		if(changed && overlay_scroll_bars_) util::update_insert(*this, update_channel::draw);
	}

	void on_focus_changed(bool is_focused) override{
		elem::on_focus_changed(is_focused);
	}

	bool update(const float delta_in_ticks) override;

	math::vec2 transform_to_content_space(math::vec2 where_relative_in_parent) const noexcept final{
		return elem::transform_to_content_space(where_relative_in_parent + scroll_.temp);
	}

	math::vec2 transform_from_content_space(math::vec2 where_relative_in_child) const noexcept final{
		return elem::transform_from_content_space(where_relative_in_child - scroll_.temp);
	}

#pragma region ScrollPaneGetters

public:
	[[nodiscard]] math::vec2 get_vel_clamp() const noexcept{
		return math::vector2{is_hori_scroll_enabled(), is_vert_scroll_enabled()}.as<float>();
	}

	[[nodiscard]] bool is_hori_scroll_enabled() const noexcept{
		return is_hori_scroll_active() || force_hori_scroll_enabled_;
	}

	[[nodiscard]] bool is_vert_scroll_enabled() const noexcept{
		return is_vert_scroll_active() || force_vert_scroll_enabled_;
	}

	[[nodiscard]] bool is_hori_scroll_active() const noexcept{
		return item_extent().x > content_width();
	}

	[[nodiscard]] bool is_vert_scroll_active() const noexcept{
		return item_extent().y > content_height();
	}

	[[nodiscard]] math::vec2 scrollable_extent() const noexcept{
		return (item_extent() - get_viewport_extent()).max({});
	}

	[[nodiscard]] math::nor_vec2 scroll_progress_at(const math::vec2 scroll_pos) const noexcept{
		const auto ext = scrollable_extent();
		return {
			ext.x > 0.01f ? (scroll_pos.x / ext.x) : 0.0f,
			ext.y > 0.01f ? (scroll_pos.y / ext.y) : 0.0f
		};
	}

	[[nodiscard]] math::vec2 item_extent() const noexcept{
		return item_extent_cache_;
	}

	[[nodiscard]] math::vec2 get_bar_extent() const noexcept{
		math::vec2 rst{};
		if(is_hori_scroll_enabled()) rst.y = scroll_bar_stroke_;
		if(is_vert_scroll_enabled()) rst.x = scroll_bar_stroke_;
		return rst;
	}

	[[nodiscard]] bool is_pos_in_bar_section(math::vec2 content_local_pos) const noexcept{
		auto max_ext = content_extent();
		auto min_ext = max_ext.copy().fdim(get_bar_extent());

		return (content_local_pos.x >= min_ext.x && content_local_pos.x <= max_ext.x) || (content_local_pos.y >= min_ext.y && content_local_pos.y <= max_ext.y);
	}

	[[nodiscard]] float bar_hori_length() const{
		const auto w = get_viewport_extent().x;
		const float theoretical_length = math::clamp_positive(math::min(w / item_extent_cache_.x, 1.0f) * w);
		const float min_length = math::min(scroll_bar_stroke_, w);
		return math::max(theoretical_length, min_length);
	}

	[[nodiscard]] float bar_vert_length() const{
		const auto h = get_viewport_extent().y;
		const float theoretical_length = math::clamp_positive(math::min(h / item_extent_cache_.y, 1.0f) * h);
		const float min_length = math::min(scroll_bar_stroke_, h);
		return math::max(theoretical_length, min_length);
	}

	[[nodiscard]] vec2 get_viewport_extent() const noexcept{
		if(overlay_scroll_bars_){
			return content_extent();
		}
		return content_extent().fdim(get_bar_extent());
	}

	[[nodiscard]] rect get_viewport() const noexcept{
		return {tags::from_extent, content_src_pos_abs(), get_viewport_extent()};
	}

	[[nodiscard]] math::vec2 get_scroll_offset() const noexcept{
		return scroll_.temp;
	}

	[[nodiscard]] rect get_hori_bar_rect() const noexcept{
		const auto [x, y] = content_src_pos_abs();
		const auto ratio = scroll_progress_at(scroll_.temp);
		const auto barSize = get_bar_extent();
		const auto width = bar_hori_length();
		return {
				x + ratio.x * (content_width() - barSize.x - width),
				y - barSize.y + content_height(),
				width,
				barSize.y
			};
	}

	[[nodiscard]] rect get_vert_bar_rect() const noexcept{
		const auto [x, y] = content_src_pos_abs();
		const auto ratio = scroll_progress_at(scroll_.temp);
		const auto barSize = get_bar_extent();
		const auto height = bar_vert_length();
		return {
				x - barSize.x + content_width(),
				y + ratio.y * (content_height() - barSize.y - height),
				barSize.x,
				height
			};
	}

	[[nodiscard]] float scroll_ratio_hori(const float pos) const{
		return math::clamp(pos / (item_extent_cache_.x - get_viewport_extent().x));
	}

	[[nodiscard]] float scroll_ratio_vert(const float pos) const{
		return math::clamp(pos / (item_extent_cache_.y - get_viewport_extent().y));
	}

	[[nodiscard]] math::vec2 scroll_ratio(const math::vec2 pos) const{
		auto [ix, iy] = item_extent_cache_;
		auto [vx, vy] = get_viewport_extent();

		return {
				is_hori_scroll_enabled() ? math::clamp(pos.x / (ix - vx)) : 0.f,
				is_vert_scroll_enabled() ? math::clamp(pos.y / (iy - vy)) : 0.f,
			};
	}
#pragma endregion

protected:
	void draw_scroll_bar(fx::layer_param_pass_t param) const;
	void record_draw_scroll_bar(draw_call_stack_recorder& call_stack_builder) const;
};

export
template <typename T>
struct scroll_adaptor_apply_interface_schema{
	using element_type = T;
	void update(element_type& element, float delta_in_tick) = delete;
	void draw_layer(const element_type& element, const scroll_adaptor_base& scroll_adaptor_base, rect clipSpace, float opacityScl, fx::layer_param_pass_t param) const = delete;
	void layout_elem(element_type& element) = delete;
	void set_prefer_extent(element_type& element, math::vec2 ext) = delete;
	void on_context_sync_bind(element_type& element) = delete;
	void update_abs_src(element_type& element, math::vec2 pos) = delete;
	std::optional<math::vec2> pre_acq_size(element_type& element, layout::optional_mastering_extent bound) = delete;
	bool resize(element_type& element, math::vec2 size, propagate_mask temp_mask) = delete;
	math::vec2 get_extent(const element_type& element) const = delete;
};

template <typename Interface>
struct interface_trait{
	using interface_type = Interface;
	using element_type = typename interface_type::element_type;

	static constexpr bool has_update = requires(interface_type& i, element_type& e, float d){
		i.update(e, d);
	};

	static constexpr bool has_draw_layer = requires(const interface_type& i, const scroll_adaptor_base& scroll_adaptor_base, const element_type& e, rect c, float o, fx::layer_param_pass_t p){
		i.draw_layer(e, scroll_adaptor_base, c, o, p);
	};

	static constexpr bool has_layout_elem = requires(interface_type& i, element_type& e){
		i.layout_elem(e);
	};

	static constexpr bool has_set_prefer_extent = requires(interface_type& i, element_type& e, math::vec2 ext){
		i.set_prefer_extent(e, ext);
	};

	static constexpr bool has_on_context_sync_bind = requires(interface_type& i, element_type& e){
		i.on_context_sync_bind(e);
	};

	static constexpr bool has_update_abs_src = requires(interface_type& i, element_type& e, math::vec2 pos){
		i.update_abs_src(e, pos);
	};

	static constexpr bool has_pre_acq_size = requires(interface_type& i, element_type& e, layout::optional_mastering_extent b){
		i.pre_acq_size(e, b);
	};

	static constexpr bool has_resize = requires(interface_type& i, element_type& e, math::vec2 s, propagate_mask m){
		i.resize(e, s, m);
	};

	static constexpr bool has_get_extent = requires(const interface_type& i, const element_type& e){
		i.get_extent(e);
	};
};

export
template <typename ElementType = elem_ptr, typename Interface = scroll_adaptor_apply_interface_schema<ElementType>>
struct scroll_adaptor : scroll_adaptor_base{
	using element_type = ElementType;
	using adaptor_interface_type = Interface;
	using adaptor_interface_trait = interface_trait<adaptor_interface_type>;

	static constexpr bool is_elem_ptr = std::is_same_v<element_type, elem_ptr>;
	static constexpr bool is_elem_value = std::derived_from<element_type, elem>;
	static constexpr bool is_elem_child = is_elem_ptr || is_elem_value;
	static_assert(is_elem_value || std::is_default_constructible_v<element_type>,
	              "if element is not a T : elem, it must be default constructible");

public:
	ADAPTED_NO_UNIQUE_ADDRESS adaptor_interface_type item_adaptor{};

private:
	element_type item_;
	cond_exist<elem*, is_elem_value> children_ptr_cache_{&item_};

public:
	[[nodiscard]] scroll_adaptor(scene& scene, elem* parent, const layout::layout_specifier policy) requires (!is_elem_value)
		: scroll_adaptor_base(scene, parent, policy), item_{}{
	}

	template <typename... Args>
		requires std::constructible_from<element_type, scene&, elem*, Args&&...>
	[[nodiscard]] scroll_adaptor(scene& scene, elem* parent, const layout::layout_specifier policy, Args&&... args) requires
		(is_elem_value)
		: scroll_adaptor_base(scene, parent, policy), item_(scene, this, std::forward<Args>(args)...){
	}

	[[nodiscard]] scroll_adaptor(scene& scene, elem* parent)
		: scroll_adaptor(scene, parent, layout::layout_specifier::fixed(layout::layout_policy::hori_major)){
	}

	bool set_layout_policy_impl(const layout::layout_policy_setting setting) override{
		const auto changed = scroll_adaptor_base::set_layout_policy_impl(setting);
		if(changed){
			update_item_layout();
			return true;
		}
		return setting.is_policy();
	}

	[[nodiscard]] elem_span exposed_children() const noexcept override {
		if constexpr (is_elem_child){
			return adaptor_children();
		}else{
			return {};
		}

	}

	void record_draw_layer(draw_call_stack_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_enter(
			*this, [](const scroll_adaptor& s, const draw_call_param& p,
					  draw_call_stack&) static -> draw_call_param{
				auto& r = s.get_scene().renderer();

				const bool activeHori = s.is_hori_scroll_active();
				const bool activeVert = s.is_vert_scroll_active();

				if(activeHori || activeVert){
					s.renderer().push_scissor({s.get_viewport()});
					s.renderer().top_viewport().push_local_transform(math::mat3{}.idt().set_translation(-s.scroll_.temp));
					s.renderer().notify_viewport_changed();

					return {
							.current_subject = &s,
							.draw_bound = p.draw_bound.copy().move(s.scroll_.temp),
							.opacity_scl = p.opacity_scl,
							.layer_param = p.layer_param
						};
				} else{
					return {
							.current_subject = &s,
							.draw_bound = p.draw_bound,
							.opacity_scl = p.opacity_scl,
							.layer_param = p.layer_param
						};
				}
			});

		//TODO when has_draw_layer is specified , the draw call should be inlined...?
		if constexpr(adaptor_interface_trait::has_draw_layer){
			call_stack_builder.push_call_noop(*this, [](const scroll_adaptor& s, const draw_call_param& p){
				s.adaptor_draw_layer(p.draw_bound, p.opacity_scl, p.layer_param);
			});
		} else if constexpr(is_elem_child){
			get_elem().record_draw_layer(call_stack_builder);
		}


		call_stack_builder.push_call_leave(
			*this, [](const scroll_adaptor& s, const draw_call_param& p, draw_call_stack&) static{

				const bool activeHori = s.is_hori_scroll_active();
				const bool activeVert = s.is_vert_scroll_active();

				if(activeHori || activeVert){
					s.renderer().top_viewport().pop_local_transform();
					s.renderer().pop_scissor();
					s.renderer().notify_viewport_changed();
				}
			});

		record_draw_scroll_bar(call_stack_builder);
	}


	template <elem_init_func Fn, typename... Args>
	auto& create(Fn&& init, Args&&... args) requires(is_elem_ptr){
		this->item_ = elem_ptr{
				get_scene(), this, [&, this](elem_init_func_create_t<Fn>& e){
					this->deduced_set_child_fill_parent(e);
					init(e);
				},
				std::forward<Args>(args)...
			};
		update_children_abs_src();
		notify_isolated_layout_changed();
		return static_cast<elem_init_func_create_t<Fn>&>(*this->item_);
	}

	template <std::derived_from<elem> E, typename... Args>
	E& emplace(Args&&... args) requires(is_elem_ptr){
		this->item_ = elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...};
		scroll_adaptor::deduced_set_child_fill_parent(*this->item_);
		update_children_abs_src();
		notify_isolated_layout_changed();

		return static_cast<E&>(*this->item_);
	}

	bool update(const float delta_in_ticks) override{
		if(!scroll_adaptor_base::update(delta_in_ticks)) return false;

		if(scroll_changed_in_update_){
			update_children_abs_src();
		}

		adaptor_update(delta_in_ticks);
		return true;
	}

	void layout_elem() override{
		elem::layout_elem();
		update_item_layout();

		if(auto new_scroll_base = saved_scroll_ratio_ * scrollable_extent(); util::try_modify(scroll_.base, new_scroll_base.clamp_xy({}, scrollable_extent()))){
			scroll_.resume();
		}
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept final{
		if(elem::update_abs_src(parent_content_src)){
			update_children_abs_src();
			return true;
		}
		return false;
	}

private:
	void update_item_layout(){
		force_hori_scroll_enabled_ = false;
		force_vert_scroll_enabled_ = false;

		if constexpr(is_elem_child){
			this->deduced_set_child_fill_parent(get_elem());
		}

		math::bool2 fill_mask{};
		switch(get_layout_policy()){
		case layout::layout_policy::hori_major : fill_mask = {true, false};
			break;
		case layout::layout_policy::vert_major : fill_mask = {false, true};
			break;
		case layout::layout_policy::none : fill_mask = {false, false};
			break;
		default : std::unreachable();
		}
		using namespace layout;

		optional_mastering_extent bound;
		if constexpr(is_elem_child){
			util::set_fill_parent(get_elem(), content_extent(), fill_mask, !fill_mask);
			bound = static_cast<const elem&>(get_elem()).restriction_extent;
			adaptor_set_prefer_extent(get_viewport_extent());
		} else{
			bound = util::get_fill_parent_restriction(content_extent(), fill_mask, !fill_mask);
		}

		if(auto sz = adaptor_pre_acq_size(bound)){
			bool need_self_relayout = false;

			if(bar_caps_size){
				bool need_elem_relayout = false;
				const float bar_occupied_size = overlay_scroll_bars_ ? 0.0f : scroll_bar_stroke_;

				switch(get_layout_policy()){
				case layout_policy::hori_major :{
					if(sz->y > content_height()){
						bound.set_width(math::clamp_positive(bound.potential_width() - bar_occupied_size));
						need_elem_relayout = true;
						force_vert_scroll_enabled_ = true;
					}
					if(bound.width_pending() && sz->x > content_width()){
						need_self_relayout = true;
					}
					break;
				}
				case layout_policy::vert_major :{
					if(sz->x > content_width()){
						bound.set_height(math::clamp_positive(bound.potential_height() - bar_occupied_size));
						need_elem_relayout = true;
						force_hori_scroll_enabled_ = true;
					}
					if(bound.height_pending() && sz->y > content_height()){
						need_self_relayout = true;
					}
					break;
				}
				default : break;
				}

				if(need_elem_relayout){
					auto b = bound;
					b.apply(content_extent());
					if constexpr(is_elem_child){
						adaptor_set_prefer_extent(b.potential_extent());
					}

					if(auto s = adaptor_pre_acq_size(bound)) sz = s;
				}
			}

			adaptor_resize(*sz, propagate_mask::local | propagate_mask::child);

			if(need_self_relayout){
				auto elemSz = item_extent();
				const float bar_occupied_size = overlay_scroll_bars_ ? 0.0f : scroll_bar_stroke_;

				switch(get_layout_policy()){
				case layout_policy::hori_major :{
					if(elemSz.x > content_width()){
						elemSz.y = content_height();
						elemSz.x += static_cast<float>(bar_caps_size) * bar_occupied_size;
					}
					break;
				}
				case layout_policy::vert_major :{
					if(elemSz.y > content_height()){
						elemSz.x = content_width();
						elemSz.y += static_cast<float>(bar_caps_size) * bar_occupied_size;
					}
					break;
				}
				default : break;
				}

				elemSz += boarder().extent();
				resize(elemSz);
			}
		}

		adaptor_layout_elem();
		if(overlay_scroll_bars_) this->post_task([](elem& e){util::update_insert(e, update_channel::draw);});
	}

	void deduced_set_child_fill_parent(elem& element) const noexcept{
		using namespace layout;
		element.restriction_extent = extent_by_external;
		switch(get_layout_policy()){
		case layout_policy::hori_major :{
			element.set_fill_parent({true, false}, propagate_mask::none);
			element.restriction_extent.set_width(content_width());
			break;
		}
		case layout_policy::vert_major :{
			element.set_fill_parent({false, true}, propagate_mask::none);
			element.restriction_extent.set_height(content_height());
			break;
		}
		case layout_policy::none : element.set_fill_parent({false, false}, propagate_mask::none);
			break;
		default : std::unreachable();
		}
	}

	void update_children_abs_src() noexcept{
		adaptor_update_abs_src(content_src_pos_abs());
		require_scene_cursor_update();
	}

#pragma region GenericAdaptors

public:
	template <typename S>
	auto& get_item(this S& self) requires(!is_elem_ptr){
		return self.item_;
	}

	template <typename S>
	auto& get_elem(this S& self) noexcept requires(is_elem_child){
		if constexpr(requires(element_type& e){
			{ *e } -> std::convertible_to<elem&>;
		}){
			return *self.item_;
		} else if constexpr(is_elem_value){
			return self.item_;
		} else{
			static_assert(false, "cannot convert item to elem");
		}
	}

protected:
	void adaptor_update(const float delta_in_ticks){
		if constexpr (adaptor_interface_trait::has_update){
			item_adaptor.update(item_, delta_in_ticks);
		}else{
		}
	}

	void adaptor_draw_layer(rect clipSpace, float opacityScl, fx::layer_param_pass_t param) const{
		if constexpr(adaptor_interface_trait::has_draw_layer){
			item_adaptor.draw_layer(item_, *this, clipSpace, opacityScl, param);
		} else if constexpr(is_elem_child){
			get_elem().draw_layer(clipSpace, param);
		}
	}

	void adaptor_layout_elem(){
		if constexpr(adaptor_interface_trait::has_layout_elem){
			item_adaptor.layout_elem(item_);
		} else if constexpr(is_elem_child){
			get_elem().layout_elem();
		}
		item_extent_cache_ = get_real_item_extent();
	}

	void adaptor_set_prefer_extent(math::vec2 ext){
		if constexpr(adaptor_interface_trait::has_set_prefer_extent){
			item_adaptor.set_prefer_extent(item_, ext);
		} else if constexpr(is_elem_child){
			get_elem().set_prefer_extent(ext);
		}
	}

	void adaptor_on_context_sync_bind(){
		if constexpr(adaptor_interface_trait::has_on_context_sync_bind){
			item_adaptor.on_context_sync_bind(item_);
		} else if constexpr(is_elem_child){
			get_elem().on_context_sync_bind();
		}
	}

	void adaptor_update_abs_src(math::vec2 pos) noexcept{
		if constexpr(adaptor_interface_trait::has_update_abs_src){
			item_adaptor.update_abs_src(item_, pos);
		} else if constexpr(is_elem_child){
			get_elem().update_abs_src(pos);
		}
	}

	elem_span adaptor_children() const noexcept requires(is_elem_child){
		if constexpr(is_elem_child){
			if constexpr(is_elem_ptr){
				return {item_, elem_ptr::cvt_mptr};
			} else if constexpr(is_elem_value){
				return {&*children_ptr_cache_, 1};
			} else{
				static_assert(false, "unknown elem type");
			}
		}
	}

	std::optional<math::vec2> adaptor_pre_acq_size(layout::optional_mastering_extent bound){
		if constexpr(adaptor_interface_trait::has_pre_acq_size){
			return item_adaptor.pre_acq_size(item_, bound);
		} else if constexpr(is_elem_child){
			return get_elem().pre_acquire_size(bound);
		} else{
			return std::nullopt;
		}
	}

	bool adaptor_resize(const math::vec2 size, propagate_mask temp_mask){
		if constexpr(adaptor_interface_trait::has_resize){
			auto rst = item_adaptor.resize(item_, size, temp_mask);
			item_extent_cache_ = get_real_item_extent();
			return rst;
		} else if constexpr(is_elem_child){
			auto rst = get_elem().resize(size, temp_mask);
			item_extent_cache_ = get_real_item_extent();
			return rst;
		} else{
			return true;
		}
	}

	[[nodiscard]] math::vec2 get_real_item_extent() const noexcept{
		if constexpr(adaptor_interface_trait::has_get_extent){
			return item_adaptor.get_extent(item_);
		} else if constexpr(is_elem_child){
			return get_elem().extent();
		} else{
			return {};
		}
	}
#pragma endregion
};

namespace style{
struct scroll_pane_bar_drawer : style_drawer<scroll_adaptor_base>{
	float minor_near_margin_ratio{0.25f};
	float minor_far_margin_ratio{0.05f};

	using style_drawer::style_drawer;

protected:
	math::raw_frect get_scroll_region(const scroll_adaptor_base& element, float isHori){
		float math::vec2::* major = isHori ? &math::vec2::x : &math::vec2::y;
		float math::vec2::* minor = isHori ? &math::vec2::y : &math::vec2::x;

		const auto barSize = element.get_bar_extent();

		const float margin_near = element.get_scroll_bar_stroke() * minor_near_margin_ratio;
		const float margin_far = element.get_scroll_bar_stroke() * minor_far_margin_ratio;
		const float shrink = margin_near + margin_far;

		math::vec2 bar_extent;
		bar_extent.*major = element.get_viewport_extent().*major;
		bar_extent.*minor = math::fdim(barSize.*minor, shrink);

		auto pos = element.content_src_pos_abs();
		pos.*minor += -barSize.*minor + element.content_extent().*minor + margin_near;

		return math::raw_frect{pos, bar_extent};
	}

	template <std::invocable<math::raw_frect, bool> BarConsumer, std::invocable<math::raw_frect, bool>
		LockBarConsumer>
	void each_scroll_rect(const scroll_adaptor_base& element, math::frect region, BarConsumer barConsumer,
	                      LockBarConsumer lockBarConsumer) const{
		const bool activeHori = element.is_hori_scroll_active();
		const bool activeVert = element.is_vert_scroll_active();

		const bool logicHori = element.is_hori_scroll_enabled();
		const bool logicVert = element.is_vert_scroll_enabled();

		if(logicHori || logicVert){
			const auto barSize = element.get_bar_extent();

			const float margin_near = element.get_scroll_bar_stroke() * minor_near_margin_ratio;
			const float margin_far = element.get_scroll_bar_stroke() * minor_far_margin_ratio;
			const float shrink = margin_near + margin_far;

			auto draw_rect = [&](bool isHori, bool active){
				float math::vec2::* major = isHori ? &math::vec2::x : &math::vec2::y;
				float math::vec2::* minor = isHori ? &math::vec2::y : &math::vec2::x;

				if(active){
					math::raw_frect bar_rect = isHori
						                           ? element.get_hori_bar_rect()
						                           : element.get_vert_bar_rect();
					bar_rect.extent.*minor = math::fdim(bar_rect.extent.*minor, shrink);
					bar_rect.src.*minor += margin_near;

					std::invoke(barConsumer, bar_rect, isHori);
				} else if(element.draw_track_if_locked){
					math::vec2 bar_extent;
					bar_extent.*major = element.get_viewport_extent().*major;
					bar_extent.*minor = math::fdim(barSize.*minor, shrink);
					auto pos = region.src;
					pos.*minor += -barSize.*minor + element.content_extent().*minor + margin_near;

					std::invoke(lockBarConsumer, math::raw_frect{pos, bar_extent}, isHori);
				}
			};

			if(logicHori){
				draw_rect(true, activeHori);
			}

			if(logicVert){
				draw_rect(false, activeVert);
			}
		}
	}

	template <std::invocable<math::raw_frect, bool> BarConsumer>
	void each_scroll_rect(const scroll_adaptor_base& element, math::frect region,
	                      BarConsumer barConsumer) const{
		this->each_scroll_rect(element, region, barConsumer, barConsumer);
	}

	void draw_layer_impl(const scroll_adaptor_base& element, math::frect region, float opacityScl,
	                     fx::layer_param layer_param) const override;
};

constexpr scroll_pane_bar_drawer default_scroll_pane_drawer{tags::persistent, {0b1}};
}

/**
 * @warning in most cases, generic scroll pane is not recommended to use, use scroll adaptor instead.
 */
export
using scroll_pane = scroll_adaptor<elem_ptr>;
}
