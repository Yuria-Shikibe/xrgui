module;

#include <cassert>

export module mo_yanxi.gui.elem.scroll_pane;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;

import mo_yanxi.snap_shot;
import mo_yanxi.math;
import std;

namespace mo_yanxi::gui{
	export
	struct scroll_pane final : elem{

		static constexpr float VelocitySensitivity = 0.95f;
		static constexpr float VelocityDragSensitivity = 0.15f;
		static constexpr float VelocityScale = 55.f;

	private:
		float scroll_bar_stroke_{20.0f};

		math::vec2 scroll_velocity_{};
		math::vec2 scroll_target_velocity_{};
		snap_shot<math::vec2> scroll_{};

		elem_ptr item_{};
		layout::layout_policy layout_policy_{layout::layout_policy::hori_major};
		bool bar_caps_size{true};
		bool force_hori_scroll_enabled_{false};
		bool force_vert_scroll_enabled_{false};


		float bar_opacity_{1.0f};
		float activity_timer_{0.0f};
		math::vec2 last_local_cursor_pos_{-10000.f, -10000.f};

		bool overlay_scroll_bars_{false};

	public:
		bool draw_track_if_locked{true};
		// --- [新增] 配置项 ---
		float fade_delay_ticks{60.0f * 1.5f};  // 1.5秒后开始淡出
		float fade_duration_ticks{60.0f * 0.5f}; // 0.5秒淡出时间


		[[nodiscard]] scroll_pane(scene& scene, elem* parent, layout::layout_policy policy)
			: elem(scene, parent), layout_policy_{policy}{
			interactivity = interactivity_flag::enabled;

			extend_focus_until_mouse_drop = true;
			layout_state.intercept_lower_to_isolated = true;
		}

		[[nodiscard]] scroll_pane(scene& scene, elem* parent)
			: scroll_pane(scene, parent, layout::layout_policy::hori_major){
		}

		void set_overlay_bar(bool enable){
			if(util::try_modify(overlay_scroll_bars_, enable)){
				notify_isolated_layout_changed();
			}
		}

		void set_layout_policy(layout::layout_policy policy){
			if(layout_policy_ != policy){
				layout_policy_ = policy;
				if(item_)update_item_layout();
			}
		}

		[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept{
			return layout_policy_;
		}

	private:
		[[nodiscard]] std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept override{
			return get_layout_policy();
		}

	public:
		[[nodiscard]] float get_scroll_bar_stroke() const noexcept{
			return scroll_bar_stroke_;
		}

		void set_scroll_bar_stroke(const float scroll_bar_stroke){
			if(util::try_modify(scroll_bar_stroke_, scroll_bar_stroke)){
				notify_isolated_layout_changed();
			}
		}

		[[nodiscard]] std::span<const elem_ptr> children() const noexcept override{
			return {&item_, 1};
		}

		[[nodiscard]] elem& get_item() const noexcept {
			return *item_;
		}

	private:

		[[nodiscard]] bool parent_contain_constrain(math::vec2 relative_pos) const noexcept override{
			return rect{tags::from_extent, content_src_pos_rel(), get_viewport_extent()}.contains_loose(relative_pos) && elem::parent_contain_constrain(relative_pos);
		}

		void on_inbound_changed(bool is_inbounded, bool changed) override{
			elem::on_inbound_changed(is_inbounded, changed);
			set_focused_scroll(is_inbounded);
		}

		void on_focus_changed(bool is_focused) override{
			elem::on_focus_changed(is_focused);
		}

		bool update(const float delta_in_ticks) override;

		void layout_elem() override{
			elem::layout_elem();
			update_item_layout();
		}


	public:
		void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override;

		template <elem_init_func Fn, typename ...Args>
		auto& create(Fn&& init, Args&& ...args){
			if(this->item_){
				clear_children_update_required(this->item_.get());
			}
			this->item_ = elem_ptr{get_scene(), this, [&, this](elem_init_func_create_t<Fn>& e){
				scroll_pane::deduced_set_child_fill_parent(e);
				init(e);
			}, std::forward<Args>(args)...};
			updateChildrenAbsSrc();
			notify_isolated_layout_changed();

			return static_cast<elem_init_func_create_t<Fn>&>(*this->item_);
		}

		template <std::derived_from<elem> E, typename ...Args>
		E& emplace(Args&& ...args){
			if(this->item_){
				clear_children_update_required(this->item_.get());
			}
			this->item_ = elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...};
			deduced_set_child_fill_parent(*this->item_);
			updateChildrenAbsSrc();
			notify_isolated_layout_changed();

			return static_cast<E&>(*this->item_);
		}


	private:

#pragma region Event
		events::op_afterwards on_cursor_moved(const events::cursor_move event) override {
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

		bool is_cursor_pos_in_scroll_bar_region() const noexcept{
			if(cursor_state().inbound) {
				const auto check_pos = last_local_cursor_pos_ - scroll_.temp;

				const auto vp = rect{tags::unchecked, tags::from_extent, {}, get_viewport_extent().fdim(get_bar_extent())};
				return !vp.contains_loose(check_pos) && (is_hori_scroll_enabled() || is_vert_scroll_enabled());
			}
			return false;
		}

		void update_item_layout();

		void deduced_set_child_fill_parent(elem& element) const noexcept;

		void set_scroll_by_ratio(math::vec2 ratio){
			//TODO
		}

		bool update_abs_src(math::vec2 parent_content_src) noexcept override{
			if(elem::update_abs_src(parent_content_src)){
				updateChildrenAbsSrc();
				return true;
			}
			return false;
		}

		void updateChildrenAbsSrc() const noexcept{
			assert(item_ != nullptr);
			// item->set_rel_pos(-scroll.temp);
			item_->update_abs_src(content_src_pos_abs());
			require_scene_cursor_update();
		}

		math::vec2 transform_to_content_space(math::vec2 where_relative_in_parent) const noexcept override{
			return elem::transform_to_content_space(where_relative_in_parent + scroll_.temp);
		}

		math::vec2 transform_from_content_space(math::vec2 where_relative_in_child) const noexcept override{
			return elem::transform_from_content_space(where_relative_in_child - scroll_.temp);
		}

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

		/**
		 * @brief 物理上是否开启滚动条（用于判断是否需要 Scissor/Transform 以及绘制滑块）
		 * 仅当内容确实溢出时为真
		 */
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
			return scroll_pos / scrollable_extent();
		}

		[[nodiscard]] math::vec2 item_extent() const noexcept{
			assert(item_ != nullptr);
			return item_->extent();
		}

		[[nodiscard]] math::vec2 get_bar_extent() const noexcept{
			math::vec2 rst{};
			if(is_hori_scroll_enabled())rst.y = scroll_bar_stroke_;
			if(is_vert_scroll_enabled())rst.x = scroll_bar_stroke_;
			return rst;
		}

	private:
		[[nodiscard]] math::vec2 get_bar_extent_at(math::vec2 temp_item_size) const noexcept{
			math::vec2 rst{};

			if(temp_item_size.x > content_width())rst.y = scroll_bar_stroke_;
			if(temp_item_size.y > content_height())rst.x = scroll_bar_stroke_;

			return rst;
		}

	public:

		[[nodiscard]] float bar_hori_length() const {
			const auto w = get_viewport_extent().x;
			return math::clamp_positive(math::min(w / item_extent().x, 1.0f) * w);
		}

		[[nodiscard]] float bar_vert_length() const {
			const auto h = get_viewport_extent().y;
			return math::clamp_positive(math::min(h / item_extent().y, 1.0f) * h);
		}

		[[nodiscard]] vec2 get_viewport_extent() const noexcept{
			if (overlay_scroll_bars_) {
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

		[[nodiscard]] float scroll_ratio_hori(const float pos) const {
			return math::clamp(pos / (item_extent().x - get_viewport_extent().x));
		}

		[[nodiscard]] float scroll_ratio_vert(const float pos) const {
			return math::clamp(pos / (item_extent().y - get_viewport_extent().y));
		}

		[[nodiscard]] math::vec2 scroll_ratio(const math::vec2 pos) const {
			auto [ix, iy] = item_extent();
			auto [vx, vy] = get_viewport_extent();

			return {
				is_hori_scroll_enabled() ? math::clamp(pos.x / (ix - vx)) : 0.f,
				is_vert_scroll_enabled() ? math::clamp(pos.y / (iy - vy)) : 0.f,
			};
		}
	};


}
