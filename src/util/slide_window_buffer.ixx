module;

#include <mo_yanxi/adapted_attributes.hpp>
#include <cassert>

export module mo_yanxi.slide_window_buf;
import std;

namespace mo_yanxi{

export
template <typename T, unsigned Cap = 16, unsigned ReverseWithCount = 3>
	requires (ReverseWithCount >= 1 && Cap > ReverseWithCount)
struct slide_window_buffer{
	using value_type = T;

private:
	static constexpr unsigned reverse_count = ReverseWithCount;
	static constexpr unsigned capacity = Cap;
	std::array<value_type, reverse_count + capacity> buffer{};
	unsigned current_idx{};

public:
	[[nodiscard]] FORCE_INLINE constexpr slide_window_buffer() noexcept = default;

	[[nodiscard]] FORCE_INLINE constexpr explicit(false) slide_window_buffer(const value_type& initial) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : buffer{initial, initial}, current_idx(2) {
	}

	FORCE_INLINE constexpr bool push_back(const value_type& node) noexcept(std::is_nothrow_copy_assignable_v<T>) {
		if(!current_idx) [[unlikely]] {
            std::fill_n(buffer.begin(), reverse_count - 1, node);
            current_idx = reverse_count - 1;
			return false;
		}

		buffer[current_idx++] = node;
		return current_idx == buffer.size();
	}

	FORCE_INLINE constexpr bool push_back(value_type&& node) noexcept(std::is_nothrow_copy_assignable_v<T> && std::is_nothrow_move_assignable_v<T>) {
		if(!current_idx) [[unlikely]] {
            std::fill_n(buffer.begin(), reverse_count - 1, node);
            current_idx = reverse_count - 1;
			return false;
		}

		buffer[current_idx++] = std::move(node);
		return current_idx == buffer.size();
	}

	FORCE_INLINE constexpr bool push_unchecked(const value_type& node) noexcept(std::is_nothrow_copy_assignable_v<T>) {
		assert(current_idx >= reverse_count - 1);
		buffer[current_idx++] = node;
		return current_idx == buffer.size();
	}

	FORCE_INLINE constexpr void advance() noexcept(std::is_nothrow_move_assignable_v<T>){
		assert(current_idx == buffer.size());
		move_reverse();
		current_idx = reverse_count;
	}

	FORCE_INLINE constexpr bool finalize() noexcept(std::is_nothrow_copy_assignable_v<T>){
		if(current_idx < reverse_count) return false;

		if(current_idx == buffer.size()){
			move_reverse();
            current_idx = reverse_count;
		}

		buffer[current_idx] = buffer[current_idx - 1];
		++current_idx;
		return true;
	}

	FORCE_INLINE constexpr auto span() const noexcept{
		return std::span{buffer.data(), current_idx};
	}

	FORCE_INLINE constexpr auto begin() const noexcept {
		return buffer.data();
	}

	FORCE_INLINE constexpr auto end() const noexcept{
		return buffer.data() + current_idx;
	}

	FORCE_INLINE constexpr auto data() const noexcept{
		return buffer.data();
	}

private:
	FORCE_INLINE constexpr void move_reverse() noexcept {
		std::ranges::move(buffer.data() + buffer.size() - reverse_count,
                          buffer.data() + buffer.size(),
                          buffer.data());
	}
};

export
template <typename T, std::size_t A, std::size_t B, std::invocable<slide_window_buffer<T, A, B>&> CallbackType>
class slide_window_output_iterator {
	using buffer_type = slide_window_buffer<T, A, B>;
	buffer_type* buf_;
	CallbackType callback_;

	static_assert(std::is_object_v<CallbackType>);

public:
	using iterator_category = std::output_iterator_tag;
	using value_type        = void;
	using difference_type   = std::ptrdiff_t;
	using pointer           = void;
	using reference         = void;

	template <typename Fn>
	constexpr slide_window_output_iterator(buffer_type& buf, Fn&& callback) noexcept(std::is_nothrow_constructible_v<CallbackType, Fn&&>)
		: buf_(&buf), callback_(std::forward<Fn>(callback)) {}

	template <std::convertible_to<T> U>
	constexpr slide_window_output_iterator& operator=(U&& value) {
		if (buf_->push_back(std::forward<U>(value))) {
			std::invoke(callback_, *buf_);
			buf_->advance();
		}
		return *this;
	}

	template <typename S>
	decltype(auto) callback(this S&& self) noexcept{
		return std::forward_like<S>(self.callback_);
	}

	[[nodiscard]] constexpr slide_window_output_iterator& operator*() noexcept{
		return *this;
	}

	constexpr slide_window_output_iterator& operator++() noexcept{
		return *this;
	}

	constexpr slide_window_output_iterator& operator++(int) noexcept{
		return *this;
	}
};

template <typename T, std::size_t A, std::size_t B, std::invocable<slide_window_buffer<T, A, B>&> CallbackType>
slide_window_output_iterator(slide_window_buffer<T, A, B>&, CallbackType&&) -> slide_window_output_iterator<T, A, B, std::decay_t<CallbackType>>;


struct slide_window_buffer_consumer_protocol{
	template <typename T, unsigned Cap = 16, unsigned ReverseWithCount = 3>
	void operator()(const slide_window_buffer<T, Cap, ReverseWithCount>& buf) = delete;

	template <typename T, unsigned Cap = 16, unsigned ReverseWithCount = 3>
	void push(this auto& self, slide_window_buffer<T, Cap, ReverseWithCount>& buf, const T& v) noexcept(noexcept(self(buf))){
		if(buf.push_back(v)){
			self(buf);
			buf.advance();
		}
	}

	template <typename T, unsigned Cap = 16, unsigned ReverseWithCount = 3>
	void finalize(this auto& self, slide_window_buffer<T, Cap, ReverseWithCount>& buf) noexcept(noexcept(self(buf))) {
		if(buf.finalize()){
			self(buf);
		}
	}
};


export
template <typename T, unsigned Cap = 16, unsigned ReverseWithCount = 3>
struct slide_window_generator : public std::ranges::view_interface<slide_window_generator<T, Cap, ReverseWithCount>>{
	struct promise_type;
	using handle_type = std::coroutine_handle<promise_type>;
	using buffer_type = slide_window_buffer<T, Cap, ReverseWithCount>;

	struct cond_awaiter{
		buffer_type& buffer;
		bool is_ready{};

		[[nodiscard]] FORCE_INLINE constexpr bool await_ready() const noexcept {
			return is_ready;
		}

		FORCE_INLINE static constexpr void await_suspend(std::coroutine_handle<>) noexcept {}

		FORCE_INLINE constexpr void await_resume() noexcept{
			if(!is_ready)buffer.advance();
		}
	};

	struct promise_type{
		buffer_type buffer{};
        bool final_chunk_available{false};

		slide_window_generator get_return_object() noexcept {
			return slide_window_generator{handle_type::from_promise(*this)};
		}

		static std::suspend_never initial_suspend() noexcept { return {}; }
		static std::suspend_always final_suspend() noexcept { return {}; }
		static void unhandled_exception(){
			std::terminate();
		}

		template<std::convertible_to<typename buffer_type::value_type> From>
		FORCE_INLINE cond_awaiter yield_value(From&& from) noexcept {
			return cond_awaiter{buffer, !buffer.push_back(typename buffer_type::value_type{std::forward<From>(from)})};
		}

		FORCE_INLINE void return_void() noexcept{
			final_chunk_available = buffer.finalize();
		}
	};

	struct iterator{
		friend slide_window_generator;

	private:
		[[nodiscard]] explicit(false) iterator(handle_type handle)
			: handle(handle){
		}

		handle_type handle{};

	public:
		using value_type = buffer_type;

		FORCE_INLINE iterator& operator++() noexcept{
			if(!handle.done()) [[likely]] {
				handle.resume();
			} else [[unlikely]] {
				handle.promise().final_chunk_available = false;
			}
			return *this;
		}

		FORCE_INLINE void operator++(int) noexcept{
			(void)++*this;
		}

		FORCE_INLINE [[nodiscard]] const auto& operator*() const noexcept {
			return handle.promise().buffer;
		}

		FORCE_INLINE friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept {
			return it.handle.done() && !it.handle.promise().final_chunk_available;
		}
	};

	~slide_window_generator(){
		if (h_) h_.destroy();
	}

	slide_window_generator(const slide_window_generator& other) = delete;

	slide_window_generator(slide_window_generator&& other) noexcept
		: h_{std::exchange(other.h_, {})}{
	}

	slide_window_generator& operator=(const slide_window_generator& other) = delete;

	slide_window_generator& operator=(slide_window_generator&& other) noexcept{
		if(this == &other) return *this;
		if (h_) h_.destroy();
		h_ = std::exchange(other.h_, {});
		return *this;
	}

	[[nodiscard]] FORCE_INLINE iterator begin(this auto& self) noexcept{
		return iterator{self.h_};
	}

	[[nodiscard]] FORCE_INLINE static std::default_sentinel_t end() noexcept{
		return std::default_sentinel;
	}

private:
	[[nodiscard]] explicit slide_window_generator(const handle_type& h) noexcept
		: h_(h){
	}

	[[nodiscard]] slide_window_generator() = default;

	handle_type h_{};
};

export
template <unsigned Cap = 16, unsigned ReverseWithCount = 3>
struct slide_window_dependent_generator{
	template <typename T>
	using type = slide_window_generator<T, Cap, ReverseWithCount>;
};

}