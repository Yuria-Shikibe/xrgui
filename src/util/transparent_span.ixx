module;

#include <cassert>

export module mo_yanxi.transparent_span;

import std;
import mo_yanxi.meta_programming;

namespace mo_yanxi{
export
template <typename T, typename C, T C::* mptr>
struct transparent_convert_t{
};

export
template <auto mptr>
	requires (std::is_member_object_pointer_v<decltype(mptr)>)
constexpr inline transparent_convert_t<typename mo_yanxi::mptr_info<decltype(mptr)>::value_type, typename
	mo_yanxi::mptr_info<decltype(mptr)>::class_type, mptr> transparent_convert{};

export
template <typename T>
class transparent_span{
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

private:
    T* data_;
    size_type size_;

    static T* offset_ptr(T* ptr, difference_type step) noexcept{
       return std::bit_cast<T*>(std::bit_cast<std::uintptr_t>(ptr) + sizeof(T) * step);
    }

public:
    class iterator{
    public:
       using iterator_category = std::random_access_iterator_tag;
       using iterator_concept = std::random_access_iterator_tag;
       using value_type = std::remove_cv_t<T>;
       using difference_type = std::ptrdiff_t;
       using pointer = T*;
       using reference = T&;

    private:
       T* ptr_;

    public:
       constexpr iterator() noexcept : ptr_(nullptr){
       }

       constexpr explicit iterator(T* p) noexcept : ptr_(p){
       }

       constexpr reference operator*() const noexcept{ return *ptr_; }

       constexpr reference operator[](difference_type n) const noexcept{
          return *transparent_span::offset_ptr(ptr_, n);
       }

       constexpr iterator& operator++() noexcept{
          ptr_ = transparent_span::offset_ptr(ptr_, 1);
          return *this;
       }

       constexpr iterator operator++(int) noexcept{
          iterator tmp = *this;
          ++(*this);
          return tmp;
       }

       constexpr iterator& operator--() noexcept{
          ptr_ = transparent_span::offset_ptr(ptr_, -1);
          return *this;
       }

       constexpr iterator operator--(int) noexcept{
          iterator tmp = *this;
          --(*this);
          return tmp;
       }

       constexpr iterator& operator+=(difference_type n) noexcept{
          ptr_ = transparent_span::offset_ptr(ptr_, n);
          return *this;
       }

       constexpr iterator& operator-=(difference_type n) noexcept{
          ptr_ = transparent_span::offset_ptr(ptr_, -n);
          return *this;
       }

       friend constexpr iterator operator+(iterator it, difference_type n) noexcept{ return it += n; }
       friend constexpr iterator operator+(difference_type n, iterator it) noexcept{ return it += n; }
       friend constexpr iterator operator-(iterator it, difference_type n) noexcept{ return it -= n; }

       friend constexpr difference_type operator-(iterator a, iterator b) noexcept{
          return (std::bit_cast<std::uintptr_t>(a.ptr_) -
             std::bit_cast<std::uintptr_t>(b.ptr_)) / sizeof(T);
       }

       friend constexpr bool operator==(iterator a, iterator b) noexcept = default;
       constexpr auto operator<=>(const iterator&) const noexcept = default;
    };

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_iterator = std::const_iterator<iterator>;
    using const_reverse_iterator = std::const_iterator<reverse_iterator>;


    constexpr transparent_span() noexcept : data_(nullptr), size_(0){
    }

    constexpr transparent_span(T* first, size_type count) noexcept : data_(first), size_(count){
       // 确保传入非零长度时指针有效
       assert(count == 0 || first != nullptr);
    }

    constexpr transparent_span(T* first, T* last) noexcept
       : data_(first), size_((std::bit_cast<std::uintptr_t>(last) - std::bit_cast<std::uintptr_t>(first)) / sizeof(T)){
       // 确保 first 不在 last 之后
       assert(std::bit_cast<std::uintptr_t>(first) <= std::bit_cast<std::uintptr_t>(last));
    }


    template <std::ranges::contiguous_range Rng, std::remove_const_t<T> std::ranges::range_value_t<Rng>::* mptr>
       requires (std::is_pointer_interconvertible_with_class(mptr) && sizeof(T) == sizeof(std::ranges::range_value_t<
             Rng>) &&
          std::ranges::borrowed_range<Rng>)
    explicit(false) constexpr transparent_span(Rng&& rng,
       transparent_convert_t<std::remove_const_t<T>, std::ranges::range_value_t<Rng>, mptr>) noexcept
       : data_(reinterpret_cast<T*>(std::ranges::data(rng))), size_(std::ranges::distance(rng)){
    }


    template <typename Ty, std::remove_const_t<T> std::remove_const_t<Ty>::* mptr>
       requires (std::is_pointer_interconvertible_with_class(mptr) && sizeof(T) == sizeof(Ty))
    explicit(false) constexpr transparent_span(Ty& value, transparent_convert_t<std::remove_const_t<T>, std::remove_const_t<Ty>, mptr>) noexcept
       : data_(reinterpret_cast<T*>(std::addressof(value))), size_(1){
    }


    template <std::ranges::contiguous_range Rng>
       requires (std::ranges::borrowed_range<Rng> &&
          std::convertible_to<std::ranges::range_reference_t<Rng>, T&>)
    explicit(false) constexpr transparent_span(Rng&& rng) noexcept
       : data_(std::ranges::data(rng)), size_(std::ranges::distance(rng)){
    }


    constexpr transparent_span(const transparent_span<std::remove_const_t<T>>& other) noexcept
       requires std::is_const_v<T>
       : data_(other.data()), size_(other.size()){
    }


    constexpr pointer data() const noexcept{ return data_; }

    constexpr iterator begin() const noexcept{ return iterator(data_); }
    constexpr iterator end() const noexcept{ return iterator(transparent_span::offset_ptr(data_, size_)); }
    constexpr reverse_iterator rbegin() const noexcept{ return reverse_iterator(end()); }
    constexpr reverse_iterator rend() const noexcept{ return reverse_iterator(begin()); }

    constexpr const_iterator cbegin() const noexcept{ return const_iterator(begin()); }
    constexpr const_iterator cend() const noexcept{ return const_iterator(end()); }
    constexpr const_reverse_iterator crbegin() const noexcept{ return const_reverse_iterator(rbegin()); }
    constexpr const_reverse_iterator crend() const noexcept{ return const_reverse_iterator(rend()); }


    constexpr reference front() const noexcept{
       assert(!empty() && "transparent_span::front() on empty span");
       return *data_;
    }

    constexpr reference back() const noexcept{
       assert(!empty() && "transparent_span::back() on empty span");
       return *transparent_span::offset_ptr(data_, size_ - 1);
    }

    constexpr reference operator[](size_type idx) const noexcept{
       assert(idx < size_ && "transparent_span index out of bounds");
       return *transparent_span::offset_ptr(data_, idx);
    }


    constexpr size_type size() const noexcept{ return size_; }
    constexpr size_type size_bytes() const noexcept{ return size_ * sizeof(T); }
    constexpr bool empty() const noexcept{ return size_ == 0; }


    template <std::size_t Count>
    constexpr transparent_span first() const noexcept{
       assert(Count <= size_ && "transparent_span::first() Count out of bounds");
       return transparent_span(data_, Count);
    }

    constexpr transparent_span first(size_type count) const noexcept{
       assert(count <= size_ && "transparent_span::first() count out of bounds");
       return transparent_span(data_, count);
    }

    template <std::size_t Count>
    constexpr transparent_span last() const noexcept{
       assert(Count <= size_ && "transparent_span::last() Count out of bounds");
       return transparent_span(transparent_span::offset_ptr(data_, size_ - Count), Count);
    }

    constexpr transparent_span last(size_type count) const noexcept{
       assert(count <= size_ && "transparent_span::last() count out of bounds");
       return transparent_span(transparent_span::offset_ptr(data_, size_ - count), count);
    }

    template <std::size_t Offset, std::size_t Count = std::dynamic_extent>
    constexpr transparent_span subspan() const noexcept{
       assert(Offset <= size_ && "transparent_span::subspan() Offset out of bounds");
       assert((Count == std::dynamic_extent || Offset + Count <= size_) && "transparent_span::subspan() Count out of bounds");
       size_type rcount = (Count == std::dynamic_extent) ? (size_ - Offset) : Count;
       return transparent_span(transparent_span::offset_ptr(data_, Offset), rcount);
    }

    constexpr transparent_span subspan(size_type offset, size_type count = std::dynamic_extent) const noexcept{
       assert(offset <= size_ && "transparent_span::subspan() offset out of bounds");
       assert((count == std::dynamic_extent || offset + count <= size_) && "transparent_span::subspan() count out of bounds");
       size_type rcount = (count == std::dynamic_extent) ? (size_ - offset) : count;
       return transparent_span(transparent_span::offset_ptr(data_, offset), rcount);
    }


    friend constexpr bool operator==(const transparent_span& lhs, const transparent_span& rhs) noexcept{
       return std::ranges::equal(lhs, rhs);
    }

    friend constexpr auto operator<=>(const transparent_span& lhs, const transparent_span& rhs) noexcept{
       return std::lexicographical_compare_three_way(
          lhs.begin(), lhs.end(), rhs.begin(), rhs.end()
       );
    }
};

template <std::ranges::contiguous_range Rng, auto mptr>
	requires (std::is_pointer_interconvertible_with_class(mptr))
transparent_span(
	Rng&&,
	transparent_convert_t<typename mo_yanxi::mptr_info<decltype(mptr)>::value_type, std::ranges::range_value_t<Rng&&>,
		mptr>) ->
	transparent_span<std::remove_reference_t<std::invoke_result_t<decltype(mptr), std::ranges::range_reference_t<Rng
		&&>>>>;

template <typename Ty, auto mptr>
	requires (std::is_pointer_interconvertible_with_class(mptr))
transparent_span(
	Ty&, transparent_convert_t<typename mptr_info<decltype(mptr)>::value_type, std::remove_const_t<Ty>,mptr>) ->
	transparent_span<std::remove_reference_t<std::invoke_result_t<decltype(mptr), Ty&>>>;


template <std::ranges::contiguous_range Rng>
transparent_span(Rng&&) -> transparent_span<std::remove_reference_t<std::ranges::range_reference_t<Rng&&>>>;
}


template <typename T>
inline constexpr bool std::ranges::enable_view<mo_yanxi::transparent_span<T>> = true;

template <typename T>
constexpr bool inline std::ranges::enable_borrowed_range<mo_yanxi::transparent_span<T>> = true;
