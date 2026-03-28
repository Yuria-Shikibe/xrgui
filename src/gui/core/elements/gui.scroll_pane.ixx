module;

#include <cassert>

export module mo_yanxi.gui.elem.scroll_pane;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;
import mo_yanxi.math.matrix3;
import mo_yanxi.cond_exist;

import mo_yanxi.snap_shot;
import mo_yanxi.math;
import std;

namespace mo_yanxi::gui{
	export
	struct scroll_adaptor_base;

	namespace style{
		export
		struct scroll_pane_bar_drawer;

		export
		extern referenced_ptr<const scroll_pane_bar_drawer> global_scroll_pane_bar_drawer;
	}

	struct scroll_adaptor_base : elem{
		static constexpr float VelocitySensitivity = 0.95f;
		static constexpr float VelocityDragSensitivity = 0.15f;
		static constexpr float VelocityScale = 55.f;

	protected:
		float scroll_bar_stroke_{20.0f};

		math::vec2 scroll_velocity_{};
		math::vec2 scroll_target_velocity_{};
		snap_shot<math::vec2> scroll_{};
		// 核心：缓存子节点的实际尺寸，基类依赖此进行几何和物理运算
		math::vec2 item_extent_cache_{};

		layout::layout_policy layout_policy_{layout::layout_policy::hori_major};
		bool bar_caps_size{true};
		bool force_hori_scroll_enabled_{false};
		bool force_vert_scroll_enabled_{false};

		float bar_opacity_{1.0f};
		float activity_timer_{0.0f};
		math::vec2 last_local_cursor_pos_{-10000.f, -10000.f};

		bool overlay_scroll_bars_{false};
		bool scroll_changed_in_update_{false};

	public:
		bool draw_track_if_locked{true};
		float fade_delay_ticks{60.0f * 1.5f};
		float fade_duration_ticks{60.0f * 0.5f};

		referenced_ptr<const style::scroll_pane_bar_drawer> drawer{style::global_scroll_pane_bar_drawer};

		[[nodiscard]] scroll_adaptor_base(scene& scene, elem* parent, layout::layout_policy policy)
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

		[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept{
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
			last_local_cursor_pos_ = event.dst;
			if(overlay_scroll_bars_ && is_cursor_pos_in_scroll_bar_region()){
				set_update_required(update_channel::draw);
			}

			return elem::on_cursor_moved(event);
		}

		events::op_afterwards on_scroll(const events::scroll e, std::span<elem* const> aboves) override;

		events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
			if(event.key.action == input_handle::act::release){
				scroll_.apply();
			}

			return elem::on_click(event, aboves);
		};

		events::op_afterwards on_drag(const events::drag e) override;
#pragma endregion

	protected:
		bool is_cursor_pos_in_scroll_bar_region() const noexcept{
			if(cursor_state().inbound){
				const auto check_pos = last_local_cursor_pos_ - scroll_.temp;
				const auto vp = rect{
						tags::unchecked, tags::from_extent, {}, get_viewport_extent().fdim(get_bar_extent())
					};
				return !vp.contains_loose(check_pos) && (is_hori_scroll_enabled() || is_vert_scroll_enabled());
			}
			return false;
		}

		[[nodiscard]] std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept override{
			return get_layout_policy();
		}

		[[nodiscard]] bool parent_contain_constrain(math::vec2 relative_pos) const noexcept override{
			return rect{tags::from_extent, content_src_pos_rel(), get_viewport_extent()}.contains_loose(relative_pos) &&
				elem::parent_contain_constrain(relative_pos);
		}

	public:
		void on_inbound_changed(bool is_inbounded, bool changed) override{
			elem::on_inbound_changed(is_inbounded, changed);
			set_focused_scroll(is_inbounded);
			if(changed && overlay_scroll_bars_) set_update_required(update_channel::draw);
		}

		void on_focus_changed(bool is_focused) override{
			elem::on_focus_changed(is_focused);
		}

		bool update(const float delta_in_ticks) override;

		math::vec2 transform_to_content_space(math::vec2 where_relative_in_parent) const noexcept override{
			return elem::transform_to_content_space(where_relative_in_parent + scroll_.temp);
		}

		math::vec2 transform_from_content_space(math::vec2 where_relative_in_child) const noexcept override{
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
			return item_extent_cache_.x > content_width();
		}

		[[nodiscard]] bool is_vert_scroll_active() const noexcept{
			return item_extent_cache_.y > content_height();
		}

		[[nodiscard]] math::vec2 scrollable_extent() const noexcept{
			return (item_extent_cache_ - get_viewport_extent()).max({});
		}

		[[nodiscard]] math::nor_vec2 scroll_progress_at(const math::vec2 scroll_pos) const noexcept{
			return scroll_pos / scrollable_extent();
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

		[[nodiscard]] float bar_hori_length() const{
			const auto w = get_viewport_extent().x;
			return math::clamp_positive(math::min(w / item_extent_cache_.x, 1.0f) * w);
		}

		[[nodiscard]] float bar_vert_length() const{
			const auto h = get_viewport_extent().y;
			return math::clamp_positive(math::min(h / item_extent_cache_.y, 1.0f) * h);
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
	};

	export
	template <typename ElementType = elem_ptr>
	struct scroll_adaptor final : scroll_adaptor_base{
		using element_type = ElementType;
		static constexpr bool is_elem_child = std::same_as<element_type, elem_ptr> || std::derived_from<element_type, elem>;

	private:
		element_type item_{};
		cond_exist<element_type*, std::derived_from<element_type, elem>> children_ptr_cache_{&item_};

	public:
		[[nodiscard]] scroll_adaptor(scene& scene, elem* parent, layout::layout_policy policy)
			: scroll_adaptor_base(scene, parent, policy){
		}

		[[nodiscard]] scroll_adaptor(scene& scene, elem* parent)
			: scroll_adaptor(scene, parent, layout::layout_policy::hori_major){
		}

		void set_layout_policy(layout::layout_policy policy){
			if(layout_policy_ != policy){
				layout_policy_ = policy;
				update_item_layout();
			}
		}

		[[nodiscard]] elem_span children() const noexcept override{
			return adaptor_children();
		}

		void draw_layer(rect clipSpace, fx::layer_param_pass_t param) const override{
			elem::draw_layer(clipSpace, param);

			auto& r = get_scene().renderer();

			const bool activeHori = is_hori_scroll_active();
			const bool activeVert = is_vert_scroll_active();

			if(activeHori || activeVert){
				scissor_guard guard{r, {get_viewport()}};
				transform_guard transform_guard{r, math::mat3{}.idt().set_translation(-scroll_.temp)};
				adaptor_draw_layer(clipSpace.move(scroll_.temp), param);
			} else{
				adaptor_draw_layer(clipSpace, param);
			}

			draw_scroll_bar(param);
		}

		template <elem_init_func Fn, typename... Args>
		auto& create(Fn&& init, Args&&... args){
			if(this->item_){
				clear_children_update_required(this->item_.get());
			}
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
		E& emplace(Args&&... args){
			if(this->item_){
				clear_children_update_required(this->item_.get());
			}
			this->item_ = elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...};
			deduced_set_child_fill_parent(*this->item_);
			update_children_abs_src();
			notify_isolated_layout_changed();

			return static_cast<E&>(*this->item_);
		}

		void on_context_sync_bind() override{
			elem::on_context_sync_bind();
			adaptor_on_context_sync_bind();
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
			if(util::try_modify(scroll_.base, scroll_.base.copy().clamp_xy({}, scrollable_extent()))){
				scroll_.resume();
			}
		}

		bool update_abs_src(math::vec2 parent_content_src) noexcept override{
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
			switch(layout_policy_){
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
				adaptor_util_set_fill_parent(content_extent(), fill_mask, !fill_mask);
				bound = adaptor_get_restriction_extent();
				adaptor_set_prefer_extent(get_viewport_extent());
			} else{
				bound = util::get_fill_parent_restriction(content_extent(), fill_mask, !fill_mask);
			}

			if(auto sz = adaptor_pre_acq_size(bound)){
				bool need_self_relayout = false;

				if(bar_caps_size){
					bool need_elem_relayout = false;
					const float bar_occupied_size = overlay_scroll_bars_ ? 0.0f : scroll_bar_stroke_;

					switch(layout_policy_){
					case layout_policy::hori_major :{
						if(sz->y > content_height()){
							bound.set_width(math::clamp_positive(bound.potential_width() - bar_occupied_size));
							need_elem_relayout = true;
							force_vert_scroll_enabled_ = true;
						}
						if(adaptor_get_restriction_extent().width_pending() && sz->x > content_width()){
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
						if(adaptor_get_restriction_extent().height_pending() && sz->y > content_height()){
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

					switch(layout_policy_){
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
			if(overlay_scroll_bars_) set_update_required(update_channel::draw);
		}

		void deduced_set_child_fill_parent(elem& element) const noexcept{
			using namespace layout;
			element.restriction_extent = extent_by_external;
			switch(layout_policy_){
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

		void update_children_abs_src() const noexcept{
			adaptor_update_abs_src(content_src_pos_abs());
			require_scene_cursor_update();
		}

#pragma region GenericAdaptors

	public:
		template <typename S>
		auto& get_elem(this S& self) noexcept requires(is_elem_child){
			if constexpr(requires(element_type& e){
				{ *e } -> std::convertible_to<elem&>;
			}){
				return *self.item_;
			} else if constexpr(std::derived_from<element_type, elem>){
				return self.item_;
			} else{
				static_assert(false, "cannot convert item to elem");
			}
		}

	protected:
		[[nodiscard]] bool adaptor_is_valid() const noexcept{
			return static_cast<bool>(item_);
		}

		void adaptor_update(const float delta_in_ticks){
			if constexpr(is_elem_child){
				auto& e = get_elem();
				if(e.update_flag.is_update_required()){
					e.update(delta_in_ticks);
				}
			}
		}

		void adaptor_draw_layer(rect clipSpace, fx::layer_param_pass_t param) const{
			if constexpr(is_elem_child){
				get_elem().draw_layer(clipSpace, param);
			}
		}

		void adaptor_layout_elem(){
			if constexpr(is_elem_child){
				get_elem().layout_elem();
				// 核心：布局完成后向基类刷新缓存尺寸
				item_extent_cache_ = get_real_item_extent();
			}
		}

		void adaptor_set_prefer_extent(math::vec2 ext){
			if constexpr(is_elem_child){
				get_elem().set_prefer_extent(ext);
			}
		}

		layout::optional_mastering_extent adaptor_get_restriction_extent() const noexcept{
			if constexpr(is_elem_child){
				return get_elem().restriction_extent;
			}
			return {};
		}

		void adaptor_util_set_fill_parent(math::vec2 content_ext, math::bool2 fill_mask, math::bool2 none_mask){
			if constexpr(is_elem_child){
				util::set_fill_parent(get_elem(), content_ext, fill_mask, none_mask);
			}
		}

		void adaptor_on_context_sync_bind(){
			if constexpr(is_elem_child){
				get_elem().on_context_sync_bind();
			}
		}

		void adaptor_update_abs_src(math::vec2 pos) const noexcept{
			if constexpr(is_elem_child){
				get_elem().update_abs_src(pos);
			}
		}

		elem_span adaptor_children() const noexcept{
			if constexpr(is_elem_child){
				if constexpr(std::same_as<elem_ptr, element_type>){
					return {item_, elem_ptr::cvt_mptr};
				}else if constexpr(std::derived_from<elem, element_type>){
					return {&children_ptr_cache_, elem_ptr::cvt_mptr};
				}else{
					static_assert(false, "unknown elem type");
				}
			}else{
				static_assert(false, "only gui.element type can return a elem span");
			}
		}

		std::optional<math::vec2> adaptor_pre_acq_size(layout::optional_mastering_extent bound){
			if constexpr(is_elem_child){
				return get_elem().pre_acquire_size(bound);
			} else{
				return std::nullopt;
			}
		}

		bool adaptor_resize(const math::vec2 size, propagate_mask temp_mask){
			if constexpr(is_elem_child){
				auto rst = get_elem().resize(size, temp_mask);
				item_extent_cache_ = get_elem().extent();
				return rst;
			} else{
				return true;
			}
		}

		[[nodiscard]] math::vec2 get_real_item_extent() const noexcept{
			if constexpr(is_elem_child){
				return item_->extent();
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

	export
	using scroll_pane = scroll_adaptor<elem_ptr>;
}
