module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:tooltip_interface;

import :elem_ptr;

export import mo_yanxi.math.vector2;
export import mo_yanxi.gui.layout.policies;
export import align;
export import mo_yanxi.handle_wrapper;

namespace mo_yanxi::gui::tooltip{

export struct tooltip_manager;

export
enum struct anchor_type{
	initial_pos,
	cursor,
	owner,
};

export
struct align_config{
	anchor_type follow{};
	align::pos align{};
	std::optional<math::vec2> pos{};
	layout::optional_mastering_extent extent{layout::extent_by_external};
};

export
struct align_meta{
	anchor_type follow{anchor_type::initial_pos};

	align::pos attach_point_spawner{align::pos::bottom_left};
	align::pos attach_point_tooltip{align::pos::top_left};

	math::vec2 offset{};
};

export
struct create_config{
	static constexpr float disable_auto_tooltip = std::numeric_limits<float>::infinity();
	static constexpr float def_tooltip_hover_time = 25.0f;

	align_meta layout_info{};

	bool use_stagnate_time{false};
	bool auto_release{true};
	float min_hover_time{def_tooltip_hover_time};

	[[nodiscard]] bool auto_build() const noexcept{
		return std::isfinite(min_hover_time);
	}
};

export
struct spawner{
	friend tooltip_manager;
protected:
	exclusive_handle_member<elem*> tooltip_handle{};

	~spawner() = default;
	[[nodiscard]] spawner() = default;

public:

	/**
	 * @brief
	 * @return the align reference point in screen space
	 */
	[[nodiscard]] virtual align_config tooltip_get_align_config() const = 0;

	[[nodiscard]] virtual bool tooltip_should_maintain(math::vec2 cursorPos) const{
		return false;
	}

	[[nodiscard]] virtual bool tooltip_spawner_contains(math::vec2 cursorPos) const noexcept = 0;

	[[nodiscard]] virtual bool tooltip_should_build(math::vec2 cursorPos) const noexcept = 0;

	[[nodiscard]] virtual bool tooltip_should_drop(math::vec2 cursorPos) const noexcept{
		return false;
	}

	[[nodiscard]] bool has_tooltip() const noexcept{
		return tooltip_handle != nullptr;
	}

	elem_ptr tooltip_setup(){
		assert(this->tooltip_handle == nullptr);
		//TODO deal the previous tooltip here?
		auto ptr = tooltip_build_impl();
		this->tooltip_handle = ptr.get();
		return ptr;
	}

	void tooltip_notify_drop(){
		tooltip_handle = nullptr;
		tooltip_on_drop_behavior_impl();
		//TODO Call scene drop
	}

	void tooltip_drop();

protected:
	virtual void tooltip_on_drop_behavior_impl(){
	}

	[[nodiscard]] virtual scene& tooltip_get_scene() const noexcept = 0;

	[[nodiscard]] virtual elem_ptr tooltip_build_impl() = 0;
};


export
template<typename T>
struct spawner_func_wrapped : public spawner{
protected:
	~spawner_func_wrapped() = default;

private:
	using builder_type = std::move_only_function<elem_ptr(T&, scene&)>;
	builder_type toolTipBuilder_{};

public:
	[[nodiscard]] spawner_func_wrapped() = default;

	template<elem_init_func InitFunc, std::derived_from<T> S>
		requires std::invocable<InitFunc, S&, elem_init_func_create_t<InitFunc>&>
	decltype(auto) set_tooltip_builder(this S& self, InitFunc&& initFunc){
		return std::exchange(self.toolTipBuilder_, builder_type{[func = std::forward<InitFunc>(initFunc)](T& owner, scene& scene){
			return elem_ptr{
				scene,
				nullptr,
				[&](elem_init_func_create_t<InitFunc>& e){
					std::invoke(func, static_cast<S&>(owner), e);
			}};
		}});
	}

	template<elem_init_func InitFunc>
		requires std::invocable<InitFunc, elem_init_func_create_t<InitFunc>&>
	decltype(auto) set_tooltip_builder(InitFunc&& initFunc){
		return std::exchange(toolTipBuilder_, decltype(toolTipBuilder_){[func = std::forward<InitFunc>(initFunc)](T& owner, scene& scene){
			return elem_ptr{scene, nullptr, func};
		}});
	}

	bool has_tooltip_builder() const noexcept{
		return static_cast<bool>(toolTipBuilder_);
	}

protected:
	elem_ptr tooltip_build_impl() override{
		assert(has_tooltip_builder());
		return std::invoke(toolTipBuilder_, static_cast<T&>(*this), tooltip_get_scene());
	}
};

export
template<typename T>
struct spawner_general : public spawner_func_wrapped<T>{
protected:
	~spawner_general() = default;

public:
	create_config tooltip_create_config{};

	template<elem_init_func InitFunc, std::derived_from<T> S>
	void set_tooltip_state(this S& self, const create_config& toolTipProperty, InitFunc&& initFunc) noexcept{
		self.tooltip_create_config = toolTipProperty;
		self.set_tooltip_builder(std::forward<InitFunc>(initFunc));
	}

};
}