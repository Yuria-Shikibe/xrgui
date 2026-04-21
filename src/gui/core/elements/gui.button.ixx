export module mo_yanxi.gui.elem.button;

export import mo_yanxi.gui.infrastructure;
import std;

//vibe shits, because msvc has bug when using std function wrapper

namespace detail {
    template <std::size_t sso_size>
    union any_storage {
        alignas(std::max_align_t) std::byte sso_data[sso_size > 0 ? sso_size : 1];
        void* heap_data;
    };

    template <typename t, std::size_t sso_size>
    inline constexpr bool use_sso =
        (sizeof(t) <= sso_size) &&
        (alignof(std::max_align_t) % alignof(t) == 0) &&
        std::is_nothrow_destructible_v<t>;
}

template <std::size_t sso_size, typename signature>
class fixed_function;

#define MAKE_FIXED_FUNCTION(const_qual, noexcept_qual) \
template <std::size_t sso_size, typename r, typename... args> \
class fixed_function<sso_size, r(args...) const_qual noexcept_qual> { \
    using storage_t = detail::any_storage<sso_size>; \
    \
    struct vtable_t { \
        void (*destroy)(storage_t*) noexcept; \
        r (*invoke)(const_qual storage_t*, args...) noexcept_qual; \
    }; \
    \
    storage_t storage_; \
    const vtable_t* vptr_ = nullptr; \
    \
    template <typename t> \
    static constexpr vtable_t vtable_for = { \
        [](storage_t* s) noexcept { \
            if constexpr (detail::use_sso<t, sso_size>) { \
                /* [修复] 使用 std::destroy_at 替代 ->~t() */ \
                std::destroy_at(static_cast<t*>(static_cast<void*>(s->sso_data))); \
            } else { \
                delete static_cast<t*>(s->heap_data); \
            } \
        }, \
        [](const_qual storage_t* s, args... a) noexcept_qual -> r { \
            if constexpr (detail::use_sso<t, sso_size>) { \
                return (*static_cast<const_qual t*>(static_cast<const_qual void*>(s->sso_data)))(std::forward<args>(a)...); \
            } else { \
                return (*static_cast<const_qual t*>(s->heap_data))(std::forward<args>(a)...); \
            } \
        } \
    }; \
    \
    /* 辅助函数：统一处理分配与构造 */ \
    template <typename f> \
    void construct_from(f&& functor) { \
        using decayed = std::remove_cvref_t<f>; \
        if constexpr (detail::use_sso<decayed, sso_size>) { \
            new (storage_.sso_data) decayed(std::forward<f>(functor)); \
        } else { \
            storage_.heap_data = new decayed(std::forward<f>(functor)); \
        } \
        vptr_ = &vtable_for<decayed>; \
    } \
    \
    /* 辅助函数：统一处理析构与清理 */ \
    void destroy_current() noexcept { \
        if (vptr_) { \
            vptr_->destroy(&storage_); \
            vptr_ = nullptr; \
        } \
    } \
    \
public: \
    fixed_function() noexcept = default; \
    \
    ~fixed_function() { \
        destroy_current(); \
    } \
    \
    /* 万能引用构造：使用 C++20 requires 约束替换 enable_if */ \
    /* 并使用 C++20 std::remove_cvref_t 简化类型推导 */ \
    template <typename f> \
    requires (!std::same_as<std::remove_cvref_t<f>, fixed_function>) \
    fixed_function(f&& functor) { \
        construct_from(std::forward<f>(functor)); \
    } \
    \
    /* 新增需求：支持从任意符合条件的 functor 进行赋值 */ \
    template <typename f> \
    requires (!std::same_as<std::remove_cvref_t<f>, fixed_function>) \
    fixed_function& operator=(f&& functor) { \
        destroy_current(); /* 先清理当前可能存在的堆内存或就地对象 */ \
        construct_from(std::forward<f>(functor)); \
        return *this; \
    } \
    \
    fixed_function(const fixed_function&) = delete; \
    fixed_function& operator=(const fixed_function&) = delete; \
    fixed_function(fixed_function&&) = delete; \
    fixed_function& operator=(fixed_function&&) = delete; \
    \
    r operator()(args... a) const_qual noexcept_qual { \
        if (!vptr_) throw std::bad_function_call(); \
        return vptr_->invoke(&storage_, std::forward<args>(a)...); \
    } \
    \
    explicit operator bool() const noexcept { \
        return vptr_ != nullptr; \
    } \
    \
    /* 顺手提供一个清空接口，对这类包装器通常很有用 */ \
    void reset() noexcept { \
        destroy_current(); \
    } \
};

MAKE_FIXED_FUNCTION(, )
MAKE_FIXED_FUNCTION(const, )
MAKE_FIXED_FUNCTION(, noexcept)
MAKE_FIXED_FUNCTION(const, noexcept)

namespace mo_yanxi::gui{
export
template <std::derived_from<elem> T = elem>
struct button : public T{
	using base_type = T;

protected:
	using callback_type = fixed_function<32, void(events::click, button<T>&)>;

	callback_type callback{};

	void add_button_prop(){
		elem::interactivity = interactivity_flag::enabled;
		elem::extend_focus_until_mouse_drop = true;
	}

public:
	template <typename... Args>
		requires (std::constructible_from<base_type, scene&, elem*, Args&&...>)
	[[nodiscard]] button(scene& scene, elem* parent, Args&&... args)
		: base_type(scene, parent, std::forward<Args>(args)...){
		add_button_prop();
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		base_type::on_click(event, aboves);
		if(this->is_disabled()) return events::op_afterwards::intercepted;
		if(callback && event.within_elem(*this)) callback(event, *this);
		return events::op_afterwards::intercepted;
	}

	void set_button_callback(callback_type&& func){
		callback = std::move(func);
	}

	template <std::invocable<> Func>
	void set_button_callback(Func&& fn){
		callback = [func = std::forward<Func>(fn)](events::click e, button&){
			if(e.key.on_release()){
				std::invoke(func);
			}
		};
	}

	template <std::invocable<button&> Func>
	void set_button_callback(Func&& fn){
		callback = [func = std::forward<Func>(fn)](events::click e, button& b){
			if(e.key.on_release()){
				std::invoke(func, b);
			}
		};
	}
};
}
