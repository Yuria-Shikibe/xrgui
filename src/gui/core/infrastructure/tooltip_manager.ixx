module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:tooltip_manager;

import :elem_ptr;
import :events;
import :tooltip_interface;

import mo_yanxi.gui.alloc;
import align;
import std;

namespace mo_yanxi::gui::tooltip{
export
class tooltip_manager;

struct tooltip_draw_info{
	elem* element{};
	bool belowScene{};

	friend bool operator==(const tooltip_draw_info& lhs, const tooltip_draw_info& rhs) noexcept{
		return lhs.element == rhs.element;
	}

	friend bool operator==(const tooltip_draw_info& lhs, const elem* rhs) noexcept{
		return lhs.element == rhs;
	}

	friend bool operator==(const elem* lhs, const tooltip_draw_info& rhs) noexcept{
		return lhs == rhs.element;
	}
};

struct tooltip_instance{
	friend tooltip_manager;
	elem_ptr element{};
	spawner* owner{};

	math::vec2 last_pos{math::vectors::constant2<float>::SNaN};

private:
	void update_layout(const tooltip_manager& manager, math::vec2 cursor_pos);

	[[nodiscard]] bool is_pos_set() const noexcept{
		return !last_pos.is_NaN();
	}
};


export struct tooltip_manager{
public:
	static constexpr float RemoveFadeTime = 15.f;
	static constexpr float MarginSize = 15.f;

private:
	struct tooltip_expired{
		elem_ptr element{};
		float remainTime{};
	};

	mr::heap_vector<tooltip_draw_info> drawSequence{};
	mr::heap_vector<tooltip_expired> dropped{};
	mr::heap_vector<tooltip_instance> actives_{};

	using ActivesItr = decltype(actives_)::iterator;

public:
	[[nodiscard]] tooltip_manager() : tooltip_manager(mr::get_default_heap_allocator()){

	}

	[[nodiscard]] explicit tooltip_manager(mr::heap_allocator<> allocator) :
	drawSequence{allocator},
	dropped{allocator},
	actives_{allocator}{
	}

	// bool hasInstance(const TooltipOwner& owner){
	// 	return std::ranges::contains(actives | std::views::reverse | std::views::transform(&ValidTooltip::owner), &owner);
	// }

	[[nodiscard]] spawner* get_top_focus() const noexcept{
		return actives_.empty() ? nullptr : actives_.back().owner;
	}

	tooltip_instance& append_tooltip(
		spawner& owner,
		bool belowScene = false,
		bool fade_in = true);

	tooltip_instance& append_tooltip(
		spawner& owner,
		elem_ptr&& elem,
		bool belowScene = false,
		bool fade_in = true);

	tooltip_instance* try_append_tooltip(
		spawner& owner,
		bool belowScene = false,
		bool fade_in = true);

	void update(float delta_in_time, math::vec2 cursor_pos, bool is_mouse_pressed);

	bool request_drop(const spawner* owner){
		const auto be = std::ranges::find(actives_, owner, &tooltip_instance::owner);

		return drop_since(be);
	}

	bool is_below_scene(const elem* element) const noexcept{
		for(const auto& draw : drawSequence){
			if(draw.element == element){
				return draw.belowScene;
			}
		}

		return false;
	}

	auto& get_active_tooltips() noexcept{
		return actives_;
	}

	void clear() noexcept{
		drawSequence.clear();
		drop_all();
		dropped.clear();
	}

	events::op_afterwards on_esc();

	[[nodiscard]] std::span<const tooltip_draw_info> get_draw_sequence() const{
		return drawSequence;
	}

private:
	bool drop(ActivesItr be, ActivesItr se);

	bool drop_one(const ActivesItr where){
		return drop(where, std::ranges::next(where));
	}

	bool drop_back(){
		assert(!actives_.empty());
		return drop(std::ranges::prev(actives_.end()), actives_.end());
	}

	bool drop_all(){
		return drop(actives_.begin(), actives_.end());
	}

	bool drop_since(const ActivesItr& where){
		return drop(where, actives_.end());
	}

	void updateDropped(float delta_in_time);
};
}
