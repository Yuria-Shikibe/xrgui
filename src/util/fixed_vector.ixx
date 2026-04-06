module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.fixed_vector;

import std;

namespace mo_yanxi{
export
template <typename T, typename Allocator = std::allocator<T>>
class fixed_vector{
public:
	// 标准容器的类型别名定义
	using value_type = T;
	using allocator_type = Allocator;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = value_type&;
	using const_reference = const value_type&;
	using pointer = typename std::allocator_traits<Allocator>::pointer;
	using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;
	using iterator = pointer;
	using const_iterator = const_pointer;

private:
	pointer data_{};
	size_type size_{};
	// 使用 C++20 属性优化空分配器的空间占用
	ADAPTED_NO_UNIQUE_ADDRESS allocator_type alloc_{};

	// 私有核心初始化函数
	template <typename... Args>
	constexpr void initialize(Args&... args){
		if(size_ == 0) return;

		// 1. 使用分配器分配未初始化的内存
		data_ = std::allocator_traits<allocator_type>::allocate(alloc_, size_);
		size_type constructed = 0;

		// 2. 强异常保证的 RAII 护卫 (Exception Guard)
		struct exception_guard{
			pointer d;
			size_type& c;
			allocator_type& a;
			size_type s;

			constexpr ~exception_guard(){
				if(d != nullptr){
					// 如果中途抛出异常，销毁已构造的 0 到 c-1 个元素
					std::destroy(std::to_address(d), std::to_address(d + c));
					// 并释放整块内存
					std::allocator_traits<allocator_type>::deallocate(a, d, s);
				}
			}
		} guard{data_, constructed, alloc_, size_};

		// 3. 在预分配的内存上使用 construct_at 构造元素
		for(; constructed < size_; ++constructed){
			// 注意：此处不使用 std::forward，以防止如果 args 是右值时被多次移动
			std::construct_at(std::to_address(data_ + constructed), args...);
		}

		// 4. 构造全部成功，解除护卫的清理职责
		guard.d = nullptr;
	}

public:
	[[nodiscard]] fixed_vector() = default;

	// 默认分配器的多参数构造
	template <typename... Args>
		requires (std::constructible_from<T, Args&...>)
	constexpr explicit fixed_vector(size_type count, Args&&... args)
		: size_(count), alloc_(Allocator()){
		this->initialize(args...);
	}

	// 支持显式传入自定义分配器的构造
	template <typename... Args>
		requires (std::constructible_from<T, Args&...>)
	constexpr fixed_vector(std::allocator_arg_t, const allocator_type& alloc, size_type count, Args&&... args)
		: size_(count), alloc_(alloc){
		this->initialize(args...);
	}

	// 拷贝构造函数 (仅当 T 支持拷贝时启用)
	constexpr fixed_vector(const fixed_vector& other) requires std::is_copy_constructible_v<T>
		: size_(other.size_),
		  alloc_(std::allocator_traits<allocator_type>::select_on_container_copy_construction(other.alloc_)){
		if(size_ == 0) return;
		data_ = std::allocator_traits<allocator_type>::allocate(alloc_, size_);
		size_type constructed = 0;

		struct exception_guard{
			pointer d;
			size_type& c;
			allocator_type& a;
			size_type s;

			constexpr ~exception_guard(){
				if(d){
					std::destroy(std::to_address(d), std::to_address(d + c));
					std::allocator_traits<allocator_type>::deallocate(a, d, s);
				}
			}
		} guard{data_, constructed, alloc_, size_};

		for(; constructed < size_; ++constructed){
			std::construct_at(std::to_address(data_ + constructed), other.data_[constructed]);
		}
		guard.d = nullptr;
	}

	// 移动构造函数
	constexpr fixed_vector(fixed_vector&& other) noexcept
		: data_(std::exchange(other.data_, {})), size_(std::exchange(other.size_, {})), alloc_(std::move(other.alloc_)){
	}

	// 显式禁用赋值操作：严格保障“仅能在构造时设定长度”
	fixed_vector& operator=(const fixed_vector&) = delete;
	fixed_vector& operator=(fixed_vector&& other) noexcept{
		if(&other == this){return *this;}
		if(data_){
			std::destroy(std::to_address(data_), std::to_address(data_ + size_));
			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, size_);
		}
		data_ = std::exchange(other.data_, {});
		size_ = std::exchange(other.size_, 0);
		alloc_ = std::move(other.alloc_);
		return *this;
	}

	// 析构函数
	constexpr ~fixed_vector(){
		if(data_){
			std::destroy(std::to_address(data_), std::to_address(data_ + size_));
			std::allocator_traits<allocator_type>::deallocate(alloc_, data_, size_);
		}
	}

	// --- 访问接口 ---

	constexpr reference operator[](size_type pos) noexcept{ return data_[pos]; }
	constexpr const_reference operator[](size_type pos) const noexcept{ return data_[pos]; }

	constexpr reference at(size_type pos){
		if(pos >= size_) throw std::out_of_range("fixed_vector::at index out of range");
		return data_[pos];
	}

	constexpr const_reference at(size_type pos) const{
		if(pos >= size_) throw std::out_of_range("fixed_vector::at index out of range");
		return data_[pos];
	}

	constexpr iterator begin() noexcept{ return data_; }
	constexpr const_iterator begin() const noexcept{ return data_; }

	constexpr iterator end() noexcept{ return data_ + size_; }
	constexpr const_iterator end() const noexcept{ return data_ + size_; }

	constexpr size_type size() const noexcept{ return size_; }
	constexpr bool empty() const noexcept{ return size_ == 0; }
};
}
