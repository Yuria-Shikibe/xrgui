module;

#include <vulkan/vulkan.h>
#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.graphic_state_context;

import std;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.backend.vulkan.pipeline_manager;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

bool operator==(const VkPipelineColorBlendAttachmentState& lhs, const VkPipelineColorBlendAttachmentState& rhs) noexcept{
	return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool operator==(const VkColorBlendEquationEXT& lhs, const VkColorBlendEquationEXT& rhs) noexcept{
	return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

namespace mo_yanxi::backend::vulkan{


// --- 状态位定义 ---
enum state_flag_bits : std::uint32_t {
	DIRTY_NONE           = 0,
	DIRTY_PIPELINE       = 1 << 0,
	DIRTY_VIEWPORT       = 1 << 1,
	DIRTY_SCISSOR        = 1 << 2,
	DIRTY_BLEND_ENABLE   = 1 << 3,
	DIRTY_BLEND_EQUATION = 1 << 4,
	DIRTY_BLEND_WRITE    = 1 << 5,
	DIRTY_ALL            = ~0U,
};

#define NO_EXPORT

BITMASK_OPS(NO_EXPORT, state_flag_bits)


constexpr auto get_blend_enable_from_state(const VkPipelineColorBlendAttachmentState& s) noexcept -> VkBool32 {
	return s.blendEnable;
}

constexpr auto get_blend_equation_from_state(const VkPipelineColorBlendAttachmentState& s) noexcept -> VkColorBlendEquationEXT {
	return {
		.srcColorBlendFactor = s.srcColorBlendFactor,
		.dstColorBlendFactor = s.dstColorBlendFactor,
		.colorBlendOp = s.colorBlendOp,
		.srcAlphaBlendFactor = s.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = s.dstAlphaBlendFactor,
		.alphaBlendOp = s.alphaBlendOp,
	};
}

constexpr auto get_component_flags_from_state(const VkPipelineColorBlendAttachmentState& s) noexcept -> VkColorComponentFlags {
	return s.colorWriteMask;
}

/**
 * @brief 内部辅助类：负责管理图形管线绑定和动态状态设置
 * 将状态追踪从 renderer 主逻辑中剥离，避免重复绑定
 */
export
struct graphics_context_trace {
    struct dynamic_state_packet {
        std::uint32_t pipeline_index{std::numeric_limits<std::uint32_t>::max()};
        VkViewport viewport{};
        VkRect2D scissor{};

        // Blend 状态缓存
        std::vector<VkBool32> blend_enables;
        std::vector<VkColorBlendEquationEXT> blend_equations;
        std::vector<VkColorComponentFlags> blend_write_flags;
    };

    state_flag_bits dirty_flags = DIRTY_ALL;
    dynamic_state_packet pending_state{};
    dynamic_state_packet current_state{};
	bool pipeline_just_changed = false;
	bool requires_rebind = false;

    void update_pipeline(std::size_t index) noexcept {
        pending_state.pipeline_index = index;
        dirty_flags |= DIRTY_PIPELINE;
    }

    void set_viewport(const VkRect2D& area) noexcept {
        // 这里的转换逻辑假设 VkRect2D 转 Viewport (全覆盖)

        VkViewport vp{
            .x = static_cast<float>(area.offset.x),
            .y = static_cast<float>(area.offset.y),
            .width = static_cast<float>(area.extent.width),
            .height = static_cast<float>(area.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

    	pending_state.viewport = vp;
    	dirty_flags |= DIRTY_VIEWPORT;
    }

    void set_viewport(const VkViewport& vp) noexcept {
    	pending_state.viewport = vp;
    	dirty_flags |= DIRTY_VIEWPORT;
    }

    void set_scissor(const VkRect2D& area) noexcept {
    	pending_state.scissor = area;
    	dirty_flags |= DIRTY_SCISSOR;
    }

    void update_blend_config(const graphic_pipeline_option& option) {
        if (option.blend_state.dynamic_blending_enable_states) {
            auto range = option.blend_state.default_blending_settings | std::views::transform(get_blend_enable_from_state);
            if (!std::ranges::equal(pending_state.blend_enables, range)) {
                pending_state.blend_enables.assign_range(range);
                dirty_flags |= DIRTY_BLEND_ENABLE;
            }
        }

        if (option.blend_state.dynamic_blending_equation_states) {
            auto range = option.blend_state.default_blending_settings | std::views::transform(get_blend_equation_from_state);
            if (pending_state.blend_equations.size() != range.size() || // 简单长度检查，完全比较可能开销较大，依赖 dirty_bit
                !std::equal(pending_state.blend_equations.begin(), pending_state.blend_equations.end(), range.begin(),
                    [](const VkColorBlendEquationEXT& a, const VkColorBlendEquationEXT& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; })) {

                pending_state.blend_equations.assign_range(range);
                dirty_flags |= DIRTY_BLEND_EQUATION;
            }
        }

        if (option.blend_state.dynamic_blending_write_flag_states) {
            auto range = option.blend_state.default_blending_settings | std::views::transform(get_component_flags_from_state);
            if (!std::ranges::equal(pending_state.blend_write_flags, range)) {
                pending_state.blend_write_flags.assign_range(range);
                dirty_flags |= DIRTY_BLEND_WRITE;
            }
        }
    }

	/**
	 * @brief 修改指定附件的混合启用状态
	 */
	void set_blend_enable(std::uint32_t index, VkBool32 enable) noexcept {
    	if (index >= pending_state.blend_enables.size()) [[unlikely]] {
    		return;
    	}

    	if (pending_state.blend_enables[index] != enable) {
    		pending_state.blend_enables[index] = enable;
    		dirty_flags |= DIRTY_BLEND_ENABLE;
    	}
    }

	/**
	 * @brief 修改指定附件的混合方程
	 */
	void set_blend_equation(std::uint32_t index, const VkColorBlendEquationEXT& eq) noexcept {
    	if (index >= pending_state.blend_equations.size()) [[unlikely]] {
    		return;
    	}

    	// 使用 memcmp 比较结构体内容
    	if (std::memcmp(&pending_state.blend_equations[index], &eq, sizeof(VkColorBlendEquationEXT)) != 0) {
    		pending_state.blend_equations[index] = eq;
    		dirty_flags |= DIRTY_BLEND_EQUATION;
    	}
    }

	/**
	 * @brief 修改指定附件的颜色写入掩码
	 */
	void set_blend_write_mask(std::uint32_t index, VkColorComponentFlags mask) noexcept {
    	if (index >= pending_state.blend_write_flags.size()) [[unlikely]] {
    		return;
    	}

    	if (pending_state.blend_write_flags[index] != mask) {
    		pending_state.blend_write_flags[index] = mask;
    		dirty_flags |= DIRTY_BLEND_WRITE;
    	}
    }

    void apply(VkCommandBuffer cmd, graphic_pipeline_manager& pipeline_manager) {
    	auto& pipe_data = pipeline_manager.get_pipelines()[pending_state.pipeline_index];

        // 1. 检查管线是否必须变更
    	if ((dirty_flags & DIRTY_PIPELINE) && (pending_state.pipeline_index != current_state.pipeline_index)) {
            pipe_data.pipeline.bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

            current_state.pipeline_index = pending_state.pipeline_index;
            pipeline_just_changed = true;

    		//set default blend states, inherit scissor and viewports
    		if (pipe_data.option.blend_state.dynamic_blending_enable_states) {
    			pending_state.blend_enables.assign_range(pipe_data.option.blend_state.default_blending_settings | std::views::transform(get_blend_enable_from_state));
    		}

    		if (pipe_data.option.blend_state.dynamic_blending_equation_states) {
    			pending_state.blend_equations.assign_range(pipe_data.option.blend_state.default_blending_settings | std::views::transform(get_blend_equation_from_state));
    		}

    		if (pipe_data.option.blend_state.dynamic_blending_write_flag_states) {
    			pending_state.blend_write_flags.assign_range(pipe_data.option.blend_state.default_blending_settings | std::views::transform(get_component_flags_from_state));
    		}

    		current_state = pending_state;
    	}

        // 辅助 lambda：判断是否需要录制指令 (Dirty 且 (强制刷新 或 内容确实改变))
        const auto should_emit = [&]<typename T>(std::uint32_t flag_bit, T dynamic_state_packet::* mptr) -> bool {
        	if (pipeline_just_changed || requires_rebind) {
        		return true;
        	}

        	const bool changed = [&]{
        		if (!(dirty_flags & flag_bit)) return false;

        		if constexpr (std::equality_comparable<T>){
        			return current_state.*mptr != pending_state.*mptr;
        		}else{
        			return std::memcmp(&(current_state.*mptr), &(pending_state.*mptr), sizeof(T)) != 0;
        		}
        	}();

        	if(changed){
        		current_state.*mptr = pending_state.*mptr;
        	}


        	return changed;
        };

        if (should_emit(DIRTY_VIEWPORT, &dynamic_state_packet::viewport)) {
            vk::cmd::set_viewport(cmd, current_state.viewport);
        }

        if (should_emit(DIRTY_SCISSOR, &dynamic_state_packet::scissor)) {
            vk::cmd::set_scissor(cmd, current_state.scissor);
        }

        if (pipe_data.option.blend_state.dynamic_blending_enable_states && should_emit(DIRTY_BLEND_ENABLE, &dynamic_state_packet::blend_enables)) {
        	vk::cmd::setColorBlendEnableEXT(cmd, 0, static_cast<std::uint32_t>(current_state.blend_enables.size()), current_state.blend_enables.data());
        }

    	if (pipe_data.option.blend_state.dynamic_blending_equation_states && should_emit(DIRTY_BLEND_EQUATION, &dynamic_state_packet::blend_equations)) {
    		vk::cmd::setColorBlendEquationEXT(cmd, 0, static_cast<std::uint32_t>(current_state.blend_equations.size()), current_state.blend_equations.data());
        }

    	if (pipe_data.option.blend_state.dynamic_blending_write_flag_states && should_emit(DIRTY_BLEND_WRITE, &dynamic_state_packet::blend_write_flags)) {
    		vk::cmd::setColorWriteMaskEXT(cmd, 0, static_cast<std::uint32_t>(current_state.blend_write_flags.size()), current_state.blend_write_flags.data());
        }

        dirty_flags = DIRTY_NONE;
        requires_rebind = pipeline_just_changed = false;
    }

	void set_rebind_required() noexcept{
    	requires_rebind = true;
    }

    void invalidate() noexcept {
        current_state.pipeline_index = std::numeric_limits<std::size_t>::max();
        dirty_flags = DIRTY_ALL;
        pending_state.blend_enables.clear();
        pending_state.blend_equations.clear();
        pending_state.blend_write_flags.clear();
    }
};

template <typename ...Ts>
struct state_trace{
private:
	using state_tup = std::tuple<Ts...>;
	state_tup states_;
	std::bitset<sizeof...(Ts)> is_valid_;

public:
	template <typename T>
		requires (mo_yanxi::is_any_of<T, Ts...>)
	bool try_set(const T& value) noexcept {
		constexpr static auto idx = mo_yanxi::tuple_index_v<T, state_tup>;
		if(is_valid_[idx]){
			auto& current = std::get<idx>(states_);
			if constexpr (std::equality_comparable<T>){
				if(current != value){
					current = value;
					return true;
				}
			}else if(std::has_unique_object_representations_v<T>){
				if(std::memcmp(&current, &value, sizeof(T)) != 0){
					current = value;
					return true;
				}
			}else{
				static_assert(false, "No equality operator available");
			}

			return false;
		}else{
			is_valid_[idx] = true;
			std::get<idx>(states_) = value;
			return true;
		}
	}

	void invalidate() noexcept{
		is_valid_.reset();
	}
};

template <typename T>
struct state_trace_group{
private:
	std::vector<T> values_;

public:

	std::span<const T> get() const noexcept{
		return values_;
	}

	void resize(std::size_t count, const T& value){
		values_.resize(count, value);
	}

	template <std::ranges::input_range Rng>
		requires (std::convertible_to<std::ranges::range_reference_t<Rng>, T>)
	void assign(Rng&& value){
		values_.assign_range(value);
	}

	bool try_set(std::size_t index, const T& value){
		auto& current = values_.at(index);
		if constexpr (std::equality_comparable<T>){
			if(current != value){
				current = value;
				return true;
			}
		}else if(std::has_unique_object_representations_v<T>){
			if(std::memcmp(&current, &value, sizeof(T)) != 0){
				current = value;
				return true;
			}
		}else{
			static_assert(false, "No equality operator available");
		}
		return false;
	}

	void invalidate() noexcept{
		values_.clear();
	}
};

}