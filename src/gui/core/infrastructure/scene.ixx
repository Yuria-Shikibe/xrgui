module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:scene;

import :elem_ptr;
import :tooltip_manager;
import :dialog_manager;
import :cursor;
import std;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.handle_wrapper;
import mo_yanxi.math.rect_ortho;

import mo_yanxi.gui.util;
export import mo_yanxi.input_handle;
export import mo_yanxi.gui.alloc;

export import mo_yanxi.react_flow;
import mo_yanxi.allocator_aware_unique_ptr;


namespace mo_yanxi::gui{

export
struct native_communicator{
	virtual ~native_communicator() = default;

	virtual void send_clipboard(std::string_view text){

	}

	virtual std::string_view get_clipboard(){
		return {};
	}
};

struct mouse_state{
	math::vec2 src{};
	bool pressed{};

	void reset(const math::vec2 pos) noexcept{
		src = pos;
		pressed = true;
	}

	void clear(const math::vec2 pos) noexcept{
		src = pos;
		pressed = false;
	}
};

export struct ui_manager;

export struct elem;

export
struct scene;

struct scene_base{
	friend elem;
	friend ui_manager;

private:
	mr::heap resource_{};
	renderer_frontend renderer_{};

	[[nodiscard]] mr::heap_handle get_heap() const noexcept{
		return resource_.get();
	}


protected:
	rect region_{};

	std::array<mouse_state, std::to_underlying(input_handle::mouse::Count)> mouse_states_{};
	input_handle::input_manager<scene&> inputs_{};

	elem* focus_scroll_{nullptr};
	elem* focus_cursor_{nullptr};
	elem* focus_key_{nullptr};

	/**
	 * @brief Request to update cursor pos, even it never moves
	 * Used for nested scene or scroll panes and other elements that change the element position
	 */
	bool request_cursor_update_{};

	//TODO double swap buffer?
	mr::heap_vector<elem*> last_inbounds_{mr::heap_allocator<elem*>{get_heap()}};
	mr::heap_uset<elem*> independent_layouts_{mr::heap_allocator<elem*>{get_heap()}};

	allocator_aware_unique_ptr<react_flow::manager, mr::heap_allocator<react_flow::manager>> react_flow_{
		mo_yanxi::make_allocate_aware_unique<react_flow::manager>(mr::heap_allocator<react_flow::manager>{get_heap()})
	};

	std::unordered_multimap<
		const elem*, react_flow::node*,
		std::hash<const elem*>, std::equal_to<const elem*>,
		mr::heap_allocator<std::pair<const elem* const, react_flow::node*>>>
	elem_owned_nodes_{};


	//must be the first to destruct
	elem_ptr root_{};

	tooltip::tooltip_manager tooltip_manager_{get_heap_allocator()};
	overlay_manager overlay_manager_{get_heap_allocator()};
	cursor_collection cursor_collection_{};

	allocator_aware_poly_unique_ptr<native_communicator, mr::heap_allocator<native_communicator>>  communicator_{};

	unsigned long long current_frame_{};
	double current_time_{};

	[[nodiscard]] scene_base() = default;

	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] explicit(false) scene_base(
		mr::arena_id_t arena_id,
		renderer_frontend&& renderer,
		std::in_place_type_t<T>,
		Args&& ...args
		);

public:
	template <std::derived_from<native_communicator> Ty, typename ...Args>
	void set_native_communicator(Args&& ...args){
		communicator_ = mo_yanxi::make_allocate_aware_poly_unique<Ty, native_communicator>(
			mr::heap_allocator<native_communicator>{get_heap()}, std::forward<Args>(args)...);
	}

	[[nodiscard]] unsigned long long get_current_frame() const noexcept{
		return current_frame_;
	}

	[[nodiscard]] double get_current_time() const{
		return current_time_;
	}

	void set_current_time(const float current_time){
		current_time_ = current_time;
	}


	[[nodiscard]] input_handle::input_manager<scene&>& get_inputs() noexcept{
		return inputs_;
	}

	[[nodiscard]] input_handle::key_mapping_interface* find_input(std::string_view name) const noexcept{
		return inputs_.find_sub_input(name);
	}

	[[nodiscard]] native_communicator* get_communicator() const noexcept{
		return communicator_.get();
	}

	[[nodiscard]] renderer_frontend& renderer() noexcept{
		return renderer_;
	}

	template <typename T = std::byte>
	[[nodiscard]] mr::heap_allocator<T> get_heap_allocator() const noexcept {
		return mr::heap_allocator<T>{get_heap()};
	}

	[[nodiscard]] auto* get_memory_resource() const noexcept {
		return get_heap();
	}

	template <std::derived_from<elem> T = elem, bool unchecked = false>
	T& root(){
		assert(root_ != nullptr);
		if constexpr (std::same_as<T, elem> || unchecked){
			return *static_cast<T*>(root_.get());
		}
		return dynamic_cast<T&>(*root_);
	}


	[[nodiscard]] rect get_region() const noexcept{
		return region_;
	}

	[[nodiscard]] vec2 get_extent() const noexcept{
		return region_.extent();
	}

	[[nodiscard]] vec2 get_cursor_pos() const noexcept{
		return inputs_.cursor_pos();
	}
	[[nodiscard]] std::span<elem const * const> get_inbounds() const noexcept{
		return last_inbounds_;
	}

	[[nodiscard]] bool is_mouse_pressed() const noexcept{
		return std::ranges::any_of(mouse_states_, std::identity{}, &mouse_state::pressed);
	}

	[[nodiscard]] bool is_mouse_pressed(input_handle::mouse mouse_button_code) const noexcept{
		return mouse_states_[std::to_underlying(mouse_button_code)].pressed;
	}

	void drop_event_focus(const elem* target) noexcept{
		if(focus_scroll_ == target)focus_scroll_ = nullptr;
		if(focus_cursor_ == target)focus_cursor_ = nullptr;
		if(focus_key_ == target)focus_key_ = nullptr;
	}

	[[nodiscard]] react_flow::manager& get_react_flow() const noexcept{
		return *react_flow_;
	}

protected:
	void drop_elem_nodes(const elem* elem) noexcept{
		auto [begin, end] = elem_owned_nodes_.equal_range(elem);
		for(auto cur = begin; cur != end; ++cur){
			react_flow_->erase_node(*cur->second);
		}
		elem_owned_nodes_.erase(begin, end);
	}

};


export
struct scene : scene_base{
	friend elem;
	friend ui_manager;

	using scene_base::scene_base;

	scene(const scene& other) = delete;
	scene(scene&& other) noexcept;
	scene& operator=(const scene& other) = delete;
	scene& operator=(scene&& other) noexcept;

public:
	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] elem_ptr create(Args&& ...args){
		return elem_ptr{*this, nullptr, std::in_place_type<T>, std::forward<Args>(args)...};
	}


	void resize(const math::frect region);

	template <invocable_elem_init_func Fn, typename... Args>
	auto create_overlay(const overlay_layout layout, Fn&& fn, Args&&... args){
		return static_cast<overlay_create_result<elem_init_func_create_t<Fn>>>(
			overlay_manager_.push_back(layout, elem_ptr{*this, nullptr, std::forward<Fn>(fn), std::forward<Args>(args)...})
		);
	}

	template <typename T, typename... Args>
		requires (constructible_elem<T, Args&&...>)
	overlay_create_result<T> emplace_overlay(const overlay_layout layout, Args&&... args){
		return static_cast<overlay_create_result<T>>(
			overlay_manager_.push_back(layout, elem_ptr{*this, nullptr, std::in_place_type<T>, std::forward<Args>(args)...})
		);
	}

	template <typename T, typename ...Args>
	[[nodiscard]] T& request_independent_react_node(Args&& ...args){
		return react_flow_->add_node<T>( std::forward<Args>(args)...);
	}

	template <typename T>
	[[nodiscard]] auto& request_independent_react_node(T&& args){
		return react_flow_->add_node( std::forward<T>(args));
	}

	bool erase_independent_react_node(react_flow::node& node) noexcept {
		return react_flow_->erase_node(node);
	}

	template <typename T, std::derived_from<elem> E, typename ...Args>
	T& request_react_node(E& elem, Args&& ...args){
		T& ptr = react_flow_->add_node<T>(elem, std::forward<Args>(args)...);
		elem_owned_nodes_.insert({std::addressof(elem), std::addressof(ptr)});
		return ptr;
	}



private:
	void update(double delta_in_tick);

	void draw(rect clip);

	void draw(){
		draw(region_);
	}

#pragma region Events
	void update_mouse_state(input_handle::key_set k);

	void inform_cursor_move(math::vec2 pos){
		inputs_.cursor_move_inform(pos);
		update_cursor();
	}

	void input_key(const input_handle::key_set key);

	void input_mouse(const input_handle::key_set key){
		inputs_.inform(key);
		update_mouse_state(key);
	}

	void input_inbound(bool is_inbound){
		inputs_.set_inbound(is_inbound);
	}

	void on_unicode_input(char32_t val) const;

	void on_scroll(const math::vec2 scroll) const;

public:
	void update_cursor();

	events::op_afterwards on_esc();

private:
#pragma endregion

	void layout();

	void request_cursor_update() noexcept{
		request_cursor_update_ = true;
	}

	void update_inbounds(mr::heap_vector<elem*>&& next);

	void try_swap_focus(elem* newFocus);

	void swap_focus(elem* newFocus);

	void drop_(const elem* target) noexcept;

	void notify_isolated_layout_update(elem* element){
		independent_layouts_.insert(element);
	}

};

template <std::derived_from<elem> T, typename ... Args>
scene_base::scene_base(
	mr::arena_id_t arena_id,
	renderer_frontend&& renderer, std::in_place_type_t<T>, Args&&... args):
	resource_(arena_id),
	renderer_(std::move(renderer)),
	root_(static_cast<scene&>(*this), nullptr, std::in_place_type<T>, std::forward<Args>(args)...){
	inputs_.main_binds.set_context(std::ref(static_cast<scene&>(*this)));
}
}