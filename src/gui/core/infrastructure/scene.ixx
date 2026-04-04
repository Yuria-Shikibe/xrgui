module;

#include <cassert>
#define UI_MAIN_THREAD_ONLY

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <plf_hive.h>
#endif

export module mo_yanxi.gui.infrastructure:scene;

import :defines;
import :elem_ptr;
import :tooltip_manager;
import :dialog_manager;
import :cursor;
import :elem_async_task;
import :object_pool;
import :flags;
import std;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.handle_wrapper;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.concurrent.mpsc_double_buffer;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.heterogeneous;
import mo_yanxi.circular_queue;

export import mo_yanxi.gui.util;
export import mo_yanxi.gui.style.manager;

export import mo_yanxi.input_handle;
export import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.fx.config;

export import mo_yanxi.react_flow;

import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.flat_set;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
#endif

namespace mo_yanxi::gui{

namespace fx{
export
struct layer_config{
	pipeline_config begin_config;
	std::optional<blit_pipeline_config> end_config;

	//TODO pre/post draw function?
};

/**
 * @brief Note that the render pass has nothing to do with VkRenderPass,
 * it's only an abstraction name for layer pass draw.
 */
export
struct scene_render_pass_config{

	using value_type = layer_config;

private:
	std::array<value_type, draw_pass_max_capacity> masks{};

	std::optional<blit_pipeline_config> tail_blit{};

	unsigned pass_count{};

public:
	constexpr scene_render_pass_config() = default;

	constexpr scene_render_pass_config(std::initializer_list<value_type> masks, std::optional<blit_pipeline_config> tail_blit) : tail_blit(tail_blit), pass_count(masks.size()){
		std::ranges::copy(masks, this->masks.begin());
	}

	inline constexpr const value_type& operator[](unsigned idx) const noexcept{
		assert(idx < draw_pass_max_capacity);
		return masks[idx];
	}

	inline constexpr unsigned size() const noexcept{
		return pass_count;
	}

	inline constexpr void resize(unsigned sz){
		if(sz >= masks.max_size()){
			throw std::bad_array_new_length();
		}

		pass_count = sz;
	}

	inline constexpr void push_back(const value_type& mask){
		if(pass_count >= masks.max_size()){
			throw std::bad_array_new_length();
		}

		masks[pass_count] = mask;
		pass_count++;
	}

	inline constexpr auto begin(this auto& self) noexcept{
		return self.masks.begin();
	}

	inline constexpr auto end(this auto& self) noexcept{
		return self.masks.begin() + self.size();
	}

	std::optional<blit_pipeline_config> get_tail_blit() const noexcept{
		return tail_blit;
	}
};

}

/**
 * @brief Principally, holes are not allowed, but this cause the destruction must begin from tail(highest), which cause some performance issue.
 *
 * TODO currently tooltip and overlays are not supported
 */
struct layer_altitude_record {
private:
	std::vector<unsigned, mr::heap_allocator<unsigned>> records_{};

public:
	layer_altitude_record() = default;

	explicit layer_altitude_record(const mr::heap_allocator<unsigned>& alloc) : records_(alloc) {
	}

	void insert(altitude_t alt, unsigned count = 1) {
		// // 修改 1: 允许在任意高度插入。
		// // 如果 alt 超过当前 size，自动扩容并填充 0 (产生空洞)
		// if (alt >= records_.size()) {
		// 	records_.resize(alt + 1, 0);
		// }
		// records_[alt] += count;
	}

	void erase(altitude_t alt, unsigned count = 1) noexcept {
		// assert(alt < records_.size()); // 确保删除的层存在
		// auto& rst = records_[alt];
		// assert(rst >= count);
		// rst -= count;
		//
		// // 修改 2: 只有当变为空的是"最高层"时，才尝试收缩容器
		// if (rst == 0 && alt == records_.size() - 1) {
		// 	records_.pop_back();
		//
		// 	// 关键修改: 循环移除尾部的空洞(0)，直到遇到非空层或容器为空
		// 	// 这样能保证 size() 始终反映真实的最高高度
		// 	while (!records_.empty() && records_.back() == 0) {
		// 		records_.pop_back();
		// 	}
		// }
	}

	altitude_t get_max() const noexcept {
		return records_.size();
	}
};

template <typename T>
struct double_buffer{
private:
	bool cur{};
	T buf_[2]{};
public:

	constexpr double_buffer() = default;

	template <typename ...Args>
		requires (std::constructible_from<T, const Args&...>)
	constexpr explicit double_buffer(const Args& ...args) : buf_{T(args...), T(args...)}{

	}

	template <typename S>
	constexpr auto&& get_cur(this S&& self) noexcept{
		return std::forward_like<S>(self.buf_[self.cur]);
	}


	template <typename S>
	constexpr auto&& get_bak(this S&& self) noexcept{
		return std::forward_like<S>(self.buf_[!self.cur]);
	}

	constexpr void swap() noexcept{
		cur = !cur;
	}

	constexpr void swap_internal() noexcept{
		std::ranges::swap(buf_[0], buf_[1]);
	}
};

export
struct native_communicator{
	virtual ~native_communicator() = default;

	void set_clipboard(std::string_view text){
		set_clipboard_impl(text, false);
	}

	void set_clipboard(std::string_view text, bool assume_zero_terminated){
		set_clipboard_impl(text, assume_zero_terminated);
	}

	virtual void set_ime_enabled(bool enabled){

	}

	virtual void set_ime_cursor_rect(const math::raw_frect region){

	}

protected:
	virtual void set_clipboard_impl(std::string_view text, bool assume_zero_terminated){

	}

public:
	virtual void set_native_cursor_visibility(bool show){

	}

	virtual std::string_view get_clipboard(){
		return {};
	}

	//TODO sound interface
};

struct mouse_state{
	math::optional_vec2<float> src{math::nullopt_vec2<float>};

	void reset(const math::vec2 pos) noexcept{
		src = pos;
	}

	void clear() noexcept{
		src.reset();
	}

	constexpr explicit operator bool() const noexcept{
		return src.has_value();
	}
};

export struct ui_manager;

export struct elem;

export
struct scene;
struct scene_base;

struct scene_resources{
	friend scene_base;
	friend scene;
private:
	mr::heap heap{};
	style::style_manager init_style_manager_() const;

public:
	any_pool<false, mr::unvs_allocator<std::byte>> object_pool{};

	style::style_manager style_manager{};
	cursor_collection cursor_collection_manager{};

	[[nodiscard]] scene_resources() = default;

	[[nodiscard]] explicit scene_resources(mr::heap&& heap)
		: heap(std::move(heap)), style_manager(init_style_manager_()){
	}

	[[nodiscard]] explicit scene_resources(mr::arena_id_t arena_id)
		: scene_resources{mr::heap{arena_id}}{
	}
};


namespace scene_submodule{

struct action_queue{
private:
	ccur::mpsc_double_buffer<elem*, mr::heap_vector<elem*>> pendings{};
	linear_flat_set<mr::heap_vector<elem*>> async_pending{};
	linear_flat_set<mr::heap_vector<elem*>> active{};

public:
	[[nodiscard]] action_queue(const mr::heap_allocator<elem*>& alloc) :
		pendings{alloc}
		, async_pending{alloc}
		, active{alloc}{
	}

	void push(elem* e){
		pendings.push(e);
	}

	void update(float delta_in_tick) noexcept;

	void try_dump_async();

	void erase(const elem* e);
};

enum struct input_key_result{
	none,
	esc_required
};

struct input{
	std::array<mouse_state, std::to_underlying(input_handle::mouse::Count)> mouse_states_{};
	input_handle::input_manager<scene&> inputs_{};
	double_buffer<mr::heap_vector<elem*>> inbounds_{};
	linear_flat_set<mr::heap_vector<elem*>> cursor_event_active_elems_{};

	elem* focus_scroll{nullptr};
	elem* focus_cursor{nullptr};
	elem* focus_key{nullptr};
	elem* last_inbound_click{nullptr};

	bool request_cursor_update_{};

	explicit input(const mr::heap_allocator<elem*>& alloc) :
		inbounds_{alloc}, cursor_event_active_elems_{alloc} {
	}

	void update_elem_cursor_state(float delta_in_tick, tooltip::tooltip_manager& tooltip) noexcept;

	void drop_event_focus(const elem* target) noexcept{
		if(focus_scroll == target)focus_scroll = nullptr;
		if(focus_cursor == target)focus_cursor = nullptr;
		if(focus_key == target)focus_key = nullptr;
		if(last_inbound_click == target)last_inbound_click = nullptr;
	}

	void drop_elem(const elem* target) noexcept{
		drop_event_focus(target);
		std::erase(inbounds_.get_bak(), target);
		std::erase(inbounds_.get_cur(), target);
		cursor_event_active_elems_.erase(const_cast<elem*>(target));
	}

	void request_cursor_update() noexcept{
		request_cursor_update_ = true;
	}

	void overwrite_last_inbound_click_quiet(elem* elem) noexcept{
		last_inbound_click = elem;
	}

	void input_inbound(bool is_inbound){
		inputs_.set_inbound(is_inbound);
	}

	[[nodiscard]] std::span<elem * const> get_inbounds() const noexcept{
		return inbounds_.get_cur();
	}

	[[nodiscard]] bool is_mouse_pressed() const noexcept{
		return std::ranges::any_of(mouse_states_, &mouse_state::operator bool);
	}

	[[nodiscard]] bool is_mouse_pressed(input_handle::mouse mouse_button_code) const noexcept{
		return mouse_states_[std::to_underlying(mouse_button_code)] ? true : false;
	}

	math::vec2 get_cursor_pos() const noexcept{
		return inputs_.cursor_pos();
	}

	// 移入的核心处理函数
	void switch_key_focus(elem* element);
	void try_swap_focus();
	void swap_focus(elem* newFocus);

	input_key_result on_key_input(input_handle::key_set key);
	void on_unicode_input(char32_t val) const;
	void on_scroll(math::vec2 scroll) const;
	void update_inbounds();
	void update_mouse_state(input_handle::key_set k);

	style::cursor_style update_cursor(overlay_manager& overlays, tooltip::tooltip_manager& tooltips, elem& scene_root);

	style::cursor_style get_cursor_style(math::vec2 cursor_local_pos) const;

	style::cursor_style get_cursor_style() const;
};

struct associated_async_sync_task_queue_base{
protected:
	struct task_entry{
		void* e;
		std::move_only_function<void(void*)> func;

		void exec(){
			func(e);
		}
	};

	using container = mr::heap_vector<task_entry>;

	ccur::mpsc_double_buffer<task_entry, container> async_tasks_{};

public:
	[[nodiscard]] explicit associated_async_sync_task_queue_base(const container::allocator_type& alloc)
		: async_tasks_(alloc){
	}

	UI_MAIN_THREAD_ONLY void consume(){
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t.exec();
			}
		}
	}
};

template <typename T>
struct associated_async_sync_task_queue : associated_async_sync_task_queue_base{
	using owner_type = T;

	using associated_async_sync_task_queue_base::associated_async_sync_task_queue_base;

	template <std::derived_from<owner_type> E, std::invocable<E&> Fn>
	void post(E& e, Fn&& fn){
		async_tasks_.emplace(std::addressof(e), [f = std::forward<Fn>(fn)](void* e) mutable {
			std::invoke(f, *static_cast<E*>(e));
		});
	}

	template <std::derived_from<owner_type> E, std::invocable<> Fn>
	void post(E& e, Fn&& fn){
		async_tasks_.emplace(std::addressof(e), [f = std::forward<Fn>(fn)](void* e) mutable {
			std::invoke(f);
		});
	}

	UI_MAIN_THREAD_ONLY void erase(const elem* e) noexcept {
		async_tasks_.modify([&](container& c) noexcept {
			std::erase_if(c, [&](const container::value_type& v) noexcept {
				return v.e == e;
			});
		});
	}

};

struct elem_async_sync_task_queue : associated_async_sync_task_queue<elem>{
private:
	mr::heap_vector<task_entry> unsync_pending_;

public:
	[[nodiscard]] explicit elem_async_sync_task_queue(const container::allocator_type& alloc)
		: associated_async_sync_task_queue(alloc), unsync_pending_(alloc){
	}

	UI_MAIN_THREAD_ONLY void erase(const elem* e) noexcept {
		associated_async_sync_task_queue::erase(e);
		std::erase_if(unsync_pending_, [&](const container::value_type& v) noexcept {
			return v.e == e;
		});
	}

	UI_MAIN_THREAD_ONLY void consume();
	UI_MAIN_THREAD_ONLY void on_sync_relocate_consume();
};


struct async_sync_task_queue{
private:
	using func = std::move_only_function<void()>;
	using container = mr::heap_vector<func>;
	ccur::mpsc_double_buffer<func, container> async_tasks_{};

public:
	[[nodiscard]] explicit async_sync_task_queue(const container::allocator_type& alloc)
		: async_tasks_(alloc){
	}

	template <std::invocable<> Fn>
	void post(Fn&& fn){
		async_tasks_.emplace([f = std::forward<Fn>(fn)](){
			std::invoke(f);
		});
	}

	UI_MAIN_THREAD_ONLY void consume(){
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t();
			}
		}
	}
};



struct async_async_task_queue{
	using elem_async_task_ptr = allocator_aware_poly_unique_ptr<basic_elem_async_task, mr::heap_allocator<basic_elem_async_task>>;

private:
	ccur::mpsc_queue<elem_async_task_ptr, std::deque<
			elem_async_task_ptr, mr::heap_allocator<elem_async_task_ptr>
		>> element_async_tasks_pending_{};
	ccur::mpsc_double_buffer<elem_async_task_ptr, mr::heap_vector<elem_async_task_ptr>> element_async_tasks_done_{};
	mr::heap_umap<elem*, unsigned, transparent::ptr_hasher<elem>, transparent::ptr_equal_to<elem>> element_async_task_alive_owners_{};
	std::jthread element_async_task_thread_{};

public:
	[[nodiscard]] async_async_task_queue(const mr::heap_allocator<elem_async_task_ptr>& alloc)
		:
		element_async_tasks_pending_(alloc),
		element_async_tasks_done_(alloc),
		element_async_task_alive_owners_(alloc),
		element_async_task_thread_([this](std::stop_token&& stop_token){
			elem_async_tasks_process(std::move(stop_token));
		}){
	}

private:
	void elem_async_tasks_process(std::stop_token stop_token){
		while(!stop_token.stop_requested()){
			auto task = element_async_tasks_pending_.consume([&] noexcept {
				return stop_token.stop_possible();
			});

			if(task){
				(*task)->process();
				element_async_tasks_done_.push(std::move(*task));
			}
		}
	}

public:
	UI_MAIN_THREAD_ONLY void process_done(){
		if(auto cont = element_async_tasks_done_.fetch()){
			for(auto&& elem_async_task : *cont){
				if(auto itr = element_async_task_alive_owners_.find(&elem_async_task->get_owner()); itr !=
					element_async_task_alive_owners_.end()){
					elem_async_task->on_done();
					if(--(itr->second) == 0){
						element_async_task_alive_owners_.erase(itr);
					}
				}
			}
		}
	}

	UI_MAIN_THREAD_ONLY void erase(const elem* owner){
		element_async_task_alive_owners_.erase(owner);
	}

	template <std::derived_from<elem> E, std::invocable<E&> Prov>
	void post(E& e, Prov&& prov){
		elem& owner = e;
		++element_async_task_alive_owners_[&owner];
		using task_type = std::decay_t<std::invoke_result_t<Prov&&, E&>>;
		struct crop{
			E& e;
			Prov&& prov;

			explicit(false) operator task_type(){
				return std::invoke_r<task_type>(prov, e);
			}
		};
		element_async_tasks_pending_.push(
			mo_yanxi::make_allocate_aware_poly_unique<task_type, basic_elem_async_task>(
				element_async_task_alive_owners_.get_allocator(), crop{e, std::forward<Prov>(prov)}));
	}
};


}



struct scene_base{
	friend elem;
	friend ui_manager;

protected:
	scene_resources* resources_;

	[[nodiscard]] mr::heap_handle get_heap() const noexcept{
		return resources_->heap.get();
	}

public:
	template <typename T = std::byte>
	[[nodiscard]] mr::heap_allocator<T> get_heap_allocator() const noexcept {
		return mr::heap_allocator<T>{get_heap()};
	}

private:
	renderer_frontend renderer_{};
	rect region_{};

public:
	std::thread::id ui_main_thread_id{std::this_thread::get_id()};

protected:

	scene_submodule::elem_async_sync_task_queue instant_task_queue_{get_heap_allocator()};

	scene_submodule::action_queue action_queue_{get_heap_allocator()};
	scene_submodule::async_async_task_queue async_task_queue_{get_heap_allocator()};
	scene_submodule::input input_handler_{get_heap_allocator()};

private:

	double_buffer<linear_flat_set<mr::heap_vector<elem*>>> independent_layouts_{mr::heap_allocator<elem*>{get_heap_allocator()}};

	struct update_entry{
		elem* elem;
		update_channel channels;

		constexpr auto operator<=>(const update_entry& o) const noexcept{
			return elem <=> o.elem;
		}

		constexpr bool operator==(const update_entry& o) const noexcept{
			return elem == o.elem;
		}
	};

	linear_flat_set<mr::heap_vector<update_entry>> active_update_elems_{get_heap_allocator()};
#ifndef NDEBUG
	bool debug__is_updating_elems_{false};
#endif


protected:
	allocator_aware_unique_ptr<react_flow::manager, mr::heap_allocator<react_flow::manager>> react_flow_{
		mo_yanxi::make_allocate_aware_unique<react_flow::manager>(mr::heap_allocator<react_flow::manager>{get_heap_allocator()})
	};

	std::unordered_multimap<
		const elem*, react_flow::node*,
		std::hash<const elem*>, std::equal_to<const elem*>,
		mr::heap_allocator<std::pair<const elem* const, react_flow::node*>>>
	elem_owned_nodes_{};

private:

	allocator_aware_poly_unique_ptr<native_communicator, mr::heap_allocator<native_communicator>>  communicator_{};

	unsigned long long current_frame_{};
	double current_time_{};

protected:
	tooltip::tooltip_manager tooltip_manager_{get_heap_allocator()};
	overlay_manager overlay_manager_{get_heap_allocator()};

	cursor_drawer current_cursor_drawers_{};
	elem* scene_root_{};

	[[nodiscard]] scene_base() = default;

	explicit(false) scene_base(
		scene_resources& resources,
		renderer_frontend&& renderer) :
		resources_(&resources), renderer_(std::move(renderer)){
	}

public:
	template <std::derived_from<native_communicator> Ty, typename ...Args>
	void set_native_communicator(Args&& ...args){
		assert(is_on_scene_thread(*this));
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

#pragma region AsyncTask
	template <std::derived_from<elem> E, std::invocable<E&> Fn>
	void post(E& e, Fn&& fn){
		instant_task_queue_.post(e, std::forward<Fn>(fn));
	}

	template <std::derived_from<elem> E, std::invocable<> Fn>
	void post(E& e, Fn&& fn){
		instant_task_queue_.post(e, std::forward<Fn>(fn));
	}

	void notify_instant_queue_consume(){
		instant_task_queue_.on_sync_relocate_consume();
	}

#pragma endregion

#pragma region IndependentUpdate

	void insert_update(elem& p, update_channel channel){
		assert(!debug__is_updating_elems_ && "insert during update causes iterators invalid");
		assert(is_on_scene_thread(*this));
		if(auto itr = active_update_elems_.find({&p}); itr != active_update_elems_.end()){
			itr->channels |= channel;
		}else{
			active_update_elems_.insert({&p, channel});
		}

	}

	void erase_update(const elem* p, update_channel channel) noexcept {
		assert(!debug__is_updating_elems_ && "erase during update causes iterators invalid");
		assert(is_on_scene_thread(*this));
		if(auto itr = active_update_elems_.find({const_cast<elem*>(p)}); itr != active_update_elems_.end()){
			if((itr->channels -= channel) == update_channel::none){
				active_update_elems_.erase(itr);
			}
		}
	}

#pragma endregion

	[[nodiscard]] native_communicator* get_communicator() const noexcept{
		assert(is_on_scene_thread(*this));
		return communicator_.get();
	}

	[[nodiscard]] renderer_frontend& renderer() noexcept{
		assert(is_on_scene_thread(*this));
		return renderer_;
	}

	[[nodiscard]] scene_resources& resources() const noexcept{
		assert(resources_ != nullptr);
		return *resources_;
	}


	[[nodiscard]] auto* get_memory_resource() const noexcept {
		return get_heap();
	}

	[[nodiscard]] rect get_region() const noexcept{
		assert(is_on_scene_thread(*this));
		return region_;
	}

	[[nodiscard]] vec2 get_extent() const noexcept{
		assert(is_on_scene_thread(*this));
		return region_.extent();
	}

	[[nodiscard]] vec2 get_cursor_pos() const noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.inputs_.cursor_pos();
	}

	[[nodiscard]] std::span<elem * const> get_inbounds() const noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.inbounds_.get_cur();
	}

	[[nodiscard]] input_handle::input_manager<scene&>& get_inputs() noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.inputs_;
	}

	[[nodiscard]] input_handle::key_mapping_interface* find_input(std::string_view name) const noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.inputs_.find_sub_input(name);
	}

	void overwrite_last_inbound_click_quiet(elem* elem) noexcept{
		assert(is_on_scene_thread(*this));
		input_handler_.last_inbound_click = elem;
	}

	[[nodiscard]] react_flow::manager& get_react_flow() const noexcept{
		assert(is_on_scene_thread(*this));
		return *react_flow_;
	}

protected:
	void drop_elem_nodes(const elem* elem) noexcept{
		assert(is_on_scene_thread(*this));
		auto [begin, end] = elem_owned_nodes_.equal_range(elem);
		for(auto cur = begin; cur != end; ++cur){
			react_flow_->erase_node(*cur->second);
		}
		elem_owned_nodes_.erase(begin, end);
	}

	void drop_(const elem* target) noexcept;


#pragma region elem_async_task_mfunc

public:
	template <std::derived_from<elem> E, std::invocable<E&> Prov>
	void post_elem_async_task(E& e, Prov&& prov){
		async_task_queue_.post(e, std::forward<Prov>(prov));
	}

#pragma endregion


	template <std::derived_from<elem> T = elem, bool unchecked = false>
	T& root(){
		assert(is_on_scene_thread(*this));
		assert(scene_root_ != nullptr);
		if constexpr (std::same_as<T, elem> || unchecked){
			return static_cast<T&>(*scene_root_);
		}else{
			return dynamic_cast<T&>(*scene_root_);
		}
	}


	void resize(const math::frect region);

	void close_overlay(const elem* overlay_elem){
		overlay_manager_.truncate(overlay_elem);
	}

#pragma region ReactFlow
	//TODO make these API async safe

	template <typename T, typename ...Args>
	[[nodiscard]] T& request_independent_react_node(Args&& ...args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		return react_flow_->add_node<T>( std::forward<Args>(args)...);
	}

	template <typename T>
	[[nodiscard]] auto& request_independent_react_node(T&& args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		return react_flow_->add_node( std::forward<T>(args));
	}

	bool erase_independent_react_node(react_flow::node& node) /*noexcept*/ {
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"erase node not on main ui thread"};
		}
		return react_flow_->erase_node(node);
	}

	template <typename T, std::derived_from<elem> E, typename ...Args>
	T& request_react_node(E& elem, Args&& ...args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		T& ptr = react_flow_->add_node<T>(elem, std::forward<Args>(args)...);
		elem_owned_nodes_.insert({std::addressof(elem), std::addressof(ptr)});
		return ptr;
	}

	template <typename T, std::derived_from<elem> E>
	T& request_embedded_react_node(E& elem, T&& args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		T& ptr = react_flow_->add_node(std::forward<T>(args));
		elem_owned_nodes_.insert({std::addressof(elem), std::addressof(ptr)});
		return ptr;
	}
#pragma endregion

private:
	void async_push_elem_to_action_pending(elem* e){
		action_queue_.push(e);
	}


#pragma region Events

public:
	void update_cursor_type();

private:
	void update(double delta_in_tick);


	void on_cursor_move(math::vec2 pos){
		assert(is_on_scene_thread(*this));
		input_handler_.inputs_.cursor_move_inform(pos);
		input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
		update_cursor_type();
	}

	void on_mouse_input(const input_handle::key_set key){
		assert(is_on_scene_thread(*this));
		input_handler_.inputs_.inform(key);
		input_handler_.update_mouse_state(key);
		update_cursor_type();
	}

	void on_unicode_input(char32_t val) const{
		assert(is_on_scene_thread(*this));
		input_handler_.on_unicode_input(val);
	}

	void on_scroll(const math::vec2 scroll) const{
		assert(is_on_scene_thread(*this));
		input_handler_.on_scroll(scroll);
	}

	void on_inbound_changed(bool inbounded){
		assert(is_on_scene_thread(*this));
		input_handler_.input_inbound(inbounded);
	}

	void on_key_input(input_handle::key_set key){
		assert(is_on_scene_thread(*this));
		switch(input_handler_.on_key_input(key)){
		case scene_submodule::input_key_result::none : break;
		case scene_submodule::input_key_result::esc_required : on_esc();
			break;
		default : break;
		}
	}

public:
	events::op_afterwards on_esc();

	void request_cursor_update() noexcept{
		input_handler_.request_cursor_update_ = true;
	}

private:
#pragma endregion

	void layout();

	void add_isolated_layout_update(elem* element){
		assert(is_on_scene_thread(*this));
		independent_layouts_.get_bak().insert(element);
	}

};

export
struct scene : scene_base{
	friend elem;
	friend ui_manager;

private:
	elem_ptr root_{};

public:
	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] explicit(false) scene(
		scene_resources& resources,
		renderer_frontend&& renderer,
		std::in_place_type_t<T>,
		Args&& ...args
		) : scene_base(resources, std::move(renderer)), root_(static_cast<scene&>(*this), nullptr, std::in_place_type<T>, std::forward<Args>(args)...){
		input_handler_.inputs_.main_binds.set_context(std::ref(*this));
		scene_root_ = root_.get();
	}

	scene(const scene& other) = delete;
	scene(scene&& other) noexcept = delete;
	scene& operator=(const scene& other) = delete;
	scene& operator=(scene&& other) noexcept = delete;

protected:
	virtual void draw_impl(rect clip);

public:
	virtual ~scene() = default;

	fx::scene_render_pass_config pass_config{};

	void draw_at(const elem& elem);

	void draw(){
		draw_impl(get_region());
	}

	void draw(rect region){
		draw_impl(region);
	}

	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] elem_ptr create(Args&& ...args){
		return elem_ptr{*this, nullptr, std::in_place_type<T>, std::forward<Args>(args)...};
	}

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
};

bool is_on_scene_thread(const scene_base& scene) noexcept{
	return std::this_thread::get_id() == scene.ui_main_thread_id;
}

}