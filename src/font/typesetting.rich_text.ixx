module;

#include <gch/small_vector.hpp>

export module mo_yanxi.typesetting.rich_text;

export import mo_yanxi.typesetting;

import mo_yanxi.font;
import mo_yanxi.math.vector2;
import mo_yanxi.graphic.color;
import std;

namespace mo_yanxi::type_setting{
namespace rich_text_token{

export enum struct setter_type{
	/**
	 * @brief set current to specified
	 */
	absolute,

	/**
	 * @brief set current to current + specified
	 */
	relative_add,

	/**
	 * @brief set current to current * specified
	 */
	relative_mul,
};

template <typename T>
using stack_of = optional_stack<T, gch::small_vector<T>>;


struct rich_text_context{
	stack_of<math::vec2> history_offset_;
	stack_of<graphic::color> history_color_;
	stack_of<font::glyph_size_type> history_size_;
	// stack_of<math::vec2> history_font_;
	stack_of<bool> history_underline_;
};

struct add_offset{
	setter_type type = setter_type::relative_add;
	math::vec2 offset;
};

struct set_color{
	setter_type type = setter_type::absolute;
	graphic::color color;
};

struct set_font{
	static_string<23> font_name;
};


struct set_size{
private:
	constexpr static std::uint16_t mul_26_6(std::uint16_t l, std::uint16_t r) noexcept{
		return static_cast<std::uint16_t>(std::uint32_t(l) * std::uint32_t(r) / 64);
	}
public:
	setter_type type = setter_type::absolute;
	font::glyph_size_type size;

	constexpr font::glyph_size_type apply_to(font::glyph_size_type dst) const noexcept{
		switch(type){
		case setter_type::absolute : return size;
		case setter_type::relative_add : return dst + size;
		case setter_type::relative_mul : return {mul_26_6(size.x, dst.x), mul_26_6(size.y, dst.y)};
		default : std::unreachable();
		}
	}
};

struct set_underline{
	bool enabled;
};

struct fallback_offset{};

struct fallback_color{};

struct fallback_font{};

struct fallback_size{};

struct fallback_underline{};

}

struct rich_text_token_argument{
	using tokens = std::variant<
		rich_text_token::add_offset,
		rich_text_token::set_color,
		rich_text_token::set_font,
		rich_text_token::set_size,
		rich_text_token::set_underline,

		rich_text_token::fallback_offset,
		rich_text_token::fallback_color,
		rich_text_token::fallback_font,
		rich_text_token::fallback_size,
		rich_text_token::fallback_underline
	>;

	tokens token;
};

export
struct tokenized_text{
	static constexpr char token_split_char = '|';

	struct token_sentinel{
		char signal{'#'};
		char begin{'<'};
		char end{'>'};
		bool reserve{false};
	};

	static constexpr token_sentinel default_sentinel{'#', '<', '>', false};

	struct posed_token_argument : rich_text_token_argument{
		std::uint32_t pos{};

		[[nodiscard]] posed_token_argument() = default;

		[[nodiscard]] explicit posed_token_argument(std::uint32_t pos)
			: pos(pos){
		}
	};

private:
	std::u32string codes{};

	std::vector<posed_token_argument> tokens{};

public:
	using pos_t = decltype(codes)::size_type;
	using token_iterator = decltype(tokens)::const_iterator;

	token_iterator get_token(const pos_t pos, const token_iterator& last){
		return std::ranges::lower_bound(last, tokens.end(), pos, {}, &posed_token_argument::pos);
	}

	token_iterator get_token(const pos_t pos){
		return get_token(pos, tokens.begin());
	}

	[[nodiscard]] auto get_token_group(const pos_t pos, const token_iterator& last) const{
		return std::ranges::equal_range(last, tokens.end(), pos, {}, &posed_token_argument::pos);
	}

	[[nodiscard]] tokenized_text() = default;

	[[nodiscard]] explicit(false) tokenized_text(const std::string_view string,
		const token_sentinel sentinel = default_sentinel);
};
}
