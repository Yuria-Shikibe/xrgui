module;

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <mimalloc.h>

export module mo_yanxi.gui.alloc;
import std;

namespace mo_yanxi::gui::mr{

export
class raw_memory_pool {
public:

    explicit raw_memory_pool(const std::size_t size) : size_(size){
        if (size > 0) {
            ptr_ = allocate(size);
            if (!ptr_) {
                throw std::bad_alloc();
            }

            bool success = mi_manage_os_memory_ex(ptr_, size, true, false, true, -1, true, &arena_id_);
            if (!success){

                deallocate(ptr_, size);
                ptr_ = nullptr;
                throw std::bad_alloc();
            }

        }
    }

    raw_memory_pool() noexcept = default;

    [[nodiscard]] void* get() const noexcept { return ptr_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] mi_arena_id_t get_arena_id() const noexcept { return arena_id_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    void* ptr_{};
    std::size_t size_{};
    mi_arena_id_t arena_id_{};

    static void* allocate(std::size_t size) {
    #ifdef _WIN32
        return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    #else
        return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    #endif
    }

    static void deallocate(void* ptr, std::size_t size) noexcept {
    #ifdef _WIN32
        VirtualFree(ptr, 0, MEM_RELEASE);
        (void)size;
    #else
        munmap(ptr, size);
    #endif
    }
};

export
struct heap{
private:
    mi_heap_t* heap_;

public:

    [[nodiscard]] heap() = default;

    [[nodiscard]] explicit(false) heap(std::in_place_t)
        : heap_(::mi_heap_new()){
    }

    [[nodiscard]] explicit(false) heap(mi_heap_t* heap)
        : heap_(heap){
    }

    [[nodiscard]] explicit heap(mi_arena_id_t arena_id, int /*tag*/ = 0) : heap_(mi_heap_new_in_arena(arena_id)){
        if(!heap_){
            throw std::bad_alloc();
        }
    }

    ~heap(){
        if(heap_){
            mi_heap_destroy(heap_);
        }
    }

    heap(const heap& other) = delete;

    heap(heap&& other) noexcept
        : heap_{std::exchange(other.heap_, {})}{
    }

    heap& operator=(const heap& other) = delete;

    heap& operator=(heap&& other) noexcept{
        if(this == &other) return *this;
        if(heap_) mi_heap_destroy(heap_);

        heap_ = std::exchange(other.heap_, {});
        return *this;
    }

    explicit operator bool() const noexcept{
        return heap_ != nullptr;
    }

    [[nodiscard]] mi_heap_t* get() const noexcept{
        return heap_;
    }

    [[nodiscard]] mi_heap_t* release() noexcept{
        return std::exchange(heap_, {});
    }
};

export
template <typename T = std::byte>
struct heap_allocator : mi_heap_stl_allocator<T>{
    using mi_heap_stl_allocator<T>::mi_heap_stl_allocator;

    template<class U> struct rebind { typedef heap_allocator<U> other; };
    auto select_on_container_copy_construction() const { return *this; }


    template<class T1, class T2>
    friend bool operator==(const heap_allocator<T1>& lhs, const heap_allocator<T2>& rhs) noexcept{
        return static_cast<const mi_heap_stl_allocator<T1>&>(lhs) == static_cast<const mi_heap_stl_allocator<T2>&>(rhs);
    }

};

export
template <typename T = std::byte>
heap_allocator<T> get_default_heap_allocator(){
    return heap_allocator<T>{mi_heap_main()};
}

export
template <typename T>
struct unvs_allocator : mi_stl_allocator<T>{
    using mi_stl_allocator<T>::mi_stl_allocator;
    template <class U> struct rebind { typedef unvs_allocator<U> other; };
    auto select_on_container_copy_construction() const { return *this; }

    template<class T1, class T2>
    friend bool operator==(const unvs_allocator<T1>& lhs, const unvs_allocator<T2>& rhs) noexcept{
        return static_cast<const mi_stl_allocator<T1>&>(lhs) == static_cast<const mi_stl_allocator<T2>&>(rhs);
    }
};

export
template <typename T, std::size_t align>
    requires (std::has_single_bit(align))
struct aligned_heap_allocator : heap_allocator<T>{
    using typename heap_allocator<T>::size_type;
    using heap_allocator<T>::heap_allocator;

    template<class U>
    [[nodiscard]] explicit(false) aligned_heap_allocator(const heap_allocator<U>& hp) noexcept
    : heap_allocator<T>(hp){
    }

    template<class U, std::size_t align_2> aligned_heap_allocator(const aligned_heap_allocator<U, align_2>& x) noexcept : heap_allocator<T>(x){

    }

    mi_decl_nodiscard T* allocate(size_type count){
        return static_cast<T*>(::mi_heap_malloc_aligned(this->heap.get(), count * sizeof(T), align));
    }

    mi_decl_nodiscard T* allocate(size_type count, const void*){
        return this->allocate(count);
    }

    aligned_heap_allocator select_on_container_copy_construction() const { return *this; }
    void deallocate(T* p, size_type size){
        ::mi_free_size_aligned(p, size * sizeof(T), align);
    }

    template<class U> struct rebind{
        typedef aligned_heap_allocator<U, align> other;
    };

    friend bool operator==(const aligned_heap_allocator& lhs, const aligned_heap_allocator& rhs) noexcept = default;
};

export
template<class T1, class T2, std::size_t align1, std::size_t align2>
bool operator==(const aligned_heap_allocator<T1, align1>& x, const aligned_heap_allocator<T2, align2>& y) mi_attr_noexcept {
    if constexpr (align1 != align2){
        return false;
    }
    return (x.is_equal(y));
}

export
raw_memory_pool make_memory_pool(std::size_t MB){
    return raw_memory_pool{MB * 1024 * 1024};
}

export
template <typename T>
using heap_vector = std::vector<T, heap_allocator<T>>;


export
template <typename T>
using vector = std::vector<T, unvs_allocator<T>>;


export
template <typename T, class Hasher = std::hash<T>, class Keyeq = std::equal_to<T>>
using heap_uset = std::unordered_set<T, Hasher, Keyeq, heap_allocator<T>>;

export
template <typename K, typename V, class Hasher = std::hash<K>, class Keyeq = std::equal_to<K>>
using heap_umap = std::unordered_map<K, V, Hasher, Keyeq, heap_allocator<std::pair<const K, V>>>;

export
using heap_string = std::basic_string<char, std::char_traits<char>, heap_allocator<char>>;

export
using string = std::basic_string<char, std::char_traits<char>, unvs_allocator<char>>;

export
template <typename T>
using vector = std::vector<T, unvs_allocator<T>>;

export using arena_id_t = mi_arena_id_t;

export using heap_handle = mi_heap_t*;

}