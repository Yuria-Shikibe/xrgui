module;

#include <cassert>

export module mo_yanxi.gui.default_config.scene;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.image_regions;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.default_config.assets;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.style.palette;


import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.progress_bar;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.style.tree.draw;
import mo_yanxi.gui.default_config.round_styles;

import std;
namespace mo_yanxi::gui{
namespace fx{
export
struct layer_config{
	pipeline_config begin_config;
	std::optional<blit_pipeline_config> end_config;

	//TODO pre/post draw function?
};

/**
 * @brief Note that the render pass has nothing to do with VkRenderPass,
 * it's only an abstraction name for layer pass draw.
 */
export
struct scene_render_pass_config{
	using value_type = layer_config;

private:
	std::array<value_type, draw_pass_max_capacity> masks{};

	std::optional<blit_pipeline_config> tail_blit{};

	unsigned pass_count{};

public:
	constexpr scene_render_pass_config() = default;

	constexpr scene_render_pass_config(std::initializer_list<value_type> masks,
	                                   std::optional<blit_pipeline_config> tail_blit) : tail_blit(tail_blit),
		pass_count(masks.size()){
		std::ranges::copy(masks, this->masks.begin());
	}

	inline constexpr const value_type& operator[](unsigned idx) const noexcept{
		assert(idx < draw_pass_max_capacity);
		return masks[idx];
	}

	inline constexpr unsigned size() const noexcept{
		return pass_count;
	}

	inline constexpr void resize(unsigned sz){
		if(sz >= masks.max_size()){
			throw std::bad_array_new_length();
		}

		pass_count = sz;
	}

	inline constexpr void push_back(const value_type& mask){
		if(pass_count >= masks.max_size()){
			throw std::bad_array_new_length();
		}

		masks[pass_count] = mask;
		pass_count++;
	}

	inline constexpr auto begin(this auto& self) noexcept{
		return self.masks.begin();
	}

	inline constexpr auto end(this auto& self) noexcept{
		return self.masks.begin() + self.size();
	}

	std::optional<blit_pipeline_config> get_tail_blit() const noexcept{
		return tail_blit;
	}
};
}
}

namespace mo_yanxi::gui::example{
export
struct example_scene : scene{
	fx::scene_render_pass_config pass_config{};
	draw_call_stack call_stack_regular_;
	std::vector<draw_call_stack> call_stack_tooltip_;
	std::vector<draw_call_stack> call_stack_overlay_;

	elem_ptr root_{};

	template <std::derived_from<elem> T, typename... Args>
	[[nodiscard]] explicit(false) example_scene(
		scene_resources& resources,
		renderer_frontend&& renderer,
		std::in_place_type_t<T>,
		Args&&... args
	) : scene(resources, std::move(renderer)),
	    root_(static_cast<scene&>(*this), nullptr, std::in_place_type<T>, std::forward<Args>(args)...){
		input_handler_.inputs_.main_binds.set_context(std::ref(static_cast<scene&>(*this)));
		scene_root_ = root_.get();
		init_root();
	}

protected:
	void draw_at(math::frect clipspace, draw_call_stack& elem);

	void draw_impl(rect clip) override;
};

struct image_cursor : style::cursor{
	gui::constant_image_region_borrow icon_region;

	[[nodiscard]] explicit image_cursor(const gui::constant_image_region_borrow& icon_region)
		: icon_region(icon_region){
	}

	rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
	          std::span<const elem* const> inbound_stack) const override{
		region.src -= region.extent * .5f;

		region.expand({mo_yanxi::graphic::msdf::sdf_image_boarder + 6, mo_yanxi::graphic::msdf::sdf_image_boarder + 6});
		state_guard g{renderer, gui::fx::batch_draw_mode::msdf};
		renderer << graphic::draw::instruction::rect_aabb{
				.generic = {icon_region->view},
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.uv00 = icon_region->uv.v00(),
				.uv11 = icon_region->uv.v11(),
				.vert_color = {graphic::colors::white}
			};

		return {tags::from_extent, region.src, region.extent};
	}
};

export
void set_cursors(scene& scene);

export
struct make_style_result{
	using node_type = react_flow::provider_cached<style::palette>;
	react_flow::node_pointer front_ptr;
	react_flow::node_pointer back_ptr;

	node_type& front() const noexcept{
		return static_cast<node_type&>(*front_ptr);
	}

	node_type& back() const noexcept{
		return static_cast<node_type&>(*back_ptr);
	}

	void add_to_scene(scene& s) const{
		s.request_embedded_react_node(s.root(), auto{front_ptr});
		s.request_embedded_react_node(s.root(), auto{back_ptr});
	}
};

export
make_style_result make_styles(scene_resources& scene);
}
