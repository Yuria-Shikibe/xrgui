module;

#include <cassert>
#include <vulkan/vulkan.h>

export module mo_yanxi.backend.vulkan.renderer.components;

import std;

import mo_yanxi.backend.vulkan.attachment_manager;
import mo_yanxi.backend.vulkan.pipeline_manager;
import mo_yanxi.graphic.g2d.batch.backend.vulkan;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.record_context;
import mo_yanxi.vk.sync_processor;
import mo_yanxi.vk.util;

namespace mo_yanxi::backend::vulkan{
export
[[nodiscard]] constexpr vk::sync::image_state make_renderer_image_state(
	const VkPipelineStageFlags2 stage_mask,
	const VkAccessFlags2 access_mask,
	const VkImageLayout layout) noexcept{
	return vk::sync::image_state{{stage_mask, access_mask}, layout};
}

export
constexpr vk::sync::image_state renderer_draw_write_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

export
constexpr vk::sync::image_state renderer_draw_sample_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	VK_ACCESS_2_SHADER_READ_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

export
constexpr vk::sync::image_state renderer_blit_rw_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
	VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

export
constexpr vk::sync::image_state renderer_blit_sample_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
	VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

export
constexpr vk::sync::image_state renderer_blit_write_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
	VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

export
constexpr vk::sync::image_state renderer_mask_write_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
);

export
constexpr vk::sync::image_state renderer_mask_read_state = mo_yanxi::backend::vulkan::make_renderer_image_state(
	VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	VK_ACCESS_2_SHADER_READ_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

export
[[nodiscard]] constexpr VkImageSubresourceRange make_renderer_mask_layer_range(
	const std::uint32_t layer) noexcept{
	return {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = layer,
		.layerCount = 1,
	};
}

export
struct renderer_frame_data{
	vk::fence fence{};
	vk::command_buffer main_command_buffer{};
	VkFence external_submit_fence{};

	[[nodiscard]] renderer_frame_data() = default;

	[[nodiscard]] renderer_frame_data(VkDevice device, VkCommandPool pool)
		: fence(device, true),
		  main_command_buffer(device, pool){
	}
};

export
template <std::size_t FramesInFlight>
class renderer_frame_ring{
private:
	std::array<renderer_frame_data, FramesInFlight> frames_{};
	std::uint32_t current_frame_index_{};

public:
	[[nodiscard]] renderer_frame_ring() = default;

	void initialize(const VkDevice device, const VkCommandPool pool){
		for(auto& frame : frames_){
			frame = renderer_frame_data(device, pool);
		}
		current_frame_index_ = 0;
	}

	void advance() noexcept{
		current_frame_index_ = (current_frame_index_ + 1) % FramesInFlight;
	}

	[[nodiscard]] std::uint32_t current_index() const noexcept{
		return current_frame_index_;
	}

	[[nodiscard]] renderer_frame_data& current_frame() noexcept{
		return frames_[current_frame_index_];
	}

	[[nodiscard]] const renderer_frame_data& current_frame() const noexcept{
		return frames_[current_frame_index_];
	}

	[[nodiscard]] VkCommandBuffer current_command_buffer() const noexcept{
		return this->current_frame().main_command_buffer;
	}

	[[nodiscard]] vk::fence& current_fence() noexcept{
		return this->current_frame().fence;
	}

	[[nodiscard]] const vk::fence& current_fence() const noexcept{
		return this->current_frame().fence;
	}
};

export
/**
 * @brief Compute pass that resolves abstract draw instructions into GPU buffers.
 *
 * When the batch backend has pending resolve work, this helper binds the
 * compute pipeline, loads the descriptor-buffer state for the current frame,
 * dispatches the resolver shader, and inserts the compute-to-draw barrier used
 * by the graphics pass.
 */
class renderer_instruction_resolver{
private:
	vk::pipeline_layout pipeline_layout_{};
	vk::pipeline pipeline_{};

public:
	[[nodiscard]] renderer_instruction_resolver() = default;

	[[nodiscard]] renderer_instruction_resolver(
		const VkDevice device,
		const VkDescriptorSetLayout descriptor_set_layout,
		const VkPipelineShaderStageCreateInfo& shader_stage)
		: pipeline_layout_(device, 0, {descriptor_set_layout}),
		  pipeline_(
			  device,
			  pipeline_layout_,
			  VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			  shader_stage){
	}

	void record_if_needed(
		graphic::g2d::batch_vulkan_executor& batch_device,
		graphic::g2d::record_context<>& descriptor_context,
		const VkCommandBuffer command_buffer,
		const std::uint32_t frame_index) const{
		if(!batch_device.has_gpu_resolve_work()){
			return;
		}

		pipeline_.bind(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);

		descriptor_context.clear();
		batch_device.load_cs_descriptors(descriptor_context, frame_index);
		descriptor_context.prepare_bindings();
		descriptor_context(pipeline_layout_, command_buffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
		batch_device.cmd_compute_resolve(command_buffer, frame_index);
		batch_device.cmd_barrier_compute_to_draw(command_buffer, frame_index);
	}

	[[nodiscard]] const vk::pipeline_layout& pipeline_layout() const noexcept{
		return pipeline_layout_;
	}

	[[nodiscard]] const vk::pipeline& pipeline() const noexcept{
		return pipeline_;
	}
};

export
struct renderer_blit_pipeline_config{
	std::uint32_t pipeline_index{};
	std::uint32_t inout_define_index{std::numeric_limits<std::uint32_t>::max()};

	[[nodiscard]] bool use_default_inouts() const noexcept{
		return inout_define_index == std::numeric_limits<std::uint32_t>::max();
	}
};

export
struct renderer_blit_request{
	static constexpr math::rect_ortho_trivial<int> full_screen_region = {
		{},
		math::vectors::constant2<int>::max_vec2
	};

	math::rect_ortho_trivial<int> blit_region{};
	renderer_blit_pipeline_config pipe_info{};
	bool reserve_original{};

	[[nodiscard]] bool use_default_inouts() const noexcept{
		return pipe_info.use_default_inouts();
	}

	void normalize_region(const VkExtent2D extent) noexcept{
		if(blit_region.extent == math::vectors::constant2<int>::max_vec2){
			assert(blit_region.src.is_zero());
			blit_region.extent.set(extent.width, extent.height);
			return;
		}

		if(blit_region.src.x < 0){
			blit_region.extent.x += blit_region.src.x;
			blit_region.src.x = 0;
			if(blit_region.extent.x < 0){
				blit_region.extent.x = 0;
			}
		}
		if(blit_region.src.y < 0){
			blit_region.extent.y += blit_region.src.y;
			blit_region.src.y = 0;
			if(blit_region.extent.y < 0){
				blit_region.extent.y = 0;
			}
		}
	}

	[[nodiscard]] math::usize2 get_dispatch_groups() const noexcept{
		return (blit_region.extent.as<unsigned>() + math::usize2{15, 15}) / math::usize2{16, 16};
	}
};

export
class renderer_blit_resources{
private:
	vk::allocator_usage allocator_usage_{};
	compute_pipeline_manager pipeline_manager_{};
	std::vector<vk::descriptor_buffer> default_inout_descriptors_{};
	std::vector<vk::descriptor_buffer> specified_inout_descriptors_{};

public:
	[[nodiscard]] renderer_blit_resources() = default;

	[[nodiscard]] renderer_blit_resources(
		vk::allocator_usage allocator,
		const compute_pipeline_create_config& create_info){
		this->reset(std::move(allocator), create_info);
	}

	void reset(vk::allocator_usage allocator, const compute_pipeline_create_config& create_info){
		allocator_usage_ = std::move(allocator);
		pipeline_manager_ = compute_pipeline_manager(allocator_usage_, create_info);

		default_inout_descriptors_.clear();
		const auto pipes = pipeline_manager_.get_pipelines();
		default_inout_descriptors_.reserve(pipes.size());

		for(std::size_t i = 0; i < pipes.size(); ++i){
			const auto& layout = pipeline_manager_.get_inout_layouts()[i];
			default_inout_descriptors_.emplace_back(allocator_usage_, layout, layout.binding_count());
		}

		specified_inout_descriptors_.clear();
		const auto inouts = pipeline_manager_.get_inout_defines();
		specified_inout_descriptors_.reserve(inouts.size());

		for(const auto& inout : inouts){
			vk::descriptor_layout layout{
				allocator_usage_.get_device(),
				VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
				inout.make_layout_builder()
			};
			specified_inout_descriptors_.emplace_back(allocator_usage_, layout, layout.binding_count());
		}
	}

	void update_descriptors(const attachment_manager& attachments){
		auto update_descriptor = [&attachments](vk::descriptor_buffer& descriptor_buffer,
			const compute_pipeline_blit_inout_config& config){
			vk::descriptor_mapper mapper{descriptor_buffer};
			for(const auto& in : config.get_input_entries()){
				(void)mapper.set_image(in.binding, {
					nullptr,
					attachments.get_draw_attachments()[in.resource_index].get_image_view(),
					VK_IMAGE_LAYOUT_GENERAL
				}, 0, in.type);
			}
			for(const auto& out : config.get_output_entries()){
				(void)mapper.set_image(out.binding, {
					nullptr,
					attachments.get_blit_attachments()[out.resource_index].get_image_view(),
					VK_IMAGE_LAYOUT_GENERAL
				}, 0, out.type);
			}
		};

		for(auto&& [descriptor_buffer, pipe] : std::views::zip(
			default_inout_descriptors_,
			pipeline_manager_.get_pipelines())){
			update_descriptor(descriptor_buffer, pipe.option.inout);
		}
		for(auto&& [descriptor_buffer, inout] : std::views::zip(
			specified_inout_descriptors_,
			pipeline_manager_.get_inout_defines())){
			update_descriptor(descriptor_buffer, inout);
		}
	}

	void record(
		const attachment_manager& attachments,
		vk::sync::sync_processor& sync_processor,
		vk::sync::sync_barrier_batch& barrier_batch,
		std::span<const vk::sync::image_slot> draw_attachment_slots,
		std::span<const vk::sync::image_slot> blit_attachment_slots,
		graphic::g2d::record_context<>& descriptor_context,
		renderer_blit_request request,
		const VkCommandBuffer command_buffer) const{
		request.normalize_region(attachments.get_extent());

		const auto& inout = this->get_inout_config(request);

		barrier_batch.clear();
		for(const auto& entry : inout.get_input_entries()){
			(void)sync_processor.transition_image(
				barrier_batch,
				draw_attachment_slots[entry.resource_index],
				attachments.get_draw_attachments()[entry.resource_index].get_image(),
				entry.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
					? renderer_blit_sample_state
					: renderer_blit_rw_state
			);
		}
		for(const auto& entry : inout.get_output_entries()){
			(void)sync_processor.transition_image(
				barrier_batch,
				blit_attachment_slots[entry.resource_index],
				attachments.get_blit_attachments()[entry.resource_index].get_image(),
				renderer_blit_rw_state
			);
		}
		barrier_batch.apply(command_buffer);

		const auto& pipeline_data = pipeline_manager_.get_pipelines()[request.pipe_info.pipeline_index];
		pipeline_data.pipeline.bind(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);

		const math::upoint2 offset = request.blit_region.src.as<unsigned>();
		vkCmdPushConstants(
			command_buffer,
			pipeline_data.pipeline_layout,
			VK_SHADER_STAGE_COMPUTE_BIT,
			0,
			sizeof(offset),
			&offset);

		const VkDescriptorBufferBindingInfoEXT descriptor_info =
			request.use_default_inouts()
				? default_inout_descriptors_[request.pipe_info.pipeline_index]
				: specified_inout_descriptors_[request.pipe_info.inout_define_index];

		descriptor_context.clear();
		descriptor_context.push(0, descriptor_info);

		pipeline_manager_.append_descriptor_buffers(descriptor_context, request.pipe_info.pipeline_index);
		descriptor_context.prepare_bindings();
		descriptor_context(pipeline_data.pipeline_layout, command_buffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);

		const auto dispatches = request.get_dispatch_groups();
		if(dispatches.area() > 0){
			vkCmdDispatch(command_buffer, dispatches.x, dispatches.y, 1);
		}

		for(const auto& entry : inout.get_output_entries()){
			sync_processor.set_image_state(blit_attachment_slots[entry.resource_index], renderer_blit_write_state);
		}
	}

	[[nodiscard]] const compute_pipeline_blit_inout_config& get_inout_config(
		const renderer_blit_request& request) const{
		return request.use_default_inouts()
			       ? pipeline_manager_.get_pipelines()[request.pipe_info.pipeline_index].option.inout
			       : pipeline_manager_.get_inout_defines()[request.pipe_info.inout_define_index];
	}

	[[nodiscard]] compute_pipeline_manager& pipeline_manager() noexcept{
		return pipeline_manager_;
	}

	[[nodiscard]] const compute_pipeline_manager& pipeline_manager() const noexcept{
		return pipeline_manager_;
	}
};

export
void record_renderer_attachment_clear_and_init_command(
	const VkCommandBuffer command_buffer,
	const attachment_manager& attachments){
	vk::cmd::dependency_gen dependency{};

	for(const auto& image : attachments.get_blit_attachments()){
		dependency.push(
			image.get_image(),
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_CLEAR_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			vk::image::default_image_subrange);
	}
	dependency.apply(command_buffer);

	VkClearColorValue black{};

	for(const auto& image : attachments.get_blit_attachments()){
		vkCmdClearColorImage(
			command_buffer,
			image.get_image(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&black,
			1,
			&vk::image::default_image_subrange);
		dependency.push(
			image.get_image(),
			VK_PIPELINE_STAGE_2_CLEAR_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			vk::image::default_image_subrange);
	}

	for(const auto& image : attachments.get_draw_attachments()){
		dependency.push(
			image.get_image(),
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			vk::image::default_image_subrange);
	}

	for(const auto& image : attachments.get_multisample_attachments()){
		dependency.push(
			image.get_image(),
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			vk::image::default_image_subrange);
	}
	dependency.apply(command_buffer);
}
}
