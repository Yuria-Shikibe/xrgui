module;

#include <mo_yanxi/adapted_attributes.hpp>
#include <cassert>
#include <version>



#ifdef MO_YANXI_UTILITY_ENABLE_CHECK
#define MO_YANXI_BYTE_POOL_LEAK_CHECK
#endif

export module byte_pool;

import std;

//TODO the buffer is actually partial RAII since the type is erased
// DO NOT PUT TYPE THAT IS NOT TRIVIALLY DESTRUCTIBLE INTO THE BUFFER!!

#ifdef __cpp_lib_is_implicit_lifetime
#define IMPLICIT_LIFETIME_PRED(ty) (std::is_implicit_lifetime_v<ty>)
#else
#define IMPLICIT_LIFETIME_PRED(ty) (std::is_trivial_v<ty>)
#endif


namespace mo_yanxi {

    export template <typename Alloc = std::allocator<std::byte>>
    struct byte_pool;

    export template <typename T, typename Alloc>
    struct byte_borrow;

    export template <typename T>
    struct byte_buffer;

    using raw_buffer = byte_buffer<std::byte>;

    export
    template <typename T>
    struct byte_buffer {
        template <typename Alloc>
        friend struct byte_pool;

        template <typename U, typename Alloc>
        friend struct byte_borrow;

        template <typename U>
        friend struct byte_buffer;

    private:
        T* data_ = nullptr;
        unsigned size_ = 0;
        unsigned capacity_ = 0;

    public:
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        constexpr byte_buffer() noexcept = default;

        constexpr byte_buffer(T* data, unsigned size, unsigned capacity) noexcept
            : data_(data), size_(size), capacity_(capacity) {}

        constexpr raw_buffer as_bytes() const noexcept {
            if constexpr (std::is_same_v<T, std::byte>) {
                return *this;
            } else {
                return raw_buffer{
                    reinterpret_cast<std::byte*>(data_),
                    size_ * sizeof(T),
                    capacity_ * sizeof(T)
                };
            }
        }

        template <typename U>
        constexpr byte_buffer<U> reinterpret_as() const noexcept {
            if constexpr (std::is_same_v<T, U>) return *this;
            return byte_buffer<U>{
                reinterpret_cast<U*>(data_),
                (size_ * sizeof(T)) / sizeof(U),
                (capacity_ * sizeof(T)) / sizeof(U)
            };
        }

        FORCE_INLINE constexpr T* data() const noexcept { return data_; }
        FORCE_INLINE constexpr std::span<T> to_span() const noexcept { return std::span<T>(data_, size_); }
        FORCE_INLINE constexpr unsigned capacity() const noexcept { return capacity_; }
        FORCE_INLINE constexpr unsigned size() const noexcept { return size_; }

        FORCE_INLINE constexpr iterator begin() const noexcept { return data_; }
        FORCE_INLINE constexpr iterator end() const noexcept { return data_ + size_; }
        FORCE_INLINE constexpr const_iterator cbegin() const noexcept { return begin(); }
        FORCE_INLINE constexpr const_iterator cend() const noexcept { return end(); }

        FORCE_INLINE constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator{end()}; }
        FORCE_INLINE constexpr reverse_iterator rend() const noexcept { return reverse_iterator{begin()}; }
        FORCE_INLINE constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator{cend()}; }
        FORCE_INLINE constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator{cbegin()}; }

        FORCE_INLINE constexpr explicit operator bool() const noexcept { return data_ != nullptr; }

        FORCE_INLINE constexpr void fill(std::uint8_t val) const noexcept
            requires(std::is_trivial_v<T>)
        {
            if (std::is_constant_evaluated()) {
                for (std::byte& v : this->as_bytes()) {
                    v = std::byte{val};
                }
            } else {
                std::memset(static_cast<void*>(data_), val, size_ * sizeof(T));
            }
        }
    };

    template <typename T, typename Alloc>
    struct byte_borrow {
    private:
        using pool_t = byte_pool<Alloc>;
        pool_t* owner_{};
        byte_buffer<T> array_{};

        constexpr void retire_() noexcept {
            if (array_.data()) {
                // 1. 只有非平凡析构类型，才需要调用析构函数
                // 对于 int/char 等，这里直接跳过，0 开销
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    std::destroy(array_.begin(), array_.end());
                }

                // 2. 归还内存
                owner_->retire(array_.as_bytes());
                array_ = {};
            }
        }

    public:
        using value_type = T;

        constexpr byte_borrow() noexcept = default;

        constexpr byte_borrow(pool_t* owner, byte_buffer<T> array) noexcept
            : owner_(owner), array_(array) {
        }

        constexpr byte_borrow(const byte_borrow& other) = delete;

        constexpr byte_borrow(byte_borrow&& other) noexcept
            : owner_(std::exchange(other.owner_, {})),
              array_(std::exchange(other.array_, {})) {
        }

        constexpr byte_borrow& operator=(const byte_borrow& other) = delete;

        constexpr byte_borrow& operator=(byte_borrow&& other) noexcept {
            if (this == &other) return *this;
            retire_();
            owner_ = std::exchange(other.owner_, {});
            array_ = std::exchange(other.array_, {});
            return *this;
        }

        constexpr byte_pool<Alloc>& owner() const noexcept {
            assert(owner_ != nullptr);
            return *owner_;
        }

    	template <bool reserve_current = true>
		constexpr void resize(unsigned count);

        FORCE_INLINE constexpr T* data() const noexcept { return array_.data(); }
        [[nodiscard]] constexpr byte_buffer<T> get() const noexcept { return array_; }
        explicit(false) operator byte_buffer<T>() const noexcept { return array_; }

        constexpr ~byte_borrow() {
            retire_();
        }

        constexpr T& operator[](unsigned idx) const noexcept {
            // 性能模式下可考虑移除 assert
            assert(idx < array_.size());
            return array_.data()[idx];
        }
    };

    template <typename Alloc>
    struct byte_pool {
        using allocator_type = Alloc;

    private:
        ADAPTED_NO_UNIQUE_ADDRESS allocator_type allocator_{};

        static constexpr unsigned MIN_SHIFT = 9;
        static constexpr unsigned BUCKET_CNT = 16;

        using bucket_t = std::vector<raw_buffer, typename std::allocator_traits<allocator_type>::template rebind_alloc<raw_buffer>>;
        std::array<bucket_t, BUCKET_CNT> buckets_{};

#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
        std::vector<std::byte*, typename std::allocator_traits<allocator_type>::template rebind_alloc<std::byte*>> allocated_{};
#endif

        FORCE_INLINE constexpr void dealloc_buf(raw_buffer arr) noexcept {
            std::allocator_traits<allocator_type>::deallocate(allocator_, arr.data(), arr.capacity());
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
            if (auto it = std::ranges::find(allocated_, arr.data()); it != allocated_.end()) {
                allocated_.erase(it);
            } else {
                std::println(std::cerr, "Return Buffer Not Belong To The Pool: {:p}", static_cast<void*>(arr.data()));
                std::terminate();
            }
#endif
        }

        FORCE_INLINE constexpr raw_buffer alloc_sys(unsigned capacity_bytes) {
            const auto ptr = allocator_.allocate(capacity_bytes);
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
            allocated_.push_back(ptr);
#endif
            return raw_buffer{ptr, capacity_bytes, capacity_bytes};
        }

        FORCE_INLINE constexpr static unsigned get_bucket_index(unsigned capacity) noexcept {
            return std::countr_zero(capacity) - MIN_SHIFT;
        }

    public:
        // --- 核心修改：Borrow 时进行条件初始化 ---
        template <typename T = std::byte>
        [[nodiscard]] constexpr byte_borrow<T, Alloc> borrow(unsigned count) {
            raw_buffer raw = acquire(count * sizeof(T));

            T* ptr = reinterpret_cast<T*>(raw.data());

            // 性能优化：
            // 如果 T 是隐式生存期类型 (如 int, char, trivial struct)，
            // 分配的原始内存已经可以被视为 T 对象，无需调用 placement new / default construct。
            // 只有当 T 是复杂类型 (如 std::string) 时，才初始化对象。
            if constexpr (!IMPLICIT_LIFETIME_PRED(T)) {
                std::uninitialized_default_construct(ptr, ptr + count);
            }

            byte_buffer<T> typed_buf{
                ptr,
                count,
                raw.capacity() / sizeof(T)
            };

            return byte_borrow<T, Alloc>{this, typed_buf};
        }

        [[nodiscard]] constexpr raw_buffer acquire(unsigned size_bytes) {
            if (size_bytes == 0) [[unlikely]] {
                return {};
            }

            const unsigned capacity = std::max(1u << MIN_SHIFT, std::bit_ceil(size_bytes));

            if (capacity > (1u << (MIN_SHIFT + BUCKET_CNT - 1))) [[unlikely]] {
                auto buf = alloc_sys(capacity);
                return raw_buffer{buf.data(), size_bytes, capacity};
            }

            const unsigned idx = get_bucket_index(capacity);
            auto& bucket = buckets_[idx];

            if (!bucket.empty()) {
                raw_buffer buf = bucket.back();
                bucket.pop_back();
                return raw_buffer{buf.data(), size_bytes, buf.capacity()};
            }

            auto buf = alloc_sys(capacity);
            return raw_buffer{buf.data(), size_bytes, capacity};
        }

        constexpr void retire(raw_buffer byte_array) noexcept {
            if (!byte_array) return;
            const unsigned cap = byte_array.capacity();
            // 检查 cap 是否合法 (>=512 且为 2 的幂)
            if (cap < (1u << MIN_SHIFT) || std::popcount(cap) != 1) {
                dealloc_buf(byte_array);
                return;
            }
            const unsigned idx = get_bucket_index(cap);
            if (idx >= BUCKET_CNT) {
                dealloc_buf(byte_array);
                return;
            }
            try {
                buckets_[idx].push_back(byte_array);
            } catch (...) {
                dealloc_buf(byte_array);
            }
        }

        constexpr byte_pool() = default;
        constexpr explicit byte_pool(const allocator_type& allocator)
            : allocator_(allocator) {}

        constexpr byte_pool(const byte_pool& other) = delete;
        constexpr byte_pool(byte_pool&& other) noexcept
            : allocator_(std::exchange(other.allocator_, {})),
              buckets_(std::exchange(other.buckets_, {}))
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
              , allocated_(std::exchange(other.allocated_, {}))
#endif
        {
        }

        constexpr byte_pool& operator=(const byte_pool& other) = delete;
        constexpr byte_pool& operator=(byte_pool&& other) noexcept {
            if (this == &other) return *this;
            clear();
            allocator_ = std::exchange(other.allocator_, {});
            buckets_ = std::exchange(other.buckets_, {});
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
            allocated_ = std::exchange(other.allocated_, {});
#endif
            return *this;
        }

        constexpr ~byte_pool() {
            clear();
        }

        constexpr void clear() noexcept {
            for (auto& bucket : buckets_) {
                for (const auto& buf : bucket) {
                    dealloc_buf(buf);
                }
                bucket.clear();
            }
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
            if (!allocated_.empty()) {
                std::println(std::cerr, "Leak On Byte Pool Detected. Count: {}", allocated_.size());
                std::terminate();
            }
#endif
        }

        constexpr std::size_t cached_count() const noexcept {
            std::size_t total = 0;
            for (const auto& bucket : buckets_) {
                total += bucket.size();
            }
            return total;
        }

        constexpr void trim(std::size_t keep_count = 0) noexcept {
            if (keep_count == 0) {
                clear();
                return;
            }
            std::size_t current = cached_count();
            if (current <= keep_count) return;

            for (auto& bucket : std::views::reverse(buckets_)) {
                while (!bucket.empty() && current > keep_count) {
                    dealloc_buf(bucket.back());
                    bucket.pop_back();
                    current--;
                }
                if (current <= keep_count) break;
            }
        }
    };

    // --- 实现 acquire_new 并在扩容时跳过不必要的初始化 ---

    template <typename T, typename Alloc>
    template <bool reserve_current>
    constexpr void byte_borrow<T, Alloc>::resize(unsigned count) {
        assert(owner_ != nullptr);
        // 1. 如果已有空间足够
        if (count <= array_.capacity()) {
            unsigned old_size = array_.size();
            if (count > old_size) {
                // 增长：仅当 T 不是隐式生存期类型时，才初始化新区域
                if constexpr (!IMPLICIT_LIFETIME_PRED(T)) {
                    std::uninitialized_default_construct(array_.data() + old_size, array_.data() + count);
                }
            } else if (count < old_size) {
                // 缩容：仅当 T 不平凡析构时，才析构多余部分
                // 注意：即使 reserve_current 为 false，原地缩容通常也意味着截断，逻辑保持一致
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    std::destroy(array_.data() + count, array_.data() + old_size);
                }
            }

            // 原地更新 buffer 大小
            array_ = byte_buffer<T>{array_.data(), count, array_.capacity()};
            return;
        }

        // 2. 空间不足，需要分配新的 Buffer
        byte_borrow<T, Alloc> new_borrow = owner_->template borrow<T>(count);

        // 3. 数据迁移 (仅当 reserve_current 为 true 时执行)
        // [修改]: 使用 if constexpr 包裹迁移逻辑
        if constexpr (reserve_current) {
            unsigned old_size = array_.size();
            if (old_size > 0) {
                if constexpr (!IMPLICIT_LIFETIME_PRED(T)) {
                    // T 是复杂类型，new_borrow 里的对象已经构造好了 (borrow 行为)
                    // 使用 move 赋值进行覆盖
                    std::move(array_.begin(), array_.end(), new_borrow.data());
                } else {
                    // T 是 implicit lifetime (int, char)，内存是 raw 的
                    // 直接 memcpy
                    std::memcpy(new_borrow.data(), array_.data(), old_size * sizeof(T));
                }
            }
        }

        // 4. 替换旧 Buffer 并释放资源
        // 这一步赋值会调用 byte_borrow 的 move assignment operator。
        // operator= 内部会调用 retire_()。
        // retire_() 内部会检查 !std::is_trivially_destructible_v<T> 并调用 std::destroy，
        // 然后归还内存给 pool。
        // 因此，如果 reserve_current 为 false，旧数据就在这里被安全析构了。
        *this = std::move(new_borrow);
    }

} // namespace mo_yanxi
