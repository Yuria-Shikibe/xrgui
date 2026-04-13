module;

#include <vulkan/vulkan.h>

export module mo_yanxi.backend.vulkan.renderer:barrier_automatic;

// 确保使用 std:: uint8_t 等规范
import std;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk;

namespace mo_yanxi::backend::vulkan {

/** 内部辅助：跟踪 Attachment 的当前状态以进行自动屏障转换 */
struct attachment_state {
    VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

enum class blit_sync_state : std::uint8_t {
    none,
    pending_barrier // 待决状态：刚被 Blit 修改，等待下一个操作决定同步策略
};

class attachment_sync_manager {
private:
    std::vector<attachment_state> draw_attachment_states_;
    std::vector<attachment_state> blit_attachment_states_;
    std::vector<blit_sync_state> blit_attachment_sync_states_;

    // 核心状态检查与屏障生成逻辑
    static void ensure_state(vk::cmd::dependency_gen& dep, attachment_state& state, VkImage image,
                             VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access, VkImageLayout next_layout) {
        const bool layout_changed = state.layout != next_layout;

        // 优化：如果是连续的颜色附件写入且没有布局切换，可以利用栅格化顺序跳过屏障
        const bool can_skip_color = !layout_changed &&
            (state.stage_mask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT && next_stage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) &&
            (state.access_mask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT && next_access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        if (can_skip_color) return;

        // 优化：如果是只读到只读的转换，且没有布局切换，也可以跳过
        auto is_write = [](VkAccessFlags2 a) {
            return a & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        };

        if (!layout_changed && !is_write(state.access_mask) && !is_write(next_access)) return;

        dep.push(image, state.stage_mask, state.access_mask, next_stage, next_access, state.layout, next_layout, vk::image::default_image_subrange);
        state = {next_stage, next_access, next_layout};
    }

public:
    attachment_sync_manager() = default;

    void resize(std::size_t draw_count, std::size_t blit_count) {
        draw_attachment_states_.resize(draw_count);
        blit_attachment_states_.resize(blit_count);
        blit_attachment_sync_states_.resize(blit_count, blit_sync_state::none);
    }

    // 帧初期的状态重置
    void reset_barriers() {
        for (auto& s : draw_attachment_states_) {
            s = {
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            };
        }
        for (auto& s : blit_attachment_states_) {
            s = {
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL
            };
        }
        std::ranges::fill(blit_attachment_sync_states_, blit_sync_state::none);
    }

    // --- 图形管线 (Graphics) 同步接口 ---

    // 为图形渲染通道的目标附件插入必要的同步屏障
    template <typename TargetMask, typename Attachments>
    void sync_draw_targets(vk::cmd::dependency_gen& dep, const TargetMask& target_mask, const Attachments& draw_attachments) {
        target_mask.for_each_popbit([&](unsigned idx) {
            ensure_state(dep, draw_attachment_states_[idx],
                draw_attachments[idx].get_image(),
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        });
    }

    // 处理来自 Compute 的待决状态，使其在进入 Graphic 管线前得到消化
    template <typename Attachments>
    void commit_pending_blit_to_graphics(vk::cmd::dependency_gen& dep, const Attachments& blit_attachments) {
        for (std::size_t idx = 0; idx < blit_attachment_sync_states_.size(); ++idx) {
            if (blit_attachment_sync_states_[idx] == blit_sync_state::pending_barrier) {
                dep.push(blit_attachments[idx].get_image(),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, // Graphic管线运行期间将自然消化
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);

                blit_attachment_sync_states_[idx] = blit_sync_state::none;
            }
        }
    }

    // --- 计算管线 (Compute/Blit) 同步接口 ---

    // 处理 Blit 的输入资源同步（来自 Graphic 的产出）
    template <typename InputEntries, typename Attachments>
    void sync_blit_inputs(vk::cmd::dependency_gen& dep, const InputEntries& inputs, const Attachments& draw_attachments) {
        for (const auto& e : inputs) {
            ensure_state(dep, draw_attachment_states_[e.resource_index],
                draw_attachments[e.resource_index].get_image(),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    // 处理背靠背的 Blit 输出同步（Compute 到 Compute）
    template <typename OutputEntries, typename Attachments>
    void sync_blit_outputs(vk::cmd::dependency_gen& dep, const OutputEntries& outputs, const Attachments& blit_attachments) {
        for (const auto& e : outputs) {
            const auto idx = e.resource_index;
            if (blit_attachment_sync_states_[idx] == blit_sync_state::pending_barrier) {
                // 场景 B：两个 Blit 背靠背执行，直接应用 Pipeline Barrier
                dep.push(blit_attachments[idx].get_image(),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);
                blit_attachment_sync_states_[idx] = blit_sync_state::none;
            }
        }
    }

    // 标记资源已被 Compute Shader 写入，挂起待决状态
    template <typename OutputEntries>
    void mark_blit_completed(const OutputEntries& outputs) {
        for (const auto& e : outputs) {
            blit_attachment_sync_states_[e.resource_index] = blit_sync_state::pending_barrier;
        }
    }
};

} // namespace mo_yanxi::backend::vulkan