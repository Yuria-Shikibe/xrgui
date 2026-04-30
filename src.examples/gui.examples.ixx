module;

#include <cassert>

export module mo_yanxi.gui.examples;


import std;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.backend.vulkan.context;
import mo_yanxi.react_flow;



namespace mo_yanxi::gui::example{


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
