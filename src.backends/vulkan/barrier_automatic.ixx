module;

#include <vulkan/vulkan.h>
#include <utility>

export module mo_yanxi.backend.vulkan.renderer:barrier_automatic;

import std;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk;
import mo_yanxi.gui.fx.config;
import mo_yanxi.backend.vulkan.pipeline_manager;

namespace mo_yanxi::backend::vulkan{
struct attachment_state{
	VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

enum class blit_sync_state : std::uint8_t{
	none,
	pending_barrier
};

class attachment_sync_manager{
private:
	std::vector<attachment_state> draw_attachment_states_;
	std::vector<attachment_state> blit_attachment_states_;
	std::vector<blit_sync_state> blit_attachment_sync_states_;

	static void ensure_state(vk::cmd::dependency_gen& dep, attachment_state& state, VkImage image,
	                         VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access, VkImageLayout next_layout){
		const bool layout_changed = state.layout != next_layout;
		const bool can_skip_color = !layout_changed &&
			(state.stage_mask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT && next_stage ==
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) &&
			(state.access_mask == (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT) &&
				next_access == (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT));


		if(can_skip_color){
			return;
		}

		auto is_write = [](VkAccessFlags2 a){
			return a & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
				VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
		};

		if(!layout_changed && !is_write(state.access_mask) && !is_write(next_access)){
			return;
		}

		dep.push(image, state.stage_mask, state.access_mask, next_stage, next_access, state.layout, next_layout,
		         vk::image::default_image_subrange);
		state = {next_stage, next_access, next_layout};
	}

public:
	attachment_sync_manager() = default;

	void resize(std::size_t draw_count, std::size_t blit_count){
		draw_attachment_states_.resize(draw_count);
		blit_attachment_states_.resize(blit_count);
		blit_attachment_sync_states_.resize(blit_count, blit_sync_state::none);
	}

	void reset_barriers(){
		for(auto& s : draw_attachment_states_){
			s = {
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,

					VK_IMAGE_LAYOUT_GENERAL
				};
		}
		for(auto& s : blit_attachment_states_){
			s = {
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					VK_IMAGE_LAYOUT_GENERAL
				};
		}
		std::ranges::fill(blit_attachment_sync_states_, blit_sync_state::none);
	}


	bool sync_draw_targets(
		vk::cmd::dependency_gen& dep,
		const gui::fx::render_target_mask target_mask, std::uint32_t absolute_input_bits,
		std::span<const vk::combined_image> draw_attachments){
		bool layout_transition_required = false;


		target_mask.for_each_popbit([&](unsigned idx){
			VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			VkAccessFlags2 access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
			VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;

			if(draw_attachment_states_[idx].layout != layout){
				layout_transition_required = true;
			}

			this->ensure_state(dep, draw_attachment_states_[idx],
			                   draw_attachments[idx].get_image(),
			                   stage, access, layout);
		});


		gui::fx::render_target_mask{absolute_input_bits}.for_each_popbit([&](unsigned idx){
			VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			VkAccessFlags2 access = VK_ACCESS_2_SHADER_READ_BIT;


			VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			if(draw_attachment_states_[idx].layout != layout){
				layout_transition_required = true;
			}

			this->ensure_state(dep, draw_attachment_states_[idx],
			                   draw_attachments[idx].get_image(),
			                   stage, access, layout);
		});

		return layout_transition_required;
	}


	void commit_pending_blit_to_graphics(vk::cmd::dependency_gen& dep,
	                                     std::span<const vk::combined_image> blit_attachments){
		for(std::size_t idx = 0; idx < blit_attachment_sync_states_.size(); ++idx){
			if(blit_attachment_sync_states_[idx] == blit_sync_state::pending_barrier){
				dep.push(blit_attachments[idx].get_image(),
				         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				         VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);
				blit_attachment_sync_states_[idx] = blit_sync_state::none;
			}
		}
	}

	void sync_blit_inputs(vk::cmd::dependency_gen& dep,
	                      std::span<const compute_pipeline_blit_inout_config::entry> inputs,
	                      std::span<const vk::combined_image> draw_attachments){
		for(const auto& e : inputs){
			this->ensure_state(dep, draw_attachment_states_[e.resource_index],
			                   draw_attachments[e.resource_index].get_image(),
			                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
			                   VK_IMAGE_LAYOUT_GENERAL);
		}
	}

	void sync_blit_outputs(vk::cmd::dependency_gen& dep,
	                       std::span<const compute_pipeline_blit_inout_config::entry> outputs,
	                       std::span<const vk::combined_image> blit_attachments){
		for(const auto& e : outputs){
			const auto idx = e.resource_index;
			if(blit_attachment_sync_states_[idx] == blit_sync_state::pending_barrier){
				dep.push(blit_attachments[idx].get_image(),
				         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				         VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);
				blit_attachment_sync_states_[idx] = blit_sync_state::none;
			}
		}
	}

	void mark_blit_completed(std::span<const compute_pipeline_blit_inout_config::entry> outputs){
		for(const auto& e : outputs){
			blit_attachment_sync_states_[e.resource_index] = blit_sync_state::pending_barrier;
		}
	}
};
}
