module;

#include <cassert>
#include <cstring>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.state_tracker;

import std;
export import mo_yanxi.graphic.draw.instruction.batch.common;

namespace mo_yanxi::graphic::draw::instruction {

export
struct state_tracker {
    using tag_type = mo_yanxi::binary_diff_trace::tag;

private:
    struct state_record {
        tag_type tag;
        std::uint32_t logical_offset; // 数据的逻辑起始偏移量
        std::uint32_t current_size;   // 当前数据的有效大小
        std::uint32_t capacity;       // 预分配的容量
        std::uint32_t buffer_offset;  // 在 storage_ 中的物理起始偏移量
        bool dirty;                   // 脏标记
        bool is_new;                  // 新增：标记是否为首次创建
    };

    // 所有的状态元数据，保持有序以支持二分查找
    std::vector<state_record> records_;

    // 全局唯一的扁平化内存池
    // 内存布局: [ Current Data (capacity) | Committed Data (capacity) ]
    std::vector<std::byte> storage_;

public:
    [[nodiscard]] state_tracker() {
        // 预分配一定内存，避免初期频繁扩容
        storage_.reserve(4096);
    }

    /**
     * @brief 扁平化的高速状态更新
     */
    void update(tag_type tag, std::span<const std::byte> payload, unsigned offset = 0) {
        if (payload.empty()) return;

        // 使用二分查找寻找现有状态
        auto it = std::ranges::lower_bound(records_, tag, {}, &state_record::tag);

        if (it == records_.end() || it->tag != tag) {
            // === 全新状态：首次写入 ===
            const std::uint32_t cap = static_cast<std::uint32_t>(payload.size());
            const std::uint32_t offset_in_buf = static_cast<std::uint32_t>(storage_.size());

            // 为 Current 和 Committed 一次性分配双倍空间
            storage_.resize(offset_in_buf + cap * 2, std::byte{0});

            // 写入 Current 数据
            std::memcpy(storage_.data() + offset_in_buf, payload.data(), payload.size());

            records_.insert(it, state_record{
                tag,
                offset,
                cap,
                cap,
                offset_in_buf,
                true, // dirty
                true  // is_new
            });
            return;
        }

        // === 现有状态：合并覆盖 ===
        auto& rec = *it;
        const unsigned req_start = offset;
        const unsigned req_end = offset + static_cast<unsigned>(payload.size());
        const unsigned cur_start = rec.logical_offset;
        const unsigned cur_end = cur_start + rec.current_size;

        const unsigned new_start = std::min(req_start, cur_start);
        const unsigned new_end = std::max(req_end, cur_end);
        const unsigned new_size = new_end - new_start;

        if (new_size > rec.capacity) {
            // 发生了罕见的容量暴涨：采用 Arena 风格，直接在末尾分配新空间，废弃旧空间 (速度极快)
            const unsigned new_cap = std::max(rec.capacity * 2, new_size);
            const unsigned new_buf_off = static_cast<unsigned>(storage_.size());
            storage_.resize(new_buf_off + new_cap * 2, std::byte{0});

            const unsigned old_rel = cur_start - new_start;

            if (rec.current_size > 0) {
                // 搬运原有的 Current 和 Committed 数据
                std::memcpy(storage_.data() + new_buf_off + old_rel,
                            storage_.data() + rec.buffer_offset,
                            rec.current_size);

                std::memcpy(storage_.data() + new_buf_off + new_cap + old_rel,
                            storage_.data() + rec.buffer_offset + rec.capacity,
                            rec.current_size);
            }

            rec.capacity = new_cap;
            rec.buffer_offset = new_buf_off;
        } else {
            // 容量足够，但如果逻辑偏移发生了向外扩张，需要处理内部偏移
            if (new_start < cur_start) {
                // 向左扩张：需要把原有数据向右推
                const unsigned shift = cur_start - new_start;
                std::byte* cur_ptr = storage_.data() + rec.buffer_offset;
                std::byte* com_ptr = cur_ptr + rec.capacity;

                std::memmove(cur_ptr + shift, cur_ptr, rec.current_size);
                std::memset(cur_ptr, 0, shift);

                std::memmove(com_ptr + shift, com_ptr, rec.current_size);
                std::memset(com_ptr, 0, shift);
            }
            if (new_end > cur_end) {
                // 向右扩张：确保新暴露出但 Payload 没覆盖到的尾部空间是 0
                const unsigned expand = new_end - cur_end;
                std::byte* cur_ptr = storage_.data() + rec.buffer_offset;
                std::byte* com_ptr = cur_ptr + rec.capacity;

                // 注意：由于上面可能发生了向右推，旧数据的新结尾变成了 (cur_end - new_start)
                const unsigned tail_offset = cur_end - new_start;
                std::memset(cur_ptr + tail_offset, 0, expand);
                std::memset(com_ptr + tail_offset, 0, expand);
            }
        }

        // 写入最新的 Payload 数据
        const unsigned new_rel = req_start - new_start;
        std::memcpy(storage_.data() + rec.buffer_offset + new_rel, payload.data(), payload.size());

        rec.logical_offset = new_start;
        rec.current_size = new_size;
        rec.dirty = true;
        // 注意：这里绝不修改 is_new 的值，以保留首次创建的状态
    }

    /**
     * @brief 高速延迟刷新
     */
    bool flush(state_transition_config& out_config) {
        bool has_changes = false;

        for (auto& rec : records_) {
            // 优化点 1：通过脏标记跳过绝大部分未操作的状态
            if (!rec.dirty) continue;

            const std::byte* cur_ptr = storage_.data() + rec.buffer_offset;
            std::byte* com_ptr = storage_.data() + rec.buffer_offset + rec.capacity;

            // 优化点 2：如果是新创建的 Tag，无条件 Emit；否则通过 memcmp 比较
            if (rec.current_size > 0 && (rec.is_new || std::memcmp(cur_ptr, com_ptr, rec.current_size) != 0)) {
                // 发生实质性变更，更新 committed 数据，并发出绘制指令
                std::memcpy(com_ptr, cur_ptr, rec.current_size);
                out_config.push(rec.tag, std::span{cur_ptr, rec.current_size}, rec.logical_offset);
                has_changes = true;
            }

            // 清理标记
            rec.dirty = false;
            rec.is_new = false;
        }

        return has_changes;
    }

    void reset() noexcept {
        records_.clear();
        storage_.clear();
    }
};

}