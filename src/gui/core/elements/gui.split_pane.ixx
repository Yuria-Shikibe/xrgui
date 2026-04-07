module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.elem.drag_split;

import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.layout.policies;
import mo_yanxi.snap_shot;
import std;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
export
struct split_pane : head_body_no_invariant{
private:
	snap_shot<float> seperator_position_{.5f};
	math::range min_margin{0.1f, 0.1f};

	// --- 新增：拖拽状态机与过渡控制 ---
	enum class drag_state{
		idle, // 未拖拽
		entering, // 正在进入拖拽（淡出过渡中）
		dragging, // 拖拽中
		exiting // 正在退出拖拽（淡入恢复中）
	};

	drag_state current_drag_state_{drag_state::idle};
	float drag_progress_{0.f}; // 0.0f (未拖拽) 到 1.0f (完全拖拽) 的淡出进度

	void update_seperator(){
		set_head_size({layout::size_category::passive, seperator_position_.base});
		set_body_size({layout::size_category::passive, 1.f - seperator_position_.base});
		notify_isolated_layout_changed();
	}

	void move_seperator(math::vec2 delta){
		auto [major_p, minor_p] = layout::get_vec_ptr(get_layout_policy());
		auto offset_in_minor = delta.*minor_p;
		auto minor_ext = content_extent().*minor_p - pad_;
		auto delta_offset = offset_in_minor / minor_ext;

		// 移除了原本直接设置 set_children_opacity_with_scl(.2f) 的硬编码逻辑
		// 透明度变化现在交由 update_state 和 drag_progress_ 统一控制
		util::try_modify(seperator_position_.temp,
		                 math::clamp(seperator_position_.base + delta_offset, min_margin.from, 1.f - min_margin.to));
	}

public:
	split_pane(scene& scene, elem* parent, layout::layout_policy layout_policy)
		: head_body_no_invariant(scene, parent, layout_policy){
		extend_focus_until_mouse_drop = true;
		interactivity = interactivity_flag::enabled;
		emplace_head<gui::elem>();
		emplace_body<gui::elem>();
		set_head_size({layout::size_category::passive, .5f});
		set_body_size({layout::size_category::passive, .5f});
	}

	split_pane(scene& scene, elem* parent)
		: split_pane(scene, parent, layout::layout_policy::vert_major){
	}

	bool update(float delta_in_ticks) override{
		if(head_body_no_invariant::update(delta_in_ticks)){
			update_state(delta_in_ticks / 45.f);
			return true;
		} else{
			return false;
		}
	}

	// --- 新增：处理布局和透明度的帧更新 ---
	void update_state(float dt){
		constexpr float fade_speed = 5.0f; // 动画速度，比如 5.0f 代表 0.2 秒完成渐变，你可以根据需要调整
		bool changed = false;

		if(current_drag_state_ == drag_state::entering){
			drag_progress_ += dt * fade_speed;
			if(drag_progress_ >= 1.0f){
				drag_progress_ = 1.0f;
				current_drag_state_ = drag_state::dragging;
				// 进度达到 1.0 后，状态变为持续拖拽，停止触发 update 回调
				this->post_task([](elem& e){ util::update_erase(e, update_channel::layout); });
			}
			changed = true;
		} else if(current_drag_state_ == drag_state::exiting){
			drag_progress_ -= dt * fade_speed;
			if(drag_progress_ <= 0.0f){
				drag_progress_ = 0.0f;
				current_drag_state_ = drag_state::idle;
				// 完全退出拖拽后，注销 update 回调
				this->post_task([](elem& e){ util::update_erase(e, update_channel::layout); });
			}
			changed = true;
		}

		if(changed){
			// 将 0~1 的拖拽进度映射到 1.0~0.2 的透明度区间
			set_children_opacity_with_scl(math::lerp(1.0f, 0.2f, drag_progress_));
		}
	}

	void set_split_pos(float p){
		if(util::try_modify(seperator_position_.base, math::clamp(p, min_margin.from, 1.f - min_margin.to))){
			seperator_position_.resume();
			update_seperator();
		}
	}

	[[nodiscard]] math::range get_min_margin() const{
		return min_margin;
	}

	void set_min_margin(math::range min_margin){
		if(min_margin.from + min_margin.to > 1.001f){
			throw std::out_of_range("margin sum > 1");
		}

		min_margin.from = math::clamp(min_margin.from);
		min_margin.to = math::clamp(min_margin.to);
		if(util::try_modify(this->min_margin, min_margin)){
		}
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		auto ret = head_body::on_click(event, aboves);
		if(event.key.on_release()){
			// 当鼠标松开时，触发状态机进入退出状态，并重新注册 update 回调执行淡出动画
			if(current_drag_state_ == drag_state::dragging || current_drag_state_ == drag_state::entering){
				current_drag_state_ = drag_state::exiting;
				this->post_task([](elem& e){ util::update_insert(e, update_channel::layout); });
			}

			if(seperator_position_.is_dirty()){
				seperator_position_.apply();
				update_seperator();
				return events::op_afterwards::intercepted;
			}
		}
		return ret;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		// 状态机判断：只有在空闲或退出动画状态下，才需要做“硬判断”（即鼠标是否在触发区域内）
		if(current_drag_state_ == drag_state::idle || current_drag_state_ == drag_state::exiting){
			const auto cursorlocal = event.src;
			const auto region = get_seperator_region_element_local();

			if(!region.contains_loose(cursorlocal - content_src_offset())){
				return events::op_afterwards::fall_through;
			}

			// 进入拖拽状态并注册 update
			current_drag_state_ = drag_state::entering;
			this->post_task([](elem& e){ util::update_insert(e, update_channel::layout); });
		}

		// 一旦状态机进入 entering 或 dragging 状态后，后续移动将直接交由 move_seperator 处理，避免卡顿硬判断
		move_seperator(event.delta());
		return events::op_afterwards::intercepted;
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
		head_body::draw_layer(clipSpace, param);
		if(param == 0){
			auto region = get_seperator_region_element_local();
			auto cursorlocal = util::transform_scene2local(*this, get_scene().get_cursor_pos()) - content_src_offset();
			auto color = region.contains_loose(cursorlocal) ? graphic::colors::pale_green : graphic::colors::YELLOW;
			region.move(content_src_pos_abs());
			// get_scene().renderer().push(graphic::draw::instruction::rect_aabb{
			// 	.v00 = region.vert_00(),
			// 	.v11 = region.vert_11(),
			// 	.vert_color = {color.copy_set_a(.5)}
			// });
		}

		// 保证在淡出动画（drag_progress_ > 0）时也能画出辅助线，而不仅仅是 dirty 的时候
		if(seperator_position_.is_dirty() || drag_progress_ > 0.0f){
			const auto [major_p, minor_p] = layout::get_vec_ptr(get_layout_policy());
			auto src = content_src_pos_abs();

			math::vec2 off{};
			math::vec2 ext{};
			off.*minor_p = seperator_position_.temp * content_extent().*minor_p;
			ext.*major_p = content_extent().*major_p;

			src += off;
			bool any = head().style || body().style;

			if(!any){
				get_scene().renderer().push(graphic::draw::instruction::line{
						.src = src,
						.dst = src + ext,
						.color = {graphic::colors::white, graphic::colors::white},
						.stroke = 4,
					});
			}

			ext.*minor_p -= get_pad() / 2.f;
			if(head().style)
				head().style->draw_layer(head(), {tags::from_vertex, content_src_pos_abs(), src + ext},
				                         drag_progress_ * 4.f, param);
			src.*minor_p += get_pad() / 2.f;
			if(body().style)
				body().style->draw_layer(body(), {
					                         tags::from_vertex, content_src_pos_abs() + content_extent(),
					                         src
				                         }, drag_progress_ * 4.f, param);
		}
	}

	style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override{
		const auto region = get_seperator_region_element_local();
		const bool hit = current_drag_state_ == drag_state::dragging || current_drag_state_ == drag_state::entering || region.contains_loose(cursor_pos_at_content_local - content_src_offset());

		if(hit){
			style::cursor_style rst{style::cursor_type::none};
			if(layout_policy_ == layout::layout_policy::vert_major){
				rst.push_dcor(style::cursor_decoration_type::to_left);
				rst.push_dcor(style::cursor_decoration_type::to_right);
			} else{
				rst.push_dcor(style::cursor_decoration_type::to_up);
				rst.push_dcor(style::cursor_decoration_type::to_down);
			}
			return rst;
		} else{
			return style::cursor_style{style::cursor_type::regular};
		}
	}
};
}
