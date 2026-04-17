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

	std::vector<attachment_state> mask_attachment_states_;

	static void ensure_state(vk::cmd::dependency_gen& dep, attachment_state& state, VkImage image,
							 VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access, VkImageLayout next_layout,
							 std::uint32_t baseArrayLayer = 0 /* 【新增】控制只对特定图层施加 Barrier */){
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

		// 【修改】配置目标 Subresource Range，限制仅操作传入的 Layer
		VkImageSubresourceRange range = vk::image::default_image_subrange;
		range.baseArrayLayer = baseArrayLayer;
		range.layerCount = 1;

		dep.push(image, state.stage_mask, state.access_mask, next_stage, next_access, state.layout, next_layout,
				 range);

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
		// 【新增】重置 Mask 状态
		for(auto& s : mask_attachment_states_){
			s = {
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				VK_ACCESS_2_NONE,
				VK_IMAGE_LAYOUT_UNDEFINED
			};
		}
		std::ranges::fill(blit_attachment_sync_states_, blit_sync_state::none);
	}




	bool sync_draw_targets(
		vk::cmd::dependency_gen& dep,

		const gui::fx::render_target_mask target_mask,
		std::uint32_t absolute_input_bits,
		std::span<const vk::combined_image> draw_attachments,

		mask_usage mask_usage_,
		std::uint32_t mask_depth,
		VkImage mask_image
		){

		bool layout_transition_required = false;

        // 【新增】按需动态扩充 Mask 层状态数组
		if (mask_usage_ != mask_usage::ignore) {
			if (mask_attachment_states_.size() <= mask_depth) {
				mask_attachment_states_.resize(mask_depth + 1, {
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
					VK_ACCESS_2_NONE,
					VK_IMAGE_LAYOUT_UNDEFINED
				});
			}
		}

        // 【规则 1】如果当前在写 Mask，则无视 gui::fx::render_target_mask，跳过常规附件同步
		if (mask_usage_ != mask_usage::write) {
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
		}

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

        // 【新增】处理 Mask Image 的格式转换与屏障
		if (mask_image != VK_NULL_HANDLE) {
			if (mask_usage_ == mask_usage::write) {
				// 写入时，当前层 (mask_depth) 应当是 ColorAttachmentOptimal
				VkPipelineStageFlags2 write_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				VkAccessFlags2 write_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
				VkImageLayout write_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				if(mask_attachment_states_[mask_depth].layout != write_layout) layout_transition_required = true;

				this->ensure_state(dep, mask_attachment_states_[mask_depth], mask_image, write_stage, write_access, write_layout, mask_depth);

				// 并且绘制需要读取 mask_depth - 1 层 (当深度 > 0 时)
				if (mask_depth > 0) {
					std::uint32_t read_layer = mask_depth - 1;
					VkPipelineStageFlags2 read_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
					VkAccessFlags2 read_access = VK_ACCESS_2_SHADER_READ_BIT;
					VkImageLayout read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					if(mask_attachment_states_[read_layer].layout != read_layout) layout_transition_required = true;

					this->ensure_state(dep, mask_attachment_states_[read_layer], mask_image, read_stage, read_access, read_layout, read_layer);
				}
			} else if (mask_usage_ == mask_usage::read) {
				// 【修复】读取时，应当读取当前正在发生采样的 mask_depth 层。
				// 删除了原本对 mask_depth - 1 的错误推断，以对齐 cmd_draw_ 中的实际采样层数。
				std::uint32_t read_layer = mask_depth;
				VkPipelineStageFlags2 read_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				VkAccessFlags2 read_access = VK_ACCESS_2_SHADER_READ_BIT;
				VkImageLayout read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				if(mask_attachment_states_[read_layer].layout != read_layout) layout_transition_required = true;
				this->ensure_state(dep, mask_attachment_states_[read_layer], mask_image, read_stage, read_access, read_layout, read_layer);
			}
		}

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
