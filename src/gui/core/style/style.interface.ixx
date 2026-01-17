module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.interface;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.graphic.color;
export import mo_yanxi.referenced_ptr;
export import mo_yanxi.gui.gfx_config;

import std;

namespace mo_yanxi::gui{

export
struct style_config{
	static constexpr std::size_t max_mask_width = 32;
	/**
	 * @brief Empty stands for dynamic
	 */
	std::bitset<max_mask_width> used_layer{};

	FORCE_INLINE constexpr bool has_layer(const gfx_config::layer_param& param) const noexcept{
		if(used_layer.none()) [[unlikely]] {
			return true;
		}else [[likely]] {
			return used_layer[param.layer_index];
		}
	}
};

namespace style{
export constexpr style_config layer_top_only{{0b1}};

export
template <unsigned Count>
	requires (Count < style_config::max_mask_width && Count > 0)
constexpr style_config layer_draw_until{{(1uz << Count) - 1uz}};
}

export
template <typename T>
struct style_drawer : referenced_object_persistable{
public:
	style_config config;

	virtual ~style_drawer() = default;

	[[nodiscard]] constexpr style_drawer() = default;

	[[nodiscard]] constexpr explicit style_drawer(const tags::persistent_tag_t& persistent_tag)
		: referenced_object_persistable(persistent_tag){
	}

	constexpr explicit style_drawer(const style_config& config)
		: config(config){
	}

	constexpr style_drawer(const tags::persistent_tag_t& persistent_tag, const style_config& config)
		: referenced_object_persistable(persistent_tag),
		config(config){
	}

	// virtual void draw(const T& element, math::frect region, float opacityScl) const = 0;

	FORCE_INLINE void draw_layer(const T& element,
		math::frect region, float opacityScl,
		gfx_config::layer_param layer_param) const{
		if(config.has_layer(layer_param)){
			this->draw_layer_impl(element, region, opacityScl, layer_param);
		}
	}

	[[nodiscard]] virtual float content_opacity(const T& element) const{
		return 1.0f;
	}

	/**
	 * @return true if element changed
	 */
	virtual bool apply_to(T& element) const{
		return false;
	}

protected:

	virtual void draw_layer_impl(
		const T& element,
		math::frect region, float opacityScl,
		gfx_config::layer_param layer_param
	) const = 0;

};

export
template <typename T>
using style_ptr = referenced_ptr<const style_drawer<T>>;
}
