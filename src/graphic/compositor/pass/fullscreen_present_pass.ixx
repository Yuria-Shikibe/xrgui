module;

#include <vulkan/vulkan.h>

export module mo_yanxi.graphic.compositor.fullscreen_present_pass;

import std;
export import mo_yanxi.graphic.compositor.manager;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

namespace mo_yanxi::graphic::compositor{
export
struct alignas(16) fullscreen_present_params{
	float exposure{1};
	float contrast{1};
	float gamma{1};
	float saturation{1};
};

export
struct fullscreen_present_shader_info{
	vk::shader_module vertex_shader{};
	vk::shader_module fragment_shader{};
	std::string vertex_entry{"main_vert"};
	std::string fragment_entry{"main_frag"};
};

export
struct fullscreen_present_stage final : pass_impl{
private:
	fullscreen_present_shader_info shaders_{};
	VkFormat output_format_{VK_FORMAT_R8G8B8A8_UNORM};

	pass_logical_socket sockets_{};

	vk::descriptor_layout descriptor_layout_{};
	vk::pipeline_layout pipeline_layout_{};
	vk::pipeline pipeline_{};
	vk::descriptor_buffer descriptor_buffer_{};
	vk::uniform_buffer uniform_buffer_{};
	VkDeviceSize uniform_frame_stride_{};
	std::uint32_t frame_count_{1};

	fullscreen_present_params params_{};

public:
	[[nodiscard]] fullscreen_present_stage() = default;

	[[nodiscard]] fullscreen_present_stage(fullscreen_present_shader_info&& shaders, const VkFormat output_format)
		: shaders_(std::move(shaders)),
		  output_format_(output_format){
		init_sockets();
	}

	void set_output_format(const VkFormat format){
		output_format_ = format;
		if(sockets_.has_output(0)){
			sockets_.at_out<image_requirement>(0).format = format;
		}
	}

	template <typename T>
	void set_ubo_value(T fullscreen_present_params::* mptr, const T& value){
		auto& current = params_.*mptr;
		if constexpr (std::equality_comparable<T>){
			if(current == value){
				return;
			}
		}

		current = value;
		write_params_to_all_frames();
	}

	[[nodiscard]] const fullscreen_present_params& params() const noexcept{
		return params_;
	}

private:
	void init_sockets(){
		sockets_ = {};
		sockets_.add(true, false, 0, resource_requirement{
			.req = image_requirement{
				.sampled = true,
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			},
			.access = access_flag::read,
			.last_used_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		});
		sockets_.add(false, true, 0, resource_requirement{
			.req = image_requirement{
				.format = output_format_,
				.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				.override_initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.override_output_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			},
			.access = access_flag::write,
			.last_used_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		});
	}

	void prepare(const vk::allocator_usage& allocator,
		const pass_data& pass,
		const math::u32size2 extent,
		const std::uint32_t frame_count) override{
		(void)extent;
		frame_count_ = std::max(1u, frame_count);

		init_sockets();
		init_descriptor_layout(allocator);
		init_uniform_buffer(allocator);
		init_pipeline(allocator);
		bind_descriptors(pass);
		write_params_to_all_frames();
	}

	[[nodiscard]] const pass_logical_socket& sockets() const noexcept override{
		return sockets_;
	}

	[[nodiscard]] std::string_view get_name() const noexcept override{
		return "post_process.fullscreen_present";
	}

	void record_command(
		const vk::allocator_usage& allocator,
		const pass_data& pass,
		math::u32size2 extent,
		VkCommandBuffer buffer,
		const std::uint32_t frame_slot) override{
		(void)allocator;
		const auto output = pass.get_used_resources(frame_slot).get_out(0).as_image();

		pipeline_.bind(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS);
		descriptor_buffer_.bind_chunk_to(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, frame_slot);

		const VkRect2D area{
			.offset = {},
			.extent = {extent.x, extent.y},
		};
		vk::cmd::set_viewport(buffer, area);
		vk::cmd::set_scissor(buffer, area);

		vk::dynamic_rendering rendering{};
		rendering.push_color_attachment(
			output.handle.image_view,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_STORE);
		rendering.begin_rendering(buffer, area);
		vkCmdDraw(buffer, 3, 1, 0, 0);
		vkCmdEndRendering(buffer);
	}

	void init_descriptor_layout(const vk::allocator_usage& allocator){
		vk::descriptor_layout_builder builder{};
		builder.push(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
		builder.push(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
		descriptor_layout_ = vk::descriptor_layout{
			allocator.get_device(),
			VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			builder
		};
	}

	void init_uniform_buffer(const vk::allocator_usage& allocator){
		const auto alignment = vk::get_device_requirement(allocator.get_device())->min_uniform_buffer_offset_alignment;
		uniform_frame_stride_ = vk::align_up<VkDeviceSize>(sizeof(fullscreen_present_params), alignment);
		uniform_buffer_ = vk::uniform_buffer{allocator, uniform_frame_stride_ * frame_count_};
	}

	void init_pipeline(const vk::allocator_usage& allocator){
		pipeline_layout_ = vk::pipeline_layout{allocator.get_device(), 0, {descriptor_layout_}};

		vk::graphic_pipeline_template pipeline_template{};
		pipeline_template.set_vertex_info(VkPipelineVertexInputStateCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.vertexBindingDescriptionCount = 0,
			.pVertexBindingDescriptions = nullptr,
			.vertexAttributeDescriptionCount = 0,
			.pVertexAttributeDescriptions = nullptr,
		});
		pipeline_template.push_color_attachment_format(output_format_);
		pipeline_template.push_color_attachment_blend_state(vk::blending::overwrite);
		pipeline_template.set_shaders(vk::shader_chain{
			shaders_.vertex_shader.get_create_info(VK_SHADER_STAGE_VERTEX_BIT, shaders_.vertex_entry),
			shaders_.fragment_shader.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, shaders_.fragment_entry),
		});

		pipeline_ = vk::pipeline{
			allocator.get_device(),
			pipeline_layout_,
			VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			pipeline_template
		};
	}

	void bind_descriptors(const pass_data& pass){
		descriptor_buffer_ = vk::descriptor_buffer{
			uniform_buffer_.get_allocator(),
			descriptor_layout_,
			descriptor_layout_.binding_count(),
			frame_count_
		};

		vk::descriptor_mapper mapper{descriptor_buffer_};
		for(std::uint32_t frame_slot = 0; frame_slot < frame_count_; ++frame_slot){
			const auto input = pass.get_used_resources(frame_slot).get_in(0).as_image();
			mapper.set_image(
				0,
				input.handle.image_view,
				frame_slot,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_NULL_HANDLE,
				VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
			mapper.set_uniform_buffer(
				1,
				uniform_buffer_.get_address() + uniform_frame_stride_ * frame_slot,
				sizeof(fullscreen_present_params),
				frame_slot);
		}
	}

	void write_params_to_all_frames(){
		if(!uniform_buffer_){
			return;
		}

		vk::buffer_mapper mapper{uniform_buffer_};
		for(std::uint32_t frame_slot = 0; frame_slot < frame_count_; ++frame_slot){
			mapper.load(params_, uniform_frame_stride_ * frame_slot);
		}
	}
};
}
