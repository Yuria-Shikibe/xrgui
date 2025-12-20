module;

#include <cassert>
#include <mo_yanxi//adapted_attributes.hpp>

export module mo_yanxi.user_data_entry;

import mo_yanxi.meta_programming;
import mo_yanxi.type_register;
import std;

namespace mo_yanxi::graphic::draw{

export
struct user_data_indices{
	std::uint32_t global_index;
	std::uint32_t group_index;
};

export
struct user_data_entry{
	std::uint32_t size;
	std::uint32_t local_offset;
	std::uint32_t global_offset;
	std::uint32_t group_index;

	explicit operator bool() const noexcept{
		return size != 0;
	}

	[[nodiscard]] std::span<const std::byte> to_range(const std::byte* base_address) const noexcept{
		return {base_address + global_offset, size};
	}
};

export
struct user_data_entries{
	const std::byte* base_address;
	std::span<const user_data_entry> entries;

	explicit operator bool() const noexcept{
		return !entries.empty();
	}
};

export
struct user_data_identity_entry{
	type_identity_index id;
	user_data_entry entry;
};

export
template <typename Container = std::vector<user_data_identity_entry>, std::size_t Align = 16>
struct user_data_index_table{
	static_assert(std::has_single_bit(Align));
	static_assert(std::same_as<std::ranges::range_value_t<Container>, user_data_identity_entry>, "Container must have user_data_identity_entry as value_type");
	static_assert(std::ranges::contiguous_range<Container>, "Container must be contiguous");
	static_assert(std::ranges::sized_range<Container>, "Container must be contiguous");

	static constexpr std::size_t align = Align;

	static constexpr bool is_allocator_aware = requires{
		typename Container::allocator_type;
	};

	static constexpr bool is_reservable = requires(Container& cont, std::size_t sz){
		cont.reserve();
	};

	using allocator_type = decltype([]{
		if constexpr (is_allocator_aware){
			return typename Container::allocator_type{};
		}else{
			return ;
		}
	}());

private:
	std::size_t required_capacity_{};
	Container entries{};

	template <typename ...Ts>
	void load(){
		auto push = [&]<typename Ty, std::size_t I>(std::size_t current_base_size){
			entries.push_back(user_data_identity_entry{
				.id = unstable_type_identity_of<Ty>(),
				.entry = {
					.size = static_cast<std::uint32_t>(sizeof(Ty)),
					.local_offset = static_cast<std::uint32_t>(required_capacity_ - current_base_size),
					.global_offset = static_cast<std::uint32_t>(required_capacity_),
					.group_index = I,
				}
			});

			required_capacity_ += (sizeof(Ty) + align - 1) / align * align;
		};

		[&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
			([&]<typename T, std::size_t I>(){
				const auto cur_base = required_capacity_;

				[&]<std::size_t ...J>(std::index_sequence<J...>){
					(push.template operator()<std::tuple_element_t<J, T>, I>(cur_base), ...);
				}(std::make_index_sequence<std::tuple_size_v<T>>{});
			}.template operator()<Ts, Idx>(), ...);
		}(std::index_sequence_for<Ts...>());
	}

public:
	template <typename T, std::size_t>
	friend struct user_data_index_table;

	[[nodiscard]] user_data_index_table() = default;

	template <std::ranges::input_range InputRng, typename ...Ts>
		requires (std::convertible_to<std::ranges::range_value_t<InputRng>, user_data_identity_entry> && std::constructible_from<Container, Ts&&...>)
	[[nodiscard]] explicit(false) user_data_index_table(const InputRng& other, Ts&& ...container_args) : entries(std::forward<Ts>(container_args)...){
		if constexpr (is_reservable && std::ranges::sized_range<const InputRng&>){
			entries.reserve(std::ranges::size(other));
		}
		std::ranges::copy(other, std::back_inserter(entries));
		if(!std::ranges::empty(entries)){
			const user_data_identity_entry& last = *std::ranges::rbegin(entries);
			required_capacity_ = last.entry.global_offset + (last.entry.size + align - 1) / align * align;
		}
	}

	template <typename ...Ts>
		requires (is_tuple_v<Ts> && ...)
	[[nodiscard]] explicit(false) user_data_index_table(
		const allocator_type& allocator,
		std::in_place_type_t<Ts>...
		) requires(is_allocator_aware) : entries(allocator){
		this->load<Ts...>();
	}

	template <typename ...Ts>
		requires (is_tuple_v<Ts> && ...)
	[[nodiscard]] explicit(false) user_data_index_table(
		std::in_place_type_t<Ts>...
	){
		this->load<Ts...>();
	}

	[[nodiscard]] std::uint32_t required_capacity() const noexcept{
		return required_capacity_;
	}

	[[nodiscard]] user_data_identity_entry* begin() noexcept{
		return std::ranges::data(entries);
	}

	[[nodiscard]] user_data_identity_entry* end() noexcept{
		return std::ranges::data(entries) + std::ranges::size(entries);
	}

	[[nodiscard]] const user_data_identity_entry* begin() const noexcept{
		return std::ranges::data(entries);
	}

	[[nodiscard]] const user_data_identity_entry* end() const noexcept{
		return std::ranges::data(entries) + std::ranges::size(entries);
	}

	[[nodiscard]] std::uint32_t group_size_at(const user_data_identity_entry* where) const noexcept{
		const auto end_ = end();
		assert(where < end_ && where > std::ranges::data(entries));

		const std::uint32_t base_offset{where->entry.group_index - where->entry.local_offset};
		for(auto p = where; p != end_; p++){
			if(p->entry.group_index != where->entry.group_index){
				return (p->entry.group_index - p->entry.local_offset) - base_offset;
			}
		}

		return required_capacity_ - base_offset;
	}


	[[nodiscard]] std::size_t size() const noexcept{
		return std::ranges::size(entries);
	}

	[[nodiscard]] bool empty() const noexcept{
		return std::ranges::empty(entries);
	}

	[[nodiscard]] const user_data_identity_entry* operator[](type_identity_index id) const noexcept{
		const auto beg = begin();
		const auto end = beg + size();
		return std::ranges::find(beg, end, id, &user_data_identity_entry::id);
	}

	template <typename T>
	[[nodiscard]] const user_data_identity_entry& at() const noexcept{
		auto ptr = (*this)[unstable_type_identity_of<T>()];
		assert(ptr != std::ranges::data(entries) + std::ranges::size(entries));
		return *ptr;
	}

	[[nodiscard]] const user_data_entry& operator[](std::uint32_t idx) const noexcept{
		assert(idx < size());
		return entries[idx].entry;
	}

	template <typename T>
	[[nodiscard]] FORCE_INLINE user_data_indices index_of() const noexcept{
		const user_data_identity_entry* ptr = (*this)[unstable_type_identity_of<T>()];
		assert(ptr < end());
		return {static_cast<std::uint32_t>(ptr - begin()), ptr->entry.group_index};
	}

	[[nodiscard]] FORCE_INLINE user_data_indices index_of(type_identity_index index) const noexcept{
		const user_data_identity_entry* ptr = (*this)[index];
		assert(ptr < end());
		return {static_cast<std::uint32_t>(ptr - begin()), ptr->entry.group_index};
	}

	[[nodiscard]] FORCE_INLINE user_data_indices index_of_checked(type_identity_index index) const{
		const user_data_identity_entry* ptr = (*this)[index];
		if(ptr >= end()){
			throw std::out_of_range("customized type index out of range");
		}
		return {static_cast<std::uint32_t>(ptr - begin()), ptr->entry.group_index};
	}

	auto get_entries() const noexcept {
		return std::span{begin(), size()};
	}

	auto get_entries_mut() noexcept {
		return std::span{begin(), size()};
	}

	void append(const user_data_index_table& other){
		if(std::ranges::empty(entries)){
			*this = other;
			return;
		}
		auto group_base = static_cast<user_data_identity_entry&>(*std::ranges::rbegin(entries)).entry.group_index + 1;
		if constexpr (is_reservable){
			entries.reserve(size() + other.size());
		}

		for (user_data_identity_entry entry : other.entries){
			entry.entry.group_index += group_base;
			entry.entry.global_offset += required_capacity_;
			entries.push_back(entry);
		}
		required_capacity_ += other.required_capacity_;
	}
};

}