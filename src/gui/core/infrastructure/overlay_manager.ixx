module;

#include <cassert>

#if !defined(XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE) || defined(__RESHARPER__)
#include "plf_hive.h"
#endif

export module mo_yanxi.gui.infrastructure:dialog_manager;

import :defines;
import :elem_ptr;
import :events;
import std;
export import mo_yanxi.react_flow;
import mo_yanxi.gui.layout.policies;
import mo_yanxi.input_handle;
import mo_yanxi.utility;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
#endif

namespace mo_yanxi::gui{

export
enum class overlay_external_press_policy : std::uint8_t{
	ignore,
	dismiss_and_intercept,
	dismiss_and_retarget_right_press
};

export
enum class overlay_external_press_result : std::uint8_t{
	ignored,
	intercepted,
	retarget
};

export
struct overlay_layout{
	layout::stated_extent extent{};
	align::pos align{align::pos::center};
	overlay_external_press_policy external_press_policy{overlay_external_press_policy::ignore};

	math::vec2 absolute_offset{};
	math::vec2 scaling_offset{};
	math::vec2 scaling{1, 1};
};

export
struct overlay;

export
enum class overlay_operation : std::uint8_t{
	dismiss
};

export
struct overlay_operation_context{
	overlay_operation operation{};
	overlay* dialog{};
	elem* element{};
};

export
using overlay_operation_provider = react_flow::provider_general<overlay_operation_context>;

export
struct overlay{
	elem_ptr element{};
	overlay_layout layout_config{};
	react_flow::node_holder_pinned<overlay_operation_provider> operation_provider{};
	bool layout_changed{true};

	[[nodiscard]] overlay() = default;

	[[nodiscard]] overlay(elem_ptr&& element, overlay_layout layout_config)
		: element(std::move(element)), layout_config(layout_config){
	}

	[[nodiscard]] math::vec2 get_overlay_extent(const math::vec2 scene_viewport_size) const;

	void update_bound(const rect scene_viewport) const;

	[[nodiscard]] inline overlay_operation_provider& get_operation_provider(){
		return operation_provider.node;
	}

	inline void notify_operation(overlay_operation operation){
		operation_provider->update_value(overlay_operation_context{
			.operation = operation,
			.dialog = this,
			.element = element.get()
		});
	}

	[[nodiscard]] inline elem* get() const noexcept{
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

struct overlay_fading{
	static constexpr float fading_time{15};
	overlay* dialog{};
	float duration{fading_time};
};

export
struct overlay_manager{
private:
	using container = plf::hive<overlay, mr::heap_allocator<overlay>>;
	using active_container = mr::heap_vector<overlay*>;

	container overlays_{};
	active_container active_stack_{};
	mr::heap_vector<overlay_fading> fading_overlays_{};
	mr::heap_vector<const elem*> draw_sequence_{};

	rect last_vp_{};
public:
	[[nodiscard]] overlay_manager() : overlay_manager(mr::get_default_heap_allocator()){
	}

	[[nodiscard]] explicit overlay_manager(const mr::heap_allocator<>& allocator) :
	overlays_(mr::heap_allocator<overlay>{allocator}),
	active_stack_(allocator),
	fading_overlays_(allocator),
	draw_sequence_(allocator){
	}

	overlay_create_result<elem> push_back(const overlay_layout& layout, elem_ptr&& elem_ptr, bool fade_in = true);

	inline void clear() noexcept{
		draw_sequence_.clear();
		active_stack_.clear();
		fading_overlays_.clear();
		overlays_.clear();
		last_vp_ = {};
	}

	inline void pop_back(){
		if(active_stack_.empty()){
			//TODO throw instead?
			return;
		}

		truncate(std::prev(active_stack_.end()));
	}

	[[nodiscard]] inline auto active_overlays() noexcept{
		return active_stack_ | std::views::transform([](overlay* dialog) -> overlay&{
			return *dialog;
		});
	}

	[[nodiscard]] inline auto active_overlays() const noexcept{
		return active_stack_ | std::views::transform([](overlay* dialog) -> const overlay&{
			return *dialog;
		});
	}

	[[nodiscard]] inline overlay* top_active_overlay() noexcept{
		return active_stack_.empty() ? nullptr : active_stack_.back();
	}

	[[nodiscard]] inline const overlay* top_active_overlay() const noexcept{
		return active_stack_.empty() ? nullptr : active_stack_.back();
	}

	[[nodiscard]] inline bool empty() const noexcept{
		return active_stack_.empty();
	}

	[[nodiscard]] inline std::span<const elem* const> get_draw_sequence() const noexcept{
		return draw_sequence_;
	}

	inline void truncate(const elem* overlay_elem){
		const auto itr = std::ranges::find(active_stack_, overlay_elem, [](const overlay* dialog){
			return dialog->get();
		});
		if(itr != active_stack_.end()){
			truncate(itr);
		}
	}

	void truncate(active_container::iterator where);

	[[nodiscard]] overlay_external_press_result handle_external_press(
		math::vec2 scene_position,
		events::key_set key);

	inline void resize(rect scene_viewport){
		if(last_vp_ == scene_viewport)return;
		last_vp_ = scene_viewport;
		for (overlay* overlay : active_stack_){
			overlay->layout_changed = true;
		}
	}

	void update(float delta_in_tick);

	events::dispatch_result on_esc() noexcept;
};
}
