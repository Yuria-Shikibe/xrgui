module;

#include <vulkan/vulkan.h>
#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <spirv_reflect.h>;
#endif

export module mo_yanxi.graphic.shader_reflect;

import mo_yanxi.graphic.compositor.resource;
import std;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <spirv_reflect.h>;
#endif

namespace mo_yanxi::graphic {

export struct shader_reflection {
private:
    spv_reflect::ShaderModule module_;

    std::vector<SpvReflectDescriptorBinding*> storage_images_{};
    std::vector<SpvReflectDescriptorBinding*> sampled_images_{};
    std::vector<SpvReflectDescriptorBinding*> uniform_buffers_{};
    std::vector<SpvReflectDescriptorBinding*> storage_buffers_{};
    // [New]
    std::vector<VkPushConstantRange> push_constant_ranges_{};

public:
    [[nodiscard]] shader_reflection() = default;

    [[nodiscard]] shader_reflection(std::span<const std::uint32_t> binary)
        : module_(binary.size_bytes(), binary.data())
    {
        if (module_.GetResult() != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error("Failed to create SPIRV-Reflect shader module");
        }
        enumerate_resources();
    }

    shader_reflection(shader_reflection&& other) noexcept = default;
    shader_reflection& operator=(shader_reflection&& other) noexcept = default;

    shader_reflection(const shader_reflection&) = delete;
    shader_reflection& operator=(const shader_reflection&) = delete;

    [[nodiscard]] const SpvReflectShaderModule& raw_module() const noexcept {
        return module_.GetShaderModule();
    }

    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& storage_images() const noexcept { return storage_images_; }
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& sampled_images() const noexcept { return sampled_images_; }
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& uniform_buffers() const noexcept { return uniform_buffers_; }
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& storage_buffers() const noexcept { return storage_buffers_; }

    // [New] Getter
    [[nodiscard]] const std::vector<VkPushConstantRange>& push_constant_ranges() const noexcept {
        return push_constant_ranges_;
    }

    [[nodiscard]] compositor::binding_info binding_info_of(const SpvReflectDescriptorBinding* resource) const noexcept {
        return {resource->binding, resource->set};
    }

private:
    void enumerate_resources() {
        // 1. 处理 Descriptor Sets
        uint32_t count = 0;
        SpvReflectResult result = module_.EnumerateDescriptorBindings(&count, nullptr);

        if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
            std::vector<SpvReflectDescriptorBinding*> bindings(count);
            module_.EnumerateDescriptorBindings(&count, bindings.data());

            for (auto* binding : bindings) {
                switch (binding->descriptor_type) {
                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                        storage_images_.push_back(binding);
                        break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                        sampled_images_.push_back(binding);
                        break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                        uniform_buffers_.push_back(binding);
                        break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                        storage_buffers_.push_back(binding);
                        break;
                    default:
                        break;
                }
            }
        }

        // 2. [New] 处理 Push Constants
        std::uint32_t pc_count = 0;
        result = module_.EnumeratePushConstantBlocks(&pc_count, nullptr);

        if (result == SPV_REFLECT_RESULT_SUCCESS && pc_count > 0) {
            std::vector<SpvReflectBlockVariable*> blocks(pc_count);
            module_.EnumeratePushConstantBlocks(&pc_count, blocks.data());

            // 获取底层的 C 结构体，方便访问入口点数组
            const auto& spv_module = module_.GetShaderModule();

            push_constant_ranges_.reserve(pc_count);

            for (auto* block : blocks) {
                VkShaderStageFlags calculated_stage_flags = 0;

                // 核心修复逻辑：遍历该 SPIR-V 中所有的入口点
                for (uint32_t i = 0; i < spv_module.entry_point_count; ++i) {
                    const auto& entry_point = spv_module.entry_points[i];

                    // 检查该入口点使用了哪些 Push Constant
                    // used_push_constants 存储的是 push_constant_blocks 数组的【索引】
                    for (uint32_t j = 0; j < entry_point.used_push_constant_count; ++j) {
                        uint32_t used_index = entry_point.used_push_constants[j];

                        // 比较地址，判断当前 entry_point 引用的块是不是我们正在处理的 block
                        if (&spv_module.push_constant_blocks[used_index] == block) {
                            calculated_stage_flags |= static_cast<VkShaderStageFlags>(entry_point.shader_stage);
                        }
                    }
                }

                // 如果该 Block 被反射出来但没有被任何入口点显式使用（极少见），
                // 为了安全起见，可以使用 module 全局的 stage 或者跳过。
                // 这里给一个兜底策略，或者你可以选择 if (calculated_stage_flags != 0) 再 push_back
                if (calculated_stage_flags == 0) {
                    calculated_stage_flags = static_cast<VkShaderStageFlags>(spv_module.shader_stage);
                }

                push_constant_ranges_.push_back({
                    .stageFlags = calculated_stage_flags,
                    .offset = block->offset,
                    .size = block->size
                });
            }
        }
    }
};


export
constexpr VkFormat convertImageFormatToVkFormat(SpvImageFormat imageFormat) noexcept {
    switch (imageFormat) {
    case SpvImageFormatUnknown: return VK_FORMAT_UNDEFINED;
    case SpvImageFormatRgba8: return VK_FORMAT_R8G8B8A8_UNORM;
    case SpvImageFormatRgba8Snorm: return VK_FORMAT_R8G8B8A8_SNORM;
    case SpvImageFormatRgba8ui: return VK_FORMAT_R8G8B8A8_UINT;
    case SpvImageFormatRgba8i: return VK_FORMAT_R8G8B8A8_SINT;
    case SpvImageFormatR32ui: return VK_FORMAT_R32_UINT;
    case SpvImageFormatR32i: return VK_FORMAT_R32_SINT;
    case SpvImageFormatRgba16: return VK_FORMAT_R16G16B16A16_UNORM;
    case SpvImageFormatRgba16Snorm: return VK_FORMAT_R16G16B16A16_SNORM;
    case SpvImageFormatRgba16ui: return VK_FORMAT_R16G16B16A16_UINT;
    case SpvImageFormatRgba16i: return VK_FORMAT_R16G16B16A16_SINT;
    case SpvImageFormatRgba16f: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case SpvImageFormatR32f: return VK_FORMAT_R32_SFLOAT;
    case SpvImageFormatRgba32ui: return VK_FORMAT_R32G32B32A32_UINT;
    case SpvImageFormatRgba32i: return VK_FORMAT_R32G32B32A32_SINT;
    case SpvImageFormatRgba32f: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case SpvImageFormatR8: return VK_FORMAT_R8_UNORM;
    case SpvImageFormatR8Snorm: return VK_FORMAT_R8_SNORM;
    case SpvImageFormatR8ui: return VK_FORMAT_R8_UINT;
    case SpvImageFormatR8i: return VK_FORMAT_R8_SINT;
    case SpvImageFormatRg8: return VK_FORMAT_R8G8_UNORM;
    case SpvImageFormatRg8Snorm: return VK_FORMAT_R8G8_SNORM;
    case SpvImageFormatRg8ui: return VK_FORMAT_R8G8_UINT;
    case SpvImageFormatRg8i: return VK_FORMAT_R8G8_SINT;
    case SpvImageFormatR16: return VK_FORMAT_R16_UNORM;
    case SpvImageFormatR16Snorm: return VK_FORMAT_R16_SNORM;
    case SpvImageFormatR16ui: return VK_FORMAT_R16_UINT;
    case SpvImageFormatR16i: return VK_FORMAT_R16_SINT;
    case SpvImageFormatR16f: return VK_FORMAT_R16_SFLOAT;
    case SpvImageFormatRg16: return VK_FORMAT_R16G16_UNORM;
    case SpvImageFormatRg16Snorm: return VK_FORMAT_R16G16_SNORM;
    case SpvImageFormatRg16ui: return VK_FORMAT_R16G16_UINT;
    case SpvImageFormatRg16i: return VK_FORMAT_R16G16_SINT;
    case SpvImageFormatRg16f: return VK_FORMAT_R16G16_SFLOAT;
    case SpvImageFormatR64ui: return VK_FORMAT_R64_UINT;
    case SpvImageFormatR64i: return VK_FORMAT_R64_SINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

export std::size_t get_buffer_size(const SpvReflectDescriptorBinding* resource) {
    return resource->block.size;
}


} // namespace mo_yanxi::graphic::compositor