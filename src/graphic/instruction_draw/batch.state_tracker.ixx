module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.state_tracker;

export import mo_yanxi.graphic.draw.instruction.batch.common;
import std;
import binary_trace; // 引入 binary_trace 模块以使用 binary_diff_trace

namespace mo_yanxi::graphic::draw::instruction {

/**
 * @brief 惰性状态追踪器
 * * 维护两份状态：
 * 1. Committed State: GPU 当前已知的状态（影子状态），扁平化存储。
 * 2. Pending State: 当前 Batch 累积的修改，使用 binary_diff_trace 记录，支持 Undo。
 */
export
struct state_tracker {
    using tag_type = mo_yanxi::binary_diff_trace::tag;

private:
    struct committed_entry {
        tag_type tag;
        std::vector<std::byte> data;
    };

    // 扁平化存储 Committed 状态，按 Tag 排序以支持二分查找
    std::vector<committed_entry> committed_state_;

    // 追踪 Pending 状态变更，支持 Undo
    mo_yanxi::binary_diff_trace pending_state_;

public:
    [[nodiscard]] state_tracker() = default;

    /**
     * @brief 更新状态 (Pending)
     * 如果这是该 Tag 在当前 Batch 的首次修改，会自动从 Committed 状态加载基准数据。
     */
    void update(tag_type tag, std::span<const std::byte> payload, unsigned offset = 0) {
        // 确保 Pending 状态中有该 Tag 的基准数据，以便正确应用 Diff
        ensure_base_loaded(tag);
        pending_state_.push(tag, payload, offset);
    }

    /**
     * @brief 撤销最近一次对指定 Tag 的修改
     */
    void undo(tag_type tag) {
        pending_state_.undo(tag);
    }

    /**
     * @brief 将 Pending 状态的差异刷新到输出配置中
     * * 对比 Pending State 和 Committed State：
     * - 如果无差异，不生成输出。
     * - 如果有差异，生成输出并更新 Committed State。
     * - 清空 Pending State 以开始下一轮追踪。
     * * @param out_config 接收差异的状态配置对象
     * @return true 如果产生了实际的状态变更输出
     */
    bool flush(state_transition_config& out_config) {
        if (pending_state_.empty()) {
            return false;
        }

        bool has_changes = false;

        // 获取 Pending 状态的最终视图 (Consolidated View)
        auto pending_records = pending_state_.get_records();

        for (const auto& record : pending_records) {
            auto it = std::ranges::lower_bound(committed_state_, record.tag, {}, &committed_entry::tag);

            bool needs_update = false;

            if (it != committed_state_.end() && it->tag == record.tag) {
                // Tag 存在，检查数据内容是否一致
                if (!std::ranges::equal(it->data, record.range)) {
                    // 数据变更
                    it->data.assign(record.range.begin(), record.range.end());
                    needs_update = true;
                }
            } else {
                // Tag 不存在，新增
                it = committed_state_.insert(it, {record.tag, {record.range.begin(), record.range.end()}});
                needs_update = true;
            }

            if (needs_update) {
                out_config.push(record.tag, record.range);
                has_changes = true;
            }
        }

        // 清空 Pending 状态，准备下一个 Batch
        // 注意：Committed State 保持不变，作为 GPU 当前状态的快照
        pending_state_.clear();

        return has_changes;
    }

    /**
     * @brief 重置所有状态 (通常在帧开始或 EndRendering 时调用)
     */
    void reset() noexcept {
        committed_state_.clear();
        pending_state_.clear();
    }

private:
    /**
     * @brief 确保 Pending State 包含该 Tag 的基准数据
     * 如果 Pending 中没有该 Tag，尝试从 Committed 中复制一份作为起点。
     * 这是为了让 binary_trace 在空基准上正确应用 offset 和 diff。
     */
    void ensure_base_loaded(tag_type tag) {
        // 检查 binary_trace 中是否已有记录
        if (pending_state_.contains(tag)) {
            return;
        }

        // 如果 Pending 中没有，查找 Committed
        auto it = std::ranges::lower_bound(committed_state_, tag, {}, &committed_entry::tag);
        if (it != committed_state_.end() && it->tag == tag) {
            // 将 Committed 数据作为初始值推入 Pending (Offset 0)
            // 这样后续的 update(offset > 0) 才能基于正确的数据修改
            pending_state_.push(tag, it->data, 0);

            // 注意：这里我们利用 binary_trace 作为临时草稿本。
            // 虽然这会增加一条 history entry，但只要 flush 时只看最终结果即可。
            // Undo 逻辑需要注意：如果用户 undo 到了这个 base load 操作，
            // 实际上 pending 会变回空，这在 flush 时会被视为 "无变更" (相对于 committed)，逻辑是正确的。
        }
    }
};

}