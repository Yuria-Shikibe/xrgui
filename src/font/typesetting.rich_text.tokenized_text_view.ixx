export module mo_yanxi.typesetting.rich_text:tokenized_text_view;

import std;
import :tokenized_text;

namespace mo_yanxi::typesetting {

export struct tokenized_text_view {
private:
    std::u32string_view chars_;
    tokenized_text::token_subrange tokens_;
    std::size_t base_pos_;

    // 代理迭代器：在解引用时自动将 token 的绝对 pos 转换为相对于当前 view 的局部 pos
    struct offset_token_iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = tokenized_text::posed_token_argument;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = value_type; // 按值返回转换后的 token

        tokenized_text::token_iterator it;
        std::size_t base;

        constexpr reference operator*() const noexcept {
            auto token = *it;
            token.pos -= base; // 转换为局部坐标
            return token;
        }
        constexpr offset_token_iterator& operator++() noexcept { ++it; return *this; }
        constexpr offset_token_iterator operator++(int) noexcept { auto tmp = *this; ++it; return tmp; }
        constexpr bool operator==(const offset_token_iterator& other) const noexcept = default;
    };

public:
    using pos_t = std::u32string_view::size_type;
    using token_iterator = offset_token_iterator;
    using token_subrange = std::ranges::subrange<token_iterator>;

    [[nodiscard]] constexpr tokenized_text_view() = default;

    constexpr explicit tokenized_text_view(
        std::u32string_view chars,
        const tokenized_text::token_subrange& tokens,
        std::size_t base_pos) 
        : chars_(chars), tokens_(tokens), base_pos_(base_pos) {}

    constexpr explicit(false) tokenized_text_view(const tokenized_text& text)
        : chars_(text.get_text()), tokens_(text.get_tokens()), base_pos_(0) {}

	constexpr explicit(false) tokenized_text_view(const tokenized_text& text, std::size_t src,
		std::size_t size = std::u32string_view::npos)
		: chars_(text.get_text().substr(src, size)), tokens_([&]{
			auto src_itr = text.get_token(src);
			// 安全计算 end_pos
			std::size_t end_pos = (size == std::u32string_view::npos) ? text.get_text().size() : (src + size);
			auto dst_itr = text.get_token_sentinel(end_pos, src_itr);
			return tokenized_text::token_subrange{src_itr, dst_itr};
		}()), base_pos_(src){
    }

    constexpr bool empty() const noexcept { return chars_.empty(); }
    constexpr std::u32string_view get_text() const noexcept { return chars_; }

    constexpr token_iterator get_init_token() const noexcept {
        return token_iterator{tokens_.begin(), base_pos_};
    }

    constexpr token_iterator get_token(const pos_t local_pos, const token_iterator& last) const noexcept {
        auto real_last = last.it;
        auto it = std::ranges::lower_bound(real_last, tokens_.end(), local_pos + base_pos_, {}, &tokenized_text::posed_token_argument::pos);
        return token_iterator{it, base_pos_};
    }

    constexpr token_subrange get_token_group(const pos_t local_pos, const token_iterator& last) const {
        auto real_last = last.it;
        auto range = std::ranges::equal_range(real_last, tokens_.end(), local_pos + base_pos_, {}, &tokenized_text::posed_token_argument::pos);
        return token_subrange{token_iterator{range.begin(), base_pos_}, token_iterator{range.end(), base_pos_}};
    }
};

}