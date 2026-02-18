module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.state_tracker;

import std;
import binary_trace;
export import mo_yanxi.graphic.draw.instruction.batch.common;

namespace mo_yanxi::graphic::draw::instruction {

/**
 * @brief 惰性状态追踪器
 */
export
struct state_tracker {
    using tag_type = mo_yanxi::binary_diff_trace::tag;

private:
    struct committed_entry {
        tag_type tag;
        std::uint32_t logical_offset; // 该段数据的起始逻辑偏移
        std::vector<std::byte> data;
    };

    // GPU 影子状态
    std::vector<committed_entry> committed_state_;

    // 当前 Batch 的 Pending 状态
    mo_yanxi::binary_diff_trace pending_state_;

public:
    [[nodiscard]] state_tracker() = default;

    /**
     * @brief 更新状态
     * @param offset 数据的逻辑起始偏移量
     */
    void update(tag_type tag, std::span<const std::byte> payload, unsigned offset = 0) {
        // 在写入新数据前，必须确保 Pending Trace 包含该 Tag 的基准数据。
        // 如果 committed 状态中有数据，将其加载进去，这样 binary_trace 才能正确处理
        // "在现有数据中间挖洞/修改" 或 "扩展现有数据" 的逻辑，而不是简单地把未覆盖区域填零。
        ensure_base_loaded(tag);

        pending_state_.push(tag, payload, offset);
    }

    void undo(tag_type tag) {
        pending_state_.undo(tag);
    }

    /**
     * @brief 刷新差异
     */
    bool flush(state_transition_config& out_config) {
        if (pending_state_.empty()) {
            return false;
        }

        bool has_changes = false;
        auto pending_view = pending_state_.get_records();

        for (const auto& record : pending_view) {
            auto it = std::ranges::lower_bound(committed_state_, record.tag, {}, &committed_entry::tag);

            bool needs_emit = false;

            if (it != committed_state_.end() && it->tag == record.tag) {
                // === 现有 Tag: 比较并合并 ===

                // 1. 计算新的逻辑范围
                std::uint32_t new_start = std::min(it->logical_offset, record.offset);
                std::uint32_t new_end = std::max(
                    it->logical_offset + static_cast<std::uint32_t>(it->data.size()),
                    record.offset + static_cast<std::uint32_t>(record.range.size())
                );
                std::uint32_t new_size = new_end - new_start;

                bool is_range_changed = (it->logical_offset != record.offset) || (it->data.size() != record.range.size());

                if (is_range_changed || !std::ranges::equal(it->data, record.range)) {
                    // 数据有变更
                    // 注意：binary_trace 已经帮我们处理了 merge (A + B + Gap)，
                    // 所以 record.range 包含了该 Tag 下所有的 Pending 数据（包括从 Committed 加载的 Base）。
                    // 我们直接用 Pending 的结果覆盖 Committed 对应的部分。

                    // 这里有一个细微点：如果 Committed 原来是 [0, 100)，Pending 只是修改了 [50, 60)。
                    // 因为 ensure_base_loaded，Pending 变成了 [0, 100) (含新修改)。
                    // Record 就是 [0, 100)。
                    // 我们直接更新 Committed 为 [0, 100)，并输出整个 [0, 100) 指令。
                    // 优化：虽然带宽可能浪费，但保证了逻辑简单且绝对正确。

                    it->logical_offset = record.offset;
                    it->data.assign(record.range.begin(), record.range.end());
                    needs_emit = true;
                }
            } else {
                // === 新 Tag ===
                it = committed_state_.insert(it, {
                    record.tag,
                    record.offset,
                    {record.range.begin(), record.range.end()}
                });
                needs_emit = true;
            }

            if (needs_emit) {
                // 输出指令时携带逻辑偏移
                out_config.push(record.tag, record.range, record.offset);
                has_changes = true;
            }
        }

        pending_state_.clear();
        return has_changes;
    }

    void reset() noexcept {
        committed_state_.clear();
        pending_state_.clear();
    }

private:
    void ensure_base_loaded(tag_type tag) {
        if (pending_state_.contains(tag)) {
            return;
        }

        auto it = std::ranges::lower_bound(committed_state_, tag, {}, &committed_entry::tag);
        if (it != committed_state_.end() && it->tag == tag) {
            // 将 Committed 的现有数据作为 Base 推入 Pending。
            // 这一点至关重要：
            // 假设 Committed 有 [0, 100)。我们现在要 Update [120, 130)。
            // 如果不 Load Base，binary_trace 会认为 Pending 只有 [120, 130)，
            // 或者是 [0, 130) 但 [0, 120) 全是 0 (取决于 binary_trace 内部实现细节，
            // 但根据你的代码，它倾向于 merge)。
            // 通过 Push Base，我们保证 Pending 状态正确地反映了 "旧数据 + 新修改"。

            pending_state_.push(tag, it->data, it->logical_offset);
        }
    }
};

}
