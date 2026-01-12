module;

#include <mo_yanxi/adapted_attributes.hpp>
#include <cassert>
#include <cstring>

#ifdef MO_YANXI_UTILITY_ENABLE_CHECK
#define MO_YANXI_BYTE_POOL_LEAK_CHECK
#endif

export module byte_pool;

import std;

namespace mo_yanxi {
    export
    template <typename Alloc = std::allocator<std::byte>>
    struct byte_pool;

    export
    struct byte_buffer {
        template <typename Alloc>
        friend struct byte_pool;

    private:
        std::byte* data_;
        unsigned size_;
        unsigned capacity_;

    public:
        using iterator = std::byte*;
        using const_iterator = const std::byte*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        template <typename Ty = std::byte>
        FORCE_INLINE constexpr Ty* data() const noexcept {
            return reinterpret_cast<Ty*>(data_);
        }

        template <typename T>
        FORCE_INLINE constexpr std::span<T> to_span() const noexcept{
            return std::span<T>(data<T>(), size_ / sizeof(T));
        }

        FORCE_INLINE constexpr unsigned capacity() const noexcept {
            return capacity_;
        }

        FORCE_INLINE constexpr unsigned size() const noexcept {
            return size_;
        }

        FORCE_INLINE constexpr iterator begin() const noexcept {
            return data_;
        }

        FORCE_INLINE constexpr iterator end() const noexcept {
            return data_ + size();
        }

        FORCE_INLINE constexpr const_iterator cbegin() const noexcept {
            return begin();
        }

        FORCE_INLINE constexpr const_iterator cend() const noexcept {
            return end();
        }

        FORCE_INLINE constexpr reverse_iterator rbegin() const noexcept {
            return reverse_iterator{end()};
        }

        FORCE_INLINE constexpr reverse_iterator rend() const noexcept {
            return reverse_iterator{begin()};
        }

        FORCE_INLINE constexpr const_reverse_iterator crbegin() const noexcept {
            return const_reverse_iterator{cend()};
        }

        FORCE_INLINE constexpr const_reverse_iterator crend() const noexcept {
            return const_reverse_iterator{cbegin()};
        }

        FORCE_INLINE constexpr explicit operator bool() const noexcept {
            return data_ != nullptr;
        }

        FORCE_INLINE constexpr void fill(std::uint8_t val) const noexcept {
            if consteval {
                for (std::byte& v : *this) {
                    v = std::byte{val};
                }
            } else {
                std::memset(data_, val, size_);
            }
        }
    };

    export
    template <typename Alloc>
    struct byte_borrow {
    private:
        using pool_t = byte_pool<Alloc>;
        pool_t* owner_{};
        byte_buffer array_{};

        constexpr void retire_() const noexcept;

    public:
        constexpr byte_borrow() noexcept = default;

        constexpr byte_borrow(pool_t* owner, const byte_buffer& array) noexcept
            : owner_(owner),
              array_(array) {
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

        [[nodiscard]] constexpr byte_buffer get() const noexcept {
            return array_;
        }

        explicit(false) operator byte_buffer() const noexcept {
            return array_;
        }

        constexpr ~byte_borrow() {
            retire_();
        }
    };

    template <typename Alloc>
    struct byte_pool {
        using allocator_type = Alloc;

    private:
        ADAPTED_NO_UNIQUE_ADDRESS allocator_type allocator_{};

        // --- 配置参数 ---
        // 最小块大小：2^9 = 512 Bytes
        static constexpr unsigned MIN_SHIFT = 9;
        // 桶数量：16。
        // 覆盖范围：512B (idx 0) 到 16MB (idx 15, 2^(9+15)=2^24)
        static constexpr unsigned BUCKET_CNT = 16;

        using bucket_t = std::vector<byte_buffer, typename std::allocator_traits<allocator_type>::template rebind_alloc<byte_buffer>>;

        // 核心存储：固定大小的数组，每个元素是一个 vector (桶)
        std::array<bucket_t, BUCKET_CNT> buckets_{};

#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
        std::vector<std::byte*, typename std::allocator_traits<allocator_type>::template rebind_alloc<std::byte*>> allocated_{};
#endif

        FORCE_INLINE constexpr void dealloc_buf(byte_buffer arr) noexcept {
            std::allocator_traits<allocator_type>::deallocate(allocator_, arr.data(), arr.capacity());
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
            // 简单的移除检查 (性能较低，仅用于Debug)
            if (auto it = std::ranges::find(allocated_, arr.data()); it != allocated_.end()) {
                allocated_.erase(it); // vector erase 是 O(N)，但在 Debug 模式下可接受
            } else {
                std::println(std::cerr, "Return Buffer Not Belong To The Pool: {:p}", arr.data<void>());
                std::terminate();
            }
#endif
        }

        // 直接从系统分配，不经过桶查找
        FORCE_INLINE constexpr byte_buffer alloc_sys(unsigned capacity) {
            const auto ptr = allocator_.allocate(capacity);
#ifdef MO_YANXI_BYTE_POOL_LEAK_CHECK
            allocated_.push_back(ptr);
#endif
            byte_buffer buf{};
            buf.data_ = ptr;
            buf.capacity_ = capacity;
            // size_ 在外部设置
            return buf;
        }

        // 根据容量计算桶索引
        // 前置条件: capacity 必须是 2 的幂次且 >= 512
        FORCE_INLINE constexpr static unsigned get_bucket_index(unsigned capacity) noexcept {
            // std::countr_zero 计算末尾 0 的个数 (即 log2)
            // e.g. 512 -> 9, 9 - 9 = 0
            return std::countr_zero(capacity) - MIN_SHIFT;
        }

    public:
        [[nodiscard]] constexpr byte_borrow<Alloc> borrow(unsigned size) {
            return {this, acquire(size)};
        }

        [[nodiscard]] constexpr byte_buffer acquire(unsigned size) {
            if (size == 0) [[unlikely]] {
                return {};
            }

            // 1. 规范化大小：向上取整到 2 的幂次，且至少 512
            // bit_ceil(500) -> 512, bit_ceil(513) -> 1024
            const unsigned capacity = std::max(1u << MIN_SHIFT, std::bit_ceil(size));

            // 2. 检查是否超出池的管理范围 (16MB)
            // 如果超出，直接走系统分配，不进池
            if (capacity > (1u << (MIN_SHIFT + BUCKET_CNT - 1))) [[unlikely]] {
                 auto buf = alloc_sys(capacity);
                 buf.size_ = size;
                 return buf;
            }

            const unsigned idx = get_bucket_index(capacity);

            // 3. 尝试从桶中获取 (LIFO)
            auto& bucket = buckets_[idx];
            if (!bucket.empty()) {
                byte_buffer buf = bucket.back();
                bucket.pop_back(); // O(1)
                buf.size_ = size;  // 重置 size 为用户需求
                return buf;
            }

            // 4. 桶为空，分配新内存
            auto buf = alloc_sys(capacity);
            buf.size_ = size;
            return buf;
        }

        constexpr void retire(byte_buffer byte_array) noexcept {
            if (!byte_array) return;

            const unsigned cap = byte_array.capacity();

            // 1. 快速过滤：容量太小或不是 2 的幂次（可能是外部构造的异常块）
            if (cap < (1u << MIN_SHIFT) || std::popcount(cap) != 1) {
                dealloc_buf(byte_array);
                return;
            }

            const unsigned idx = get_bucket_index(cap);

            // 2. 过滤：超出池管理上限的块，直接释放
            if (idx >= BUCKET_CNT) {
                dealloc_buf(byte_array);
                return;
            }

            // 3. 归还到桶 (O(1))
            try {
                buckets_[idx].push_back(byte_array);
            } catch (...) {
                // 如果 vector 扩容失败（极罕见），直接释放内存保平安
                dealloc_buf(byte_array);
            }
        }

        constexpr byte_pool() = default;

        constexpr explicit byte_pool(const allocator_type& allocator)
            : allocator_(allocator) {
        }

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
            clear(); // 先清理自己的资源
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
            if(!allocated_.empty()){
                std::println(std::cerr, "Leak On Byte Pool Detected. Count: {}", allocated_.size());
                std::terminate();
            }
#endif
        }

        // 统计当前缓存的 Buffer 总数
        constexpr std::size_t cached_count() const noexcept {
            std::size_t total = 0;
            for(const auto& bucket : buckets_) {
                total += bucket.size();
            }
            return total;
        }

        // 清理缓存，直到保留的内存块数量不超过 keep_count
        // 策略：优先清理大内存桶
        constexpr void trim(std::size_t keep_count = 0) noexcept {
             if (keep_count == 0) {
                 clear();
                 return;
             }

             std::size_t current = cached_count();
             if (current <= keep_count) return;

             // 从最大的桶开始清理
             for (auto& bucket : std::views::reverse(buckets_)) {
                 while(!bucket.empty() && current > keep_count) {
                     dealloc_buf(bucket.back());
                     bucket.pop_back();
                     current--;
                 }
                 if (current <= keep_count) break;
             }
        }
    };

    template <typename Alloc>
    constexpr void byte_borrow<Alloc>::retire_() const noexcept {
        if (array_) {
            owner_->retire(array_);
        }
    }
}