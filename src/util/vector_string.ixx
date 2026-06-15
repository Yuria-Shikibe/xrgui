module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.vector_string;

import std;

namespace mo_yanxi {

export
template <
	typename CharT = char,
	typename Traits = std::char_traits<CharT>,
	typename Allocator = std::allocator<CharT>>
class basic_vector_string {
public:
	using traits_type = Traits;
	using allocator_type = Allocator;
	using char_type = CharT;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using string_type = std::basic_string<char_type, traits_type, allocator_type>;
	using string_view_type = std::basic_string_view<char_type, traits_type>;
	using span_type = std::span<char_type>;
	using const_span_type = std::span<const char_type>;
	using index_container_type = std::vector<size_type>;

	static constexpr char_type terminator = char_type{};
	static constexpr char_type separator = terminator;
	static constexpr size_type npos = static_cast<size_type>(-1);

	class const_iterator {
		friend class basic_vector_string;

		const char_type* base_{};
		const size_type* index_{};

		constexpr const_iterator(const char_type* base, const size_type* index) noexcept
			: base_(base), index_(index) {
		}

	public:
		using iterator_concept = std::random_access_iterator_tag;
		using iterator_category = std::random_access_iterator_tag;
		using value_type = string_view_type;
		using difference_type = basic_vector_string::difference_type;
		using pointer = void;
		using reference = string_view_type;

		[[nodiscard]] constexpr const_iterator() noexcept = default;

		[[nodiscard]] constexpr reference operator*() const noexcept {
			assert(base_ != nullptr && index_ != nullptr && "dereferencing default vector_string iterator");
			const size_type start = index_[-1] + 1;
			return reference{base_ + start, *index_ - start};
		}

		[[nodiscard]] constexpr reference operator[](difference_type offset) const noexcept {
			return *(*this + offset);
		}

		constexpr const_iterator& operator++() noexcept {
			++index_;
			return *this;
		}

		constexpr const_iterator operator++(int) noexcept {
			const_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		constexpr const_iterator& operator--() noexcept {
			--index_;
			return *this;
		}

		constexpr const_iterator operator--(int) noexcept {
			const_iterator tmp = *this;
			--(*this);
			return tmp;
		}

		constexpr const_iterator& operator+=(difference_type offset) noexcept {
			index_ += offset;
			return *this;
		}

		constexpr const_iterator& operator-=(difference_type offset) noexcept {
			return *this += -offset;
		}

		[[nodiscard]] friend constexpr const_iterator operator+(const_iterator iter, difference_type offset) noexcept {
			iter += offset;
			return iter;
		}

		[[nodiscard]] friend constexpr const_iterator operator+(difference_type offset, const_iterator iter) noexcept {
			iter += offset;
			return iter;
		}

		[[nodiscard]] friend constexpr const_iterator operator-(const_iterator iter, difference_type offset) noexcept {
			iter -= offset;
			return iter;
		}

		[[nodiscard]] friend constexpr difference_type operator-(const_iterator lhs, const_iterator rhs) noexcept {
			assert(lhs.base_ == rhs.base_ && "subtracting iterators from different vector_string objects");
			return lhs.index_ - rhs.index_;
		}

		[[nodiscard]] friend constexpr bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept {
			return lhs.base_ == rhs.base_ && lhs.index_ == rhs.index_;
		}

		[[nodiscard]] friend constexpr auto operator<=>(const const_iterator& lhs, const const_iterator& rhs) noexcept {
			assert(lhs.base_ == rhs.base_ && "comparing iterators from different vector_string objects");
			return lhs.index_ <=> rhs.index_;
		}
	};

	using iterator = const_iterator;

	[[nodiscard]] constexpr basic_vector_string()
		: indices_{npos} {
	}

	constexpr basic_vector_string(std::initializer_list<string_view_type> values)
		: basic_vector_string() {
		for(const string_view_type value : values) {
			this->push_back(value);
		}
	}

	constexpr basic_vector_string(const basic_vector_string& other)
		: indices_(other.indices_),
		  alloc_(std::allocator_traits<allocator_type>::select_on_container_copy_construction(other.alloc_)) {
#ifndef NDEBUG
		debug_modify_index_ = other.debug_modify_index_;
#endif
		this->copy_storage_from(other);
	}

	constexpr basic_vector_string& operator=(const basic_vector_string& other) {
		if(this == std::addressof(other)) {
			return *this;
		}

		basic_vector_string tmp(other);
		swap(tmp);
		return *this;
	}

	constexpr basic_vector_string(basic_vector_string&& other) noexcept
		: data_(std::exchange(other.data_, nullptr)),
		  capacity_(std::exchange(other.capacity_, 0)),
		  indices_(std::move(other.indices_)),
		  alloc_(std::move(other.alloc_)) {
#ifndef NDEBUG
		debug_modify_index_ = std::exchange(other.debug_modify_index_, npos);
#endif
		other.restore_empty_indices();
	}

	constexpr basic_vector_string& operator=(basic_vector_string&& other) noexcept {
		if(this == std::addressof(other)) {
			return *this;
		}

		deallocate_storage();
		data_ = std::exchange(other.data_, nullptr);
		capacity_ = std::exchange(other.capacity_, 0);
		indices_ = std::move(other.indices_);
		alloc_ = std::move(other.alloc_);
#ifndef NDEBUG
		debug_modify_index_ = std::exchange(other.debug_modify_index_, npos);
#endif
		other.restore_empty_indices();
		return *this;
	}

	constexpr ~basic_vector_string() {
		deallocate_storage();
	}

	constexpr void swap(basic_vector_string& other) noexcept {
		std::ranges::swap(data_, other.data_);
		std::ranges::swap(capacity_, other.capacity_);
		std::ranges::swap(indices_, other.indices_);
		std::ranges::swap(alloc_, other.alloc_);
#ifndef NDEBUG
		std::ranges::swap(debug_modify_index_, other.debug_modify_index_);
#endif
	}

	template <typename... Args>
	constexpr string_view_type emplace_back(Args&&... args) {
		string_type value(std::forward<Args>(args)...);
		return this->push_back(string_view_type{value});
	}

	constexpr string_view_type push_back(string_view_type value) {
		ensure_not_modifying();
		basic_vector_string::validate_string(value);
		return this->append_checked(value);
	}

	constexpr string_view_type push_back(const char_type* value) {
		return this->push_back(string_view_type{value, traits_type::length(value)});
	}

	constexpr string_view_type push_back(const string_type& value) {
		return this->push_back(string_view_type{value});
	}

	[[nodiscard]] constexpr size_type modify_begin() {
		return modify_begin(0);
	}

	[[nodiscard]] constexpr size_type modify_begin(size_type count) {
		ensure_not_modifying();

		const size_type index = size();
		const size_type start = storage_size();
		const size_type end = start + count;
		const size_type next_size = end + 1;

		indices_.reserve(indices_.size() + 1);
		ensure_capacity(next_size);
		if(count != 0) {
			traits_type::assign(data_ + start, count, terminator);
		}
		assign_terminator(end);
		indices_.push_back(end);
#ifndef NDEBUG
		debug_modify_index_ = index;
#endif
		return index;
	}

	constexpr string_view_type modify_end(size_type index) {
		ensure_active_modifier(index);

		const string_view_type value = (*this)[index];
		basic_vector_string::validate_string(value);
		assign_terminator(indices_[index + 1]);
#ifndef NDEBUG
		debug_modify_index_ = npos;
#endif
		return value;
	}

	[[nodiscard]] constexpr span_type span_at(size_type index) noexcept {
		assert(index < size() && "vector_string::span_at index out of bounds");
		const size_type start = string_start(index);
		return span_type{data_ + start, string_end(index) - start};
	}

	[[nodiscard]] constexpr const_span_type span_at(size_type index) const noexcept {
		assert(index < size() && "vector_string::span_at index out of bounds");
		const size_type start = string_start(index);
		return const_span_type{data_ + start, string_end(index) - start};
	}

	constexpr void clear() noexcept {
		indices_.resize(1);
		indices_[0] = npos;
#ifndef NDEBUG
		debug_modify_index_ = npos;
#endif
	}

	constexpr void reserve(size_type storage_capacity) {
		ensure_capacity(storage_capacity);
	}

	constexpr void reserve_strings(size_type count) {
		indices_.reserve(count + 1);
	}

	[[nodiscard]] constexpr string_view_type operator[](size_type index) const noexcept {
		assert(index < size() && "vector_string index out of bounds");
		const size_type start = string_start(index);
		return string_view_type{data_ + start, string_end(index) - start};
	}

	[[nodiscard]] constexpr string_view_type at(size_type index) const {
		if(index >= size()) {
			throw std::out_of_range{"vector_string::at index out of range"};
		}
		return (*this)[index];
	}

	[[nodiscard]] constexpr string_view_type front() const noexcept {
		assert(!empty() && "vector_string::front on empty vector_string");
		return (*this)[0];
	}

	[[nodiscard]] constexpr string_view_type back() const noexcept {
		assert(!empty() && "vector_string::back on empty vector_string");
		return (*this)[size() - 1];
	}

	[[nodiscard]] constexpr const_iterator begin() const noexcept {
		return const_iterator{data_, indices_.data() + 1};
	}

	[[nodiscard]] constexpr const_iterator end() const noexcept {
		return const_iterator{data_, indices_.data() + indices_.size()};
	}

	[[nodiscard]] constexpr const_iterator cbegin() const noexcept {
		return begin();
	}

	[[nodiscard]] constexpr const_iterator cend() const noexcept {
		return end();
	}

	[[nodiscard]] constexpr bool empty() const noexcept {
		return size() == 0;
	}

	[[nodiscard]] constexpr size_type size() const noexcept {
		return indices_.size() - 1;
	}

	[[nodiscard]] constexpr size_type storage_size() const noexcept {
		return empty() ? size_type{} : indices_.back() + 1;
	}

	[[nodiscard]] constexpr size_type capacity() const noexcept {
		return capacity_;
	}

	[[nodiscard]] constexpr size_type index_capacity() const noexcept {
		return indices_.capacity() == 0 ? 0 : indices_.capacity() - 1;
	}

	[[nodiscard]] constexpr bool modifying() const noexcept {
#ifndef NDEBUG
		return debug_modify_index_ != npos;
#else
		return false;
#endif
	}

	[[nodiscard]] constexpr const char_type* data() const noexcept {
		return data_;
	}

	[[nodiscard]] constexpr string_view_type storage() const noexcept {
		const size_type current_size = storage_size();
		if(current_size == 0) {
			return {};
		}
		return string_view_type{data_, current_size};
	}

	[[nodiscard]] constexpr std::span<const size_type> indices() const noexcept {
		return terminators();
	}

	[[nodiscard]] constexpr std::span<const size_type> terminators() const noexcept {
		return std::span<const size_type>{indices_.data() + 1, size()};
	}

	[[nodiscard]] friend constexpr bool operator==(const basic_vector_string& lhs, const basic_vector_string& rhs) noexcept {
		const size_type lhs_size = lhs.storage_size();
		if(lhs_size != rhs.storage_size() || lhs.indices_ != rhs.indices_) {
			return false;
		}

		return lhs_size == 0 || traits_type::compare(lhs.data_, rhs.data_, lhs_size) == 0;
	}

private:
	char_type* data_{};
	size_type capacity_{};
	index_container_type indices_;
#ifndef NDEBUG
	size_type debug_modify_index_{npos};
#endif
	ADAPTED_NO_UNIQUE_ADDRESS allocator_type alloc_{};

	[[nodiscard]] constexpr size_type string_start(size_type index) const noexcept {
		return indices_[index] + 1;
	}

	[[nodiscard]] constexpr size_type string_end(size_type index) const noexcept {
		return indices_[index + 1];
	}

	constexpr void assign_terminator(size_type index) noexcept {
		traits_type::assign(data_[index], terminator);
	}

	constexpr string_view_type append_checked(string_view_type value) {
		const size_type start = storage_size();
		const size_type end = start + value.size();
		const size_type next_size = end + 1;

		indices_.reserve(indices_.size() + 1);
		ensure_capacity(next_size);
		if(!value.empty()) {
			traits_type::copy(data_ + start, value.data(), value.size());
		}
		assign_terminator(end);
		indices_.push_back(end);
		return string_view_type{data_ + start, value.size()};
	}

	constexpr void copy_storage_from(const basic_vector_string& other) {
		const size_type other_size = other.storage_size();
		if(other_size == 0) {
			return;
		}

		data_ = std::allocator_traits<allocator_type>::allocate(alloc_, other_size);
		capacity_ = other_size;
		try {
			traits_type::copy(data_, other.data_, other_size);
		} catch(...) {
			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, capacity_);
			data_ = nullptr;
			capacity_ = 0;
			throw;
		}
	}

	constexpr void ensure_capacity(size_type required_capacity) {
		if(required_capacity <= capacity_) {
			return;
		}

		size_type next_capacity = capacity_ == 0 ? size_type{1} : capacity_ * 2;
		if(next_capacity < required_capacity) {
			next_capacity = required_capacity;
		}

		char_type* next_data = std::allocator_traits<allocator_type>::allocate(alloc_, next_capacity);
		try {
			const size_type current_size = storage_size();
			if(current_size != 0) {
				traits_type::copy(next_data, data_, current_size);
			}
		} catch(...) {
			std::allocator_traits<allocator_type>::deallocate(alloc_, next_data, next_capacity);
			throw;
		}

		if(data_ != nullptr) {
			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, capacity_);
		}
		data_ = next_data;
		capacity_ = next_capacity;
	}

	constexpr void deallocate_storage() noexcept {
		if(data_ != nullptr) {
			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, capacity_);
			data_ = nullptr;
			capacity_ = 0;
		}
	}

	static constexpr void validate_string(string_view_type value) {
		if(!value.empty() && traits_type::find(value.data(), value.size(), terminator) != nullptr) {
			throw std::invalid_argument{"vector_string string cannot contain the terminator character"};
		}
	}

	constexpr void ensure_not_modifying() const {
#ifndef NDEBUG
		if(debug_modify_index_ != npos) {
			throw std::logic_error{"vector_string already has an open modifier"};
		}
#endif
	}

	constexpr void ensure_active_modifier(size_type index) const {
		if(index >= size() || index + 1 != size()) {
			throw std::logic_error{"vector_string modifier must target the tail string"};
		}
#ifndef NDEBUG
		if(debug_modify_index_ != index) {
			throw std::logic_error{"vector_string modifier position is not active"};
		}
#endif
	}

	constexpr void restore_empty_indices() {
		if(indices_.empty()) {
			indices_.push_back(npos);
		}
	}
};

export using vector_string = basic_vector_string<char>;

} // namespace mo_yanxi

template <typename CharT, typename Traits, typename Allocator>
constexpr inline bool std::ranges::enable_view<mo_yanxi::basic_vector_string<CharT, Traits, Allocator>> = false;

template <typename CharT, typename Traits, typename Allocator>
constexpr inline bool std::ranges::enable_borrowed_range<mo_yanxi::basic_vector_string<CharT, Traits, Allocator>> = false;
