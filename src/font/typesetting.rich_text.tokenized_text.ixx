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
	};

private:
	std::u32string chars_{};

	std::vector<posed_token_argument> tokens_{};

public:
	using pos_t = decltype(chars_)::size_type;
	using token_iterator = decltype(tokens_)::const_iterator;
	using token_subrange = std::ranges::subrange<token_iterator>;

	constexpr bool empty() const noexcept{
		return chars_.empty();
	}

	constexpr std::u32string_view get_text() const noexcept{
		return chars_;
	}

	constexpr token_iterator get_token(const pos_t pos, const token_iterator& last) const noexcept{
		return std::ranges::lower_bound(last, tokens_.end(), pos, {}, &posed_token_argument::pos);
	}

	constexpr token_iterator get_token(const pos_t pos) const noexcept{
		return get_token(pos, tokens_.begin());
	}

	constexpr token_iterator get_init_token() const noexcept{
		return tokens_.begin();
	}

	[[nodiscard]] constexpr token_subrange get_token_group(const pos_t pos, const token_iterator& last) const{
		return std::ranges::equal_range(last, tokens_.end(), pos, {}, &posed_token_argument::pos);
	}

	[[nodiscard]] constexpr tokenized_text() = default;

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::string_view string, tokenize_tag tag = tokenize_tag::def, const rich_text_look_up_table* table){
		parse_from_(string, table, tag);
	}

	[[nodiscard]] constexpr explicit(false) tokenized_text(const std::u32string_view string, tokenize_tag tag = tokenize_tag::def, const rich_text_look_up_table* table){
		parse_from_(string, table, tag);
	}

	constexpr void reset(std::string_view string, const rich_text_look_up_table* table = look_up_table, tokenize_tag tag = tokenize_tag::def){
		chars_.clear();
		tokens_.clear();
		parse_from_(string, table, tag);
	}

private:
	template <bool IsKepMode, typename CharT>
	constexpr void parse_state_machine_(std::basic_string_view<CharT> string, const rich_text_look_up_table* table);

	template <typename CharT>
	constexpr void parse_from_impl_(std::basic_string_view<CharT> string, const rich_text_look_up_table* table, tokenize_tag tag);

	constexpr void parse_from_(std::string_view string, const rich_text_look_up_table* table, tokenize_tag tag = tokenize_tag::def) {
		parse_from_impl_(string, table, tag);
	}

	constexpr void parse_from_(std::u32string_view string, const rich_text_look_up_table* table, tokenize_tag tag = tokenize_tag::def) {
		parse_from_impl_(string, table, tag);
	}

	constexpr void parse_tokens_(const rich_text_look_up_table* table, std::uint32_t pos, std::string_view name, bool has_arg,
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

template <bool IsKepMode, typename CharT>
constexpr void tokenized_text::parse_state_machine_(std::basic_string_view<CharT> string, const rich_text_look_up_table* table){
	const CharT* ptr = string.data();
	const size_t size = string.size();
	std::size_t text_start_idx = 0;
	std::size_t i = 0;

	const auto flush_pending_text = [&](std::size_t current_end){
		if constexpr (!IsKepMode) {
			if(current_end > text_start_idx){
				auto pending = string.substr(text_start_idx, current_end - text_start_idx);
				typesetting::append_to_u32(chars_, pending);
			}
		}
	};

	while(i < size){
		const auto c = ptr[i];

		switch(c) {
			case CharT('\\'): {
				flush_pending_text(i);
				if(i + 1 < size){
					auto next = ptr[i + 1];
					std::size_t skip_len = 0;

					switch(next) {
						case CharT('\\'):
						case CharT('{'):
						case CharT('}'):
							if constexpr (!IsKepMode) chars_.push_back(static_cast<char32_t>(next));
							skip_len = 2;
							break;
						case CharT('\r'):
							skip_len = 2;
							if(i + 2 < size && ptr[i + 2] == CharT('\n')) skip_len = 3;
							break;
						case CharT('\n'):
							skip_len = 2;
							if(i + 2 < size && ptr[i + 2] == CharT('\r')) skip_len = 3;
							break;
						default:
							if constexpr (!IsKepMode) chars_.push_back(static_cast<char32_t>(next));
							skip_len = 2;
							break;
					}
					i += skip_len;
				} else{
					i++;
				}
				if constexpr (!IsKepMode) text_start_idx = i;
				continue;
			}

			case CharT('{'): {
				if(i + 1 < size && ptr[i + 1] == CharT('{')){
					flush_pending_text(i);
					if constexpr (!IsKepMode) chars_.push_back(U'{');
					i += 2;
					if constexpr (!IsKepMode) text_start_idx = i;
					continue;
				}

				const std::size_t start_content = i + 1;
				const auto post_string = string.substr(start_content);
				const auto relative_pos = post_string.find(CharT('}'));

				if(relative_pos != std::basic_string_view<CharT>::npos){
					if(post_string.substr(0, relative_pos).find(CharT('{')) != std::basic_string_view<CharT>::npos){
						flush_pending_text(i);
						if constexpr (!IsKepMode) chars_.push_back(U'{');
						i++;
						if constexpr (!IsKepMode) text_start_idx = i;
						continue;
					}

					flush_pending_text(i);
					const std::size_t end_content_idx = start_content + relative_pos;
					auto content = string.substr(start_content, relative_pos);

					auto dispatch_parse = [&](std::string_view name_view) {
						std::string_view arg_view;
						auto colon_pos = name_view.find(':');
						if(colon_pos != std::string_view::npos){
							arg_view = name_view.substr(colon_pos + 1);
							name_view = name_view.substr(0, colon_pos);
						}
						std::uint32_t token_pos = IsKepMode ? static_cast<std::uint32_t>(i) : static_cast<std::uint32_t>(chars_.size());
						parse_tokens_(table, token_pos, name_view, colon_pos != std::string_view::npos, arg_view);
					};

					if constexpr (std::is_same_v<CharT, char>) {
						dispatch_parse(content);
					} else {
						std::string token_buffer;
						token_buffer.reserve(content.size());
						for(char32_t ch : content) {
							token_buffer.push_back(static_cast<char>(ch & 0xFF));
						}
						dispatch_parse(token_buffer);
					}

					i = end_content_idx + 1;
					if constexpr (!IsKepMode) text_start_idx = i;
					continue;
				}

				flush_pending_text(i);
				if constexpr (!IsKepMode) chars_.push_back(U'{');
				i++;
				if constexpr (!IsKepMode) text_start_idx = i;
				continue;
			}

			case CharT('}'): {
				if(i + 1 < size && ptr[i + 1] == CharT('}')){
					flush_pending_text(i);
					if constexpr (!IsKepMode) chars_.push_back(U'}');
					i += 2;
					if constexpr (!IsKepMode) text_start_idx = i;
					continue;
				}
				i++;
				continue;
			}

			default: {
				i++;
				break;
			}
		}
	}

	if constexpr (!IsKepMode) flush_pending_text(size);
}

template <typename CharT>
constexpr void tokenized_text::parse_from_impl_(std::basic_string_view<CharT> string, const rich_text_look_up_table* table, tokenize_tag tag){
	const size_t size = string.size();
	chars_.reserve(std::bit_ceil((size * sizeof(CharT) / 2) | 31));

	if (tag == tokenize_tag::raw || tag == tokenize_tag::kep) {
		typesetting::append_to_u32(chars_, string);

		if (tag == tokenize_tag::raw) {
			return;
		}

		std::u32string_view u32_view = chars_;
		this->parse_state_machine_<true>(u32_view, table);
	} else {
		this->parse_state_machine_<false>(string, table);
	}
}


constexpr void tokenized_text::parse_tokens_(const rich_text_look_up_table* table, std::uint32_t pos, std::string_view name,
	bool has_arg, std::string_view args){
	if(name.empty()){
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
				tokens_.emplace_back(tokens_[src_index].make_fallback(), chars_.size());
			}
		} else{
			const auto limit = std::min(name.size(), original_size);

			for(std::size_t i = 0; i < limit; ++i){
				if(name[i] != '/') break;
				const std::size_t src_index = original_size - 1 - i;
				tokens_.emplace_back(tokens_[src_index].make_fallback(), chars_.size());
			}
		}
		break;
	}


	default : tokens_.emplace_back(rich_text_token_argument{table, name, has_arg, args}, pos);
	}
}

}