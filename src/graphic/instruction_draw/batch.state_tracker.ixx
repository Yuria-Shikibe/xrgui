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
        std::uint32_t logical_offset;
        std::uint32_t current_size;
        std::uint32_t capacity;
        std::uint32_t buffer_offset;
        bool dirty;
        bool is_new;
    };


    std::vector<state_record> records_;



    std::vector<std::byte> storage_;

public:
    [[nodiscard]] state_tracker() {

        storage_.reserve(4096);
    }

    /**
     * @brief 扁平化的高速状态更新
     */
    void update(tag_type tag, std::span<const std::byte> payload, unsigned offset = 0) {
        if (payload.empty()) return;


        auto it = std::ranges::lower_bound(records_, tag, {}, &state_record::tag);

        if (it == records_.end() || it->tag != tag) {

            const std::uint32_t cap = static_cast<std::uint32_t>(payload.size());
            const std::uint32_t offset_in_buf = static_cast<std::uint32_t>(storage_.size());


            storage_.resize(offset_in_buf + cap * 2, std::byte{0});


            std::memcpy(storage_.data() + offset_in_buf, payload.data(), payload.size());

            records_.insert(it, state_record{
                tag,
                offset,
                cap,
                cap,
                offset_in_buf,
                true,
                true
            });
            return;
        }


        auto& rec = *it;
        const unsigned req_start = offset;
        const unsigned req_end = offset + static_cast<unsigned>(payload.size());
        const unsigned cur_start = rec.logical_offset;
        const unsigned cur_end = cur_start + rec.current_size;

        const unsigned new_start = std::min(req_start, cur_start);
        const unsigned new_end = std::max(req_end, cur_end);
        const unsigned new_size = new_end - new_start;

        if (new_size > rec.capacity) {

            const unsigned new_cap = std::max(rec.capacity * 2, new_size);
            const unsigned new_buf_off = static_cast<unsigned>(storage_.size());
            storage_.resize(new_buf_off + new_cap * 2, std::byte{0});

            const unsigned old_rel = cur_start - new_start;

            if (rec.current_size > 0) {

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

            if (new_start < cur_start) {

                const unsigned shift = cur_start - new_start;
                std::byte* cur_ptr = storage_.data() + rec.buffer_offset;
                std::byte* com_ptr = cur_ptr + rec.capacity;

                std::memmove(cur_ptr + shift, cur_ptr, rec.current_size);
                std::memset(cur_ptr, 0, shift);

                std::memmove(com_ptr + shift, com_ptr, rec.current_size);
                std::memset(com_ptr, 0, shift);
            }
            if (new_end > cur_end) {

                const unsigned expand = new_end - cur_end;
                std::byte* cur_ptr = storage_.data() + rec.buffer_offset;
                std::byte* com_ptr = cur_ptr + rec.capacity;


                const unsigned tail_offset = cur_end - new_start;
                std::memset(cur_ptr + tail_offset, 0, expand);
                std::memset(com_ptr + tail_offset, 0, expand);
            }
        }


        const unsigned new_rel = req_start - new_start;
        std::memcpy(storage_.data() + rec.buffer_offset + new_rel, payload.data(), payload.size());

        rec.logical_offset = new_start;
        rec.current_size = new_size;
        rec.dirty = true;

    }

    /**
     * @brief 高速延迟刷新
     */
    bool flush(state_transition_config& out_config) {
        bool has_changes = false;

        for (auto& rec : records_) {

            if (!rec.dirty) continue;

            const std::byte* cur_ptr = storage_.data() + rec.buffer_offset;
            std::byte* com_ptr = storage_.data() + rec.buffer_offset + rec.capacity;


            if (rec.current_size > 0 && (rec.is_new || std::memcmp(cur_ptr, com_ptr, rec.current_size) != 0)) {

                std::memcpy(com_ptr, cur_ptr, rec.current_size);
                out_config.push(rec.tag, std::span{cur_ptr, rec.current_size}, rec.logical_offset);
                has_changes = true;
            }


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