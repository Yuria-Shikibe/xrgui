//
// Created by Matrix on 2024/9/13.
//

export module mo_yanxi.gui.util;

import mo_yanxi.math.vector2;
import std;
import mo_yanxi.concepts;
import mo_yanxi.meta_programming;

namespace mo_yanxi::gui::util{

export
template <
	std::ranges::range Rng,
	std::predicate<std::ranges::range_const_reference_t<Rng>> Proj = std::identity>
	requires requires(Rng rng){
		std::ranges::rbegin(rng);
		std::ranges::empty(rng);
	}
auto countRowAndColumn(Rng&& rng, Proj pred_isEndRow = {}){
	math::usize2 grid{};

	if(std::ranges::empty(rng)){
		return grid;
	}

	std::uint32_t curX{};
	auto itr = std::ranges::begin(rng);
	for(; itr != std::ranges::prev(std::ranges::end(rng)); ++itr){
		curX++;

		if(std::invoke(pred_isEndRow, itr)){
			grid.max_x(curX);
			grid.y++;
			curX = 0;
		}
	}

	grid.y++;
	grid.max_x(++curX);

	return grid;
}

export
template <
	std::ranges::range Rng,
	std::predicate<std::ranges::range_const_reference_t<Rng>> Proj = std::identity>
	requires requires(Rng rng){
		std::ranges::rbegin(rng);
		std::ranges::empty(rng);
	}
auto countRowAndColumn_toVector(Rng&& rng, Proj pred_isEndRow = {}){
	std::vector<std::uint32_t> grid{};

	if(std::ranges::empty(rng)){
		return grid;
	}

	std::uint32_t curX{};
	auto itr = std::ranges::begin(rng);
	for(; itr != std::ranges::prev(std::ranges::end(rng)); ++itr){
		curX++;

		if(std::invoke(pred_isEndRow, itr)){
			grid.push_back(curX);
			curX = 0;
		}
	}

	grid.push_back(++curX);

	return grid;
}

export
float flipY(float height, const float height_in_valid, const float itemHeight){
	height = height_in_valid - height - itemHeight;
	return height;
}

export
math::vec2 flipY(math::vec2 pos_in_valid, const float height_in_valid, const float itemHeight){
	pos_in_valid.y = flipY(pos_in_valid.y, height_in_valid, itemHeight);
	return pos_in_valid;
}

export
/**
 * @brief
 * @return true if modification happens
 */
template <typename T>
	requires (std::equality_comparable<T> && std::is_move_assignable_v<T> && !std::is_trivial_v<T>)
constexpr bool try_modify(
	T& target, std::type_identity_t<T>&& value) noexcept(noexcept(target != value) && std::is_nothrow_move_assignable_v<T>){
	if(target != value){
		target = std::move(value);
		return true;
	}
	return false;
}

export
/**
 * @brief
 * @return true if modification happens
 */
template <typename T>
	requires (std::equality_comparable<T> && std::is_copy_assignable_v<T>)
constexpr bool try_modify(
	T& target, const T& value) noexcept(noexcept(target != value) && std::is_nothrow_copy_assignable_v<T>){
	if(target != value){
		target = value;
		return true;
	}
	return false;
}

export
/**
 * @brief
 * @return true if modification happens
 */
template <typename Lhs, typename Rhs>
	requires (std::equality_comparable_with<Lhs, Rhs> && std::assignable_from<Lhs&, Rhs&&>)
constexpr bool try_modify(
	Lhs& target, Rhs&& value) noexcept(noexcept(target != value) && std::is_nothrow_assignable_v<Lhs&, Rhs&&>){
	if(target != value){
		target = std::forward<Rhs>(value);
		return true;
	}
	return false;
}
}
