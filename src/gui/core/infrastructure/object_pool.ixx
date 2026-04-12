module;

#include <mo_yanxi/adapted_attributes.hpp>
#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <gtl/phmap.hpp>
#endif

export module mo_yanxi.gui.infrastructure:object_pool;

import std;
import mo_yanxi.type_register;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <gtl/phmap.hpp>;
#endif

namespace mo_yanxi::gui{
template <typename T, bool destroyOnRelease = true, typename Allocator = std::allocator<T>>
class object_pool{
public:
	struct node{
		std::atomic<node*> next{nullptr};

		union{
			T value;
			std::byte dummy;
		};

		node() noexcept : next(nullptr), dummy{}{
		}

		~node() noexcept{
		}

		T* get() noexcept{
			return std::addressof(value);
		}
	};

	class pool_ptr{
	private:
		node* node_{nullptr};
		object_pool* pool_{nullptr};

		friend class object_pool;

		pool_ptr(node* n, object_pool* p) noexcept : node_(n), pool_(p){
		}

	public:
		pool_ptr() = default;

		~pool_ptr(){
			if(node_ && pool_){
				pool_->release_node(node_);
			}
		}

		pool_ptr(const pool_ptr&) = delete;
		pool_ptr& operator=(const pool_ptr&) = delete;

		pool_ptr(pool_ptr&& other) noexcept
			: node_(std::exchange(other.node_, nullptr)),
			  pool_(std::exchange(other.pool_, nullptr)){
		}

		pool_ptr& operator=(pool_ptr&& other) noexcept{
			if(this != &other){
				if(node_ && pool_){
					pool_->release_node(node_);
				}
				node_ = std::exchange(other.node_, nullptr);
				pool_ = std::exchange(other.pool_, nullptr);
			}
			return *this;
		}

		T* get() const noexcept{ return node_ ? node_->get() : nullptr; }
		T& operator*() const noexcept{ return *get(); }
		T* operator->() const noexcept{ return get(); }
		explicit operator bool() const noexcept{ return node_ != nullptr; }
	};

private:
	struct alignas(2 * sizeof(void*)) tagged_ptr{
		node* ptr{nullptr};
		std::uint64_t tag{0};
	};

	using node_allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<node>;
	using node_allocator_traits = typename std::allocator_traits<node_allocator_type>;

	std::atomic<tagged_ptr> head_{};
	ADAPTED_NO_UNIQUE_ADDRESS node_allocator_type allocator_;

public:
	object_pool() = default;

	explicit object_pool(const Allocator& alloc) : allocator_(alloc){
	}

	~object_pool(){
		tagged_ptr current = head_.load(std::memory_order_relaxed);
		while(current.ptr != nullptr){
			node* next_node = current.ptr->next.load(std::memory_order_relaxed);

			if constexpr(!destroyOnRelease){
				std::destroy_at(std::addressof(current.ptr->value));
			}

			std::destroy_at(current.ptr);
			node_allocator_traits::deallocate(allocator_, current.ptr, 1);
			current.ptr = next_node;
		}
	}

	object_pool(const object_pool&) = delete;
	object_pool& operator=(const object_pool&) = delete;

	template <typename... Args>
		requires std::constructible_from<T, Args...>
	pool_ptr acquire(Args&&... args){
		node* n = pop_node();
		const bool is_new = (n == nullptr);

		if(is_new){
			n = node_allocator_traits::allocate(allocator_, 1);
			std::construct_at(n);
		}

		try{
			if constexpr(destroyOnRelease){
				std::construct_at(std::addressof(n->value), std::forward<Args>(args)...);
			} else{
				if(is_new){
					std::construct_at(std::addressof(n->value), std::forward<Args>(args)...);
				}
			}
		} catch(...){
			this->push_raw_node(n);
			throw;
		}

		return pool_ptr(n, this);
	}

private:
	node* pop_node() noexcept{
		tagged_ptr old_head = head_.load(std::memory_order_acquire);
		while(old_head.ptr != nullptr){
			node* next_node = old_head.ptr->next.load(std::memory_order_acquire);
			tagged_ptr new_head{next_node, old_head.tag + 1};

			if(head_.compare_exchange_weak(old_head, new_head,
			                               std::memory_order_release,
			                               std::memory_order_acquire)){
				return old_head.ptr;
			}
		}
		return nullptr;
	}

	void release_node(node* n) noexcept{
		if constexpr(destroyOnRelease){
			std::destroy_at(std::addressof(n->value));
		}
		this->push_raw_node(n);
	}

	void push_raw_node(node* n) noexcept{
		tagged_ptr old_head = head_.load(std::memory_order_relaxed);
		tagged_ptr new_head;
		new_head.ptr = n;

		do{
			n->next.store(old_head.ptr, std::memory_order_relaxed);
			new_head.tag = old_head.tag + 1;
		} while(!head_.compare_exchange_weak(old_head, new_head,
		                                     std::memory_order_release,
		                                     std::memory_order_relaxed));
	}
};

template <bool destroyOnRelease = true, typename Alloc = std::allocator<std::byte>>
struct any_pool{
	template <typename T>
	using pool = object_pool<T, destroyOnRelease, typename std::allocator_traits<Alloc>::template rebind_alloc<T>>;

	struct object_pool_wrapper{
		alignas(pool<void*>) std::byte storage[sizeof(pool<void*>)] ADAPTED_INDETERMINATE;
		void (*destructor_)(void*) noexcept;

		template <typename T, typename... Args>
		[[nodiscard]] object_pool_wrapper(std::in_place_type_t<T>, Args&&... args) : destructor_{
				+[](void* p) static noexcept{
					std::destroy_at(static_cast<pool<T>*>(p));
				}
			}{
			static_assert(sizeof(pool<T>) <= sizeof(storage), "T must be same size");
			static_assert(alignof(pool<T>) <= alignof(pool<void*>), "T must be same size");
			std::construct_at(reinterpret_cast<pool<T>*>(&storage), std::forward<Args>(args)...);
		}

		~object_pool_wrapper(){
			if(destructor_){
				destructor_(storage);
			}
		}

		template <typename T>
		explicit operator pool<T>&() noexcept{
			return *reinterpret_cast<pool<T>*>(storage);
		}
	};

private:

	using map_allocator_type = typename std::allocator_traits<Alloc>::template rebind_alloc<
		std::pair<const mo_yanxi::type_identity_index, object_pool_wrapper>
	>;

	using map_type = gtl::parallel_node_hash_map<
		mo_yanxi::type_identity_index,
		object_pool_wrapper,
		std::hash<mo_yanxi::type_identity_index>,
		std::equal_to<mo_yanxi::type_identity_index>,
		map_allocator_type
	>;

	map_type pool_;

public:

	any_pool() = default;


	explicit any_pool(const Alloc& alloc)
		: pool_(0, std::hash<mo_yanxi::type_identity_index>{},
		        std::equal_to<mo_yanxi::type_identity_index>{},
		        map_allocator_type(alloc)){
	}

	template <typename T>
	auto& acquire_pool(){

		using pool_allocator_type = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
		pool_allocator_type pool_alloc{pool_.get_allocator()};

		return static_cast<pool<T>&>(
			pool_.try_emplace(
				mo_yanxi::unstable_type_identity_of<T>(),
				std::in_place_type<T>,
				std::move(pool_alloc)
			).first->second
		);
	}

	template <typename T, typename... Args>
		requires (std::constructible_from<T, Args&&...>)
	auto acquire(Args&& ...args){

		return acquire_pool<T>().acquire(std::forward<Args>(args)...);
	}
};

}
