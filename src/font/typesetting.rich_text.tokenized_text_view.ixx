export module mo_yanxi.typesetting.rich_text:tokenized_text_view;

import std;
import :tokenized_text;

namespace mo_yanxi::typesetting {

export struct tokenized_text_view {
private:
    std::u32string_view chars_;
    std::span<const tokenized_text::posed_token_argument> tokens_;
    std::uint32_t base_pos_{0};

public:
    using pos_t = std::u32string_view::size_type;
    // 直接使用 span 的迭代器，它是天然的 contiguous_iterator
    using token_iterator = std::span<const tokenized_text::posed_token_argument>::iterator;
    using token_span = std::span<const tokenized_text::posed_token_argument>;

    constexpr explicit tokenized_text_view(
        std::u32string_view chars,
        token_span tokens,
        std::uint32_t base_pos)
        : chars_(chars), tokens_(tokens), base_pos_(base_pos) {}

    constexpr explicit(false) tokenized_text_view(const tokenized_text& text)
        : chars_(text.get_text()), tokens_(text.get_tokens()), base_pos_(0) {}

    constexpr explicit(false) tokenized_text_view(const tokenized_text& text, std::size_t src,
        std::size_t size = std::u32string_view::npos)
        : chars_(text.get_text().substr(src, size)), base_pos_(static_cast<std::uint32_t>(src)) {

        auto src_itr = text.get_token(src);
        std::size_t end_pos = (size == std::u32string_view::npos) ? text.get_text().size() : (src + size);
        auto dst_itr = text.get_token_sentinel(end_pos, src_itr);

        // 直接借用连续内存区间
        tokens_ = token_span{src_itr, dst_itr};
    }

    constexpr bool empty() const noexcept { return chars_.empty(); }
    constexpr std::u32string_view get_text() const noexcept { return chars_; }

    constexpr token_iterator get_init_token() const noexcept {
        return tokens_.begin();
    }

    constexpr token_iterator get_token(const pos_t local_pos, const token_iterator& last) const noexcept {
        // 核心修改：在外部查找时，将局部坐标映射回全局坐标
        const std::uint32_t global_pos = static_cast<std::uint32_t>(local_pos + base_pos_);
        return std::ranges::lower_bound(last, tokens_.end(), global_pos, {}, &tokenized_text::posed_token_argument::pos);
    }

    constexpr token_span get_token_group(const pos_t local_pos, const token_iterator& last) const {
        // 核心修改：同样在区间查找时进行坐标偏移补偿
        const std::uint32_t global_pos = static_cast<std::uint32_t>(local_pos + base_pos_);
    	//Work around, remove to address when msvcstl is fixed
        auto range = std::ranges::equal_range(std::to_address(last), std::to_address(tokens_.end()), global_pos, {}, &tokenized_text::posed_token_argument::pos);
        return token_span{range.begin(), range.end()};
    }
};

}