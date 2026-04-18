module;

#include <mo_yanxi/enum_operator_gen.hpp>
#include <mo_yanxi/adapted_attributes.hpp>


export module mo_yanxi.gui.action.elem;

import std;
export import mo_yanxi.gui.action;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;

namespace mo_yanxi::gui::action{
export
enum struct elem_action_tag{
	no_spec,
	alpha = 1 << 0,
	color = 1 << 1,
	position = 1 << 2,
};


export
struct alpha_action final : action<elem>{
private:
	float initialAlpha{};

public:
	float dst_alpha{};

	[[nodiscard]] alpha_action(const mr::heap_allocator<>& allocator, float lifetime, interp_func_t interpFunc,
		float dst_alpha)
		: action<elem>(allocator, lifetime, interpFunc),
		dst_alpha(dst_alpha){
	}

	[[nodiscard]] alpha_action(const mr::heap_allocator<>& allocator, float lifetime, float dst_alpha)
		: action<elem>(allocator, lifetime),
		dst_alpha(dst_alpha){
	}

protected:
	void apply(elem& elem, const float progress) override{
		elem.update_context_opacity(math::lerp(initialAlpha, dst_alpha, progress));
	}

	void begin(elem& elem) override{
		initialAlpha = elem.get_draw_opacity();
	}

	void end(elem& elem) override{
		elem.update_context_opacity(dst_alpha);
	}
};

export
struct alpha_ctx_fade_in_action final : action<elem>{
private:
	float initialAlpha{};
public:

	[[nodiscard]] alpha_ctx_fade_in_action(const mr::heap_allocator<>& allocator, float lifetime, float initial, interp_func_t interpFunc)
		: action<elem>(allocator, lifetime, interpFunc){
	}

	[[nodiscard]] alpha_ctx_fade_in_action(const mr::heap_allocator<>& allocator, float lifetime, float initial = 0)
		: action<elem>(allocator, lifetime), initialAlpha(initial){
	}

protected:
	void apply(elem& elem, const float progress) override{
		float targetAbs{1};
		if(auto p = elem.parent()){
			targetAbs = p->get_draw_opacity();
		}
		elem.update_context_opacity(math::lerp(initialAlpha, targetAbs, progress));
	}
};


}
