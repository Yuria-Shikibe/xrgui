module;

#include <cassert>

export module mo_yanxi.double_buffer;

import std;

namespace mo_yanxi {

export
template <typename T>
class double_buffer {
public:
	using value_type = T;
	using size_type = std::uint8_t;
	using reference = value_type&;
	using const_reference = const value_type&;

	static constexpr size_type buffer_count = 2;

private:
	std::array<value_type, buffer_count> buffers_{};
	size_type current_index_{};

	struct from_buffers_t {};

	constexpr double_buffer(
		from_buffers_t,
		value_type current_buffer,
		value_type backup_buffer
	) noexcept(std::is_nothrow_move_constructible_v<value_type>)
		: buffers_{std::move(current_buffer), std::move(backup_buffer)} {
	}

public:
	constexpr double_buffer() requires std::default_initializable<value_type> = default;

	template <typename... Args>
		requires std::constructible_from<value_type, Args&...>
	constexpr explicit double_buffer(Args&&... args)
		noexcept(std::is_nothrow_constructible_v<value_type, Args&...>)
		: buffers_{value_type(args...), value_type(args...)} {
	}

	[[nodiscard]] static constexpr double_buffer from_buffers(
		value_type current_buffer,
		value_type backup_buffer
	) noexcept(std::is_nothrow_move_constructible_v<value_type>) {
		return double_buffer{
			from_buffers_t{},
			std::move(current_buffer),
			std::move(backup_buffer)
		};
	}

	[[nodiscard]] static constexpr size_type size() noexcept {
		return buffer_count;
	}

	[[nodiscard]] constexpr size_type current_index() const noexcept {
		return current_index_;
	}

	[[nodiscard]] constexpr size_type backup_index() const noexcept {
		return buffer_count - 1 - current_index_;
	}

	constexpr void set_current_index(size_type index) noexcept {
		assert(index < buffer_count);
		current_index_ = index;
	}

	template <typename Self>
	[[nodiscard]] constexpr decltype(auto) current(this Self&& self) noexcept {
		return std::forward_like<Self>(self.buffers_[self.current_index_]);
	}

	template <typename Self>
	[[nodiscard]] constexpr decltype(auto) backup(this Self&& self) noexcept {
		return std::forward_like<Self>(self.buffers_[self.backup_index()]);
	}

	template <typename Self>
	[[nodiscard]] constexpr decltype(auto) get_cur(this Self&& self) noexcept {
		return std::forward_like<Self>(self.current());
	}

	template <typename Self>
	[[nodiscard]] constexpr decltype(auto) get_bak(this Self&& self) noexcept {
		return std::forward_like<Self>(self.backup());
	}

	[[nodiscard]] constexpr reference operator[](size_type index) noexcept {
		assert(index < buffer_count);
		return buffers_[index];
	}

	[[nodiscard]] constexpr const_reference operator[](size_type index) const noexcept {
		assert(index < buffer_count);
		return buffers_[index];
	}

	[[nodiscard]] constexpr std::span<value_type, buffer_count> buffers() noexcept {
		return std::span<value_type, buffer_count>{buffers_};
	}

	[[nodiscard]] constexpr std::span<const value_type, buffer_count> buffers() const noexcept {
		return std::span<const value_type, buffer_count>{buffers_};
	}

	constexpr void clear() noexcept(noexcept(std::declval<value_type&>().clear()))
		requires requires(value_type& value) { value.clear(); }
	{
		buffers_[0].clear();
		buffers_[1].clear();
	}

	constexpr void flip() noexcept {
		current_index_ = backup_index();
	}

	constexpr void swap() noexcept {
		flip();
	}

	constexpr void swap_buffers() noexcept(std::is_nothrow_swappable_v<value_type>)
		requires std::swappable<value_type>
	{
		std::ranges::swap(buffers_[0], buffers_[1]);
	}

	constexpr void swap_internal() noexcept(std::is_nothrow_swappable_v<value_type>)
		requires std::swappable<value_type>
	{
		swap_buffers();
	}

	constexpr void copy_current_to_backup()
		noexcept(noexcept(std::declval<value_type&>() = std::declval<const value_type&>()))
		requires std::assignable_from<value_type&, const value_type&>
	{
		backup() = current();
	}

	constexpr void copy_backup_to_current()
		noexcept(noexcept(std::declval<value_type&>() = std::declval<const value_type&>()))
		requires std::assignable_from<value_type&, const value_type&>
	{
		current() = backup();
	}
};

} // namespace mo_yanxi
