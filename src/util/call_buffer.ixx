module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.call_stream:call_stream_buffer;

import std;

namespace mo_yanxi{
export template <typename Allocator = std::allocator<std::byte>>
class call_stream_buffer{
public:
	using allocator_type = Allocator;
	using value_type = std::byte;
	using pointer = std::allocator_traits<allocator_type>::pointer;
	using const_pointer = std::allocator_traits<allocator_type>::const_pointer;

private:
	ADAPTED_NO_UNIQUE_ADDRESS allocator_type alloc_;
	pointer data_{nullptr};
	std::size_t size_{0};
	std::size_t capacity_{0};

public:
	explicit call_stream_buffer(const allocator_type& alloc = allocator_type()) noexcept
		: alloc_(alloc){
	}

	call_stream_buffer(const call_stream_buffer&) = delete;
	call_stream_buffer& operator=(const call_stream_buffer&) = delete;

	call_stream_buffer(call_stream_buffer&& other) noexcept
		: alloc_(std::move(other.alloc_)),
		  data_(std::exchange(other.data_, nullptr)),
		  size_(std::exchange(other.size_, 0)),
		  capacity_(std::exchange(other.capacity_, 0)){
	}

	call_stream_buffer& operator=(call_stream_buffer&& other) noexcept{
		if(this == &other) return *this;
		clear_and_deallocate();
		alloc_ = std::move(other.alloc_);
		data_ = std::exchange(other.data_, nullptr);
		size_ = std::exchange(other.size_, 0);
		capacity_ = std::exchange(other.capacity_, 0);
		return *this;
	}

	~call_stream_buffer(){
		clear_and_deallocate();
	}


	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback, pointer, pointer>
	pointer allocate_uninitialized(std::size_t extra_size, RelocationCallback&& on_relocate){
		if(size_ + extra_size > capacity_){
			this->grow_(size_ + extra_size, std::forward<RelocationCallback>(on_relocate));
		}
		pointer result = data_ + size_;
		size_ += extra_size;
		return result;
	}

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback, pointer, pointer>
	void reserve(std::size_t extra_size, RelocationCallback&& on_relocate){
		if(extra_size > capacity_){
			this->grow_(extra_size, std::forward<RelocationCallback>(on_relocate));
		}

		capacity_ = extra_size;
	}


	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback, pointer, pointer>
	void append(const void* src, std::size_t bytes, RelocationCallback&& on_relocate){
		pointer dest = this->allocate_uninitialized(bytes, std::forward<RelocationCallback>(on_relocate));
		std::memcpy(dest, src, bytes);
	}


	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback, pointer, pointer>
	void append_zeros(std::size_t bytes, RelocationCallback&& on_relocate){
		pointer dest = this->allocate_uninitialized(bytes, std::forward<RelocationCallback>(on_relocate));
		std::memset(dest, 0, bytes);
	}

	template <typename RelocationCallback>
		requires std::is_nothrow_invocable_v<RelocationCallback, pointer, pointer>
	void append_uninitialized(std::size_t bytes, RelocationCallback&& on_relocate){
		this->allocate_uninitialized(bytes, std::forward<RelocationCallback>(on_relocate));
	}

	[[nodiscard]] pointer data() noexcept{ return data_; }
	[[nodiscard]] const_pointer data() const noexcept{ return data_; }
	[[nodiscard]] std::size_t size() const noexcept{ return size_; }
	[[nodiscard]] std::size_t capacity() const noexcept{ return capacity_; }
	[[nodiscard]] bool empty() const noexcept{ return size_ == 0; }
	[[nodiscard]] allocator_type get_allocator() const noexcept{ return alloc_; }

	void clear() noexcept{
		size_ = 0;
	}

	void rollback_size(std::size_t previous_size) noexcept{
		assert(previous_size <= size_ && "Cannot rollback to a larger size");
		size_ = previous_size;
	}

private:
	void clear_and_deallocate() noexcept{
		if(data_){
			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, capacity_);
			data_ = nullptr;
			size_ = 0;
			capacity_ = 0;
		}
	}


	template <typename RelocationCallback>
	FORCE_INLINE void grow_(std::size_t required_capacity, RelocationCallback&& on_relocate){
		std::size_t new_capacity = capacity_ < 512 ? 512 : capacity_ * 2;
		while(new_capacity < required_capacity){
			new_capacity *= 2;
		}

		pointer new_data = std::allocator_traits<allocator_type>::allocate(alloc_, new_capacity);

		if(data_ != nullptr){
			if(size_ > 0){
				std::memcpy(new_data, data_, size_);

				ATTR_FORCEINLINE_SENTENCE on_relocate(data_, new_data);
			}

			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, capacity_);
		}

		data_ = new_data;
		capacity_ = new_capacity;
	}
};
}
