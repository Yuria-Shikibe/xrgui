module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.call_stream:call_stream_buffer;

import std;
import mo_yanxi.raw_byte_buffer;

namespace mo_yanxi{
export template <typename Allocator = std::allocator<std::byte>>
class call_stream_buffer{
public:
	using allocator_type = Allocator;
	using value_type = std::byte;
	using buffer_type = raw_buffer<value_type, allocator_type, std::size_t>;
	using pointer = value_type*;
	using const_pointer = const value_type*;

private:
	static constexpr std::size_t min_capacity_ = 512;

	buffer_type buffer_;

public:
	explicit call_stream_buffer(const allocator_type& alloc = allocator_type()) noexcept(
		std::is_nothrow_copy_constructible_v<allocator_type>)
		: buffer_(alloc){
	}

	call_stream_buffer(const call_stream_buffer&) = delete;
	call_stream_buffer& operator=(const call_stream_buffer&) = delete;

	call_stream_buffer(call_stream_buffer&& other) noexcept(std::is_nothrow_move_constructible_v<buffer_type>) =
		default;

	call_stream_buffer& operator=(call_stream_buffer&& other) noexcept(std::is_nothrow_move_assignable_v<buffer_type>) =
		default;

	~call_stream_buffer() = default;

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback&, pointer, pointer>
	pointer allocate_uninitialized(std::size_t extra_size, RelocationCallback&& on_relocate){
		const std::size_t old_size = buffer_.size();
		const std::size_t required_size = this->checked_add_(old_size, extra_size);
		this->reserve_at_least_(required_size, on_relocate);

		pointer result = buffer_.data();
		if(result != nullptr){
			result += old_size;
		}

		this->set_size_uninitialized_(required_size);
		return result;
	}

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback&, pointer, pointer>
	void reserve(std::size_t required_capacity, RelocationCallback&& on_relocate){
		this->reserve_at_least_(required_capacity, on_relocate);
	}

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback&, pointer, pointer>
	void append(const void* src, std::size_t bytes, RelocationCallback&& on_relocate){
		pointer dest = this->allocate_uninitialized(bytes, std::forward<RelocationCallback>(on_relocate));
		if(bytes != 0){
			std::memcpy(dest, src, bytes);
		}
	}

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback&, pointer, pointer>
	void append_zeros(std::size_t bytes, RelocationCallback&& on_relocate){
		pointer dest = this->allocate_uninitialized(bytes, std::forward<RelocationCallback>(on_relocate));
		if(bytes != 0){
			std::memset(dest, 0, bytes);
		}
	}

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback&, pointer, pointer>
	void append_uninitialized(std::size_t bytes, RelocationCallback&& on_relocate){
		this->allocate_uninitialized(bytes, std::forward<RelocationCallback>(on_relocate));
	}

	[[nodiscard]] pointer data() noexcept{ return buffer_.data(); }
	[[nodiscard]] const_pointer data() const noexcept{ return buffer_.data(); }
	[[nodiscard]] std::size_t size() const noexcept{ return buffer_.size(); }
	[[nodiscard]] std::size_t capacity() const noexcept{ return buffer_.capacity(); }
	[[nodiscard]] bool empty() const noexcept{ return buffer_.empty(); }
	[[nodiscard]] allocator_type get_allocator() const noexcept(noexcept(buffer_.get_allocator())){
		return buffer_.get_allocator();
	}

	void clear() noexcept{
		buffer_.clear();
	}

	void rollback_size(std::size_t previous_size) noexcept{
		assert(previous_size <= buffer_.size() && "Cannot rollback to a larger size");
		buffer_.resize(previous_size);
	}

private:
	void clear_and_deallocate() noexcept{
		buffer_.release();
	}

	[[nodiscard]] std::size_t checked_add_(std::size_t lhs, std::size_t rhs) const{
		if(rhs > buffer_.max_size() - lhs){
			throw std::bad_array_new_length{};
		}
		return lhs + rhs;
	}

	[[nodiscard]] std::size_t next_capacity_(std::size_t required_capacity) const{
		std::size_t new_capacity = buffer_.capacity() < min_capacity_ ? min_capacity_ : buffer_.capacity() * 2;
		while(new_capacity < required_capacity){
			if(new_capacity > buffer_.max_size() / 2){
				return required_capacity;
			}
			new_capacity *= 2;
		}
		return new_capacity;
	}

	template <typename RelocationCallback>
	void reserve_at_least_(std::size_t required_capacity, RelocationCallback& on_relocate){
		if(required_capacity <= buffer_.capacity()) return;
		buffer_.reserve(this->next_capacity_(required_capacity),
		                [&on_relocate](std::byte* old_data, std::byte* new_data, std::size_t count) noexcept{
			                if(count == 0) return;
			                std::memcpy(new_data, old_data, count);
			                ATTR_FORCEINLINE_SENTENCE std::invoke(on_relocate, old_data, new_data);
		                });
	}

	void set_size_uninitialized_(std::size_t new_size) noexcept{
		assert(new_size <= buffer_.capacity());
		buffer_.resize_and_overwrite(new_size, [](std::byte*, std::size_t, std::size_t requested_size) noexcept{
			return requested_size;
		});
	}
};
}
