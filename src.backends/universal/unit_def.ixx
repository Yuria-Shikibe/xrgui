export module mo_yanxi.backend.unit;

import std;

export namespace mo_yanxi::backend{

#pragma region Timing
using tick_ratio = std::ratio<1, 60>;

using timer_setter = double(*)();
using delta_setter = double(*)(double);
using time_reseter = void(*)(double);

template <typename T = float, typename Ratio = std::ratio<1>>
struct direct_access_time_unit : std::chrono::duration<T, Ratio>{
	using std::chrono::duration<T, Ratio>::count;
	using rep = std::chrono::duration<T, Ratio>::rep;
	using std::chrono::duration<T, Ratio>::duration;
	[[nodiscard]] constexpr direct_access_time_unit() noexcept = default;

	[[nodiscard]] constexpr explicit(false) direct_access_time_unit(const T Val) noexcept
	: std::chrono::duration<T, Ratio>(Val){
	}

	[[nodiscard]] constexpr explicit(false) operator T() const noexcept{
		return this->count();
	}

	using std::chrono::duration<T, Ratio>::operator++;
	using std::chrono::duration<T, Ratio>::operator--;

	using std::chrono::duration<T, Ratio>::operator%=;

	using std::chrono::duration<T, Ratio>::operator+=;
	using std::chrono::duration<T, Ratio>::operator-=;
	using std::chrono::duration<T, Ratio>::operator*=;
	using std::chrono::duration<T, Ratio>::operator/=;

	using std::chrono::duration<T, Ratio>::operator+;
	using std::chrono::duration<T, Ratio>::operator-;

	constexpr direct_access_time_unit& operator++() noexcept(std::is_arithmetic_v<rep>) /* strengthened */{
		this->std::chrono::duration<T, Ratio>::operator++();
		return *this;
	}

	constexpr direct_access_time_unit operator++(int) noexcept(std::is_arithmetic_v<rep>) /* strengthened */{
		auto t = *this;
		this->operator++();
		return t;
	}

	constexpr direct_access_time_unit& operator--() noexcept(std::is_arithmetic_v<rep>) /* strengthened */{
		this->std::chrono::duration<T, Ratio>::operator--();
		return *this;
	}

	constexpr direct_access_time_unit operator--(int) noexcept(std::is_arithmetic_v<rep>) /* strengthened */{
		auto t = *this;
		this->operator--();
		return t;
	}

	friend T operator%(direct_access_time_unit l, const std::convertible_to<T> auto val) noexcept{
		return std::fmod(T(l), val);
	}

	friend constexpr T operator+(direct_access_time_unit l, const std::convertible_to<T> auto val) noexcept{
		return T(l) + val;
	}

	friend constexpr T operator-(direct_access_time_unit l, const std::convertible_to<T> auto val) noexcept{
		return T(l) - val;
	}

	friend constexpr T operator*(direct_access_time_unit l, const std::convertible_to<T> auto val) noexcept{
		return T(l) * val;
	}

	friend constexpr T operator/(direct_access_time_unit l, const std::convertible_to<T> auto val) noexcept{
		return T(l) / val;
	}

	friend T operator%(const std::convertible_to<T> auto val, direct_access_time_unit l) noexcept{
		return std::fmod(val, T(l));
	}

	friend constexpr T operator+(const std::convertible_to<T> auto val, direct_access_time_unit l) noexcept{
		return val + T(l);
	}

	friend constexpr T operator-(const std::convertible_to<T> auto val, direct_access_time_unit l) noexcept{
		return val - T(l);
	}

	friend constexpr T operator*(const std::convertible_to<T> auto val, direct_access_time_unit l) noexcept{
		return val * T(l);
	}

	friend constexpr T operator/(const std::convertible_to<T> auto val, direct_access_time_unit l) noexcept{
		return val / T(l);
	}
};

using tick_t = direct_access_time_unit<float, tick_ratio>;
using sec = direct_access_time_unit<float>;
using sec_highres = direct_access_time_unit<double>;
#pragma endregion

}
