module;

#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

export module mo_yanxi.graphic.compositor.shader_reflect;

import mo_yanxi.graphic.compositor.resource;

import std;

namespace mo_yanxi::graphic::compositor{

	export struct shader_reflection{
	private:
		spirv_cross::CompilerGLSL compiler_{nullptr, 0};
		spirv_cross::ShaderResources resources_{};

	public:
		[[nodiscard]] shader_reflection() = default;

		[[nodiscard]] shader_reflection(std::span<const std::uint32_t> binary) :
		compiler_ {binary.data(), binary.size()}, resources_{compiler_.get_shader_resources()}{
		}

		[[nodiscard]] const auto& compiler() const noexcept{
			return compiler_;
		}

		[[nodiscard]] const spirv_cross::ShaderResources& resources() const noexcept{
			return resources_;
		}

		binding_info binding_info_of(const spirv_cross::Resource& resource) const noexcept{

			return {compiler_.get_decoration(resource.id, spv::DecorationBinding), compiler_.get_decoration(resource.id, spv::DecorationDescriptorSet)};
		}

		void foo(){
			for (const auto & storage_image : resources_.storage_images){
				std::println("{}: Set: {}, Binding: {}, ",
					storage_image.name,
					compiler_.get_decoration(storage_image.id, spv::DecorationDescriptorSet),
					compiler_.get_decoration(storage_image.id, spv::DecorationBinding)
				);
			}
		}
	};

}
