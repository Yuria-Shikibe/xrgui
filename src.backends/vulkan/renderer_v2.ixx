module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

export module mo_yanxi.backend.vulkan.renderer;

import mo_yanxi.graphic.draw.instruction.batch.frontend;
import mo_yanxi.graphic.draw.instruction.batch.backend.vulkan;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.fx.config;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.backend.vulkan.pipeline_manager;
import std;

namespace mo_yanxi::backend::vulkan {

/** 预定义数据布局表 */
const graphic::draw::data_layout_table table{
    std::in_place_type<gui::gui_reserved_user_data_tuple>
};

const graphic::draw::data_layout_table table_non_vertex{
    std::in_place_type<std::tuple<gui::fx::ui_state, gui::fx::slide_line_config>>
};

export struct renderer_create_info {
    vk::allocator_usage allocator_usage;
    VkCommandPool command_pool;
    VkSampler sampler;
    draw_attachment_create_info attachment_draw_config;
    blit_attachment_create_info attachment_blit_config;
    graphic_pipeline_create_config draw_pipe_config;
    compute_pipeline_create_config blit_pipe_config;
};

/** 内部辅助：跟踪 Attachment 的当前状态以进行自动屏障转换 */
struct attachment_state {
    VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

export struct renderer {
    static constexpr std::size_t frames_in_flight = 3;

private:
	vk::allocator_usage allocator_usage_{};

public:
    /** 绘图指令批处理器 */
    graphic::draw::instruction::draw_list_context batch_host{};
    graphic::draw::instruction::batch_vulkan_executor batch_device{};

private:

	attachment_manager attachment_manager_{};
	vk::dynamic_rendering rendering_config_{};

	graphic_pipeline_manager draw_pipeline_manager_{};

	compute_pipeline_manager blit_pipeline_manager_{};
	std::vector<vk::descriptor_buffer> blit_default_inout_descriptors_{};
	std::vector<vk::descriptor_buffer> blit_specified_inout_descriptors_{};

	vk::command_seq<> command_seq_draw_{};
	vk::command_seq<> command_seq_blit_{};

	//TODO optimize the fence
	struct frame_data {
		vk::fence fence{};
		vk::command_buffer main_command_buffer{};

		frame_data() = default;

		frame_data(VkDevice device, VkCommandPool pool)
			: fence(device, true)
			, main_command_buffer(device, pool)
		{}
	};
	std::array<frame_data, frames_in_flight> frames_{};
	std::uint32_t current_frame_index_{frames_in_flight - 1};

	vk::command_buffer blit_attachment_clear_and_init_command_buffer{};


	std::vector<gui::fx::draw_config> cache_draw_param_stack_{};
	std::vector<std::uint8_t> cache_attachment_enter_mark_{};
	graphic::draw::record_context<> cache_record_context_{};

	std::vector<attachment_state> draw_attachment_states_{};
	std::vector<attachment_state> blit_attachment_states_{};
	vk::cmd::dependency_gen cache_barrier_gen_{};

	VkSampler sampler_{};

public:
	[[nodiscard]] explicit(false) renderer(
		renderer_create_info&& create_info
	)
		: allocator_usage_(create_info.allocator_usage)
		, batch_host([&]{
			VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
			VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &meshProperties};
			vkGetPhysicalDeviceProperties2(allocator_usage_.get_physical_device(), &prop);
			return graphic::draw::instruction::hardware_limit_config{
					.max_group_count = meshProperties.maxTaskWorkGroupCount[0],
					.max_group_size = meshProperties.maxTaskWorkGroupSize[0],
					.max_vertices_per_group = meshProperties.maxMeshOutputVertices,
					.max_primitives_per_group = meshProperties.maxMeshOutputPrimitives
				};
		}(), table, table_non_vertex)
		, batch_device(allocator_usage_, batch_host, frames_in_flight)
		, attachment_manager_{
			allocator_usage_, std::move(create_info.attachment_draw_config), std::move(create_info.attachment_blit_config)
		}
		, draw_pipeline_manager_(allocator_usage_, create_info.draw_pipe_config, batch_device.get_descriptor_set_layout(), attachment_manager_.get_draw_config())
		, command_seq_draw_(allocator_usage_.get_device(), create_info.command_pool, 4,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, command_seq_blit_(allocator_usage_.get_device(), create_info.command_pool, 4,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, sampler_(create_info.sampler){

		for(std::size_t i = 0; i < frames_in_flight; ++i){
			frames_[i] = frame_data(allocator_usage_.get_device(), create_info.command_pool);
		}

		blit_attachment_clear_and_init_command_buffer = vk::command_buffer{allocator_usage_.get_device(), create_info.command_pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY};

		cache_attachment_enter_mark_.resize(attachment_manager_.get_draw_attachments().size());


		blit_pipeline_manager_ = compute_pipeline_manager( allocator_usage_, create_info.blit_pipe_config);


        initialize_frames(create_info.command_pool);
        initialize_blit_resources(create_info);
    }

    /** 窗口缩放时重置附件与描述符绑定 */
    void resize(VkExtent2D extent) {
        attachment_manager_.resize(extent);

        // 更新状态追踪数组大小
        draw_attachment_states_.resize(attachment_manager_.get_draw_attachments().size());
        blit_attachment_states_.resize(attachment_manager_.get_blit_attachments().size());

        auto update_descriptor = [this](vk::descriptor_buffer& db, const compute_pipeline_blit_inout_config& cfg) {
            vk::descriptor_mapper mapper{db};
            for (const auto& in : cfg.get_input_entries()) {
                (void)mapper.set_image(in.binding, {nullptr, attachment_manager_.get_draw_attachments()[in.resource_index].get_image_view(), VK_IMAGE_LAYOUT_GENERAL}, 0, in.type);
            }
            for (const auto& out : cfg.get_output_entries()) {
                (void)mapper.set_image(out.binding, {nullptr, attachment_manager_.get_blit_attachments()[out.resource_index].get_image_view(), VK_IMAGE_LAYOUT_GENERAL}, 0, out.type);
            }
        };

        // 重新绑定所有 Blit 相关的描述符
        for (auto&& [db, pipe] : std::views::zip(blit_default_inout_descriptors_, blit_pipeline_manager_.get_pipelines())) {
            update_descriptor(db, pipe.option.inout);
        }
        for (auto&& [db, inout] : std::views::zip(blit_specified_inout_descriptors_, blit_pipeline_manager_.get_inout_defines())) {
            update_descriptor(db, inout);
        }

        create_blit_clear_and_init_cmd();
    }

    /** 上传渲染数据并切换帧上下文 */
    void upload() {
        current_frame_index_ = (current_frame_index_ + 1) % frames_in_flight;
        try {
            frames_[current_frame_index_].fence.wait_and_reset();
            batch_device.upload(batch_host, sampler_, current_frame_index_);
        } catch (...) {
            frames_[current_frame_index_].fence.reset();
        }
    }

    /** 录制当前帧的主指令缓冲 */
    void create_command() {
        vk::scoped_recorder recorder{frames_[current_frame_index_].main_command_buffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        record_cmd(recorder);
    }

    gui::renderer_frontend create_frontend() noexcept {
        return gui::renderer_frontend{table, table_non_vertex, {
            *this,
            [](renderer& r, auto h, const std::byte* d) static { return r.batch_host.push_instr(h, d); },
            [](renderer&) static {},
        	[](renderer&) static {},
            [](renderer& r, auto cfg, auto f, auto p) static { r.batch_host.push_state(cfg, f, p); }
        }};
    }

    VkCommandBuffer get_valid_cmd_buf() const noexcept { return frames_[current_frame_index_].main_command_buffer; }
    VkFence get_fence() const noexcept { return frames_[current_frame_index_].fence; }

	vk::image_handle get_base() const noexcept{
    	return attachment_manager_.get_blit_attachments()[0];
    }

	vk::image_handle get_draw_base() const noexcept{
    	return attachment_manager_.get_blit_attachments()[0];
    }

private:
    /** 核心录制逻辑：遍历指令流并处理状态切换 */
    void record_cmd(VkCommandBuffer cmd) {
        vkCmdExecuteCommands(cmd, 1, blit_attachment_clear_and_init_command_buffer.as_data());
        reset_barrier_context();

        const auto cmd_sz = batch_host.get_submit_sections_count();
        if (batch_host.get_valid_submit_groups().empty()) return;

        std::ranges::fill(cache_attachment_enter_mark_, false);
        cache_draw_param_stack_.assign(1, {.pipeline_index = {}});

        bool is_rendering = false;
        gui::fx::render_target_mask current_pass_mask{};
        bool current_pass_msaa = false;
        const bool global_msaa = attachment_manager_.enables_multisample();

        auto flush_pass = [&]() {
            if (is_rendering) {
                vkCmdEndRendering(cmd);
                is_rendering = false;
            }
        };

        for (unsigned i = 0; i < cmd_sz; ++i) {
            const auto& draw_cfg = get_current_draw_config();
            const auto& pipe_opt = draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
            const bool is_msaa = pipe_opt.enables_multisample && global_msaa;
            const auto target_mask = get_current_target(draw_cfg);

            // 1. 自动同步检查：如果目标 Attachment 状态改变，则生成屏障
            cache_barrier_gen_.clear();
            target_mask.for_each_popbit([&](unsigned idx) {
                ensure_attachment_state(cache_barrier_gen_, draw_attachment_states_[idx],
                    attachment_manager_.get_draw_attachments()[idx].get_image(),
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            });

            // 2. 渲染通道切换判断：如果屏障生效、目标变更或 MSAA 切换，则必须重启 Rendering
            if (is_rendering && (!cache_barrier_gen_.empty() || target_mask != current_pass_mask || is_msaa != current_pass_msaa)) {
                flush_pass();
            }

            if (!cache_barrier_gen_.empty()) {
                cache_barrier_gen_.apply(cmd);
            }

            // 3. 开始新的 Rendering Pass
            if (!is_rendering) {
                configure_rendering_info(draw_cfg);
                std::size_t slot = 0;
                target_mask.for_each_popbit([&](unsigned idx) {
                    auto& info = rendering_config_.get_color_attachment_infos()[slot++];
                    info.loadOp = cache_attachment_enter_mark_[idx] ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
                    cache_attachment_enter_mark_[idx] = true;
                });
                rendering_config_.begin_rendering(cmd, attachment_manager_.get_screen_area());
                is_rendering = true;
                current_pass_mask = target_mask;
                current_pass_msaa = is_msaa;
            }

            // 4. 执行 Draw Call
            record_draw_call(cmd, i, draw_cfg);

            // 5. 处理断点（如管线变更或触发 Blit）
            bool requires_clear = batch_host.get_break_config_at(i).clear_draw_after_break;
            for (const auto& entry : batch_host.get_break_config_at(i).get_entries()) {
                requires_clear |= process_breakpoints(entry, cmd, flush_pass);
            }

            if (requires_clear) {
                flush_pass();
                std::ranges::fill(cache_attachment_enter_mark_, 0);
            }
        }
        flush_pass();
    }

    void record_draw_call(VkCommandBuffer cmd, std::uint32_t index, const gui::fx::draw_config& arg) {
        auto& pc = draw_pipeline_manager_.get_pipelines()[arg.pipeline_index];
        pc.pipeline.bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

        struct { std::uint32_t flag; } push{ std::to_underlying(arg.mode) };
        vkCmdPushConstants(cmd, pc.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

        const VkRect2D area = attachment_manager_.get_screen_area();
        vk::cmd::set_viewport(cmd, area);
        vk::cmd::set_scissor(cmd, area);

        cache_record_context_.clear();
        batch_device.load_descriptors(cache_record_context_, current_frame_index_);
        cache_record_context_.prepare_bindings();
        cache_record_context_(pc.pipeline_layout, cmd, index, VK_PIPELINE_BIND_POINT_GRAPHICS);

        batch_device.cmd_draw(cmd, index, current_frame_index_);
    }

    /** 处理 Blit 操作：Compute Shader 实现的图像处理 */
    void blit(gui::fx::blit_config cfg, VkCommandBuffer cmd) {
        cfg.get_clamped_to_positive();
        const auto& inout = get_blit_inout_config(cfg);

        // 自动处理读写屏障
        cache_barrier_gen_.clear();
        for (const auto& e : inout.get_input_entries()) {
            ensure_attachment_state(cache_barrier_gen_, draw_attachment_states_[e.resource_index], attachment_manager_.get_draw_attachments()[e.resource_index].get_image(),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
        }
        for (const auto& e : inout.get_output_entries()) {
            ensure_attachment_state(cache_barrier_gen_, blit_attachment_states_[e.resource_index], attachment_manager_.get_blit_attachments()[e.resource_index].get_image(),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
        }
        cache_barrier_gen_.apply(cmd);

        auto& pc = blit_pipeline_manager_.get_pipelines()[cfg.pipe_info.pipeline_index];
        pc.pipeline.bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

        math::upoint2 offset = cfg.blit_region.src.as<unsigned>();
        vkCmdPushConstants(cmd, pc.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(offset), &offset);

        // 绑定预先准备好的描述符缓冲
        VkDescriptorBufferBindingInfoEXT db_info = cfg.use_default_inouts()
            ? blit_default_inout_descriptors_[cfg.pipe_info.pipeline_index]
            : blit_specified_inout_descriptors_[cfg.pipe_info.inout_define_index];

        cache_record_context_.clear();
        cache_record_context_.push(0, db_info);
        blit_pipeline_manager_.append_descriptor_buffers(cache_record_context_, 0);
        cache_record_context_.prepare_bindings();
        cache_record_context_(pc.pipeline_layout, cmd, 0, VK_PIPELINE_BIND_POINT_COMPUTE);

        auto dispatches = cfg.get_dispatch_groups();
        vkCmdDispatch(cmd, dispatches.x, dispatches.y, 1);

        cache_barrier_gen_.apply(cmd); // 提交后续可能的转换
    }

    // --- 内部初始化与工具函数 ---

    static graphic::draw::instruction::hardware_limit_config query_hardware_limits(vk::allocator_usage& usage) {
        VkPhysicalDeviceMeshShaderPropertiesEXT mesh_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &mesh_props};
        vkGetPhysicalDeviceProperties2(usage.get_physical_device(), &prop);
        return { mesh_props.maxTaskWorkGroupCount[0], mesh_props.maxTaskWorkGroupSize[0], mesh_props.maxMeshOutputVertices, mesh_props.maxMeshOutputPrimitives };
    }

    void initialize_frames(VkCommandPool pool) {
        for (auto& f : frames_) f = frame_data(allocator_usage_.get_device(), pool);
        blit_attachment_clear_and_init_command_buffer = vk::command_buffer{allocator_usage_.get_device(), pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY};
    }

    void initialize_blit_resources(const renderer_create_info& info) {
        blit_pipeline_manager_ = compute_pipeline_manager(allocator_usage_, info.blit_pipe_config);

        // 分配并初始化默认/特定 Blit 描述符缓冲
        auto pipes = blit_pipeline_manager_.get_pipelines();
        blit_default_inout_descriptors_.reserve(pipes.size());
        for (std::size_t i = 0; i < pipes.size(); ++i) {
            auto& layout = blit_pipeline_manager_.get_inout_layouts()[i];
            blit_default_inout_descriptors_.emplace_back(allocator_usage_, layout, layout.binding_count());
        }

        auto inouts = blit_pipeline_manager_.get_inout_defines();
        blit_specified_inout_descriptors_.reserve(inouts.size());
        for (const auto& def : inouts) {
            vk::descriptor_layout layout{allocator_usage_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, def.make_layout_builder()};
            blit_specified_inout_descriptors_.emplace_back(allocator_usage_, layout, layout.binding_count());
        }
    }

    static void ensure_attachment_state(vk::cmd::dependency_gen& dep, attachment_state& state, VkImage image,
                                     VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access, VkImageLayout next_layout) {
        const bool layout_changed = state.layout != next_layout;
        // 优化：如果是连续的颜色附件写入且没有布局切换，可以利用栅格化顺序(Rasterization Order)跳过屏障
        const bool can_skip_color = !layout_changed &&
            (state.stage_mask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT && next_stage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) &&
            (state.access_mask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT && next_access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        if (can_skip_color) return;

        // 优化：如果是只读到只读的转换，且没有布局切换，也可以跳过
        auto is_write = [](VkAccessFlags2 a) {
            return a & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        };
        if (!layout_changed && !is_write(state.access_mask) && !is_write(next_access)) return;

        dep.push(image, state.stage_mask, state.access_mask, next_stage, next_access, state.layout, next_layout, vk::image::default_image_subrange);
        state = {next_stage, next_access, next_layout};
    }

    void reset_barrier_context() {
        for (auto& s : draw_attachment_states_) s = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        for (auto& s : blit_attachment_states_) s = {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    }

    void create_blit_clear_and_init_cmd() const {
        vk::scoped_recorder recorder{blit_attachment_clear_and_init_command_buffer, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, true};
        vk::cmd::dependency_gen dep;

        // 初始化所有附件到初始布局并清空 Blit 目标
        for (const auto& img : attachment_manager_.get_blit_attachments()) {
            dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::image::default_image_subrange);
        }
        dep.apply(recorder);

        VkClearColorValue black{};
        for (const auto& img : attachment_manager_.get_blit_attachments()) {
            vkCmdClearColorImage(recorder, img.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &vk::image::default_image_subrange);
            dep.push(img.get_image(), VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);
        }

        // 初始化绘制附件
        for (const auto& img : attachment_manager_.get_draw_attachments()) {
            dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk::image::default_image_subrange);
        }
        for (const auto& img : attachment_manager_.get_multisample_attachments()) {
            dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk::image::default_image_subrange);
        }
        dep.apply(recorder);
    }

    const gui::fx::draw_config& get_current_draw_config() const noexcept { return cache_draw_param_stack_.back(); }

    gui::fx::render_target_mask get_current_target(const gui::fx::draw_config& param) const noexcept {
        const auto& pipes = draw_pipeline_manager_.get_pipelines()[param.pipeline_index].option;
        if (param.draw_targets.any()) return param.draw_targets;
        return pipes.is_partial_target() ? pipes.default_target_attachments : gui::fx::render_target_mask{~0U};
    }

    void configure_rendering_info(const gui::fx::draw_config& param) {
        const auto& pipes = draw_pipeline_manager_.get_pipelines()[param.pipeline_index].option;
        attachment_manager_.configure_dynamic_rendering<32>(rendering_config_,
            param.draw_targets.none() ? pipes.default_target_attachments : param.draw_targets,
            pipes.enables_multisample && attachment_manager_.enables_multisample());
    }

    const compute_pipeline_blit_inout_config& get_blit_inout_config(const gui::fx::blit_config& cfg) {
        return cfg.use_default_inouts()
            ? blit_pipeline_manager_.get_pipelines()[cfg.pipe_info.pipeline_index].option.inout
            : blit_pipeline_manager_.get_inout_defines()[cfg.pipe_info.inout_define_index];
    }

    template<typename F>
    bool process_breakpoints(const graphic::draw::instruction::state_transition_entry& entry, VkCommandBuffer buffer, F&& flush_callback) {
        if (entry.process_builtin(buffer, draw_pipeline_manager_.get_pipelines()[get_current_draw_config().pipeline_index].pipeline_layout)) return false;

        switch (entry.flag) {
        case gui::fx::draw_state_index_deduce_v<gui::fx::blit_config>:
            flush_callback(); // Blit 必须在渲染通道外
            blit(entry.as<gui::fx::blit_config>(), buffer);
            return true;
        case gui::fx::draw_state_index_deduce_v<gui::fx::draw_config>:
            auto param = entry.as<gui::fx::draw_config>();
            if (param.mode == gui::fx::draw_mode::COUNT_or_fallback) {
                cache_draw_param_stack_.pop_back();
            } else {
                if (param.use_fallback_pipeline()) param.pipeline_index = get_current_draw_config().pipeline_index;
                cache_draw_param_stack_.push_back(param);
            }
            return false;
        }
        return false;
    }
};

} // namespace mo_yanxi::backend::vulkan