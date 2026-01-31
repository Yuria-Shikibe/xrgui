module;

#include <gch/small_vector.hpp>

export module mo_yanxi.typesetting.rich_text;

export import mo_yanxi.typesetting;

import mo_yanxi.font;
import mo_yanxi.math.vector2;
import mo_yanxi.graphic.color;
import mo_yanxi.static_string;
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

// /**
//  * @tparam off
//  * @param type[empty(as abs), abs, *, mul, +, add], num X, num Y
//  */
struct set_offset{
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


struct set_feature{

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

}

struct rich_text_token_argument{
	using tokens = std::variant<
		rich_text_token::set_offset,
		rich_text_token::set_color,
		rich_text_token::set_font,
		rich_text_token::set_size,
		rich_text_token::set_underline,

		rich_text_token::fallback_offset,
		rich_text_token::fallback_color,
		rich_text_token::fallback_font,
		rich_text_token::fallback_size
	>;

	tokens token;

	constexpr rich_text_token_argument() = default;
	constexpr rich_text_token_argument(std::string_view name, std::string_view args){
		using token_str = static_string<8>;

		static constexpr token_str name_set_offset{"off"};
		constexpr auto t = std::bit_cast<std::uint64_t>(name_set_offset.get_data());
		// static_string<7> str;
		// str.assign_or_truncate(name);
		// auto& t = str.get_data();
	}
};

export
struct tokenized_text{
	static constexpr char token_split_char = '|';

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

	constexpr token_iterator get_token(const pos_t pos, const token_iterator& last){
		return std::ranges::lower_bound(last, tokens.end(), pos, {}, &posed_token_argument::pos);
	}

	constexpr token_iterator get_token(const pos_t pos){
		return get_token(pos, tokens.begin());
	}

	[[nodiscard]] constexpr auto get_token_group(const pos_t pos, const token_iterator& last) const{
		return std::ranges::equal_range(last, tokens.end(), pos, {}, &posed_token_argument::pos);
	}

	[[nodiscard]] constexpr tokenized_text() = default;

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::string_view string){
		parse_from_(string);
	}

	constexpr void reset(std::string_view string){
		codes.clear();
		tokens.clear();
		parse_from_(string);
	}

private:
	constexpr void parse_from_(std::string_view string);
};
}

namespace mo_yanxi::type_setting {

    // 辅助函数：将字符串参数转换为 rich_text_token_argument
    // TODO: 用户需要根据实际需求完善此函数中的字符串匹配和转换逻辑
    static constexpr void append_utf8_to_u32(std::u32string& dest, std::string_view source) {
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

    // TODO: 留给你完成的参数解析函数
    // 将 name (如 "color") 和 arg (如 "#FF0000") 转换为具体的 Token 结构
    constexpr rich_text_token_argument parse_rich_token_payload(std::string_view name, std::string_view arg){
	    return {};
    }

   constexpr void tokenized_text::parse_from_(const std::string_view string) {
        const char* ptr = string.data();
        const size_t size = string.size();

        codes.reserve(size);

        // 记录上一次转换结束后的下一个字符位置（即当前未处理文本片段的起始位置）
        std::size_t text_start_idx = 0;
        std::size_t i = 0;

        // 辅助 lambda：将 [text_start_idx, current_end) 的内容转为 UTF-32 并追加到 codes
        const auto flush_pending_text = [&](std::size_t current_end) {
        	assert(current_end >= text_start_idx);
        	std::string_view pending(ptr + text_start_idx, current_end - text_start_idx);
        	append_utf8_to_u32(codes, pending);

        };

        while (i < size) {
            const auto c = ptr[i];

            // 1. 处理转义字符 '\'
            if (c == '\\') {
                // 先结算之前的普通文本
                flush_pending_text(i);

                if (i + 1 < size) {
                    auto next = ptr[i + 1];
                    std::size_t skip_len{};

                    if (next == '\\') {
                        // case: \\ -> 转义为字面量 '\'
                        codes.push_back(U'\\');
                        skip_len = 2;
                    }
                    else if (next == '{' || next == '}') {
                        // case: \{ or \} -> 转义为字面量 '{' or '}'
                        codes.push_back(static_cast<char32_t>(next));
                        skip_len = 2;
                    }
                    else if (next == '\r') {
                        // case: \ + \r\n (Windows换行续行) 或 \ + \r (Mac旧版)
                        skip_len = 2;
                        if (i + 2 < size && ptr[i + 2] == '\n') skip_len = 3;
                        // 不push任何字符，通过跳过索引实现“忽略”
                    }
                    else if (next == '\n') {
                        // case: \ + \n\r (极少见) 或 \ + \n (Linux/Unix换行续行)
                        skip_len = 2;
                        if (i + 2 < size && ptr[i + 2] == '\r') skip_len = 3;
                        // 不push任何字符
                    }
                    else {
                        // case: \ + 其他字符 (如 \t, \a 等)
                        // 根据你的需求，这里可以扩展支持标准转义。
                        // 目前默认行为：消耗掉反斜杠，保留后续字符作为字面量
                        // 例如 "\a" -> "a"
                        // codes.push_back(static_cast<char32_t>(next));
                        skip_len = 2;
                    }

                    i += skip_len;
                } else {
                    // 字符串末尾的单个反斜杠，忽略或作为字面量
                    i++;
                }

                // 更新下一次转换的起始位置
                text_start_idx = i;
                continue;
            }

            // 2. 处理 Token 开始 '{'
            if (c == '{') {
                // 检查双括号 '{{'
                if (i + 1 < size && ptr[i + 1] == '{') {
                    flush_pending_text(i); // 结算之前的文本
                    codes.push_back(U'{'); // 插入字面量 '{'
                    i += 2;
                    text_start_idx = i;
                    continue;
                }

                // Token 解析模式
                // 先寻找闭合的 '}'
                const std::size_t start_content = i + 1;

                if (
                	const std::size_t end_content = string.substr(start_content).find('}');
                	end_content != std::string_view::npos) {
                    // 找到了 Token，先结算之前的普通文本
                    flush_pending_text(i);

                    // 解析 Token 内容
                    std::string_view content(ptr + start_content, end_content - start_content);
                    std::string_view name = content;
                    std::string_view arg = {};

                    if (auto colon_pos = content.find(':'); colon_pos != std::string_view::npos) {
                        name = content.substr(0, colon_pos);
                        arg = content.substr(colon_pos + 1);
                    }

                    // 调用外部转换函数
                    rich_text_token_argument parsed_arg = parse_rich_token_payload(name, arg);

                    posed_token_argument posed_token;
                    static_cast<rich_text_token_argument&>(posed_token) = std::move(parsed_arg);
                    // Token 生效位置绑定到当前 codes 的末尾（即接下来的文字）
                    posed_token.pos = static_cast<std::uint32_t>(codes.size());

                    tokens.push_back(std::move(posed_token));

                    // 移动索引跳过整个 Token "{...}"
                    i = end_content + 1;
                    text_start_idx = i; // 更新起始位置
                    continue;
                }
                else {
                	//Throw directly?
                    // 没找到配对的 '}'，视为普通字符，暂不处理，留给下一次 flush 或循环继续
                    i++;
                    continue;
                }
            }

            // 3. 处理转义结束符 '}' (用于双括号 '}}')
            // 单独的 '}' 在这里不需要特殊处理（除非是错误语法），它会被包含在下一次 flush 中
            // 但我们需要处理 '}}' 转义的情况
            if (c == '}') {
                if (i + 1 < size && ptr[i + 1] == '}') {
                    flush_pending_text(i); // 结算之前的文本
                    codes.push_back(U'}'); // 插入字面量 '}'
                    i += 2;
                    text_start_idx = i;
                    continue;
                }
            }

            // 普通字符，继续扫描
            i++;
        }

        // 循环结束，处理剩余的普通文本
        flush_pending_text(size);
    }
}