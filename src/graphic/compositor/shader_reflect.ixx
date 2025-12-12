module;

#include <spirv_reflect.h>
#include <vulkan/vulkan.h>

export module mo_yanxi.graphic.compositor.shader_reflect;

import mo_yanxi.graphic.compositor.resource;
import std;

namespace mo_yanxi::graphic::compositor {

export struct shader_reflection {
private:
    spv_reflect::ShaderModule module_;

    std::vector<SpvReflectDescriptorBinding*> storage_images_{};
    std::vector<SpvReflectDescriptorBinding*> sampled_images_{};
    std::vector<SpvReflectDescriptorBinding*> uniform_buffers_{};
    std::vector<SpvReflectDescriptorBinding*> storage_buffers_{};

public:
    [[nodiscard]] shader_reflection() = default;

    // 构造函数简化
    [[nodiscard]] shader_reflection(std::span<const std::uint32_t> binary)
        : module_(binary.size_bytes(), binary.data()) // C++ 包装类构造函数
    {
        if (module_.GetResult() != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error("Failed to create SPIRV-Reflect shader module");
        }
        enumerate_resources();
    }

    // 析构函数不再需要，module_ 会自动析构
    // 移动构造和赋值使用默认即可
    shader_reflection(shader_reflection&& other) noexcept = default;
    shader_reflection& operator=(shader_reflection&& other) noexcept = default;

    // 禁用拷贝（因为 spv_reflect::ShaderModule 内部是一大块内存，拷贝代价大且通常不需要）
    shader_reflection(const shader_reflection&) = delete;
    shader_reflection& operator=(const shader_reflection&) = delete;

    [[nodiscard]] const SpvReflectShaderModule& raw_module() const noexcept {
        return module_.GetShaderModule(); // C++ 包装类的 getter
    }

    // 保持原有接口不变，这样 post_process_pass.ixx 不需要修改
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& storage_images() const noexcept { return storage_images_; }
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& sampled_images() const noexcept { return sampled_images_; }
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& uniform_buffers() const noexcept { return uniform_buffers_; }
    [[nodiscard]] const std::vector<SpvReflectDescriptorBinding*>& storage_buffers() const noexcept { return storage_buffers_; }

    [[nodiscard]] binding_info binding_info_of(const SpvReflectDescriptorBinding* resource) const noexcept {
        return {resource->binding, resource->set};
    }

private:
    void enumerate_resources() {
        uint32_t count = 0;
        // 使用 C++ 包装类的成员函数
        SpvReflectResult result = module_.EnumerateDescriptorBindings(&count, nullptr);

        if (result != SPV_REFLECT_RESULT_SUCCESS || count == 0) return;

        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        module_.EnumerateDescriptorBindings(&count, bindings.data());

        // 分类逻辑保持不变
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