// ReSharper disable CppExpressionWithoutSideEffects
module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.batch.backend.vulkan;

export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.graphic.draw.instruction.batch.frontend;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import mo_yanxi.type_register; // For user_data_index_table
import std;

namespace mo_yanxi::graphic::draw::instruction {

    // 
    struct state_transition_command_context {
        vk::cmd::dependency_gen dependency{};
        std::vector<std::uint32_t> timelines{};
        std::vector<VkDeviceSize> buffer_offsets{};

        std::vector<VkBufferCopy> copy_info{};
        std::uint32_t current_submit_group_index{};

        [[nodiscard]] state_transition_command_context() = default;
        [[nodiscard]] explicit state_transition_command_context(std::size_t data_entry_count)
            : timelines(data_entry_count)
            , buffer_offsets(data_entry_count) {
            dependency.buffer_memory_barriers.reserve(data_entry_count);
            copy_info.reserve(data_entry_count);
        }

        void submit_copy(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst) {
            if (copy_info.empty()) return;
            vkCmdCopyBuffer(cmd, src, dst, static_cast<std::uint32_t>(copy_info.size()), copy_info.data());
            copy_info.clear();
        }
    };

    // 
    VkDeviceSize load_data_group_to_buffer(const data_entry_group& group, const vk::allocator_usage& allocator, vk::buffer_cpu_to_gpu& gpu_buffer, VkBufferUsageFlags flags) {
        VkDeviceSize required_size{};
        for (const auto& entry : group.entries) {
            required_size += entry.get_required_byte_size();
        }

        if (gpu_buffer.get_size() < required_size) {
            gpu_buffer = vk::buffer_cpu_to_gpu{
                allocator, required_size, flags
            };
        }

        vk::buffer_mapper mapper{gpu_buffer};
        VkDeviceSize cur_offset{};
        for (const auto& entry : group.entries) {
            mapper.load_range(entry.get_data_span(), cur_offset);
            cur_offset += entry.get_required_byte_size();
        }

        return cur_offset;
    }

    export class batch_vulkan_executor {
    private:
        vk::allocator_usage allocator_{};

        // GPU Resources
        std::vector<VkDrawMeshTasksIndirectCommandEXT> submit_info_{};
        vk::buffer_cpu_to_gpu buffer_dispatch_info_{};
        vk::buffer_cpu_to_gpu buffer_instruction_{};
        vk::buffer_cpu_to_gpu buffer_indirect_{};

        vk::buffer_cpu_to_gpu buffer_vertex_info_{};
        vk::buffer_cpu_to_gpu buffer_non_vertex_info_{};

        // Uniform buffer for non-vertex updates (GPU only, transfer dest)
        vk::buffer buffer_non_vertex_info_uniform_buffer_{};

        // Descriptors
        std::vector<vk::binding_spec> bindings_{};
        vk::descriptor_layout descriptor_layout_{};
        vk::dynamic_descriptor_buffer descriptor_buffer_{};

        vk::descriptor_layout non_vertex_descriptor_layout_{};
        vk::descriptor_buffer non_vertex_descriptor_buffer_{};

        VkSampler sampler_{};

    public:
        [[nodiscard]] batch_vulkan_executor() = default;

        [[nodiscard]] explicit batch_vulkan_executor(
            const vk::allocator_usage& a,
            const batch_host_context& batch_host,
            VkSampler sampler
        )
            : allocator_{a}
            , buffer_indirect_(allocator_, sizeof(VkDrawMeshTasksIndirectCommandEXT) * 32, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            , buffer_non_vertex_info_uniform_buffer_(allocator_, {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = sizeof(dispatch_config) + batch_host.get_data_group_non_vertex_info().table.required_capacity(),
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            }, {.usage = VMA_MEMORY_USAGE_GPU_ONLY})
            , bindings_({
                {0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                {3, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}, // Dynamic count
                {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},         // Base for vertex UBOs
                {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
            })
            , descriptor_layout_(allocator_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                [&](vk::descriptor_layout_builder& builder) {
                    // dispatch uniform info
                    builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
                    // dispatch group info
                    builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
                    // instructions
                    builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
                    // Textures (partially bound)
                    builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
                    // vertex ubos
                    for (unsigned i = 0; i < batch_host.get_data_group_vertex_info().size(); ++i) {
                        builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
                    }
                })
            , descriptor_buffer_(allocator_, descriptor_layout_, descriptor_layout_.binding_count(), {})
            , non_vertex_descriptor_layout_{
                a.get_device(),
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                [&](vk::descriptor_layout_builder& builder) {
                    for(std::size_t i = 0; i < batch_host.get_data_group_non_vertex_info().size(); ++i){
                        builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
                    }
                }}
            , non_vertex_descriptor_buffer_(a, non_vertex_descriptor_layout_, non_vertex_descriptor_layout_.binding_count())
            , sampler_(sampler)
        {

            // Setup static descriptors for non-vertex buffers
            for (auto&& [group, chunk] : batch_host.get_data_group_non_vertex_info().table
                 | std::views::chunk_by([](const data_layout_type_aware_entry& l, const data_layout_type_aware_entry& r) {
                     return l.entry.group_index == r.entry.group_index;
                 })
                 | std::views::enumerate) {
                assert(group == 0);
                vk::descriptor_mapper mapper{non_vertex_descriptor_buffer_};
                for (auto&& [binding, entry] : chunk | std::views::enumerate) {
                    (void)mapper.set_uniform_buffer(
                        binding,
                        buffer_non_vertex_info_uniform_buffer_.get_address() + sizeof(dispatch_config) + entry.entry.global_offset, entry.entry.size
                    );
                }
            }
        }

        bool upload(batch_host_context& host_ctx) {
            auto submit_group_subrange = host_ctx.get_valid_submit_groups();
            if (submit_group_subrange.empty()) {
                return false;
            }

            // 1. Prepare Indirect Commands
            const auto dispatchCountGroups = [&] {
                submit_info_.resize(host_ctx.get_state_transitions().size());
                std::uint32_t currentSubmitGroupIndex = 0;
                for (const auto& [idx, submit_breakpoint] : host_ctx.get_state_transitions() | std::views::enumerate) {
                    const auto section_end = submit_breakpoint.break_before_index;
                    std::uint32_t submitCount{};
                    for (auto i = currentSubmitGroupIndex; i < section_end; ++i) {
                        submitCount += static_cast<std::uint32_t>(submit_group_subrange[i].get_used_dispatch_groups().size());
                    }
                    submit_info_[idx] = {submitCount, 1, 1};
                    currentSubmitGroupIndex = section_end;
                }
                return std::span{std::as_const(submit_info_)};
            }();

            bool requires_command_record = false;

            if (const auto reqSize = dispatchCountGroups.size() * sizeof(VkDrawMeshTasksIndirectCommandEXT); buffer_indirect_.get_size() < reqSize) {
                buffer_indirect_ = vk::buffer_cpu_to_gpu(allocator_, reqSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
            }
            vk::buffer_mapper{buffer_indirect_}.load_range(dispatchCountGroups);

            // 2. Prepare Dispatch Info & Timeline
            std::uint32_t totalDispatchCount{};
            const auto dispatch_timeline_size = host_ctx.get_data_group_vertex_info().size() * sizeof(std::uint32_t);
            const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;

            {
                VkDeviceSize deviceSize{};
                for (const auto& group : submit_group_subrange) {
                    deviceSize += group.get_used_dispatch_groups().size_bytes() + group.get_used_time_line_datas().size_bytes();
                }
                deviceSize += dispatch_unit_size; // Sentinel

                if (buffer_dispatch_info_.get_size() < deviceSize) {
                    buffer_dispatch_info_ = vk::buffer_cpu_to_gpu{
                        allocator_, deviceSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                    };
                }

                vk::buffer_mapper mapper{buffer_dispatch_info_};
                std::vector<std::byte> buffer{};
                VkDeviceSize pushed_size{};
                std::uint32_t current_instr_offset{};
                for (const auto& [idx, group] : submit_group_subrange | std::views::enumerate) {
                    const auto dispatch = group.get_used_dispatch_groups();
                    const auto timeline = group.get_used_time_line_datas();
                    buffer.resize(dispatch.size_bytes() + timeline.size_bytes());

                    for (std::size_t i = 0; i < dispatch.size(); ++i) {
                        auto info = dispatch[i];
                        info.instruction_offset += current_instr_offset;
                        std::memcpy(buffer.data() + dispatch_unit_size * i, &info, sizeof(info));
                        std::memcpy(buffer.data() + dispatch_unit_size * i + sizeof(info), timeline.data() + i * host_ctx.get_data_group_vertex_info().size(), dispatch_timeline_size);
                    }

                    mapper.load_range(buffer, pushed_size);
                    pushed_size += buffer.size();
                    totalDispatchCount += static_cast<std::uint32_t>(dispatch.size());

                    current_instr_offset += static_cast<std::uint32_t>(group.get_pushed_instruction_size());
                }

                // Add instruction sentinel
                mapper.load(dispatch_group_info{current_instr_offset}, pushed_size);
            }

            // 3. Upload User Data Entries
            load_data_group_to_buffer(host_ctx.get_data_group_vertex_info(), allocator_, buffer_vertex_info_, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
            load_data_group_to_buffer(host_ctx.get_data_group_non_vertex_info(), allocator_, buffer_non_vertex_info_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

            // 4. Upload Instructions & Update Descriptors
            {
                std::uint32_t instructionSize{};
                for (const auto& [idx, submit_group] : submit_group_subrange | std::views::enumerate) {
                    instructionSize += static_cast<std::uint32_t>(submit_group.get_pushed_instruction_size());
                }

                if (buffer_instruction_.get_size() < instructionSize) {
                    buffer_instruction_ = vk::buffer_cpu_to_gpu{
                        allocator_, instructionSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                    };
                }

                {
                    vk::buffer_mapper mapper{buffer_instruction_};
                    std::size_t current_offset{};
                    for (const auto& [idx, submit_group] : submit_group_subrange | std::views::enumerate) {
                        (void)mapper.load_range(std::span{submit_group.get_buffer_data(), submit_group.get_pushed_instruction_size()}, current_offset);
                        current_offset += submit_group.get_pushed_instruction_size();
                    }
                }

                {
                    // Update Dynamic Descriptor Buffer
                    if (const auto cur_size = host_ctx.get_used_images().size(); bindings_[3].count != cur_size) {
                        bindings_[3].count = static_cast<std::uint32_t>(cur_size);
                        descriptor_buffer_.reconfigure(descriptor_layout_, descriptor_layout_.binding_count(), bindings_);
                        requires_command_record = true;
                    }

                    vk::dynamic_descriptor_mapper dbo_mapper{descriptor_buffer_};
                    dbo_mapper.set_element_at(0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buffer_non_vertex_info_uniform_buffer_.get_address(), sizeof(dispatch_config));
                    dbo_mapper.set_element_at(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_dispatch_info_.get_address(), dispatch_unit_size * (1 + totalDispatchCount));
                    dbo_mapper.set_element_at(2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_instruction_.get_address(), instructionSize);
                    dbo_mapper.set_images_at(3, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler_, host_ctx.get_used_images());

                    VkDeviceSize cur_offset{};
                    for (const auto& [i, entry] : host_ctx.get_data_group_vertex_info().entries | std::views::enumerate) {
                        dbo_mapper.set_element_at(4 + i, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_vertex_info_.get_address() + cur_offset, entry.get_required_byte_size());
                        cur_offset += entry.get_required_byte_size();
                    }
                }
            }

            return requires_command_record;
        }

        std::array<VkDescriptorSetLayout, 2> get_descriptor_set_layout() const noexcept {
            return {descriptor_layout_, non_vertex_descriptor_layout_};
        }

        template <typename T = std::allocator<descriptor_buffer_usage>>
        void load_descriptors(record_context<T>& record_context) {
            record_context.push(0, descriptor_buffer_);
            record_context.push(1, non_vertex_descriptor_buffer_);
        }

        void cmd_draw(VkCommandBuffer cmd, std::uint32_t dispatch_group_index) const{
            vk::cmd::drawMeshTasksIndirect(cmd, buffer_indirect_,
                dispatch_group_index * sizeof(VkDrawMeshTasksIndirectCommandEXT), 1);
        }

        //
        state_transition_command_context cmd_set_up_state(VkCommandBuffer cmd,
            const batch_host_context& host_ctx) const{
            const auto& data_group = host_ctx.get_data_group_non_vertex_info();
            state_transition_command_context ctx{data_group.size()};

            VkDeviceSize currentBufferOffset{};
            for(const auto& [idx, table] : data_group.table | std::views::enumerate){
                const auto& entry = data_group.entries[idx];
                if(entry.empty()) continue;
                ctx.copy_info.push_back({
                        .srcOffset = currentBufferOffset,
                        .dstOffset = sizeof(dispatch_config) + table.entry.global_offset,
                        .size = table.entry.size
                    });
                ctx.buffer_offsets[idx] = currentBufferOffset;
                currentBufferOffset += entry.get_required_byte_size();
            }

            constexpr dispatch_config cfg{};
            vkCmdUpdateBuffer(cmd, buffer_non_vertex_info_uniform_buffer_, 0, sizeof(dispatch_config), &cfg);

            ctx.submit_copy(cmd, buffer_non_vertex_info_, buffer_non_vertex_info_uniform_buffer_);

            ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, sizeof(dispatch_config)
            );
            ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, sizeof(dispatch_config)
            );
            ctx.dependency.apply(cmd);
            return ctx;
        }

        // 
        void cmd_translate_state(VkCommandBuffer cmd,
            const batch_host_context& host_ctx,
            state_transition_command_context& ctx, std::size_t current_breakpoint_idx) const{
            const auto& breakpoint = host_ctx.get_state_transitions()[current_breakpoint_idx];
            const auto& data_group = host_ctx.get_data_group_non_vertex_info();

            const bool insertFullBuffer = breakpoint.uniform_buffer_marching_indices.size() > data_group.size() / 2;
            for(const auto idx : breakpoint.uniform_buffer_marching_indices){
                const auto timestamp = ++ctx.timelines[idx];
                const auto unitSize = data_group.table[idx].size;
                const auto dst_offset = sizeof(dispatch_config) + data_group.table[idx].global_offset;
                const auto src_offset = ctx.buffer_offsets[idx] + timestamp * unitSize;

                ctx.copy_info.push_back({
                        .srcOffset = src_offset,
                        .dstOffset = dst_offset,
                        .size = unitSize
                    });

                if(!insertFullBuffer){
                    ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_UNIFORM_READ_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dst_offset, unitSize
                    );
                }
            }

            if(insertFullBuffer){
                ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_UNIFORM_READ_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, sizeof(dispatch_config)
                );
            }

            ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, sizeof(dispatch_config)
            );

            ctx.current_submit_group_index += submit_info_[current_breakpoint_idx].groupCountX;
            const dispatch_config cfg{ctx.current_submit_group_index};

            ctx.dependency.apply(cmd, true); // true = insert barriers before update
            vkCmdUpdateBuffer(cmd, buffer_non_vertex_info_uniform_buffer_, 0, sizeof(dispatch_config), &cfg);
            ctx.submit_copy(cmd, buffer_non_vertex_info_, buffer_non_vertex_info_uniform_buffer_);
            ctx.dependency.swap_stages();
            ctx.dependency.apply(cmd);
        }
    };
}