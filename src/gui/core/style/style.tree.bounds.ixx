module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree.bounds;

import std;
import mo_yanxi.math;
export import mo_yanxi.gui.style.tree;

namespace mo_yanxi::gui::style::bounds{

export
struct bounds_identity{
	template <typename Target>
	draw_call_param operator()(const typed_draw_param<Target>& p) const{
		return p.param;
	}
};

export
namespace side{
inline constexpr struct left_t{} left{};
inline constexpr struct right_t{} right{};
inline constexpr struct top_t{} top{};
inline constexpr struct bottom_t{} bottom{};
}

export
template <typename SideTag>
struct bounds_side_bar{
	float stroke{};

	template <typename Target>
	draw_call_param operator()(const auto& self, const typed_draw_param<Target>& p) const{
		if(!p || stroke <= 0.f) return {};

		auto result = p.param;
		if constexpr(std::is_same_v<SideTag, side::left_t>){
			result.draw_bound = {p->draw_bound.src, {stroke, p->draw_bound.extent().y}};
		}else if constexpr(std::is_same_v<SideTag, side::right_t>){
			result.draw_bound = {p->draw_bound.vert_01(), {-stroke, p->draw_bound.extent().y}};
		}else if constexpr(std::is_same_v<SideTag, side::top_t>){
			result.draw_bound = {p->draw_bound.src, {p->draw_bound.extent().x, stroke}};
		}else if constexpr(std::is_same_v<SideTag, side::bottom_t>){
			result.draw_bound = {p->draw_bound.vert_10(), {p->draw_bound.extent().x, -stroke}};
		}else{
			static_assert(sizeof(SideTag) < 0, "Unknown side tag");
		}
		return result.draw_bound.is_roughly_zero_area(0.01f) ? draw_call_param{} : result;
	}
};

}
