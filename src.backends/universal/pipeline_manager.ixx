module;

#include <cassert>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "gch/small_vector.hpp"

export module mo_yanxi.backend.vulkan.pipeline_manager;

export import mo_yanxi.backend.vulkan.renderer.attachment_manager;
export import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction.batch;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.vk.util.uniform;
import mo_yanxi.vk.util;
import mo_yanxi.vk;
import mo_yanxi.gui.alloc;

import std;

namespace mo_yanxi::backend::vulkan {
    using namespace gui;
    // -------------------------------------------------------------------------
    // 描述符相关配置 (从原代码移植)
    // -------------------------------------------------------------------------

#pragma region DescriptorCustomize

    export
    using user_data_table = graphic::draw::instruction::user_data_index_table<>;

    export
    using descriptor_slots = graphic::draw::instruction::batch_descriptor_slots;

    export
    struct descriptor_create_config {
        user_data_table user_data_table{};
        vk::descriptor_layout_builder builder{};
    };

#pragma endregion

    // -------------------------------------------------------------------------
    // 管线相关配置 (从原代码移植)
    // -------------------------------------------------------------------------
#pragma region PipelineCustomize

    export
    struct descriptor_buffer_use_entry {
        std::uint32_t source;
        std::uint32_t target;
    };

    export
    struct pipeline_data {
        vk::pipeline_layout pipeline_layout{};
        vk::pipeline pipeline{};

        gch::small_vector<descriptor_buffer_use_entry, 4, mr::unvs_allocator<descriptor_buffer_use_entry>> used_descriptor_sets{};

        [[nodiscard]] pipeline_data() = default;
    };

    export
    struct draw_pipeline_data : pipeline_data {
        bool enables_multisample{};
    };

    export struct pipeline_config {
        std::vector<VkPipelineShaderStageCreateInfo> shader_modules;
        std::vector<descriptor_buffer_use_entry> entries;
    };

    export struct draw_pipeline_config {
        pipeline_config config{};
        bool enables_multisample{};
        void(*creator)(
            draw_pipeline_data&,
            const draw_pipeline_config&,
            const draw_attachment_create_info&
            );
    };

    export
    struct draw_pipeline_create_group {
        std::span<const draw_pipeline_config> pipeline_create_info;
        std::span<const descriptor_create_config> descriptor_create_info;
    };

    export
    struct blit_pipeline_create_group {
        std::span<const pipeline_config> pipeline_create_info;
        std::span<const descriptor_create_config> descriptor_create_info;
    };

#pragma endregion

    // -------------------------------------------------------------------------
    // 管线管理器 (新封装)
    // -------------------------------------------------------------------------
    export class pipeline_manager {
    private:
        // 存储所有的自定义管线
        mr::vector<draw_pipeline_data> pipelines_{};

        // 存储自定义的 Descriptor Buffer Slots
        // 对应 create_info 中的 descriptor_create_config
        mr::vector<descriptor_slots> custom_descriptors_{};

    public:
        [[nodiscard]] pipeline_manager() = default;

        // 禁用拷贝，允许移动
        pipeline_manager(const pipeline_manager&) = delete;
        pipeline_manager& operator=(const pipeline_manager&) = delete;
        pipeline_manager(pipeline_manager&&) noexcept = default;
        pipeline_manager& operator=(pipeline_manager&&) noexcept = default;

        /**
         * @brief 初始化所有的自定义描述符和管线
         *
         * @param allocator 内存分配器
         * @param create_group 包含管线和描述符的创建配置
         * @param batch_layout 系统核心 Batch 的 DescriptorSetLayout (Set 0)
         * @param auxiliary_layouts 辅助的全局 DescriptorSetLayouts (如 viewport, ui_state 等，依次为 Set 1, Set 2...)
         * @param attachment_info 渲染附件信息，用于管线创建时的混合状态等设置
         * @param work_group_count Descriptor Buffer 的 Chunk 数量 (与 Batch 保持一致)
         */
        void init(
            vk::allocator_usage allocator,
            const draw_pipeline_create_group& create_group,
            VkDescriptorSetLayout batch_layout,
            std::span<const VkDescriptorSetLayout> auxiliary_layouts,
            const draw_attachment_create_info& attachment_info,
            std::uint32_t work_group_count
        ) {
            auto device = allocator.get_device();

            // 1. 初始化自定义的 Descriptor Slots
            custom_descriptors_.reserve(create_group.descriptor_create_info.size());
            for (const auto& user_data_index_table : create_group.descriptor_create_info) {
                custom_descriptors_.push_back(descriptor_slots(
                    allocator,
                    user_data_index_table.builder,
                    work_group_count
                ));
            }

            // 2. 初始化管线
            pipelines_.resize(create_group.pipeline_create_info.size());
            mr::vector<VkDescriptorSetLayout> layouts_buffer;

            for (const auto& [pipe, creator] : std::views::zip(pipelines_, create_group.pipeline_create_info)) {
                layouts_buffer.clear();

                // 2.1 绑定全局 Layouts
                // Set 0: Batch Layout
                layouts_buffer.push_back(batch_layout);

                // Set 1, 2...: Auxiliary Layouts (General, UI State, etc.)
                for (auto layout : auxiliary_layouts) {
                    layouts_buffer.push_back(layout);
                }

                // 2.2 记录该管线使用的自定义 Descriptor Sets
                std::ranges::copy(creator.config.entries, std::back_inserter(pipe.used_descriptor_sets));

                // 2.3 绑定自定义 Layouts (根据 source 索引从 custom_descriptors_ 获取)
                for (auto used_descriptor_set : pipe.used_descriptor_sets) {
                    layouts_buffer.push_back(custom_descriptors_.at(used_descriptor_set.source).descriptor_set_layout());
                }

                // 2.4 创建 Pipeline Layout
                pipe.pipeline_layout = vk::pipeline_layout(
                    device,
                    0,
                    layouts_buffer
                );

                // 2.5 调用回调函数创建具体的 VkPipeline
                // creator 函数通常会使用 pipelines_.back().pipeline_layout 以及 attachment_info
                creator.creator(pipe, creator, attachment_info);
            }
        }

        // --- Getters ---

        [[nodiscard]] std::span<draw_pipeline_data> get_pipelines() noexcept {
            return pipelines_;
        }

        [[nodiscard]] std::span<const draw_pipeline_data> get_pipelines() const noexcept {
            return pipelines_;
        }

        [[nodiscard]] descriptor_slots& get_custom_descriptor(std::size_t index) {
            assert(index < custom_descriptors_.size());
            return custom_descriptors_[index];
        }

        [[nodiscard]] const descriptor_slots& get_custom_descriptor(std::size_t index) const {
            assert(index < custom_descriptors_.size());
            return custom_descriptors_[index];
        }

        [[nodiscard]] std::size_t get_custom_descriptor_count() const noexcept {
            return custom_descriptors_.size();
        }
    };

}