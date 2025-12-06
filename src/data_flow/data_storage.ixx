module;

#include <cassert>
#define EMPTY_BUFFER_INIT /*[[indeterminate]]*/

export module mo_yanxi.react_flow.data_storage;

import std;

namespace mo_yanxi::react_flow{


export enum struct data_state : std::uint8_t{
	fresh,
	expired,
	awaiting,
	failed
};

export
template <typename T>
struct request_result{
    using value_type = T;

private:
    union {
       T value_;
    };

    enum internal_data_state_ : std::underlying_type_t<data_state>{
       fresh,
       expired,
       awaiting,
       failed,
       expired_with_data_
    } state_;

    // 辅助：检查内部状态是否持有数据
    constexpr bool has_internal_value_() const noexcept {
        return state_ == fresh || state_ == expired_with_data_;
    }

    template <bool hasValue>
    static constexpr internal_data_state_ convert_state_(data_state ds) noexcept{
       if constexpr (hasValue){
          // 有值时，状态不应该是 awaiting 或 failed
          assert(ds != data_state::awaiting && ds != data_state::failed);
          if(ds == data_state::expired){
             return expired_with_data_;
          }
       }else{
          // 无值时，状态不应该是 fresh
          assert(ds != data_state::fresh);
       }
       return internal_data_state_{std::to_underlying(ds)};
    }

    constexpr void try_destruct_() noexcept {
       if(has_internal_value_()){
          value_.~value_type();
       }
    }

public:
    // ========================================================================
    // 构造函数 (Constructors)
    // ========================================================================

    template <typename ...Args>
       requires (std::constructible_from<value_type, Args&&...>)
    [[nodiscard]] constexpr request_result(bool isExpired, Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args&&...>)
    : value_(std::forward<Args>(args)...), state_(isExpired ? expired_with_data_ : fresh){
    }

    [[nodiscard]] constexpr request_result(data_state result)
    : state_(convert_state_<false>(result)){
    }

    constexpr ~request_result(){
       try_destruct_();
    }

    // 1. 拷贝构造函数
    constexpr request_result(const request_result& other)
        requires (std::is_copy_constructible_v<T>)
        : state_(other.state_)
    {
        if (other.has_internal_value_()) {
            std::construct_at(&value_, other.value_);
        }
    }

    // 2. 移动构造函数
    constexpr request_result(request_result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires (std::is_move_constructible_v<T>)
        : state_(other.state_)
    {
        if (other.has_internal_value_()) {
            std::construct_at(&value_, std::move(other.value_));
        }
    }

    // ========================================================================
    // 赋值操作符 (Assignment Operators)
    // ========================================================================

    // 3. 拷贝赋值
    constexpr request_result& operator=(const request_result& other)
        requires (std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>)
    {
        if (this == &other) return *this;

        if (other.has_internal_value_()) {
            if (this->has_internal_value_()) {
                // Case 1: 都有值 -> 直接赋值
                this->value_ = other.value_;
                this->state_ = other.state_;
            } else {
                // Case 2: 他有值，我无值 -> 构造我
                std::construct_at(&this->value_, other.value_);
                this->state_ = other.state_;
            }
        } else {
            if (this->has_internal_value_()) {
                // Case 3: 他无值，我有值 -> 析构我
                this->try_destruct_();
            }
            // Case 4: 都无值 -> 仅拷贝状态
            this->state_ = other.state_;
        }
        return *this;
    }

    // 4. 移动赋值
    constexpr request_result& operator=(request_result&& other)
        noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T>)
        requires (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
    {
        if (this == &other) return *this;

        if (other.has_internal_value_()) {
            if (this->has_internal_value_()) {
                this->value_ = std::move(other.value_);
                this->state_ = other.state_;
            } else {
                std::construct_at(&this->value_, std::move(other.value_));
                this->state_ = other.state_;
            }
        } else {
            if (this->has_internal_value_()) {
                this->try_destruct_();
            }
            this->state_ = other.state_;
        }
        return *this;
    }

    // ========================================================================
    // 基础观察器 (Basic Observers)
    // ========================================================================

    constexpr bool has_value() const noexcept{
       // 对外暴露的 has_value 语义：只要不是 awaiting 或 failed 就算有值（包括 fresh 和 expired）
       return state_ == fresh || state_ == expired_with_data_;
    }

    constexpr data_state state() const noexcept {
        switch(state_) {
            case fresh: return data_state::fresh;
            case expired: return data_state::expired; // 实际上如果是 expired_with_data_ 也会进这里
            case awaiting: return data_state::awaiting;
            case failed: return data_state::failed;
            case expired_with_data_: return data_state::expired;
        }
        std::unreachable();
    }

    constexpr explicit operator bool() const noexcept{
       return has_value();
    }

    // C++23 Deducing This Accessors
    template <typename S>
    constexpr auto&& value(this S&& self){
       if(!self.has_value()){
          throw std::bad_optional_access{};
       }
       return std::forward_like<S>(self.value_);
    }

    template <typename S>
    constexpr auto&& value_unchecked(this S&& self) noexcept{
       assert(self.has_value());
       return std::forward_like<S>(self.value_);
    }

    // ========================================================================
    // 类似 Optional 的扩展函数 (Optional-like Extensions)
    // ========================================================================

    // value_or: 如果有值返回引用/拷贝，否则返回默认值
    template <typename S, typename U>
		requires (std::constructible_from<T, U&&>)
    constexpr T value_or(this S&& self, U&& default_value)
	    noexcept(
	    	std::is_nothrow_constructible_v<T, U&&> &&
	    	std::is_nothrow_constructible_v<T, decltype(std::forward_like<S>(self.value_))>)
	    requires (std::convertible_to<U, T> && std::copy_constructible<T>){
	    return self.has_value() ? std::forward_like<S>(self.value_) : static_cast<T>(std::forward<U>(default_value));
    }

    // emplace: 原地构造新值，并设置状态为 fresh
    template <typename... Args>
        requires (std::constructible_from<T, Args&&...>)
    constexpr T& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        try_destruct_();
        std::construct_at(&value_, std::forward<Args>(args)...);
        state_ = fresh;
        return value_;
    }

    // reset: 清除值并设置为指定状态 (默认为 failed)
    // 注意：不能 reset 为 fresh 或 expired_with_data，因为那需要值
    constexpr void reset(data_state new_state = data_state::failed) noexcept {
        assert(new_state != data_state::fresh); // 不能 reset 成需要值的状态而不给值
        try_destruct_();
        // 简单映射，因为这里确信没有值
        state_ = static_cast<internal_data_state_>(std::to_underlying(new_state));
    }

    // ========================================================================
    // 比较操作符 (Comparison Operators)
    // ========================================================================

    // 1. 与另一个 request_result 比较
    // 逻辑：状态必须完全一致。如果有值，值也必须一致。
    friend constexpr bool operator==(const request_result& lhs, const request_result& rhs) noexcept(noexcept(lhs.value_ == rhs.value_))
        requires (std::equality_comparable<T>)
    {
        if (lhs.state_ != rhs.state_) {
            return false;
        }
        // 状态相同。如果是有值状态，比较值。
        if (lhs.has_internal_value_()) {
            return lhs.value_ == rhs.value_;
        }
        // 都是无值状态且状态相同 (如都为 awaiting)
        return true;
    }

    friend constexpr bool operator==(const request_result& lhs, const T& rhs)  noexcept(noexcept(lhs.value_ == rhs))
        requires (std::equality_comparable<T>)
    {
        return lhs.has_value() && lhs.value_ == rhs;
    }

    friend constexpr bool operator==(const T& lhs, const request_result& rhs)  noexcept(noexcept(lhs == rhs.value_))
        requires (std::equality_comparable<T>)
    {
        return rhs.has_value() && lhs == rhs.value_;
    }

    friend constexpr bool operator==(const request_result& lhs, data_state rhs) noexcept  {
        return lhs.state() == rhs;
    }
};


export
template <typename T>
struct data_package{
	static_assert(std::is_object_v<T>);

private:
	union U{
		const T* ref_ptr{};
		T local;

		[[nodiscard]] constexpr U(){

		}

		[[nodiscard]] constexpr U(const T* r) noexcept : ref_ptr{r}{

		}

		constexpr ~U(){

		}
	};

	bool is_local{false};
	U u;

public:

	[[nodiscard]] constexpr T* get_mut() noexcept{
		if(!is_local)return nullptr;
		return std::addressof(u.local);
	}

	[[nodiscard]] constexpr const T* get() const noexcept{
		return is_local ? std::addressof(u.local) : u.ref_ptr;
	}


	[[nodiscard]] constexpr std::optional<T> fetch() const noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>){
		if(is_local){
			return std::optional<T>{std::move(u.local)};
		}else if(u.ref_ptr){
			return std::optional<T>{*u.ref_ptr};
		}
		return std::nullopt;
	}

	[[nodiscard]] constexpr std::optional<T> clone() const noexcept(std::is_nothrow_copy_constructible_v<T>){
		if(is_local){
			return std::optional<T>{u.local};
		}else if(u.ref_ptr){
			return std::optional<T>{*u.ref_ptr};
		}
		return std::nullopt;
	}

	[[nodiscard]] constexpr T fetch_unchecked() const noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>){
		if(is_local){
			return std::move(u.local);
		}else if(u.ref_ptr){
			return *u.ref_ptr;
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr T clone_unchecked() const noexcept(std::is_nothrow_copy_constructible_v<T>){
		if(is_local){
			return u.local;
		}else if(u.ref_ptr){
			return *u.ref_ptr;
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr bool empty() const noexcept{
		return !is_local && u.ref_ptr == nullptr;
	}

	constexpr explicit operator bool() const noexcept{
		return !empty();
	}

	[[nodiscard]] constexpr data_package() noexcept = default;

	[[nodiscard]] constexpr explicit data_package(const T& to_ref) noexcept : is_local(false), u{std::addressof(to_ref)}{

	}

	template <typename ...Args>
		requires (std::constructible_from<T, Args&&...>)
	[[nodiscard]] constexpr explicit data_package(std::in_place_t, Args&& ...args)
		noexcept(std::is_nothrow_constructible_v<T, Args&&...>)
	: is_local(true){
		std::construct_at(std::addressof(u.local), std::forward<Args>(args)...);
	}

	constexpr void reset() noexcept{
		if(is_local){
			std::destroy_at(std::addressof(u.local));
			is_local = false;
		}
		u.ref_ptr = nullptr;
	}

private:
	constexpr void reset_unchecked() noexcept{
		assert(is_local);
		std::destroy_at(std::addressof(u.local));
		is_local = false;
		u.ref_ptr = nullptr;
	}
public:

	constexpr ~data_package(){
		if(is_local){
			std::destroy_at(std::addressof(u.local));
		}
	}

	constexpr data_package(const data_package& other) noexcept(std::is_nothrow_copy_constructible_v<T>)
	: is_local{other.is_local}{
		if(is_local){
			std::construct_at(std::addressof(u.local), other.u.local);
		}else{
			u.ref_ptr = other.u.ref_ptr;
		}
	}

	constexpr data_package(data_package&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
	: is_local{other.is_local}{
		if(is_local){
			std::construct_at(std::addressof(u.local), std::move(other.u.local));
			other.reset_unchecked();
		}else{
			u.ref_ptr = other.u.ref_ptr;
		}
	}

	constexpr data_package& operator=(const data_package& other) noexcept(std::is_nothrow_copy_assignable_v<T>) {
		if(this == &other) return *this;

		if(is_local && other.is_local){
			u.local = other.u.local;
		}else if(other.is_local){
			std::construct_at(std::addressof(u.local), other.u.local);
			is_local = true;
		}else if(is_local){
			std::destroy_at(std::addressof(u.local));
			is_local = false;
			u.ref_ptr = other.u.ref_ptr;
		}else{
			u.ref_ptr = other.u.ref_ptr;
		}

		return *this;
	}

	constexpr data_package& operator=(data_package&& other) noexcept(std::is_nothrow_move_assignable_v<T>){
		if(this == &other) return *this;

		if(is_local && other.is_local){
			u.local = std::move(other.u.local);
			other.reset_unchecked();
		}else if(other.is_local){
			std::construct_at(std::addressof(u.local), std::move(other.u.local));
			is_local = true;
			other.reset_unchecked();
		}else if(is_local){
			std::destroy_at(std::addressof(u.local));
			is_local = false;
			u.ref_ptr = other.u.ref_ptr;
		}else{
			u.ref_ptr = other.u.ref_ptr;
		}

		return *this;
	}
};

export
template <typename T>
struct data_package_optimal{
	static constexpr bool is_small_object = (std::is_trivially_copyable_v<T> && sizeof(T) <= 16);
	using value_type = std::conditional_t<is_small_object, T, data_package<T>>;

private:
	value_type storage_;

public:
	[[nodiscard]] T* get_mut() noexcept{
		if constexpr (is_small_object){
			return std::addressof(storage_);
		}else{
			return storage_.get_mut();
		}
	}

	[[nodiscard]] const T* get() const noexcept{
		if constexpr (is_small_object){
			return std::addressof(storage_);
		}else{
			return storage_.get();
		}
	}


	[[nodiscard]] std::optional<T> fetch() const noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>){
		if constexpr (is_small_object){
			return std::optional<T>{std::move(storage_)};
		}else{
			return storage_.fetch();
		}
	}

	[[nodiscard]] std::optional<T> clone() const noexcept(std::is_nothrow_copy_constructible_v<T>){
		if constexpr (is_small_object){
			return std::optional<T>{storage_};
		}else{
			return storage_.clone();
		}
	}

	[[nodiscard]] T fetch_unchecked() const noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>){
		if constexpr (is_small_object){
			return std::move(storage_);
		}else{
			return storage_.fetch_unchecked();
		}
	}

	[[nodiscard]] T clone_unchecked() const noexcept(std::is_nothrow_copy_constructible_v<T>){
		if constexpr (is_small_object){
			return storage_;
		}else{
			return storage_.clone_unchecked();
		}
	}

	[[nodiscard]] bool empty() const noexcept{
		if constexpr (is_small_object){
			return false;
		}else{
			return storage_.empty();
		}
	}

	explicit operator bool() const noexcept{
		return !empty();
	}

	[[nodiscard]] data_package_optimal() = default;

	[[nodiscard]] explicit data_package_optimal(const T& to_ref) noexcept : storage_(to_ref){

	}

	template <typename ...Args>
		requires (std::constructible_from<T, Args&&...>)
	[[nodiscard]] explicit data_package_optimal(std::in_place_t, Args&& ...args)
		noexcept(std::is_nothrow_constructible_v<T, Args&&...>) requires (is_small_object)
	: storage_(std::forward<Args>(args)...){
	}

	template <typename ...Args>
		requires (std::constructible_from<T, Args&&...>)
	[[nodiscard]] explicit data_package_optimal(std::in_place_t, Args&& ...args)
		noexcept(std::is_nothrow_constructible_v<T, Args&&...>) requires (!is_small_object)
	: storage_(std::in_place, std::forward<Args>(args)...){
	}

};

//Legacy
template <typename T>
struct data_storage_view;

//Legacy
template <std::size_t Size = 32, std::size_t Align = alignof(std::max_align_t)>
struct data_storage{
	static constexpr std::size_t local_storage_size{Size};
	static constexpr std::size_t local_storage_align{std::max(Align, alignof(void*))};

	template <typename T>
	friend struct data_storage_view;
private:
	//State0: empty (all null)
	//State1: by_reference(no owning ship) (dctr_ptr == null, ref != null)
	//State2: owner/local
	//State3: owner/heap

	void(* destructor_func)(void*) noexcept = nullptr;
	void* reference_ptr{};

	alignas(local_storage_align) std::byte local_buf_[local_storage_size] EMPTY_BUFFER_INIT;

public:
	[[nodiscard]] bool empty() const noexcept{
		return destructor_func == nullptr && reference_ptr == nullptr;
	}

	explicit operator bool() const noexcept{
		return !empty();
	}

	[[nodiscard]] bool is_owner() const noexcept{
		return destructor_func != nullptr;
	}

	template <typename T>
	[[nodiscard]] T* get_mut() noexcept{
		if(!is_owner())return nullptr;
		return static_cast<T*>(reference_ptr);
	}

	template <typename T>
	[[nodiscard]] const T* get() const noexcept{
		if(empty())return nullptr;
		return static_cast<const T*>(reference_ptr);
	}

	[[nodiscard]] data_storage() = default;

	template <typename T>
	[[nodiscard]] explicit data_storage(const T& to_ref) :
		reference_ptr(const_cast<void*>(static_cast<const void*>(std::addressof(to_ref)))){}

	template <typename T, typename ...Args>
		requires (std::constructible_from<T, Args&&...>)
	[[nodiscard]] data_storage(std::in_place_type_t<T>, Args&& ...args) :
		destructor_func(+[](void* p) static noexcept{
			delete static_cast<T*>(p);
		}), reference_ptr(new T{std::forward<Args&&>(args) ...}){
	}

	template <typename T, typename ...Args>
		requires (sizeof(T) <= local_storage_size && alignof(T) <= local_storage_align && std::constructible_from<T, Args&&...>)
	[[nodiscard]] data_storage(std::in_place_type_t<T>, Args&& ...args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) :
		destructor_func(+[](void* p) static noexcept{
			std::destroy_at(static_cast<T*>(p));
		}), reference_ptr(local_buf_){
		std::construct_at(static_cast<T*>(reference_ptr), std::forward<Args&&>(args)...);
	}


	~data_storage(){
		if(is_owner()){
			destructor_func(reference_ptr);
		}
	}

	data_storage(const data_storage& other) = delete;
	data_storage(data_storage&& other) noexcept = delete;
	data_storage& operator=(const data_storage& other) = delete;
	data_storage& operator=(data_storage&& other) noexcept = delete;
};

template <typename T>
struct data_storage_view{
private:
	bool storage_is_owner_{};
	T* ptr{};

public:
	[[nodiscard]] data_storage_view() noexcept = default;

	template <std::size_t Size, std::size_t Align>
	[[nodiscard]] explicit(false) data_storage_view(data_storage<Size, Align>& storage) noexcept :
	storage_is_owner_{storage.is_owner()}, ptr{static_cast<T*>(storage.reference_ptr)}{}


	[[nodiscard]] T* get_mut() noexcept{
		if(!storage_is_owner_)return nullptr;
		return static_cast<T*>(ptr);
	}

	[[nodiscard]] const T* get() const noexcept{
		return static_cast<const T*>(ptr);
	}

	[[nodiscard]] std::optional<T> fetch() const noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>){
		if(ptr){
			if(storage_is_owner_){
				//TODO check double move?
				return std::optional<T>{std::move(*ptr)};
			}else{
				return std::optional<T>{*ptr};
			}
		}
		return std::nullopt;
	}
	
	[[nodiscard]] std::optional<T> clone() const noexcept(std::is_nothrow_copy_constructible_v<T>){
		if(ptr){
			return std::optional<T>{*ptr};
		}
		return std::nullopt;
	}
};

}