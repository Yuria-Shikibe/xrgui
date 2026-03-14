module;

#if __has_include(<simdutf.h>)
#define HAS_SIMDUTF 1
#else
#define HAS_SIMDUTF 0
#endif

#ifdef HAS_SIMDUTF
#include <simdutf.h>
#endif

export module mo_yanxi.unicode;

import std;

namespace mo_yanxi::unicode {

// ==========================================
// Concepts 定义 (对外导出)
// ==========================================
export template <typename T>
concept utf8_type = sizeof(T) == sizeof(char8_t) && std::integral<T>;

export template <typename T>
concept utf32_type = sizeof(T) == sizeof(char32_t) && std::integral<T>;


// ==========================================
// 内部 Fallback 实现 (不加 export，仅模块内可见)
// ==========================================

// --- Fallback: UTF-8 转 UTF-32 ---
template <utf8_type InChar, utf32_type OutChar>
constexpr std::size_t fallback_utf8_to_utf32(const InChar* src, std::size_t src_len, OutChar* dest, std::size_t dest_len) noexcept {
    std::size_t src_pos = 0;
    std::size_t dest_pos = 0;

    while (src_pos < src_len && dest_pos < dest_len) {
        std::uint32_t code_point = 0;
        std::uint8_t first_byte = static_cast<std::uint8_t>(src[src_pos]);

        if (first_byte < 0x80) {
            code_point = first_byte;
            src_pos += 1;
        } else if ((first_byte & 0xE0) == 0xC0) {
            if (src_pos + 1 >= src_len) break;
            code_point = (first_byte & 0x1F) << 6;
            code_point |= (static_cast<std::uint8_t>(src[src_pos + 1]) & 0x3F);
            src_pos += 2;
        } else if ((first_byte & 0xF0) == 0xE0) {
            if (src_pos + 2 >= src_len) break;
            code_point = (first_byte & 0x0F) << 12;
            code_point |= ((static_cast<std::uint8_t>(src[src_pos + 1]) & 0x3F) << 6);
            code_point |= (static_cast<std::uint8_t>(src[src_pos + 2]) & 0x3F);
            src_pos += 3;
        } else if ((first_byte & 0xF8) == 0xF0) {
            if (src_pos + 3 >= src_len) break;
            code_point = (first_byte & 0x07) << 18;
            code_point |= ((static_cast<std::uint8_t>(src[src_pos + 1]) & 0x3F) << 12);
            code_point |= ((static_cast<std::uint8_t>(src[src_pos + 2]) & 0x3F) << 6);
            code_point |= (static_cast<std::uint8_t>(src[src_pos + 3]) & 0x3F);
            src_pos += 4;
        } else {
            src_pos += 1;
            continue;
        }

        dest[dest_pos++] = static_cast<OutChar>(code_point);
    }

    return dest_pos;
}

// --- Fallback: UTF-32 转 UTF-8 ---
template <utf32_type InChar, utf8_type OutChar>
constexpr std::size_t fallback_utf32_to_utf8(const InChar* src, std::size_t src_len, OutChar* dest, std::size_t dest_len) noexcept {
    std::size_t src_pos = 0;
    std::size_t dest_pos = 0;

    while (src_pos < src_len && dest_pos < dest_len) {
        std::uint32_t code_point = static_cast<std::uint32_t>(src[src_pos]);

        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            src_pos++;
            continue;
        }

        if (code_point <= 0x7F) {
            dest[dest_pos++] = static_cast<OutChar>(code_point);
        } else if (code_point <= 0x7FF) {
            if (dest_pos + 1 >= dest_len) break;
            dest[dest_pos++] = static_cast<OutChar>(0xC0 | ((code_point >> 6) & 0x1F));
            dest[dest_pos++] = static_cast<OutChar>(0x80 | (code_point & 0x3F));
        } else if (code_point <= 0xFFFF) {
            if (dest_pos + 2 >= dest_len) break;
            dest[dest_pos++] = static_cast<OutChar>(0xE0 | ((code_point >> 12) & 0x0F));
            dest[dest_pos++] = static_cast<OutChar>(0x80 | ((code_point >> 6) & 0x3F));
            dest[dest_pos++] = static_cast<OutChar>(0x80 | (code_point & 0x3F));
        } else if (code_point <= 0x10FFFF) {
            if (dest_pos + 3 >= dest_len) break;
            dest[dest_pos++] = static_cast<OutChar>(0xF0 | ((code_point >> 18) & 0x07));
            dest[dest_pos++] = static_cast<OutChar>(0x80 | ((code_point >> 12) & 0x3F));
            dest[dest_pos++] = static_cast<OutChar>(0x80 | ((code_point >> 6) & 0x3F));
            dest[dest_pos++] = static_cast<OutChar>(0x80 | (code_point & 0x3F));
        }

        src_pos++;
    }

    return dest_pos;
}

// ==========================================
// 基础转换 API (ptr, size) - 对外导出
// ==========================================

export template <utf8_type InChar, utf32_type OutChar>
constexpr std::size_t utf8_to_utf32(const InChar* src, std::size_t src_len, OutChar* dest, std::size_t dest_len) noexcept {
#if HAS_SIMDUTF
    if !consteval {
        std::size_t req_len = simdutf::utf32_length_from_utf8(reinterpret_cast<const char*>(src), src_len);
        if (req_len <= dest_len) {
            return simdutf::convert_utf8_to_utf32(
                reinterpret_cast<const char*>(src),
                src_len,
                reinterpret_cast<char32_t*>(dest)
            );
        }
    }
#endif
    return unicode::fallback_utf8_to_utf32(src, src_len, dest, dest_len);
}

export template <utf32_type InChar, utf8_type OutChar>
constexpr std::size_t utf32_to_utf8(const InChar* src, std::size_t src_len, OutChar* dest, std::size_t dest_len) noexcept {
#if HAS_SIMDUTF
    if !consteval {
        std::size_t req_len = simdutf::utf8_length_from_utf32(reinterpret_cast<const char32_t*>(src), src_len);
        if (req_len <= dest_len) {
            return simdutf::convert_utf32_to_utf8(
                reinterpret_cast<const char32_t*>(src),
                src_len,
                reinterpret_cast<char*>(dest)
            );
        }
    }
#endif
    return unicode::fallback_utf32_to_utf8(src, src_len, dest, dest_len);
}

// ==========================================
// 基础转换 API (contiguous_range) - 对外导出
// ==========================================

export template <std::ranges::contiguous_range R, utf32_type OutChar>
requires utf8_type<std::ranges::range_value_t<R>>
constexpr std::size_t utf8_to_utf32(R&& src_rng, OutChar* dest, std::size_t dest_len) noexcept {
    return unicode::utf8_to_utf32(std::ranges::data(src_rng), std::ranges::size(src_rng), dest, dest_len);
}

export template <std::ranges::contiguous_range R, utf8_type OutChar>
requires utf32_type<std::ranges::range_value_t<R>>
constexpr std::size_t utf32_to_utf8(R&& src_rng, OutChar* dest, std::size_t dest_len) noexcept {
    return unicode::utf32_to_utf8(std::ranges::data(src_rng), std::ranges::size(src_rng), dest, dest_len);
}

// ==========================================
// Assign 系列 API (ptr, size) - 对外导出
// ==========================================

export template <utf8_type InChar, utf32_type OutChar, typename Traits, typename Alloc>
constexpr void assign_utf8_to_utf32(const InChar* source, std::size_t length, std::basic_string<OutChar, Traits, Alloc>& target) {
    std::size_t max_len = length;
#if HAS_SIMDUTF
    if !consteval { max_len = simdutf::utf32_length_from_utf8(reinterpret_cast<const char*>(source), length); }
#endif
    target.resize_and_overwrite(max_len, [source, length](OutChar* buf, std::size_t buf_size) {
        return unicode::utf8_to_utf32(source, length, buf, buf_size);
    });
}

export template <utf8_type InChar, utf32_type OutChar, typename Alloc>
constexpr void assign_utf8_to_utf32(const InChar* source, std::size_t length, std::vector<OutChar, Alloc>& target) {
    std::size_t max_len = length;
#if HAS_SIMDUTF
    if !consteval { max_len = simdutf::utf32_length_from_utf8(reinterpret_cast<const char*>(source), length); }
#endif
    target.resize(max_len);
    std::size_t actual_len = unicode::utf8_to_utf32(source, length, target.data(), max_len);
    target.resize(actual_len);
}

export template <utf32_type InChar, utf8_type OutChar, typename Traits, typename Alloc>
constexpr void assign_utf32_to_utf8(const InChar* source, std::size_t length, std::basic_string<OutChar, Traits, Alloc>& target) {
    std::size_t max_len = length * 4;
#if HAS_SIMDUTF
    if !consteval { max_len = simdutf::utf8_length_from_utf32(reinterpret_cast<const char32_t*>(source), length); }
#endif
    target.resize_and_overwrite(max_len, [source, length](OutChar* buf, std::size_t buf_size) {
        return unicode::utf32_to_utf8(source, length, buf, buf_size);
    });
}

export template <utf32_type InChar, utf8_type OutChar, typename Alloc>
constexpr void assign_utf32_to_utf8(const InChar* source, std::size_t length, std::vector<OutChar, Alloc>& target) {
    std::size_t max_len = length * 4;
#if HAS_SIMDUTF
    if !consteval { max_len = simdutf::utf8_length_from_utf32(reinterpret_cast<const char32_t*>(source), length); }
#endif
    target.resize(max_len);
    std::size_t actual_len = unicode::utf32_to_utf8(source, length, target.data(), max_len);
    target.resize(actual_len);
}

// ==========================================
// Assign 系列 API (contiguous_range) - 对外导出
// ==========================================

export template <std::ranges::contiguous_range R, utf32_type OutChar, typename Traits, typename Alloc>
requires utf8_type<std::ranges::range_value_t<R>>
constexpr void assign_utf8_to_utf32(R&& source, std::basic_string<OutChar, Traits, Alloc>& target) {
    unicode::assign_utf8_to_utf32(std::ranges::data(source), std::ranges::size(source), target);
}

export template <std::ranges::contiguous_range R, utf32_type OutChar, typename Alloc>
requires utf8_type<std::ranges::range_value_t<R>>
constexpr void assign_utf8_to_utf32(R&& source, std::vector<OutChar, Alloc>& target) {
    unicode::assign_utf8_to_utf32(std::ranges::data(source), std::ranges::size(source), target);
}

export template <std::ranges::contiguous_range R, utf8_type OutChar, typename Traits, typename Alloc>
requires utf32_type<std::ranges::range_value_t<R>>
constexpr void assign_utf32_to_utf8(R&& source, std::basic_string<OutChar, Traits, Alloc>& target) {
    unicode::assign_utf32_to_utf8(std::ranges::data(source), std::ranges::size(source), target);
}

export template <std::ranges::contiguous_range R, utf8_type OutChar, typename Alloc>
requires utf32_type<std::ranges::range_value_t<R>>
constexpr void assign_utf32_to_utf8(R&& source, std::vector<OutChar, Alloc>& target) {
    unicode::assign_utf32_to_utf8(std::ranges::data(source), std::ranges::size(source), target);
}


export template <std::ranges::contiguous_range R>
requires utf8_type<std::ranges::range_value_t<R>>
constexpr std::u32string utf8_to_utf32(R&& src_rng) {
	std::u32string rst;
	unicode::assign_utf8_to_utf32(std::forward<R>(src_rng), rst);
	return rst;
}

export template <utf8_type OutChar = char, std::ranges::contiguous_range R>
requires utf32_type<std::ranges::range_value_t<R>>
constexpr std::basic_string<OutChar> utf32_to_utf8(R&& src_rng) {
	std::basic_string<OutChar> rst;
	unicode::assign_utf32_to_utf8(std::forward<R>(src_rng), rst);
	return rst;
}

// ==========================================
// Append 系列 API (ptr, size) - 对外导出
// ==========================================
// ==========================================
// Append 系列 API (ptr, size) - 对外导出
// ==========================================

export template <utf8_type InChar, utf32_type OutChar, typename Traits, typename Alloc>
constexpr void append_utf8_to_utf32(const InChar* source, std::size_t length, std::basic_string<OutChar, Traits, Alloc>& target) {
    std::size_t append_max_len = length;
#if HAS_SIMDUTF
    if !consteval { append_max_len = simdutf::utf32_length_from_utf8(reinterpret_cast<const char*>(source), length); }
#endif
    std::size_t old_size = target.size();
    target.resize_and_overwrite(old_size + append_max_len, [source, length, old_size](OutChar* buf, std::size_t buf_size) {
        std::size_t actual_added = unicode::utf8_to_utf32(source, length, buf + old_size, buf_size - old_size);
        return old_size + actual_added;
    });
}

export template <utf8_type InChar, utf32_type OutChar, typename Alloc>
constexpr void append_utf8_to_utf32(const InChar* source, std::size_t length, std::vector<OutChar, Alloc>& target) {
    std::size_t append_max_len = length;
#if HAS_SIMDUTF
    if !consteval { append_max_len = simdutf::utf32_length_from_utf8(reinterpret_cast<const char*>(source), length); }
#endif
    std::size_t old_size = target.size();
    target.resize(old_size + append_max_len);
    std::size_t actual_added = unicode::utf8_to_utf32(source, length, target.data() + old_size, append_max_len);
    target.resize(old_size + actual_added);
}

export template <utf32_type InChar, utf8_type OutChar, typename Traits, typename Alloc>
constexpr void append_utf32_to_utf8(const InChar* source, std::size_t length, std::basic_string<OutChar, Traits, Alloc>& target) {
    std::size_t append_max_len = length * 4;
#if HAS_SIMDUTF
    if !consteval { append_max_len = simdutf::utf8_length_from_utf32(reinterpret_cast<const char32_t*>(source), length); }
#endif
    std::size_t old_size = target.size();
    target.resize_and_overwrite(old_size + append_max_len, [source, length, old_size](OutChar* buf, std::size_t buf_size) {
        std::size_t actual_added = unicode::utf32_to_utf8(source, length, buf + old_size, buf_size - old_size);
        return old_size + actual_added;
    });
}

export template <utf32_type InChar, utf8_type OutChar, typename Alloc>
constexpr void append_utf32_to_utf8(const InChar* source, std::size_t length, std::vector<OutChar, Alloc>& target) {
    std::size_t append_max_len = length * 4;
#if HAS_SIMDUTF
    if !consteval { append_max_len = simdutf::utf8_length_from_utf32(reinterpret_cast<const char32_t*>(source), length); }
#endif
    std::size_t old_size = target.size();
    target.resize(old_size + append_max_len);
    std::size_t actual_added = unicode::utf32_to_utf8(source, length, target.data() + old_size, append_max_len);
    target.resize(old_size + actual_added);
}

// ==========================================
// Append 系列 API (contiguous_range) - 对外导出
// ==========================================

export template <std::ranges::contiguous_range R, utf32_type OutChar, typename Traits, typename Alloc>
requires utf8_type<std::ranges::range_value_t<R>>
constexpr void append_utf8_to_utf32(R&& source, std::basic_string<OutChar, Traits, Alloc>& target) {
    unicode::append_utf8_to_utf32(std::ranges::data(source), std::ranges::size(source), target);
}

export template <std::ranges::contiguous_range R, utf32_type OutChar, typename Alloc>
requires utf8_type<std::ranges::range_value_t<R>>
constexpr void append_utf8_to_utf32(R&& source, std::vector<OutChar, Alloc>& target) {
    unicode::append_utf8_to_utf32(std::ranges::data(source), std::ranges::size(source), target);
}

export template <std::ranges::contiguous_range R, utf8_type OutChar, typename Traits, typename Alloc>
requires utf32_type<std::ranges::range_value_t<R>>
constexpr void append_utf32_to_utf8(R&& source, std::basic_string<OutChar, Traits, Alloc>& target) {
    unicode::append_utf32_to_utf8(std::ranges::data(source), std::ranges::size(source), target);
}

export template <std::ranges::contiguous_range R, utf8_type OutChar, typename Alloc>
requires utf32_type<std::ranges::range_value_t<R>>
constexpr void append_utf32_to_utf8(R&& source, std::vector<OutChar, Alloc>& target) {
    unicode::append_utf32_to_utf8(std::ranges::data(source), std::ranges::size(source), target);
}

} // namespace mo_yanxi::unicode