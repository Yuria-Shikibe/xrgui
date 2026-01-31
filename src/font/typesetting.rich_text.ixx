module;
#include <hb.h>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <gch/small_vector.hpp>
#endif

export module mo_yanxi.typesetting.rich_text;

export import mo_yanxi.typesetting;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <gch/small_vector.hpp>;
#endif

import std;

import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.math.vector2;
import mo_yanxi.graphic.color;
import mo_yanxi.heterogeneous;
import mo_yanxi.static_string;

import mo_yanxi.utility;


namespace mo_yanxi::type_setting{

export
struct look_up_table{
	string_hash_map<font::font_family> family;
	string_hash_map<graphic::color> color;
};

// export
/*inline*/ look_up_table global_look_up_table{};

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

struct set_offset{
	setter_type type = setter_type::relative_add;
	math::vec2 offset;
};

struct set_color{
	setter_type type = setter_type::absolute;
	graphic::color color;
};

struct set_font_by_name{
	static_string<23> font_name;
};

struct set_font_directly{
	const font::font_family* family;
};


struct set_size{
private:
	constexpr static std::uint16_t mul_26_6(std::uint16_t l, std::uint16_t r) noexcept{
		return static_cast<std::uint16_t>(std::uint32_t(l) * std::uint32_t(r) / 64);
	}
public:
	setter_type type = setter_type::absolute;
	math::vec2 size;

	// constexpr font::glyph_size_type apply_to(font::glyph_size_type dst) const noexcept{
	// 	switch(type){
	// 	case setter_type::absolute : return size;
	// 	case setter_type::relative_add : return dst + size;
	// 	case setter_type::relative_mul : return {mul_26_6(size.x, dst.x), mul_26_6(size.y, dst.y)};
	// 	default : std::unreachable();
	// 	}
	// }
};


struct set_feature{
	hb_feature_t feature;
};

struct set_underline{
	bool enabled;
};

/**
 * @brief using empty arguments to spec fallback
 */
struct fallback_offset{};
struct fallback_color{};
struct fallback_font{};
struct fallback_size{};
struct fallback_feature{};

struct setter_parse_result{
	setter_type type;
	std::string_view remain;
};

using tokens = std::variant<
	std::monostate, //used for invalid token

	set_underline,

	set_offset,
	set_color,
	set_size,
	set_feature,
	set_font_by_name,
	set_font_directly,

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
	case '*': return {setter_type::relative_mul, args.substr(1)};
	case '+': return {setter_type::relative_add, args.substr(1)};
	case '=': return {setter_type::absolute, args.substr(1)};
	default: return {setter_type::absolute, args};
	}
}

[[nodiscard]] constexpr set_offset parse_set_offset(std::string_view args) noexcept{
	const auto [type, arg_remain] = get_setter_type_from(args);
	auto [off, count] = string_cast_seq<2>(arg_remain, 0.f);
	return {type, off[0], off[1]};
}

[[nodiscard]] constexpr set_color parse_set_color(const look_up_table& table, std::string_view args) noexcept{
	auto [type, remain] = get_setter_type_from(args);
	if(remain.front() == '#'){
		return {type, graphic::color::from_string(remain.substr(1))};
	}

	if(const auto c = table.color.try_find(remain)){
		return {type, *c};
	}

	return {type, graphic::colors::white};
}

[[nodiscard]] constexpr set_size parse_set_size(std::string_view args) noexcept{
	const auto [type, arg_remain] = get_setter_type_from(args);
	switch(auto [off, count] = string_cast_seq<2>(arg_remain, 0.f); count){
	case 0:
		return {setter_type::relative_add, {}};
	case 1:
		return {type, off[0], off[0]};
	case 2:
		return {type, off[0], off[1]};
	default: std::unreachable();
	}
}

[[nodiscard]] constexpr tokens parse_set_font(const look_up_table& table, bool has_arg, std::string_view args) noexcept{
	if(!has_arg){
		return fallback_font{};
	}
	if(args.empty()){
		//set to fallback
		return set_font_directly{};
	}

	if(auto ptr = table.family.try_find(args)){
		return set_font_directly{ptr};
	}

	return set_font_by_name{args};
}

namespace hb_constexpr{
consteval hb_tag_t make_tag_constexpr(const char* s) {
	return (hb_tag_t)((((std::uint32_t)(s[0]) & 0xFF) << 24) |
					  (((std::uint32_t)(s[1]) & 0xFF) << 16) |
					  (((std::uint32_t)(s[2]) & 0xFF) << 8) |
					  ((std::uint32_t)(s[3]) & 0xFF));
}

// 简单的编译期字符串转整数解析
consteval std::uint32_t parse_uint(std::string_view s) {
	std::uint32_t res = 0;
	auto rst = std::from_chars(s.data(), s.data() + s.size(), res);
	if(rst.ec != std::errc{}){
		throw std::invalid_argument{"invalid feature arg"};
	}
	return res;
}

// 核心解析函数
consteval hb_feature_t parse_feature_constexpr(std::string_view str) {
	hb_feature_t feature = {HB_TAG_NONE, 0, 0, static_cast<std::uint32_t>(-1)};

	if (str.empty()) return feature;

	std::size_t start_idx = 0;

	// 处理前缀 + 或 -
	if (str[0] == '+') {
		feature.value = 1;
		start_idx = 1;
	} else if (str[0] == '-') {
		feature.value = 0;
		start_idx = 1;
	} else {
		feature.value = 1; // 默认开启
	}

	// 解析 4 字节 Tag
	// 注意：实际解析需处理 tag 长度不足 4 位的情况（用空格填充）
	char tag_chars[4] = {' ', ' ', ' ', ' '};
	std::size_t i = 0;
	while (i < 4 && (start_idx + i) < str.size() &&
		   str[start_idx + i] != '[' && str[start_idx + i] != '=') {
		tag_chars[i] = str[start_idx + i];
		i++;
		   }
	feature.tag = make_tag_constexpr(tag_chars);

	// 查找 '=' 处理自定义值
	std::size_t equal_pos = str.find('=');
	if (equal_pos != std::string_view::npos) {
		feature.value = parse_uint(str.substr(equal_pos + 1));
	}

	return feature;
}
}

[[nodiscard]] constexpr set_feature parse_set_feature(std::string_view args) noexcept{
	hb_feature_t feature;

	if consteval {
		feature = hb_constexpr::parse_feature_constexpr(args);
	}else{
		hb_feature_from_string(args.data(), args.length(), &feature);
	}

	return {feature};
}

}


struct rich_text_token_argument{
	rich_text_token::tokens token;

	constexpr rich_text_token_argument() = default;

	constexpr rich_text_token_argument(
		const look_up_table& table,
		const rich_text_token_argument& last,

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
			case s2i("/") :{
				switch(last.token.index()){
					case token_index_of<set_feature>: return fallback_feature{};
					case token_index_of<set_font_by_name>: [[fallthrough]];
					case token_index_of<set_font_directly>: return fallback_font{};
					case token_index_of<set_color>: return fallback_color{};
					case token_index_of<set_offset>: return fallback_offset{};
					case token_index_of<set_size>: return fallback_size{};
				case token_index_of<set_underline>: return set_underline{!std::get<set_underline>(last.token).enabled};
					default: return std::monostate{};
				}
			}
		case s2i("off") : return args.empty() ? tokens{fallback_offset{}} : parse_set_offset(args);

		case s2i("sz") : [[fallthrough]];
		case s2i("size") : return args.empty() ? tokens{fallback_size{}} : parse_set_size(args);

		case s2i("f") : [[fallthrough]];
		case s2i("font") : return parse_set_font(table, has_arg, args);

		case s2i("c") : [[fallthrough]];
		case s2i("#") : [[fallthrough]];
		case s2i("color") : return args.empty() ? tokens{fallback_color{}} : parse_set_color(table, args);

		case s2i("ftr") : [[fallthrough]];
		case s2i("feature") : return args.empty() ? tokens{fallback_feature{}} : parse_set_feature(args);

			// case s2i("^") : TODO superscript
			// case s2i("_") : TODO subscript
			// case s2i("/") : TODO end script

		case s2i("u") : [[fallthrough]];
		case s2i("ul") : return set_underline{true};

		case s2i("/u") : [[fallthrough]];
		case s2i("/ul") : return set_underline{false};

		default: return std::monostate{};
		}
	}()){
	}
};

export
struct tokenized_text{
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
	using token_subrange = std::ranges::subrange<token_iterator>;

	constexpr std::u32string_view get_text() const noexcept{
		return codes;
	}

	constexpr token_iterator get_token(const pos_t pos, const token_iterator& last) const noexcept {
		return std::ranges::lower_bound(last, tokens.end(), pos, {}, &posed_token_argument::pos);
	}

	constexpr token_iterator get_token(const pos_t pos) const noexcept {
		return get_token(pos, tokens.begin());
	}

	constexpr token_iterator get_init_token() const noexcept {
		return tokens.begin();
	}

	[[nodiscard]] constexpr token_subrange get_token_group(const pos_t pos, const token_iterator& last) const{
		return std::ranges::equal_range(last, tokens.end(), pos, {}, &posed_token_argument::pos);
	}

	[[nodiscard]] constexpr tokenized_text() = default;

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::string_view string, const look_up_table& table){
		parse_from_(string, table);
	}
	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::string_view string) : tokenized_text{string, global_look_up_table}{
	}

	constexpr void reset(std::string_view string, const look_up_table& table){
		codes.clear();
		tokens.clear();
		parse_from_(string, table);
	}

	constexpr void reset(std::string_view string){
		reset(string, global_look_up_table);
	}

private:
	constexpr void parse_from_(std::string_view string, const look_up_table& table);
};



template <typename T>
using stack_of = optional_stack<T, gch::small_vector<T>>;

export
struct rich_text_context{
private:
	stack_of<math::vec2> history_offset_;
	stack_of<graphic::color> history_color_;
	stack_of<math::vec2> history_size_;
	stack_of<const font::font_family*> history_font_;
	stack_of<gch::small_vector<hb_feature_t>> history_features_;

	bool enables_underline_{};

public:

	[[nodiscard]] bool is_underline_enabled() const noexcept{
		return enables_underline_;
	}

	[[nodiscard]] math::vec2 get_size(math::vec2 default_font_size) const noexcept{
		return history_size_.top(default_font_size);
	}

	[[nodiscard]] math::vec2 get_offset() const noexcept{
		return history_offset_.top({});
	}

	[[nodiscard]] graphic::color get_color() const noexcept{
		return history_color_.top(graphic::colors::white);
	}

	[[nodiscard]] const font::font_family& get_font(const font::font_family* default_family) const noexcept{
		auto rst = history_font_.top(default_family);
		assert(rst != nullptr);
		return *rst;
	}

	[[nodiscard]] std::span<const hb_feature_t> get_features() const noexcept{
		if(history_features_.empty()){
			return {};
		}else{
			return history_features_.top_ref();
		}
	}

	void update(font::font_manager& manager, math::vec2 default_font_size, const tokenized_text::token_subrange& tokens){
		using namespace rich_text_token;

		bool is_contiguous_feature{};
		for(const rich_text_token_argument& token : tokens){
			std::visit(overload{
					[&](const std::monostate t){

					},
					[&](const  set_underline& t){
						enables_underline_ = t.enabled;
					},
					[&](const  set_offset& t){
						switch(t.type){
						case setter_type::absolute: history_offset_.push(t.offset); break;
						case setter_type::relative_add: history_offset_.push(history_offset_.top({}) + t.offset); break;
						case setter_type::relative_mul: history_offset_.push(history_offset_.top({}) * t.offset); break;
						default: std::unreachable();
						}
					},
					[&](const  set_color& t){
						switch(t.type){
						case setter_type::absolute: history_color_.push(t.color); break;
						case setter_type::relative_add: history_color_.push(history_color_.top(graphic::colors::white / 2) + t.color); break;
						case setter_type::relative_mul: history_color_.push(history_color_.top(graphic::colors::white / 2) * t.color); break;
						default: std::unreachable();
						}
					},
					[&](const set_size& t){
						switch(t.type){
						case setter_type::absolute: history_size_.push(t.size); break;
						case setter_type::relative_add: history_size_.push(history_size_.top(default_font_size) + t.size); break;
						case setter_type::relative_mul: history_size_.push(history_size_.top(default_font_size) * t.size); break;
						default: std::unreachable();
						}
					},
					[&](const set_feature& t){
						if(is_contiguous_feature){
							history_features_.top_ref().push_back(t.feature);
						}else{
							history_features_.push({t.feature});
						}
					},
					[&](const set_font_by_name& t){
						if(auto ptr = manager.find_family(t.font_name)){
							history_font_.push(ptr);
						}else{
							history_font_.push(manager.get_default_family());
						}
					},
					[&](const set_font_directly& t){
						history_font_.push(t.family ? t.family : manager.get_default_family());
					},
					[&](const fallback_offset& t){
						history_offset_.pop();
					},
					[&](const fallback_color& t){
						history_color_.pop();
					},
					[&](const fallback_size& t){
						history_size_.pop();
					},
					[&](const fallback_feature& t){
						history_features_.pop();
					},
					[&](const fallback_font& t){
						history_font_.pop();
					},
				}, token.token);


			if(std::holds_alternative<set_feature>(token.token)){
				is_contiguous_feature = true;
			}else{
				is_contiguous_feature = false;
			}
		}
	}
};
}

namespace mo_yanxi::type_setting {

    constexpr void append_utf8_to_u32(std::u32string& dest, std::string_view source) {
        const char* p = source.data();
        const char* end = p + source.size();

        while (p < end) {
            const unsigned char c = std::bit_cast<unsigned char>(*p);
            if (c < 0x80) {
                dest.push_back(c);
                p++;
            } else if ((c & 0xE0) == 0xC0) {
                if (p + 2 > end) break;
                dest.push_back(((c & 0x1F) << 6) | (p[1] & 0x3F));
                p += 2;
            } else if ((c & 0xF0) == 0xE0) {
                if (p + 3 > end) break;
                dest.push_back(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
                p += 3;
            } else if ((c & 0xF8) == 0xF0) {
                if (p + 4 > end) break;
                dest.push_back(((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F));
                p += 4;
            } else {
                p++; // Skip invalid
            }
        }
    }

   constexpr void tokenized_text::parse_from_(const std::string_view string, const look_up_table& table) {
    const char* ptr = string.data();
    const size_t size = string.size();

    codes.reserve(size);

    std::size_t text_start_idx = 0;
    std::size_t i = 0;

    const auto flush_pending_text = [&](std::size_t current_end) {
        assert(current_end >= text_start_idx);
        if (current_end > text_start_idx) { // 增加判空，虽然 append 内部也会处理
            std::string_view pending(ptr + text_start_idx, current_end - text_start_idx);
            append_utf8_to_u32(codes, pending);
        }
    };

    while (i < size) {
        const auto c = ptr[i];

        // 1. 处理转义字符 '\'
        if (c == '\\') {
            flush_pending_text(i);

            if (i + 1 < size) {
                auto next = ptr[i + 1];
                std::size_t skip_len{};

                if (next == '\\') {
                    codes.push_back(U'\\');
                    skip_len = 2;
                }
                else if (next == '{' || next == '}') {
                    codes.push_back(static_cast<char32_t>(next));
                    skip_len = 2;
                }
                else if (next == '\r') {
                    // Windows (\r\n) or Old Mac (\r)
                    skip_len = 2;
                    if (i + 2 < size && ptr[i + 2] == '\n') skip_len = 3;
                }
                else if (next == '\n') {
                    // Unix (\n) or Rare (\n\r)
                    skip_len = 2;
                    if (i + 2 < size && ptr[i + 2] == '\r') skip_len = 3;
                }
                else {
                    // 未知转义：保留后续字符作为字面量 (例如 \a -> a)
                    // 如果你想保留反斜杠 (例如 \a -> \a)，则需要 push U'\\' 和 next
                    // 这里采用常见做法：转义为字面量
                    codes.push_back(static_cast<char32_t>(next));
                    skip_len = 2;
                }

                i += skip_len;
            } else {
                // 字符串末尾的单个反斜杠，忽略
                i++;
            }
            text_start_idx = i;
            continue;
        }

        // 2. 处理 Token 开始 '{'
        if (c == '{') {
            // 检查双括号 '{{' -> 转义为 '{'
            if (i + 1 < size && ptr[i + 1] == '{') {
                flush_pending_text(i);
                codes.push_back(U'{');
                i += 2;
                text_start_idx = i;
                continue;
            }

            // Token 解析模式
            const std::size_t start_content = i + 1;
            // [修复]：使用 string_view 的 find 查找，并加上偏移量

        	const auto post_string = string.substr(start_content);
            const auto relative_pos = post_string.find('}');
            if (relative_pos != std::string_view::npos) {
	            if(const auto find_another = post_string.substr(0, relative_pos).find('{') != std::string_view::npos){
            		i++;
            		continue;
            	}

                // 找到了 Token
                flush_pending_text(i);

                // 计算内容的实际结束位置（绝对索引）
                // relative_pos 是相对于 start_content 的
                const std::size_t end_content_idx = start_content + relative_pos;

                // 解析 Token 内容
                // 长度 = end_content_idx - start_content (也就是 relative_pos)
                std::string_view content(ptr + start_content, relative_pos);

                std::string_view name = content;
                std::string_view arg = {};

                auto colon_pos = content.find(':');
                if (colon_pos != std::string_view::npos) {
                    name = content.substr(0, colon_pos);
                    arg = content.substr(colon_pos + 1);
                }

                rich_text_token_argument parsed_arg = rich_text_token_argument(table, (tokens.empty() ? rich_text_token_argument{} : tokens.back()), name, colon_pos != std::string_view::npos, arg);

                posed_token_argument posed_token;
                static_cast<rich_text_token_argument&>(posed_token) = std::move(parsed_arg);
                posed_token.pos = static_cast<std::uint32_t>(codes.size());

                tokens.push_back(std::move(posed_token));

                // i 跳到 '}' 之后
                i = end_content_idx + 1;
                text_start_idx = i;
                continue;
            }

        	// 没找到配对的 '}'，视为普通字符 '{'，继续扫描
            i++;
            continue;
        }

        // 3. 处理转义结束符 '}' (用于双括号 '}}')
        if (c == '}') {
            if (i + 1 < size && ptr[i + 1] == '}') {
                flush_pending_text(i);
                codes.push_back(U'}');
                i += 2;
                text_start_idx = i;
                continue;
            }
        }

        i++;
    }

    flush_pending_text(size);
}
}