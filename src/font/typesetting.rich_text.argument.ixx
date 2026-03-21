module;

#include <hb.h>
#include <freetype/freetype.h>

export module mo_yanxi.typesetting.rich_text:argument;

import std;
import mo_yanxi.font.manager;
import mo_yanxi.utility;
import mo_yanxi.static_string;
import mo_yanxi.typesetting.util;
import mo_yanxi.heterogeneous;
import mo_yanxi.graphic.color;

export
constexpr bool operator==(const hb_feature_t& lhs, const hb_feature_t& rhs) noexcept{
	return lhs.start == rhs.start && lhs.end == rhs.end && lhs.tag == rhs.tag && lhs.value == rhs.value;
}

namespace mo_yanxi::typesetting{
export
struct rich_text_look_up_table{
	string_hash_map<font::font_family> family;
	string_hash_map<graphic::color> color;
};

export inline rich_text_look_up_table* look_up_table{};

export namespace rich_text_token{
enum struct setter_type{
	/** @brief set current to specified */
	absolute,

	/** @brief set current to current + specified */
	relative_add,

	/** @brief set current to current * specified */
	relative_mul,
};


struct set_offset{
	setter_type type = setter_type::relative_add;
	math::vec2 offset;

	constexpr bool operator==(const set_offset&) const noexcept = default;
};

struct set_color{
	setter_type type = setter_type::absolute;
	graphic::color color;

	constexpr bool operator==(const set_color&) const noexcept = default;
};

struct set_font_by_name{
	static_string<23> font_name;

	constexpr bool operator==(const set_font_by_name&) const noexcept = default;
};

struct set_font_directly{
	const font::font_family* family;

	constexpr bool operator==(const set_font_directly&) const noexcept = default;
};


struct set_size{
private:
	constexpr static std::uint16_t mul_26_6(std::uint16_t l, std::uint16_t r) noexcept{
		return static_cast<std::uint16_t>(std::uint32_t(l) * std::uint32_t(r) / 64);
	}

public:
	setter_type type = setter_type::absolute;
	math::vec2 size;

	constexpr bool operator==(const set_size&) const noexcept = default;
};



struct set_feature{
	hb_feature_t feature;
	constexpr bool operator==(const set_feature& other) const noexcept{
		return feature == other.feature;
	}
};

struct set_underline{
	bool enabled;
	constexpr bool operator==(const set_underline&) const noexcept = default;
};

struct set_italic {
	bool enabled;
	constexpr bool operator==(const set_italic&) const noexcept = default;
};

struct set_bold {
	bool enabled;
	constexpr bool operator==(const set_bold&) const noexcept = default;
};


enum struct script_type{
	ends,
	sups,
	subs
};

struct set_script{
	script_type type;
	constexpr bool operator==(const set_script&) const noexcept = default;
};

/**
 * @brief using empty arguments to spec fallback
 */
struct fallback_offset{
	constexpr bool operator==(const fallback_offset&) const noexcept = default;
};

struct fallback_color{
	constexpr bool operator==(const fallback_color&) const noexcept = default;
};

struct fallback_font{
	constexpr bool operator==(const fallback_font&) const noexcept = default;
};

struct fallback_size{
	constexpr bool operator==(const fallback_size&) const noexcept = default;
};

struct fallback_feature{
	constexpr bool operator==(const fallback_feature&) const noexcept = default;
};

struct setter_parse_result{
	setter_type type;
	std::string_view remain;
};

using tokens = std::variant<
	std::monostate, //used for invalid token

	set_underline,
	set_italic,
	set_bold,

	set_offset,
	set_color,
	set_size,
	set_feature,
	set_font_by_name,
	set_font_directly,

	set_script,

	fallback_offset,
	fallback_color,
	fallback_size,
	fallback_feature,
	fallback_font

>;

template <typename T>
constexpr inline std::size_t token_index_of = mo_yanxi::tuple_index_v<T, variant_to_tuple_t<tokens>>;

[[nodiscard]] constexpr setter_parse_result get_setter_type_from(std::string_view args) noexcept{
	switch(args.front()){
	case '*' : return {setter_type::relative_mul, args.substr(1)};
	case '+' : return {setter_type::relative_add, args.substr(1)};
	case '=' : return {setter_type::absolute, args.substr(1)};
	default : return {setter_type::absolute, args};
	}
}

[[nodiscard]] constexpr set_offset parse_set_offset(std::string_view args) noexcept{
	const auto [type, arg_remain] = get_setter_type_from(args);
	auto [off, count] = string_cast_seq<2>(arg_remain, 0.f);
	return {type, off[0], off[1]};
}

[[nodiscard]] constexpr set_color parse_set_color(const rich_text_look_up_table* table, std::string_view args) noexcept{
	auto [type, remain] = get_setter_type_from(args);
	if(remain.empty()){
		return {setter_type::absolute, graphic::colors::white};
	}
	if(remain.front() == '#'){
		return {type, graphic::color::from_string(remain.substr(1))};
	}

	if(table)
		if(const auto c = table->color.try_find(remain)){
			return {type, *c};
		}

	return {type, graphic::colors::white};
}

[[nodiscard]] constexpr set_size parse_set_size(std::string_view args) noexcept{
	const auto [type, arg_remain] = get_setter_type_from(args);
	switch(auto [off, count] = string_cast_seq<2>(arg_remain, 0.f); count){
	case 0 : return {setter_type::relative_add, {}};
	case 1 : return {type, off[0], off[0]};
	case 2 : return {type, off[0], off[1]};
	default : std::unreachable();
	}
}

[[nodiscard]] constexpr tokens parse_set_font(const rich_text_look_up_table* table, bool has_arg,
	std::string_view args) noexcept{
	if(!has_arg){
		return fallback_font{};
	}
	if(args.empty()){
		//set to fallback
		return set_font_directly{};
	}

	if(table)
		if(auto ptr = table->family.try_find(args)){
			return set_font_directly{ptr};
		}

	return set_font_by_name{args};
}

namespace hb_constexpr{
consteval hb_tag_t make_tag_constexpr(const char* s){
	return (hb_tag_t)((((std::uint32_t)(s[0]) & 0xFF) << 24) |
		(((std::uint32_t)(s[1]) & 0xFF) << 16) |
		(((std::uint32_t)(s[2]) & 0xFF) << 8) |
		((std::uint32_t)(s[3]) & 0xFF));
}

// 简单的编译期字符串转整数解析
consteval std::uint32_t parse_uint(std::string_view s){
	std::uint32_t res = 0;
	auto rst = std::from_chars(s.data(), s.data() + s.size(), res);
	if(rst.ec != std::errc{}){
		throw std::invalid_argument{"invalid feature arg"};
	}
	return res;
}

// 核心解析函数
consteval hb_feature_t parse_feature_constexpr(std::string_view str){
	hb_feature_t feature = {HB_TAG_NONE, 0, 0, static_cast<std::uint32_t>(-1)};

	if(str.empty()) return feature;

	std::size_t start_idx = 0;

	// 处理前缀 + 或 -
	if(str[0] == '+'){
		feature.value = 1;
		start_idx = 1;
	} else if(str[0] == '-'){
		feature.value = 0;
		start_idx = 1;
	} else{
		feature.value = 1; // 默认开启
	}

	// 解析 4 字节 Tag
	// 注意：实际解析需处理 tag 长度不足 4 位的情况（用空格填充）
	char tag_chars[4] = {' ', ' ', ' ', ' '};
	std::size_t i = 0;
	while(i < 4 && (start_idx + i) < str.size() &&
		str[start_idx + i] != '[' && str[start_idx + i] != '='){
		tag_chars[i] = str[start_idx + i];
		i++;
	}
	feature.tag = make_tag_constexpr(tag_chars);

	// 查找 '=' 处理自定义值
	std::size_t equal_pos = str.find('=');
	if(equal_pos != std::string_view::npos){
		feature.value = parse_uint(str.substr(equal_pos + 1));
	}

	return feature;
}
}

[[nodiscard]] constexpr set_feature parse_set_feature(std::string_view args) noexcept{
	hb_feature_t feature;

	if consteval{
		feature = hb_constexpr::parse_feature_constexpr(args);
	} else{
		hb_feature_from_string(args.data(), args.length(), &feature);
	}

	return {feature};
}
}


struct rich_text_token_argument{
	rich_text_token::tokens token;

	constexpr rich_text_token_argument() = default;

	constexpr explicit(false) rich_text_token_argument(const rich_text_token::tokens& token)
		: token(token){
	}

	constexpr rich_text_token_argument(
		const rich_text_look_up_table* table,

		std::string_view name,
		bool has_arg,
		std::string_view args

	) : token([&] -> rich_text_token::tokens{
		using namespace rich_text_token;
		using token_str = array_string<sizeof(std::uint64_t), alignof(std::uint64_t)>;

		static constexpr auto s2i = [](token_str str) static constexpr{
			return std::bit_cast<std::uint64_t>(str.get_data());
		};


		switch(s2i(name)){
		case s2i("off") : return args.empty() ? tokens{fallback_offset{}} : parse_set_offset(args);

		case s2i("s") :[[fallthrough]];
		case s2i("sz") :[[fallthrough]];
		case s2i("size") : return args.empty() ? tokens{fallback_size{}} : parse_set_size(args);

		case s2i("f") :[[fallthrough]];
		case s2i("font") : return parse_set_font(table, has_arg, args);

		case s2i("c") :[[fallthrough]];
		case s2i("#") :[[fallthrough]];
		case s2i("color") : return args.empty() ? tokens{fallback_color{}} : parse_set_color(table, args);

		case s2i("ftr") :[[fallthrough]];
		case s2i("feature") : return args.empty() ? tokens{fallback_feature{}} : parse_set_feature(args);

		case s2i("^") : return set_script{script_type::sups};
		case s2i("_") : return set_script{script_type::subs};
		case s2i("-") : return set_script{script_type::ends};

		case s2i("u") : return set_underline{true};
		case s2i("/u") :return set_underline{false};

		case s2i("i") : return set_italic{true};
		case s2i("/i") :return set_italic{false};

		case s2i("b") : return set_bold{true};
		case s2i("/b") :return set_bold{false};

		default : return std::monostate{};
		}
	}()){
	}

	constexpr rich_text_token::tokens make_fallback() const noexcept{
		using namespace rich_text_token;
		switch(token.index()){
		case token_index_of<set_feature> : return fallback_feature{};
		case token_index_of<set_font_by_name> :[[fallthrough]];
		case token_index_of<set_font_directly> : return fallback_font{};
		case token_index_of<set_color> : return fallback_color{};
		case token_index_of<set_offset> : return fallback_offset{};
		case token_index_of<set_size> : return fallback_size{};
		case token_index_of<set_underline> : return set_underline{!std::get<set_underline>(token).enabled};
		case token_index_of<set_italic> : return set_underline{!std::get<set_italic>(token).enabled};
		case token_index_of<set_bold> : return set_underline{!std::get<set_bold>(token).enabled};
		case token_index_of<set_script> : return std::get<set_script>(token).type != script_type{}
			                                         ? set_script{script_type::ends}
			                                         : tokens{};
		default : return std::monostate{};
		}
	}

	constexpr bool need_reset_run() const noexcept{
		constexpr static auto map = []{
			using namespace rich_text_token;
			using tpl = variant_to_tuple_t<tokens>;
			std::array<bool, std::tuple_size_v<tpl>> arr{};
			arr[tuple_index_v<set_font_directly, tpl>] = true;
			arr[tuple_index_v<set_font_by_name, tpl>] = true;
			arr[tuple_index_v<fallback_font, tpl>] = true;

			arr[tuple_index_v<set_size, tpl>] = true;
			arr[tuple_index_v<fallback_size, tpl>] = true;

			arr[tuple_index_v<set_bold, tpl>] = true;
			arr[tuple_index_v<set_italic, tpl>] = true;

			arr[tuple_index_v<set_feature, tpl>] = true;
			arr[tuple_index_v<fallback_feature, tpl>] = true;

			arr[tuple_index_v<set_script, tpl>] = true;

			return arr;
		}();

		return map[token.index()];
	}

	constexpr bool operator==(const rich_text_token_argument&) const noexcept = default;
};
}
