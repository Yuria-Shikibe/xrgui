//

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


    std::vector<chunk_start_pos, typename std::allocator_traits<Alloc>::template rebind_alloc<chunk_start_pos>> chunks_{};

public:
    [[nodiscard]] constexpr draw_record_chunked_storage() {

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

        chunks_.push_back({0, 0});
    }

    template <known_instruction Instr>
    void push(const Instr& instr) {
        storage_.push(instr);
    }

    void split(bool allow_empty) {
        std::uint32_t head_total = static_cast<std::uint32_t>(storage_.heads().size());
        std::uint32_t byte_total = static_cast<std::uint32_t>(storage_.data().size());


        if (allow_empty || head_total > chunks_.back().head_idx) {
            chunks_.push_back({head_total, byte_total});
        }
    }

    [[nodiscard]] instr_chunk operator[](std::size_t index) const {
        const auto& start_pos = chunks_[index];


        std::uint32_t head_end = static_cast<std::uint32_t>(storage_.heads().size());
        std::uint32_t byte_end = static_cast<std::uint32_t>(storage_.data().size());


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

        if (head_total == 0) {
            return 0;
        }

        if (head_total == chunks_.back().head_idx) [[unlikely]] {
            return chunks_.size() - 1;
        }

        return chunks_.size();
    }

    [[nodiscard]] const draw_record_storage<Alloc>& base_storage() const noexcept {
        return storage_;
    }
};

}