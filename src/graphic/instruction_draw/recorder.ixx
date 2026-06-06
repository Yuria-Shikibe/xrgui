//

//

export module mo_yanxi.graphic.g2d.recorder;

import std;
import mo_yanxi.graphic.g2d.general;
import mo_yanxi.user_data_entry;
import mo_yanxi.type_register;
import mo_yanxi.raw_byte_buffer;

namespace mo_yanxi::graphic::g2d {

export
/**
 * @brief CPU-side storage for abstract 2D drawing instructions.
 *
 * Style and element code emits typed instructions into this sink instead of
 * building vertices directly. The storage keeps compact instruction headers
 * separately from trivially-copyable payload bytes so the Vulkan backend can
 * upload a contiguous stream and let the GPU-side resolver expand it into
 * draw-ready geometry.
 */
template <typename Alloc = std::allocator<std::byte>>
struct draw_record_storage : emit_stream_sink<draw_record_storage<Alloc>> {
private:
	using data_allocator_type = typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;

    std::vector<instruction_head, typename std::allocator_traits<Alloc>::template rebind_alloc<instruction_head>> heads_{};
    raw_vector<std::byte, data_allocator_type, std::size_t> data_{};

    void push_bytes(const void* bytes, std::size_t size) {
        const auto cur = data_.size();
        data_.resize_and_overwrite(cur + size, [bytes, size, cur](std::byte* data, std::size_t, std::size_t requested_size) noexcept {
            if(size != 0) {
                std::memcpy(data + cur, bytes, size);
            }
            return requested_size;
        });
    }

    void push_bytes(const auto& val) {
        this->push_bytes(std::addressof(val), sizeof(val));
    }

	template <std::ranges::contiguous_range Rng>
		requires (std::ranges::sized_range<Rng> && std::is_trivially_copyable_v<std::ranges::range_value_t<Rng>>)
	void push_range_bytes(const Rng& rng) {
		this->push_bytes(std::ranges::data(rng), sizeof(std::ranges::range_value_t<Rng>) * std::ranges::size(rng));
	}

public:
    [[nodiscard]] draw_record_storage() = default;
    [[nodiscard]] explicit(false) draw_record_storage(const Alloc& alloc) : heads_(alloc), data_(alloc) {}

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
        heads_.push_back(make_instruction_head(instr));
        this->push_bytes(instr);
    }

	template <known_instruction Instr, typename... Args>
		requires (sizeof...(Args) > 0 && valid_consequent_argument<Instr, Args...>)
	void push(const Instr& instr, const Args&... args) {
		heads_.push_back(make_instruction_head(instr, args...));
		this->push_bytes(instr);
		([&] {
			if constexpr (std::ranges::contiguous_range<Args>) {
				this->push_range_bytes(args);
			} else {
				this->push_bytes(args);
			}
		}(), ...);
	}

	template <known_instruction Instr>
	void operator()(const Instr& instr) {
		this->push(instr);
	}

	template <known_instruction Instr, typename... Args>
		requires(sizeof...(Args) > 0)
	void operator()(const Instr& instr, const Args&... args) {
		this->push(instr, args...);
	}

	void operator()(emit_t, auto& sink) const {
		emit(sink, batch_push(heads(), data()));
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

	void operator()(emit_t, auto& sink) const {
		emit(sink, batch_push(heads, data));
	}
};

export
/**
 * @brief Draw instruction storage with explicit split points.
 *
 * Chunks preserve the same underlying instruction stream as
 * `draw_record_storage`, but expose ranges for callers that need to reuse or
 * replay separately recorded draw segments, such as text layout caches.
 */
template <typename Alloc = std::allocator<std::byte>>
struct draw_record_chunked_storage : graphic::g2d::emit_stream_sink<draw_record_chunked_storage<Alloc>> {
private:
    draw_record_storage<Alloc> storage_{};


    std::vector<chunk_start_pos, typename std::allocator_traits<Alloc>::template rebind_alloc<chunk_start_pos>> chunks_{};

public:
    [[nodiscard]] draw_record_chunked_storage() {

        chunks_.push_back({0, 0});
    }

    [[nodiscard]] explicit(false) draw_record_chunked_storage(const Alloc& alloc)
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

	template <known_instruction Instr, typename... Args>
		requires(sizeof...(Args) > 0)
	void push(const Instr& instr, const Args&... args) {
		storage_.push(instr, args...);
	}

	template <known_instruction Instr>
	void operator()(const Instr& instr) {
		push(instr);
	}

	template <known_instruction Instr, typename... Args>
		requires(sizeof...(Args) > 0)
	void operator()(const Instr& instr, const Args&... args) {
		push(instr, args...);
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
