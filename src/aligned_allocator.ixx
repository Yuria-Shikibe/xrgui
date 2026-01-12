
export module mo_yanxi.aligned_allocator;

import std;

namespace mo_yanxi{

	export
	template <typename T, std::size_t Alignment = alignof(T)>
	class aligned_allocator {
	public:
		using value_type = T;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::true_type;

		static_assert(std::has_single_bit(Alignment), "Alignment must be a power of 2");
		static_assert(Alignment >= alignof(T), "Alignment must be at least alignof(T)");

		constexpr aligned_allocator() noexcept = default;
		constexpr aligned_allocator(const aligned_allocator&) noexcept = default;

		template <typename U>
		constexpr aligned_allocator(const aligned_allocator<U, Alignment>&) noexcept {}

		template <typename U>
		struct rebind {
			using other = aligned_allocator<U, Alignment>;
		};

		[[nodiscard]] constexpr inline T* allocate(std::size_t n) const{
			if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
				throw std::bad_array_new_length();
			}

			const std::size_t bytes = n * sizeof(T);
			void* p = ::operator new(bytes, std::align_val_t{Alignment});
			return static_cast<T*>(p);
		}

		constexpr inline void deallocate(T* p, std::size_t n) const noexcept {
			::operator delete(p, static_cast<std::align_val_t>(Alignment));
		}

		constexpr friend bool operator==(const aligned_allocator&, const aligned_allocator&) { return true; }
	};
}