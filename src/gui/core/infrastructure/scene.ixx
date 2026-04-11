module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>
#include <mo_yanxi/adapted_attributes.hpp>

#define UI_MAIN_THREAD_ACCESS_ONLY
#define UI_TRANSIENT
#define UI_MERGE_ON_JOIN

#ifndef NDEBUG
#define SCENE_REFERENCE_COUNT_CHECK
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
export import mo_yanxi.gui.util.task_queue;
export import mo_yanxi.gui.style.manager;

export import mo_yanxi.input_handle;
export import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.fx.config;

export import mo_yanxi.react_flow;

import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.flat_set;
import mo_yanxi.fixed_vector;
import mo_yanxi.call_stream;

namespace mo_yanxi::gui{
std::thread::id exchange_scene_thread(scene& s, std::thread::id id);

export enum struct elem_tree_channel : std::uint8_t{
	deduced = 0,
	regular = 0b001,
	tooltip = 0b010,
	overlay = 0b100,
	all = regular | tooltip | overlay,
};

BITMASK_OPS(export, elem_tree_channel);


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

	void clear() noexcept{
		buf_[0].clear();
		buf_[1].clear();
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
struct scene_resources{
	friend scene_base;
	friend scene;
private:
	mr::heap heap{};
	style::style_manager init_style_manager_() const;

	allocator_aware_poly_unique_ptr<native_communicator, mr::heap_allocator<native_communicator>>  communicator_{};
public:
	any_pool<false, mr::unvs_allocator<std::byte>> object_pool{};

	UI_MAIN_THREAD_ACCESS_ONLY style::style_manager style_manager{};
	UI_MAIN_THREAD_ACCESS_ONLY cursor_collection cursor_collection_manager{};

	template <std::derived_from<native_communicator> Ty, typename ...Args>
	void set_native_communicator(Args&& ...args){
		communicator_ = mo_yanxi::make_allocate_aware_poly_unique<Ty, native_communicator>(
			mr::heap_allocator<native_communicator>{heap.get()}, std::forward<Args>(args)...);
	}

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
	linear_flat_set<mr::heap_vector<elem*>> active{};

public:
	[[nodiscard]] action_queue(const mr::heap_allocator<elem*>& alloc) :
		pendings{alloc}, active{alloc}{
	}

	void push(elem* e){
		pendings.push(e);
	}

	void update(float delta_in_tick) noexcept;

	void erase(const elem* e);

	void merge(action_queue&& other){
		pendings.merge(std::move(other).pendings);
		active.merge(std::move(other).active);
	}

	void clear() noexcept{
		pendings.clear();
		active.clear();
	}
};

enum struct input_key_result{
	intercepted,
	fall_through,
	esc_required
};

struct input{
	struct cursor_update_result{
		events::op_afterwards op;
		style::cursor_style style;
	};
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

	events::op_afterwards on_unicode_input(char32_t val) const;
	events::op_afterwards on_scroll(math::vec2 scroll) const;
	events::op_afterwards on_mouse_input(input_handle::key_set k);

	void update_inbounds();

	cursor_update_result update_cursor(overlay_manager& overlays, tooltip::tooltip_manager& tooltips, elem& scene_root);

	style::cursor_style get_cursor_style(math::vec2 cursor_local_pos) const;

	style::cursor_style get_cursor_style() const;
};

struct async_async_task_queue{
	using elem_async_task_ptr = allocator_aware_poly_unique_ptr<basic_elem_async_task, mr::heap_allocator<basic_elem_async_task>>;

private:
	ccur::mpsc_queue<elem_async_task_ptr, std::deque<
			elem_async_task_ptr, mr::heap_allocator<elem_async_task_ptr>
		>> element_async_tasks_pending_{};

	std::atomic<basic_elem_async_task*> current_done_task_{};
	mr::heap_umap<elem*, unsigned, transparent::ptr_hasher<elem>, transparent::ptr_equal_to<elem>> element_async_task_alive_owners_{};
	std::unique_ptr<scene> forked_scene_{};
	std::jthread element_async_task_thread_{};

public:
	[[nodiscard]] async_async_task_queue(const mr::heap_allocator<elem_async_task_ptr>& alloc,
	                                     std::unique_ptr<scene>&& scene)
		:
		element_async_tasks_pending_(alloc),
		element_async_task_alive_owners_(alloc),
		forked_scene_((assert(scene != nullptr), std::move(scene))),
		element_async_task_thread_([this](std::stop_token&& stop_token){
			exchange_scene_thread(*forked_scene_, std::this_thread::get_id());
			elem_async_tasks_process(std::move(stop_token));
		}){
	}

	[[nodiscard]] std::jthread& get_element_async_task_thread() noexcept{
		return element_async_task_thread_;
	}

	~async_async_task_queue(){
		element_async_task_thread_.request_stop();
		element_async_tasks_pending_.notify();

		current_done_task_.store(nullptr, std::memory_order_relaxed);
		current_done_task_.notify_one();

	}

private:
	void elem_async_tasks_process(std::stop_token stop_token){
		while(!stop_token.stop_requested()){
			auto task = element_async_tasks_pending_.consume([&] noexcept {
				return stop_token.stop_requested();
			});

			if(task){
				(*task)->process(*forked_scene_);
				auto addr = std::to_address(*task);
				current_done_task_.store(addr, std::memory_order_release);
				if(stop_token.stop_requested())break;
				current_done_task_.wait(addr, std::memory_order_relaxed);
			}
		}
	}

public:
	void process_done(){
		if(auto elem_async_task = current_done_task_.load(std::memory_order_acquire)){
			auto thread = std::this_thread::get_id();
			auto last = exchange_scene_thread(*forked_scene_, thread);
			auto& alive_set = element_async_task_alive_owners_;
			if(auto itr = alive_set.find(&elem_async_task->get_owner()); itr != alive_set.end()){
				elem_async_task->on_done(*forked_scene_);
				if(--(itr->second) == 0){
					alive_set.erase(itr);
				}
			}
			exchange_scene_thread(*forked_scene_, last);
			current_done_task_.store(nullptr, std::memory_order_relaxed);
			current_done_task_.notify_one();
		}
	}

	void erase(const elem* owner){
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

struct scene_shared_resources{
protected:
	scene_resources* resources_{};
	UI_MAIN_THREAD_ACCESS_ONLY rect region_{};

public:
	[[nodiscard]] scene_shared_resources() = default;

	[[nodiscard]] explicit scene_shared_resources(scene_resources& resources)
		: resources_(&resources){
	}

	[[nodiscard]] rect get_region() const noexcept{
		return region_;
	}

	[[nodiscard]] vec2 get_extent() const noexcept{
		return region_.extent();
	}


};

struct scene_base : scene_shared_resources{
	friend elem;
	friend ui_manager;

protected:

	[[nodiscard]] mr::heap_handle get_heap() const noexcept{
		return resources_->heap.get();
	}

public:
	template <typename T = std::byte>
	[[nodiscard]] mr::heap_allocator<T> get_heap_allocator() const noexcept {
		return mr::heap_allocator<T>{get_heap()};
	}

private:
	UI_MAIN_THREAD_ACCESS_ONLY UI_TRANSIENT renderer_frontend renderer_{};

public:
	std::thread::id ui_main_thread_id{std::this_thread::get_id()};

private:
#ifdef SCENE_REFERENCE_COUNT_CHECK
	UI_TRANSIENT std::size_t element_on_this_scene_{};
	struct check_on_destruction{
		scene_base* scene_base{nullptr};
		~check_on_destruction(){
			assert(scene_base->element_on_this_scene_ == 0);
		}
	} check_on_destruction_{this};

#endif

	FORCE_INLINE inline void incr_ref_count_() noexcept{
#ifdef SCENE_REFERENCE_COUNT_CHECK
		++element_on_this_scene_;
#endif
	}

	FORCE_INLINE inline void decr_ref_count_() noexcept{
#ifdef SCENE_REFERENCE_COUNT_CHECK
		// if(element_on_this_scene_  == 0){
		// 	static unsigned overdecr = 0;
		// 	++overdecr;
		// 	std::println(std::cerr, "{}", overdecr);
		// 	return;
		// }
		assert(element_on_this_scene_ != 0);
		--element_on_this_scene_;
#endif
	}

protected:

	FORCE_INLINE inline void check_ref_count_zero_() noexcept{
#ifdef SCENE_REFERENCE_COUNT_CHECK
		assert(element_on_this_scene_ == 0);
#endif

	}

public:

	UI_TRANSIENT unsigned long long current_frame_{};
	UI_TRANSIENT double current_time_{};

protected:
	UI_TRANSIENT elem_tree_channel display_state_changed_channel_{};

protected:

	UI_MERGE_ON_JOIN associated_async_sync_task_queue<elem> instant_task_queue_{get_heap_allocator()};
	UI_MERGE_ON_JOIN scene_submodule::action_queue action_queue_{get_heap_allocator()};

	//TODO use unique ptr
	std::unique_ptr<scene_submodule::async_async_task_queue> async_task_queue_{};
	UI_TRANSIENT scene_submodule::input input_handler_{get_heap_allocator()};

private:
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY double_buffer<linear_flat_set<mr::heap_vector<elem*>>> independent_layouts_{mr::heap_allocator<elem*>{get_heap_allocator()}};

#pragma region Updates

	struct update_entry{
		enum{
			add,
			erase,
			pending
		};
		elem* elem;
		update_channel added_channels;
		update_channel erase_channels;

		constexpr auto operator<=>(const update_entry& o) const noexcept{
			return elem <=> o.elem;
		}

		constexpr bool operator==(const update_entry& o) const noexcept{
			return elem == o.elem;
		}
	};
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY linear_flat_set<mr::heap_vector<update_entry>> active_update_elems_{get_heap_allocator()};
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY linear_flat_set<mr::heap_vector<update_entry>> active_update_elems_state_changes{get_heap_allocator()};

#pragma endregion

protected:
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY react_flow::manager react_flow_{};

	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY std::unordered_multimap<
		const elem*, react_flow::node*,
		std::hash<const elem*>, std::equal_to<const elem*>,
		mr::heap_allocator<std::pair<const elem* const, react_flow::node*>>>
	elem_owned_nodes_{get_heap_allocator()};

private:
	UI_MERGE_ON_JOIN fixed_vector<call_stream_task_queue, mr::heap_allocator<call_stream_task_queue>> output_communicate_async_task_queues_{};
	async_sync_task_queue<scene&> input_communicate_async_task_queue_{get_heap_allocator()};

protected:
	UI_TRANSIENT tooltip::tooltip_manager tooltip_manager_{get_heap_allocator()};
	UI_TRANSIENT overlay_manager overlay_manager_{get_heap_allocator()};
	UI_TRANSIENT cursor_drawer current_cursor_drawers_{};
	elem* scene_root_{};

	[[nodiscard]] scene_base() = default;

	explicit(false) scene_base(scene_resources& resources, renderer_frontend&& renderer);

public:

	[[nodiscard]] explicit scene_base(const scene_shared_resources& resources)
		: scene_shared_resources(resources){
	}

	template <std::derived_from<elem> T = elem, bool unchecked = false>
	T& root(){
		assert(scene_root_ != nullptr);
		if constexpr (std::same_as<T, elem> || unchecked){
			return static_cast<T&>(*scene_root_);
		}else{
			return dynamic_cast<T&>(*scene_root_);
		}
	}

	std::size_t get_output_communicate_async_task_queues_size() const noexcept{
		return output_communicate_async_task_queues_.size();
	}

	void drop_and_reset_communicate_async_task_queue_size(std::size_t total_channels){
		output_communicate_async_task_queues_ = decltype(output_communicate_async_task_queues_){std::allocator_arg, get_heap_allocator(), total_channels};
	}

	auto& get_output_communicate_async_task_queue(std::size_t channel){
		return output_communicate_async_task_queues_.at(channel);
	}

	auto& get_input_communicate_async_task_queue(){
		return input_communicate_async_task_queue_;
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

#pragma endregion

#pragma region IndependentUpdate

	void insert_update(elem& p, update_channel channel){
		assert(is_on_scene_thread(*this));
		auto& set = active_update_elems_state_changes;
		if(auto itr = set.find({&p}); itr != set.end()){
			itr->added_channels |= channel;
		}else{
			set.insert({.elem = &p, .added_channels = channel});
		}

	}

	void erase_update(const elem* p, update_channel channel) noexcept {
		assert(is_on_scene_thread(*this));
		auto& set = active_update_elems_state_changes;

		if(auto itr = set.find({const_cast<elem*>(p)}); itr != set.end()){
			itr->erase_channels |= channel;
		}else{
			set.insert({.elem = const_cast<elem*>(p), .erase_channels = channel});
		}
	}

	void apply_update_state_changes() noexcept {
		assert(is_on_scene_thread(*this));

		for (auto && change : active_update_elems_state_changes){
			if(auto itr = active_update_elems_.find(change); itr != active_update_elems_.end()){
				itr->added_channels |= change.added_channels;
				if((itr->added_channels -= change.erase_channels) == update_channel::none){
					active_update_elems_.erase(itr);
				}
			}else{
				if(auto c = change.added_channels - change.erase_channels; c != update_channel::none){
					active_update_elems_.insert({.elem = change.elem, .added_channels = c});
				}
			}
		}
		active_update_elems_state_changes.clear();
	}

#pragma endregion

	[[nodiscard]] native_communicator* get_communicator() const noexcept{
		assert(is_on_scene_thread(*this));
		return resources_->communicator_.get();
	}

	[[nodiscard]] renderer_frontend& renderer() noexcept{
		assert(is_on_scene_thread(*this));
		return renderer_;
	}

	[[nodiscard]] scene_resources& resources() const noexcept{
		assert(resources_ != nullptr);
		return *resources_;
	}

	void notify_display_state_changed(elem_tree_channel channel) noexcept{
		assert(is_on_scene_thread(*this));
		if(channel == elem_tree_channel::deduced){
			display_state_changed_channel_ = elem_tree_channel::all;
		}else{
			display_state_changed_channel_ |= channel;
		}
	}

	elem_tree_channel check_display_state_changed() noexcept{
		assert(is_on_scene_thread(*this));
		return std::exchange(display_state_changed_channel_, elem_tree_channel{});
	}


	[[nodiscard]] auto* get_memory_resource() const noexcept {
		return get_heap();
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

	[[nodiscard]] react_flow::manager& get_react_flow() noexcept{
		assert(is_on_scene_thread(*this));
		return react_flow_;
	}

protected:
	void drop_elem_nodes(const elem* elem) noexcept{
		assert(is_on_scene_thread(*this));
		auto [begin, end] = elem_owned_nodes_.equal_range(elem);
		for(auto cur = begin; cur != end; ++cur){
			react_flow_.erase_node(*cur->second);
		}
		elem_owned_nodes_.erase(begin, end);
	}

	void drop_(const elem* target) noexcept;

public:
	void resize(const math::frect region);

	void close_overlay(const elem* overlay_elem){
		overlay_manager_.truncate(overlay_elem);
	}

#pragma region ReactFlow
	//TODO make these API async safe

public:
	template <typename T, typename ...Args>
	[[nodiscard]] T& request_independent_react_node(Args&& ...args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		return react_flow_.add_node<T>( std::forward<Args>(args)...);
	}

	template <typename T>
	[[nodiscard]] auto& request_independent_react_node(T&& args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		return react_flow_.add_node( std::forward<T>(args));
	}

	bool erase_independent_react_node(react_flow::node& node) /*noexcept*/ {
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"erase node not on main ui thread"};
		}
		return react_flow_.erase_node(node);
	}

	template <typename T, std::derived_from<elem> E, typename ...Args>
	T& request_react_node(E& elem, Args&& ...args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		T& ptr = react_flow_.add_node<T>(elem, std::forward<Args>(args)...);
		elem_owned_nodes_.insert({std::addressof(elem), std::addressof(ptr)});
		return ptr;
	}

	template <typename T, std::derived_from<elem> E>
	T& request_embedded_react_node(E& elem, T&& args){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}
		T& ptr = react_flow_.add_node(std::forward<T>(args));
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

	void update(double delta_in_tick);

	events::op_afterwards on_cursor_move(math::vec2 pos){
		assert(is_on_scene_thread(*this));
		input_handler_.inputs_.cursor_move_inform(pos);
		auto [op, style] = input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
		current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(style);
		return op;
	}

	events::op_afterwards on_mouse_input(const input_handle::key_set key){
		assert(is_on_scene_thread(*this));
		input_handler_.inputs_.inform(key);
		auto rst = input_handler_.on_mouse_input(key);
		update_cursor_type();
		return rst;
	}

	events::op_afterwards on_unicode_input(char32_t val) const{
		assert(is_on_scene_thread(*this));
		return input_handler_.on_unicode_input(val);
	}

	events::op_afterwards on_scroll(const math::vec2 scroll) const{
		assert(is_on_scene_thread(*this));
		return input_handler_.on_scroll(scroll);
	}

	events::op_afterwards on_key_input(input_handle::key_set key){
		assert(is_on_scene_thread(*this));
		switch(input_handler_.on_key_input(key)){
		case scene_submodule::input_key_result::intercepted :
			return events::op_afterwards::intercepted;
		case scene_submodule::input_key_result::esc_required :
			return on_esc();
		default :
			return events::op_afterwards::fall_through;
		}
	}

	void on_inbound_changed(bool inbounded){
		assert(is_on_scene_thread(*this));
		input_handler_.input_inbound(inbounded);
	}

	events::op_afterwards on_esc();

	void request_cursor_update() noexcept{
		input_handler_.request_cursor_update_ = true;
	}

	void layout();

private:
#pragma endregion

	void add_isolated_layout_update(elem* element){
		assert(is_on_scene_thread(*this));
		independent_layouts_.get_bak().insert(element);
	}

protected:
	void merge(scene_base&& target){
		target.tooltip_manager_.clear();
		target.overlay_manager_.clear();


		for (auto&& [lhs, rhs] : std::views::zip(output_communicate_async_task_queues_, target.output_communicate_async_task_queues_)){
			lhs.merge(std::move(rhs));
		}

		//TODO ensure target has no child to avoid resource leaking
		{
			target.independent_layouts_.swap();
			independent_layouts_.get_bak().merge(target.independent_layouts_.get_cur());
			target.independent_layouts_.clear();
		}

		{
			target.apply_update_state_changes();
			active_update_elems_.merge(std::move(target.active_update_elems_));
		}

		{
			react_flow_.merge(std::move(target.react_flow_));
			std::destroy_at(&target.react_flow_);
			std::construct_at(&target.react_flow_);
			elem_owned_nodes_.merge(std::move(target.elem_owned_nodes_));
			target.elem_owned_nodes_.clear();
		}

		action_queue_.merge(std::move(target.action_queue_));
		instant_task_queue_.merge(std::move(target.instant_task_queue_));
	}
};

export
struct scene : scene_base{
	friend elem;
	friend ui_manager;

protected:

	void init_root() const;

public:
	using scene_base::scene_base;



	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] explicit(false) scene(
		scene_resources& resources,
		renderer_frontend&& renderer,
		std::in_place_type_t<T>,
		Args&& ...args
		) = delete;

	scene(const scene& other) = delete;
	scene(scene&& other) noexcept = delete;
	scene& operator=(const scene& other) = delete;
	scene& operator=(scene&& other) noexcept = delete;

protected:
	virtual void draw_impl(rect clip){
		throw std::runtime_error{"Draw is not impl"};
	}

public:

	void enable_elem_async_task_post(bool enable);

	template <std::derived_from<elem> E, std::invocable<E&> Prov>
	void post_elem_async_task(E& e, Prov&& prov){
		if(async_task_queue_){
			async_task_queue_->post(e, std::forward<Prov>(prov));
		}else{
			std::derived_from<basic_elem_async_task> auto task = std::invoke(std::forward<Prov>(prov), e);
			task.process(*this);
			task.on_done(*this);
		}
	}


	virtual std::unique_ptr<scene> fork(){
		auto rst = std::make_unique<scene>(static_cast<scene_shared_resources>(*this));
		rst->drop_and_reset_communicate_async_task_queue_size(get_output_communicate_async_task_queues_size());
		return rst;
	}

	virtual void join(scene&& scene){
		get_input_communicate_async_task_queue().consume(scene);
		merge(std::move(scene));
		assert(scene.scene_root_ == nullptr && "target scene must drop all element ownership");
		scene.check_ref_count_zero_();
	}

	virtual ~scene() = default;

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