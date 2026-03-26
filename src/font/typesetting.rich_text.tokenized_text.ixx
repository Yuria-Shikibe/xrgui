//
// Created by Matrix on 2026/3/14.
//

export module mo_yanxi.typesetting.rich_text:tokenized_text;

import std;
import mo_yanxi.unicode;

import :argument;

namespace mo_yanxi::typesetting{
export
enum struct tokenize_tag{
	/**
	 * @brief apply escape and token and clip token command
	 */
	def,

	/**
	 * @brief input as raw string, no token parse
	 */
	raw,

	/**
	 * @brief input as raw string, but also parse the token and apply escape
	 */
	kep
};

export
struct tokenized_text{
	struct posed_token_argument : rich_text_token_argument{
		std::uint32_t pos{};

		[[nodiscard]] posed_token_argument() = default;

		[[nodiscard]] explicit posed_token_argument(const rich_text_token_argument& t, std::uint32_t pos) :
			rich_text_token_argument(t)
			, pos(pos){
		}

		constexpr bool operator==(const posed_token_argument&) const noexcept = default;
	};

private:
	std::u32string chars_{};

	std::vector<posed_token_argument> tokens_{};

public:
	using pos_t = decltype(chars_)::size_type;
	using token_iterator = decltype(tokens_)::const_iterator;
	using token_span = std::span<const posed_token_argument>;

	constexpr bool operator==(const tokenized_text&) const noexcept = default;

	constexpr bool empty() const noexcept { return chars_.empty(); }
	constexpr std::u32string_view get_text() const noexcept { return chars_; }

	constexpr token_iterator get_token(const pos_t pos, const token_iterator& last) const noexcept{
		return std::ranges::lower_bound(last, tokens_.end(), pos, {}, &posed_token_argument::pos);
	}

	constexpr token_iterator get_token_sentinel(const pos_t pos, const token_iterator& last) const noexcept{
		return std::ranges::upper_bound(last, tokens_.end(), pos, {}, &posed_token_argument::pos);
	}

	constexpr token_iterator get_token(const pos_t pos) const noexcept{
		return get_token(pos, tokens_.begin());
	}

	constexpr token_span get_tokens() const noexcept{
		return tokens_;
	}

	constexpr token_iterator get_init_token() const noexcept{
		return tokens_.begin();
	}

	[[nodiscard]] constexpr token_span get_token_group(const pos_t pos, const token_iterator& last) const{
		auto range = std::ranges::equal_range(last, tokens_.end(), pos, {}, &posed_token_argument::pos);
		return token_span{range.begin(), range.end()};
	}

	[[nodiscard]] constexpr tokenized_text() = default;

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::u32string_view string, tokenize_tag tag,
		const rich_text_look_up_table* table){
		parse_from_(string, table, tag);
	}

	[[nodiscard]] constexpr explicit(false) tokenized_text(std::u32string&& string, tokenize_tag tag,
		const rich_text_look_up_table* table) : chars_(std::move(string)){
		switch(tag){
		case tokenize_tag::kep :{
			this->parse_state_machine_<true>(table);
			return;
		}
		case tokenize_tag::raw :{
			return;
		}
		case tokenize_tag::def :{
			this->parse_state_machine_<false>(table);
			return;
		}
		}
	}

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::string_view string, tokenize_tag tag,
		const rich_text_look_up_table* table){
		parse_from_(string, table, tag);
	}

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::u32string_view string,
		tokenize_tag tag = tokenize_tag::def) : tokenized_text(string, tag, nullptr){
	}

	[[nodiscard]] constexpr explicit(false) tokenized_text(std::u32string&& string,
		tokenize_tag tag = tokenize_tag::def) : tokenized_text(std::move(string), tag, nullptr){
	}

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::string_view string,
		tokenize_tag tag = tokenize_tag::def) : tokenized_text(string, tag, nullptr){
	}

	template <std::invocable<std::u32string&> Fn>
	constexpr auto modify(tokenize_tag tag, Fn fn, const rich_text_look_up_table* table = look_up_table){
		auto apply = [&]{
			tokens_.clear();
			switch(tag){
			case tokenize_tag::kep :{
				this->parse_state_machine_<true>(table);
				return;
			}
			case tokenize_tag::raw :{
				return;
			}
			case tokenize_tag::def :{
				// 第二步：触发原地双指针擦除
				this->parse_state_machine_<false>(table);
				return;
			}
			}
		};

		if constexpr (std::predicate<Fn&, std::u32string&>){
			if(std::invoke_r<bool>(fn, chars_)){
				apply();
				return true;
			}
			return false;
		}else{
			std::invoke(fn, chars_);
			apply();
			return ;
		}
	}

	constexpr void reset(std::string_view string,
		tokenize_tag tag = tokenize_tag::def, const rich_text_look_up_table* table = look_up_table){
		parse_from_(string, table, tag);
	}

	constexpr void reset(std::u32string_view string,
		tokenize_tag tag = tokenize_tag::def, const rich_text_look_up_table* table = look_up_table){
		parse_from_(string, table, tag);
	}

	constexpr void reset(std::u32string&& string, tokenize_tag tag = tokenize_tag::def, const rich_text_look_up_table* table = look_up_table){
		const std::size_t size = string.size();
		chars_ = std::move(string);
		tokens_.clear();
		tokens_.reserve((size / 64) + 1);

		switch(tag){
		case tokenize_tag::kep :{
			this->parse_state_machine_<true>(table);
			return;
		}
		case tokenize_tag::raw :{
			return;
		}
		case tokenize_tag::def :{
			this->parse_state_machine_<false>(table);
			return;
		}
		}
		parse_from_(string, table, tag);
	}

private:
	template <bool IsKepMode>
	constexpr void parse_state_machine_(const rich_text_look_up_table* table);

	template <typename CharT>
	constexpr void parse_from_impl_(std::basic_string_view<CharT> string, const rich_text_look_up_table* table,
		tokenize_tag tag);

	constexpr void parse_from_(std::string_view string, const rich_text_look_up_table* table,
		tokenize_tag tag = tokenize_tag::def){
		parse_from_impl_(string, table, tag);
	}

	constexpr void parse_from_(std::u32string_view string, const rich_text_look_up_table* table,
		tokenize_tag tag = tokenize_tag::def){
		parse_from_impl_(string, table, tag);
	}

	constexpr void parse_tokens_(const rich_text_look_up_table* table, std::uint32_t pos, std::string_view name,
		bool has_arg,
		std::string_view args);
};
}

namespace mo_yanxi::typesetting{
constexpr void append_to_u32(std::u32string& dest, std::string_view source){
	unicode::append_utf8_to_utf32(source, dest);
}

constexpr void append_to_u32(std::u32string& dest, std::u32string_view source){
	dest.append_range(source);
}

template <bool IsKepMode>
constexpr void tokenized_text::parse_state_machine_(const rich_text_look_up_table* table){
	const std::size_t size = chars_.size();
	if(size == 0) return;

	char32_t* const base_ptr = chars_.data();
	char32_t* read_ptr = base_ptr;
	char32_t* write_ptr = base_ptr;
	const char32_t* const end_ptr = base_ptr + size;

	std::string token_buffer;

	while(read_ptr < end_ptr){
		char32_t c = *read_ptr;

		if(c == U'\\'){
			if(read_ptr + 1 < end_ptr){
				std::ptrdiff_t skip_len{};
				switch(const auto next = read_ptr[1]){
				case U'\\' :
				case U'{' :
				case U'}' : if constexpr(!IsKepMode) *write_ptr++ = next;
					skip_len = 2;
					break;
				case U'\r' :
					skip_len = 2 + (read_ptr + 2 < end_ptr && read_ptr[2] == U'\n');
					break;
				case U'\n' :
					skip_len = 2 + (read_ptr + 2 < end_ptr && read_ptr[2] == U'\r');
					break;
				default : if constexpr(!IsKepMode) *write_ptr++ = next;
					skip_len = 2;
					break;
				}
				read_ptr += skip_len;
			} else{
				read_ptr++;
			}
		} else if(c == U'{'){
			if(read_ptr + 1 < end_ptr && read_ptr[1] == U'{'){
				if constexpr(!IsKepMode) *write_ptr++ = U'{';
				read_ptr += 2;
				continue;
			}

			char32_t* const start_content = read_ptr + 1;
			const std::size_t remaining_len = static_cast<std::size_t>(end_ptr - start_content);
			const std::u32string_view view(start_content, remaining_len);
			const auto relative_pos = view.find(U'}');

			if(relative_pos != std::u32string_view::npos){
				const auto content = view.substr(0, relative_pos);

				// 检查是否有嵌套的 '{'，如果有，则当前标签无效
				if(content.find(U'{') != std::u32string_view::npos){
					if constexpr(!IsKepMode) *write_ptr++ = U'{';
					read_ptr++;
					continue;
				}

				const auto csz = content.size();
				// 使用 resize_and_overwrite 和基于下标的写入，彻底干掉 push_back 扩容惩罚
				token_buffer.resize_and_overwrite(csz, [&](char* p, std::size_t /*sz*/){
					for(std::size_t k = 0; k < csz; ++k){
						p[k] = static_cast<char>(content[k]);
					}
					return csz;
				});

				auto dispatch_parse = [&](std::string_view name_view){
					std::string_view arg_view;
					auto colon_pos = name_view.find(':');
					if(colon_pos != std::string_view::npos){
						arg_view = name_view.substr(colon_pos + 1);
						name_view = name_view.substr(0, colon_pos);
					}

					// 通过指针运算恢复绝对下标位置
					const auto token_pos = IsKepMode
						                          ? read_ptr - base_ptr
						                          : write_ptr - base_ptr;

					parse_tokens_(table, static_cast<std::uint32_t>(token_pos), name_view, colon_pos != std::string_view::npos, arg_view);
				};

				dispatch_parse(token_buffer);
				// 跳过整个标签及其内容，加上相对位置和闭合的大括号 '}'
				read_ptr = start_content + relative_pos + 1;
			} else{
				if constexpr(!IsKepMode) *write_ptr++ = U'{';
				read_ptr++;
			}
		} else if(c == U'}'){
			if(read_ptr + 1 < end_ptr && read_ptr[1] == U'}'){
				if constexpr(!IsKepMode) *write_ptr++ = U'}';
				read_ptr += 2;
				continue;
			}
			read_ptr++;
		} else{
			if constexpr(!IsKepMode){
				// 只有当写指针落后于读指针时才发生赋值，最大限度减少不必要的内存覆写
				if(write_ptr != read_ptr){
					*write_ptr = c;
				}
				write_ptr++;
			}
			read_ptr++;
		}
	}

	// 最终只做一次裁剪
	if constexpr(!IsKepMode){
		chars_.resize(static_cast<std::size_t>(write_ptr - base_ptr));
	}
}

template <typename CharT>
constexpr void tokenized_text::parse_from_impl_(std::basic_string_view<CharT> string,
	const rich_text_look_up_table* table, tokenize_tag tag){
	const std::size_t size = string.size();
	chars_.clear();
	tokens_.clear();

	// 提前为 tokens_ 分配内存，假设平均每 16 个字符出现一个 token，避免 emplace_back 的扩容开销
	tokens_.reserve((size / 64) + 1);

	// 第一步：直接全部转码进 chars_
	typesetting::append_to_u32(chars_, string);

	switch(tag){
	case tokenize_tag::kep :{
		this->parse_state_machine_<true>(table);
		return;
	}
	case tokenize_tag::raw :{
		return;
	}
	case tokenize_tag::def :{
		// 第二步：触发原地双指针擦除
		this->parse_state_machine_<false>(table);
		return;
	}
	}
}

constexpr void tokenized_text::parse_tokens_(const rich_text_look_up_table* table, std::uint32_t pos,
	std::string_view name,
	bool has_arg, std::string_view args){
	if(name.empty()){
		return;
	}

	if(name.front() == '#' || (name.size() > 1 && (name.front() == '*' || name.front() == '+' || name.front() == '=') && name[1] == '#')){
		// 特殊处理仅有 "{#}" 的情况，将其映射为 fallback_color
		if(name.size() == 1){
			tokens_.emplace_back(rich_text_token_argument{table, "c", false, ""}, pos);
		} else{
			// 将整个 name（例如 "#FF0000" 或 "+#112233"）作为参数，
			// 伪装成名称为 "c" 的标准颜色 token 发送给参数解析器
			tokens_.emplace_back(rich_text_token_argument{table, "c", true, name}, pos);
		}
		return;
	}

	switch(name.front()){
	case '/' :{
		unsigned count;
		const auto original_size = tokens_.size();

		if(const auto [ptr, ec] = std::from_chars(name.data() + 1, name.data() + name.size(), count); ec == std::errc
			{}){
			for(std::size_t i = 0; i < count; ++i){
				const std::size_t src_index = original_size - 1 - i;
				tokens_.emplace_back(tokens_[src_index].make_revert(), pos);
			}
			break;
		} else{
			if(name.back() == '/'){
				const auto limit = std::min(name.size(), original_size);

				for(std::size_t i = 0; i < limit; ++i){
					if(name[i] != '/') break;
					const std::size_t src_index = original_size - 1 - i;
					tokens_.emplace_back(tokens_[src_index].make_revert(), pos);
				}
				break;
			}
		}
	}

	default : tokens_.emplace_back(rich_text_token_argument{table, name, has_arg, args}, pos);
	}
}
}
