//
// Created by Matrix on 2025/12/28.
//

export module mo_yanxi.graphic.draw.instruction.recorder;

import std;
import mo_yanxi.graphic.draw.instruction.general;
import mo_yanxi.user_data_entry;
import mo_yanxi.type_register;

namespace mo_yanxi::graphic::draw::instruction {

export
template <typename Alloc = std::allocator<std::byte>>
struct draw_record_storage {
private:
    std::vector<instruction_head, typename std::allocator_traits<Alloc>::template rebind_alloc<instruction_head>> heads_{};
    std::vector<std::byte, typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>> data_{};

    void push_bytes(const void* bytes, std::size_t size) {
        const auto cur = data_.size();
        data_.resize(cur + size);
        std::memcpy(data_.data() + cur, bytes, size);
    }

    void push_bytes(const auto& val) {
        this->push_bytes(std::addressof(val), sizeof(val));
    }

public:
    [[nodiscard]] constexpr draw_record_storage() = default;
    [[nodiscard]] explicit(false) constexpr draw_record_storage(const Alloc& alloc) : heads_(alloc), data_(alloc) {}

    void reserve_heads(std::size_t count) {
        heads_.reserve(count);
    }

    void reserve_bytes(std::size_t size) {
        data_.reserve(size);
    }

    void clear() noexcept {
        heads_.clear();
        data_.clear();
    }

    template <known_instruction Instr>
    void push(const Instr& instr) {
        heads_.push_back(instruction::make_instruction_head(instr));
        this->push_bytes(instr);
    }

    std::span<const instruction_head> heads() const noexcept {
        return heads_;
    }

    // [MODIFIED] 将返回类型从 const std::byte* 改为 std::span，以便后续拆分 chunk 时可以获取 size
    std::span<const std::byte> data() const noexcept {
        return data_;
    }
};

export
struct chunk_start_pos {
    std::uint32_t head_idx;
    std::uint32_t byte_idx;
};

export
struct instr_chunk {
    std::span<const instruction_head> heads;
    std::span<const std::byte> data;
};

export
template <typename Alloc = std::allocator<std::byte>>
struct draw_record_chunked_storage {
private:
    draw_record_storage<Alloc> storage_{};

    // 只记录每个 chunk 的起始索引，内存占用减半
    std::vector<chunk_start_pos, typename std::allocator_traits<Alloc>::template rebind_alloc<chunk_start_pos>> chunks_{};

public:
    [[nodiscard]] constexpr draw_record_chunked_storage() {
        // 始终保留一个初始游标，代表第 0 个 chunk 的起点
        chunks_.push_back({0, 0});
    }

    [[nodiscard]] explicit(false) constexpr draw_record_chunked_storage(const Alloc& alloc)
        : storage_(alloc), chunks_(alloc) {
        chunks_.push_back({0, 0});
    }

    void reserve_heads(std::size_t count) { storage_.reserve_heads(count); }
    void reserve_bytes(std::size_t size) { storage_.reserve_bytes(size); }

    void clear() noexcept {
        storage_.clear();
        chunks_.clear();
        // 清理后重新推入初始游标
        chunks_.push_back({0, 0});
    }

    template <known_instruction Instr>
    void push(const Instr& instr) {
        storage_.push(instr);
    }

    void split(bool allow_empty) {
        std::uint32_t head_total = static_cast<std::uint32_t>(storage_.heads().size());
        std::uint32_t byte_total = static_cast<std::uint32_t>(storage_.data().size());

        // 只有当当前活跃 chunk 确实有写入新数据时，才记录新的分裂点作为下一 chunk 的起点
        if (allow_empty || head_total > chunks_.back().head_idx) {
            chunks_.push_back({head_total, byte_total});
        }
    }

    [[nodiscard]] instr_chunk operator[](std::size_t index) const {
        const auto& start_pos = chunks_[index];

        // 默认使用当前 buffer 的末尾作为区间终点（适用于末尾的 chunk）
        std::uint32_t head_end = static_cast<std::uint32_t>(storage_.heads().size());
        std::uint32_t byte_end = static_cast<std::uint32_t>(storage_.data().size());

        // 如果不是最后一个记录点，则使用下一 chunk 的起点作为当前 chunk 的终点
        if (index + 1 < chunks_.size()) {
            head_end = chunks_[index + 1].head_idx;
            byte_end = chunks_[index + 1].byte_idx;
        }

        return {
            storage_.heads().subspan(start_pos.head_idx, head_end - start_pos.head_idx),
            storage_.data().subspan(start_pos.byte_idx, byte_end - start_pos.byte_idx)
        };
    }

    [[nodiscard]] std::size_t chunk_count() const noexcept {
        std::uint32_t head_total = static_cast<std::uint32_t>(storage_.heads().size());
        // 没有任何指令被 push
        if (head_total == 0) {
            return 0;
        }
        // 如果最后一次 split() 之后还没有推入新指令，排除掉末尾那个空的活跃游标
        if (head_total == chunks_.back().head_idx) [[unlikely]] {
            return chunks_.size() - 1;
        }
        // 正常返回包含了活跃末尾 chunk 的总数
        return chunks_.size();
    }

    [[nodiscard]] const draw_record_storage<Alloc>& base_storage() const noexcept {
        return storage_;
    }
};

}