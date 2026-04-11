module;

#include <cassert>

export module mo_yanxi.gui.examples;


import std;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.backend.vulkan.context;
import mo_yanxi.react_flow;


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

	constexpr scene_render_pass_config(std::initializer_list<value_type> masks, std::optional<blit_pipeline_config> tail_blit) : tail_blit(tail_blit), pass_count(masks.size()){
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

struct example_scene : scene{
	fx::scene_render_pass_config pass_config{};
	draw_call_stack call_stack_regular_;
	std::vector<draw_call_stack> call_stack_tooltip_;
	std::vector<draw_call_stack> call_stack_overlay_;

	elem_ptr root_{};

	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] explicit(false) example_scene(
		scene_resources& resources,
		renderer_frontend&& renderer,
		std::in_place_type_t<T>,
		Args&& ...args
		) : scene(resources, std::move(renderer)), root_(static_cast<scene&>(*this), nullptr, std::in_place_type<T>, std::forward<Args>(args)...){
		input_handler_.inputs_.main_binds.set_context(std::ref(static_cast<scene&>(*this)));
		scene_root_ = root_.get();
		init_root();
	}

protected:
	void draw_at(math::frect clipspace, draw_call_stack& elem);

	void draw_impl(rect clip) override;
};


export
struct ui_outputs{
	scene* scene_ptr;

	react_flow::node* shader_bloom_scale;
	react_flow::node* shader_bloom_src_factor;
	react_flow::node* shader_bloom_dst_factor;
	react_flow::node* shader_bloom_mix_factor;

	react_flow::node* highlight_filter_threshold;
	react_flow::node* highlight_filter_smooth;

	react_flow::node* tonemap_exposure;
	react_flow::node* tonemap_contrast;
	react_flow::node* tonemap_gamma;
	react_flow::node* tonemap_saturation;

	void apply(scene& scene) const{
		scene.get_input_communicate_async_task_queue().post([*this](gui::scene& s){
			if(shader_bloom_scale)shader_bloom_scale->pull_and_push(false);
			if(shader_bloom_src_factor)shader_bloom_src_factor->pull_and_push(false);
			if(shader_bloom_dst_factor)shader_bloom_dst_factor->pull_and_push(false);
			if(shader_bloom_mix_factor)shader_bloom_mix_factor->pull_and_push(false);
			if(highlight_filter_threshold)highlight_filter_threshold->pull_and_push(false);
			if(highlight_filter_smooth)highlight_filter_smooth->pull_and_push(false);
			if(tonemap_exposure)tonemap_exposure->pull_and_push(false);
			if(tonemap_contrast)tonemap_contrast->pull_and_push(false);
			if(tonemap_gamma)tonemap_gamma->pull_and_push(false);
			if(tonemap_saturation)tonemap_saturation->pull_and_push(false);
		});
	}
};

export
ui_outputs build_main_ui(backend::vulkan::context& ctx, renderer_frontend r);

export
void clear_main_ui();
}
