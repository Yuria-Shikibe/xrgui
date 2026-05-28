module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.interface;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.graphic.color;
export import mo_yanxi.referenced_ptr;
export import mo_yanxi.gui.fx.config;
export import mo_yanxi.function_call_stack;

import std;

namespace mo_yanxi::gui{

export
struct draw_call_mutable_param{
	math::frect draw_bound;
	float opacity_scl;
};

export
struct draw_call_param{
	/**
	 * @brief used for style drawer, when set to nullptr, style drawer should skip draw (this is used to impl draw clip)
	 */
	const void* current_subject;

	/**
	 * @brief used as clip space for elements, draw region for style drawers
	 */
	math::frect draw_bound;

	/**
	 * @brief context opacity, currently only for style drawers, should also apply to elements in the future
	 */
	float opacity_scl;
	fx::layer_param layer_param;

	constexpr bool is_draw_allowed() const noexcept{
		return opacity_scl >= 0 && current_subject != nullptr && !draw_bound.is_roughly_zero_area(0.01f);
	}

	explicit constexpr operator bool() const noexcept{
		return is_draw_allowed();
	}
};

export
using draw_call_stack = function_call_stack<draw_call_param>;

export
using draw_recorder = draw_call_stack::function_call_stack_builder;


}
