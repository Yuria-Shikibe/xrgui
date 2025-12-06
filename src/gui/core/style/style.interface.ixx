//
// Created by Matrix on 2024/10/7.
//

export module mo_yanxi.gui.style.interface;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.graphic.color;
export import mo_yanxi.referenced_ptr;

import std;

namespace mo_yanxi::gui{
export
template <typename T>
struct style_drawer : referenced_object_persistable{
public:
	virtual ~style_drawer() = default;

	[[nodiscard]] constexpr style_drawer() = default;

	[[nodiscard]] constexpr explicit style_drawer(const tags::persistent_tag_t& persistent_tag)
		: referenced_object_persistable(persistent_tag){
	}

	virtual void draw(const T& element, math::frect region, float opacityScl) const = 0;

	[[nodiscard]] virtual float content_opacity(const T& element) const{
		return 1.0f;
	}

	/**
	 * @return true if element changed
	 */
	virtual bool apply_to(T& element) const{
		return false;
	}

};

export
template <typename T>
using style_ptr = referenced_ptr<const style_drawer<T>>;
}
