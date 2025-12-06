module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:dialog_manager;

import :elem_ptr;
import :events;
import mo_yanxi.gui.layout.policies;
import mo_yanxi.utility;

namespace mo_yanxi::gui{


export
struct overlay_layout{
	layout::stated_extent extent{};
	align::pos align{align::pos::center};

	math::vec2 absolute_offset{};
	math::vec2 scaling_offset{};
	math::vec2 scaling{1, 1};
};

export
struct overlay{
	elem_ptr element{};
	overlay_layout layout_config{};
	bool layout_changed{true};

	[[nodiscard]] math::vec2 get_overlay_extent(const math::vec2 scene_viewport_size) const;

	void update_bound(const rect scene_viewport) const;

	[[nodiscard]] elem* get() const noexcept{
		return element.get();
	}
};

template <std::derived_from<elem> ElemTy = elem>
struct overlay_create_result{
	overlay& dialog;

	ElemTy* operator->() const noexcept{
		return static_cast<ElemTy*>(dialog.get());
	}

	ElemTy& operator*() const noexcept{
		return static_cast<ElemTy&>(*dialog.get());
	}

	[[nodiscard]] ElemTy& elem() const noexcept{
		return static_cast<ElemTy&>(*dialog.get());
	}

	template <std::derived_from<ElemTy> Ty>
	explicit operator overlay_create_result<Ty>() const noexcept{
		return overlay_create_result<Ty>{dialog};
	}
};

struct overlay_fading : overlay{
	static constexpr float fading_time{15};
	float duration{fading_time};
};

export
struct overlay_manager{
private:
	using container = mr::heap_vector<overlay>;

	container overlays_{};
	mr::heap_vector<overlay_fading> overlay_fadings_{};
	mr::heap_vector<const elem*> draw_sequence_{};

	rect last_vp_{};
public:
	[[nodiscard]] overlay_manager() : overlay_manager(mr::get_default_heap_allocator()){
	}

	[[nodiscard]] explicit overlay_manager(const mr::heap_allocator<>& allocator) :
	overlays_(allocator),
	overlay_fadings_(allocator),
	draw_sequence_(allocator){
	}

	overlay_create_result<elem> push_back(const overlay_layout& layout, elem_ptr&& elem_ptr, bool fade_in = true);

	void pop_back() noexcept{
		if(overlays_.empty()){
			//TODO throw instead?
			return;
		}

		auto dialog = std::move(overlays_.back());
		overlays_.pop_back();
		overlay_fadings_.push_back({std::move(dialog)});
	}

	auto begin(this auto& self) noexcept{
		return self.overlays_.begin();
	}

	auto end(this auto& self) noexcept{
		return self.overlays_.end();
	}

	[[nodiscard]] bool empty() const noexcept{
		return overlays_.empty();
	}

	[[nodiscard]] std::span<const elem* const> get_draw_sequence() const noexcept{
		return draw_sequence_;
	}

	void truncate(const elem* elem){
		if(const auto itr = std::ranges::find(overlays_, elem, &overlay::get); itr != overlays_.end()){
			truncate(itr);
		}
	}

	void truncate(container::iterator where);

	void resize(rect scene_viewport){
		if(last_vp_ == scene_viewport)return;
		last_vp_ = scene_viewport;
		for (auto & overlay : overlays_){
			overlay.layout_changed = true;
		}
	}

	void update(float delta_in_tick);

	events::op_afterwards on_esc() noexcept;
};
}
