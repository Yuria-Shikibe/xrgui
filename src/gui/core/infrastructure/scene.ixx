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
import :scene_input;
import std;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.handle_wrapper;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.concurrent.mpsc_double_buffer;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.heterogeneous;
import mo_yanxi.circular_queue;
import mo_yanxi.log;
import mo_yanxi.audio;

export import mo_yanxi.gui.util;
export import mo_yanxi.gui.util.task_queue;
export import mo_yanxi.gui.style.tree.manager;
export import mo_yanxi.gui.sound.manager;
export import mo_yanxi.audio;

export import mo_yanxi.input_handle;
export import mo_yanxi.input_handle.input_event_queue;
export import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.fx.config;

export import mo_yanxi.react_flow;
export import mo_yanxi.i18n.text_tree.react_flow;

import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.flat_set;
import mo_yanxi.fixed_vector;
import mo_yanxi.call_stream;
import mo_yanxi.double_buffer;

namespace mo_yanxi::gui{
std::thread::id exchange_scene_thread(scene& s, std::thread::id id);

BITMASK_OPS(export, elem_tree_channel);

export struct ui_manager;
export struct elem;

export using i18n_text_root_node = mo_yanxi::i18n::i18n_text_root_node;

/**
 * @brief Tracks element count per altitude level with O(1) max query.
 *
 * Holes are allowed: when the highest occupied altitude drops to zero, max is scanned downward.
 */
struct layer_altitude_record {
private:
	mr::heap_vector<unsigned> records_{};
	altitude_t max_used_{};

public:
	layer_altitude_record() = default;

	explicit layer_altitude_record(const mr::heap_allocator<unsigned>& alloc) : records_(alloc) {
	}

	void insert(altitude_t alt, unsigned count = 1) {
		if(alt >= records_.size()){
			records_.resize(alt + 1);
		}
		records_[alt] += count;
		if(alt > max_used_){
			max_used_ = alt;
		}
	}

	void erase(altitude_t alt, unsigned count = 1) noexcept {
		if(alt >= records_.size()){
			return;
		}
		records_[alt] -= count;
		if(alt == max_used_ && records_[alt] == 0){
			while(max_used_ > 0 && records_[max_used_] == 0){
				--max_used_;
			}
		}
	}

	altitude_t get_max() const noexcept {
		return max_used_;
	}
};

export
/**
 * @brief One-shot request object used by native clipboard implementations.
 *
 * Window-thread code receives this object, obtains the clipboard text, then
 * calls `std::move(request).set_value(text)`. The completion is routed back to
 * the GUI scene through an owner-bound callback.
 */
struct native_clipboard_request{
	std::move_only_function<void(std::string)> complete{};

	void set_value(std::string text) &&{
		if(!complete){
			throw std::runtime_error{"native clipboard request completed without a receiver"};
		}
		std::invoke(std::move(complete), std::move(text));
	}
};

export
/**
 * @brief GUI-thread facade for native window operations.
 *
 * Elements should use this interface instead of touching `GLFWwindow*`, Win32
 * IME, clipboard, or cursor APIs directly. Concrete backends may marshal calls
 * to the window thread. Requests that return data must bind an owner element so
 * completion is dropped safely if that element is detached before the native
 * result arrives.
 */
struct native_communicator{
	virtual ~native_communicator() = default;

	/**
	 * @brief Set native clipboard text.
	 *
	 * Fire-and-forget operations may be called from the GUI thread; backend
	 * implementations decide how to dispatch to the native window thread.
	 */
	void set_clipboard(std::string text){
		set_clipboard_impl(std::move(text));
	}

	/**
	 * @brief Request clipboard text and deliver it to a live owner element.
	 *
	 * `on_ready` runs later on the GUI thread as `on_ready(owner, text)`. Do not
	 * capture raw element pointers in the callback; use the owner parameter.
	 */
	template <typename E, typename Fn>
	void request_clipboard(E& owner, Fn&& on_ready);

	/**
	 * @brief Enable or disable native IME composition support.
	 */
	virtual void set_ime_enabled(bool enabled){

	}

	/**
	 * @brief Report the caret rectangle for native IME candidate placement.
	 */
	virtual void set_ime_cursor_rect(const math::raw_frect region){

	}

protected:
	virtual void set_clipboard_impl(std::string&& text){

	}

	virtual void request_clipboard_impl(native_clipboard_request&& request){
		(void)request;
	}

public:
	/**
	 * @brief Show or hide the native cursor.
	 *
	 * Default standalone applications usually hide it and let XRGUI draw the
	 * cursor style from scene state.
	 */
	virtual void set_native_cursor_visibility(bool show){

	}

	//TODO sound interface
};

struct native_gui_callback_entry{
	elem_ref<> owner{};
	std::move_only_function<void(elem&)> callback{};

	void exec(){
		if(!callback){
			throw std::runtime_error{"native GUI callback entry is empty"};
		}
		if(!owner){
			throw std::runtime_error{"native GUI callback entry has no owner"};
		}
		if(auto* live_owner = owner.get_live()){
			std::invoke(std::move(callback), *live_owner);
		}
	}
};

struct native_gui_callback_state{
private:
	using container = mr::heap_vector<native_gui_callback_entry>;
	ccur::mpsc_double_buffer<native_gui_callback_entry, container> callbacks_{};
	std::atomic_bool stopped_{false};

public:
	[[nodiscard]] explicit native_gui_callback_state(const container::allocator_type& alloc)
		: callbacks_(alloc){
	}

	void post(native_gui_callback_entry&& entry){
		if(stopped_.load(std::memory_order_acquire)){
			throw std::runtime_error{"native GUI callback state is stopped"};
		}
		callbacks_.emplace(std::move(entry));
	}

	void consume(){
		if(auto callbacks = callbacks_.fetch()){
			for(auto&& callback : *callbacks){
				callback.exec();
			}
		}
	}

	void stop() noexcept{
		stopped_.store(true, std::memory_order_release);
		callbacks_.clear();
	}
};

export
/**
 * @brief Shared resources owned outside an individual scene instance.
 *
 * A `scene` and its forked async scenes share the heap, style manager, cursor
 * collection, object pool, and native communicator stored here. Access to GUI
 * resource managers is intended from the scene/UI thread.
 */
struct scene_resources{
	friend scene_base;
	friend scene;
private:
	mr::heap heap{};
	style::style_tree_manager init_style_tree_manager_() const;

	allocator_aware_poly_unique_ptr<native_communicator, mr::heap_allocator<native_communicator>>  communicator_{};
public:
	any_pool<false, mr::unvs_allocator<std::byte>> object_pool{};

	UI_MAIN_THREAD_ACCESS_ONLY style::style_tree_manager style_tree_manager{};
	UI_MAIN_THREAD_ACCESS_ONLY sound::manager sound_manager{};
	UI_MAIN_THREAD_ACCESS_ONLY cursor_collection cursor_collection_manager{};
	UI_MAIN_THREAD_ACCESS_ONLY react_flow::node_holder_pinned<i18n_text_root_node> i18n_prov{};

	UI_MAIN_THREAD_ACCESS_ONLY audio::audio_channel audio_channel_;

	/**
	 * @brief Install the backend communicator used by GUI elements.
	 *
	 * For GLFW this is normally called during scene setup with the native window
	 * handle and the window-thread dispatcher.
	 */
	template <std::derived_from<native_communicator> Ty, typename ...Args>
	void set_native_communicator(Args&& ...args){
		communicator_ = mo_yanxi::make_allocate_aware_poly_unique<Ty, native_communicator>(
			mr::heap_allocator<native_communicator>{heap.get()}, std::forward<Args>(args)...);
	}

	[[nodiscard]] audio::audio_channel& audio_channel() noexcept{
		return audio_channel_;
	}

	[[nodiscard]] const audio::audio_channel& audio_channel() const noexcept{
		return audio_channel_;
	}

	template <typename Target, std::invocable<Target&, std::string_view> ApplyFn>
	decltype(auto) bind_i18n(i18n::text_subscription&& subscription, Target& tgt, ApplyFn&& fn) noexcept{
		return i18n::bind_i18n_text(i18n_prov.node, tgt, std::move(subscription), std::forward<ApplyFn>(fn));
	}

	scene_resources() = delete;

	[[nodiscard]] explicit scene_resources(audio::audio_channel audio_channel)
		: audio_channel_(audio_channel){
	}

	[[nodiscard]] scene_resources(audio::audio_channel audio_channel, mr::heap&& heap)
		: heap(std::move(heap)),
		  style_tree_manager(init_style_tree_manager_()),
		  audio_channel_(audio_channel){
	}

	[[nodiscard]] scene_resources(audio::audio_channel audio_channel, mr::arena_id_t arena_id)
		: scene_resources{audio_channel, mr::heap{arena_id}}{
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

struct scene_deleter{
	static void operator()(scene* ptr) noexcept;;
};

struct async_async_task_queue{
	using elem_async_task_ptr = allocator_aware_poly_unique_ptr<basic_elem_async_task, mr::heap_allocator<basic_elem_async_task>>;

private:
	ccur::mpsc_queue<elem_async_task_ptr, std::deque<
			elem_async_task_ptr, mr::heap_allocator<elem_async_task_ptr>
		>> element_async_tasks_pending_{};
	mr::heap_allocator<basic_elem_async_task> task_allocator_{};

	std::atomic<basic_elem_async_task*> current_done_task_{};
	std::deque<elem_async_task_runtime_state> task_runtime_states_{};
	std::unique_ptr<scene, scene_deleter> forked_scene_{};
	std::jthread element_async_task_thread_{};

public:
	[[nodiscard]] async_async_task_queue(const mr::heap_allocator<elem_async_task_ptr>& alloc,
	                                     std::unique_ptr<scene, scene_deleter>&& scene)
		:
		element_async_tasks_pending_(alloc),
		task_allocator_(alloc),
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
		for(auto& state : task_runtime_states_){
			state.stop_source.request_stop();
			state.active.store(false, std::memory_order_release);
		}
		element_async_task_thread_.request_stop();
		element_async_tasks_pending_.notify();

		current_done_task_.store(nullptr, std::memory_order_relaxed);
		current_done_task_.notify_one();

		if(element_async_task_thread_.joinable()
			&& element_async_task_thread_.get_id() != std::this_thread::get_id()){
			element_async_task_thread_.join();
		}
		element_async_tasks_pending_.clear();
		current_done_task_.store(nullptr, std::memory_order_relaxed);
		exchange_scene_thread(*forked_scene_, std::this_thread::get_id());
	}

private:
	static void log_async_task_exception(std::exception_ptr exception){
		if(exception == nullptr){
			return;
		}
		try{
			std::rethrow_exception(std::move(exception));
		}catch(const std::exception& e){
			log::error({"GUI"}, "elem async task failed: {}", e.what());
		}catch(...){
			log::error({"GUI"}, "elem async task failed with an unknown exception");
		}
	}

	[[nodiscard]] elem_async_task_runtime_state& create_task_runtime_state(){
		auto& state = task_runtime_states_.emplace_back();
		state.progress_current.store(0u, std::memory_order_release);
		state.progress_total.store(0u, std::memory_order_release);
		state.active.store(true, std::memory_order_release);
		return state;
	}

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
			std::exception_ptr ui_exception{};
			try{
				elem* owner = elem_async_task->owner();
				if(owner != nullptr && !elem_async_task->stop_requested()){
					if(elem_async_task->has_exception()){
						if(!elem_async_task->on_error(*owner, *forked_scene_, elem_async_task->exception())){
							log_async_task_exception(elem_async_task->exception());
						}
					}else{
						elem_async_task->on_done(*owner, *forked_scene_);
					}
				}else if(elem_async_task->has_exception()){
					log_async_task_exception(elem_async_task->exception());
				}
			}catch(...){
				ui_exception = std::current_exception();
			}
			elem_async_task->mark_finished();
			exchange_scene_thread(*forked_scene_, last);
			current_done_task_.store(nullptr, std::memory_order_relaxed);
			current_done_task_.notify_one();
			if(ui_exception != nullptr){
				std::rethrow_exception(ui_exception);
			}
		}
	}

	void cancel_owner(const elem* owner) noexcept{
		(void)owner;
	}

	template <std::derived_from<elem> E, std::invocable<E&> Prov>
	elem_async_task_handle post(E& e, Prov&& prov){
		using task_type = std::decay_t<std::invoke_result_t<Prov&&, E&>>;
		static_assert(std::derived_from<task_type, basic_elem_async_task>);
		auto task = std::invoke_r<task_type>(std::forward<Prov>(prov), e);
		elem_ref<> owner_ref{e};
		elem_async_task_runtime_state* task_runtime_state{};
		try{
			task_runtime_state = std::addressof(this->create_task_runtime_state());
			task.bind_async_owner(std::move(owner_ref), elem_ref_access::stop_token(std::addressof(e)), *task_runtime_state);
			element_async_tasks_pending_.push(
				mo_yanxi::make_allocate_aware_poly_unique<task_type, basic_elem_async_task>(
					task_allocator_, std::move(task)));
		}catch(...){
			if(task_runtime_state != nullptr){
				task_runtime_state->active.store(false, std::memory_order_release);
			}
			throw;
		}
		return elem_async_task_handle{*task_runtime_state};
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


	UI_MERGE_ON_JOIN associated_async_sync_task_queue<elem> instant_task_queue_{get_heap_allocator()};
	UI_MERGE_ON_JOIN scene_submodule::action_queue action_queue_{get_heap_allocator()};

	std::unique_ptr<scene_submodule::async_async_task_queue> async_task_queue_{};
	UI_TRANSIENT scene_submodule::input_state input_handler_{get_heap_allocator()};

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

		friend constexpr bool operator==(const update_entry& s, const gui::elem* o) noexcept{
			return s.elem == o;
		}

		friend constexpr bool operator==(const gui::elem* o, const update_entry& s) noexcept{
			return s.elem == o;
		}
	};
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY linear_flat_set<mr::heap_vector<update_entry>> active_update_elems_{get_heap_allocator()};
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY mr::heap_vector<update_entry> active_update_elems_state_changes{get_heap_allocator()};

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
	std::shared_ptr<native_gui_callback_state> native_gui_callbacks_{
		std::make_shared<native_gui_callback_state>(get_heap_allocator<native_gui_callback_entry>())
	};

	struct retired_elem_record{
		elem* element{};
	};

	mr::heap_vector<retired_elem_record> retired_elements_{get_heap_allocator<retired_elem_record>()};

protected:
	UI_TRANSIENT tooltip::tooltip_manager tooltip_manager_{get_heap_allocator()};
	UI_TRANSIENT overlay_manager overlay_manager_{get_heap_allocator()};
	UI_TRANSIENT cursor_drawer current_cursor_drawers_{};
	UI_TRANSIENT layer_altitude_record layer_altitude_record_{get_heap_allocator<unsigned>()};
	elem* scene_root_{};

	[[nodiscard]] scene_base() = default;

	explicit(false) scene_base(scene_resources& resources, renderer_frontend&& renderer);

	~scene_base(){
		native_gui_callbacks_->stop();
		async_task_queue_ = nullptr;
		input_communicate_async_task_queue_.clear();
		instant_task_queue_.clear();
		tooltip_manager_.clear();
		overlay_manager_.clear();
		collect_retired_elements();
		assert(retired_elements_.empty() && "scene destroyed while retired elements are still externally referenced");
	}

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

	/**
	 * @brief Queue used by GUI code to send work out to the renderer/main loop side.
	 *
	 * The default examples use channel 0 for post-render updates such as
	 * compositor parameter changes.
	 */
	auto& get_output_communicate_async_task_queue(std::size_t channel){
		return output_communicate_async_task_queues_.at(channel);
	}

	/**
	 * @brief Queue consumed on the GUI thread before scene update work.
	 */
	auto& get_input_communicate_async_task_queue(){
		return input_communicate_async_task_queue_;
	}

	/**
	 * @brief Create an owner-bound native clipboard completion.
	 *
	 * The returned request may be completed from a native/window thread. It posts
	 * a GUI callback that executes only if `owner` is still live.
	 */
	template <std::derived_from<elem> E, std::invocable<E&, std::string> Fn>
	native_clipboard_request make_native_clipboard_request(E& owner, Fn&& on_ready){
		assert(is_on_scene_thread(*this));
		assert(std::addressof(owner.get_scene()) == this);
		auto callbacks = native_gui_callbacks_;
		elem_ref<> owner_ref{owner};

		return native_clipboard_request{
			.complete = [
				callbacks = std::move(callbacks),
				owner_ref = std::move(owner_ref),
				on_ready = std::forward<Fn>(on_ready)
			](std::string text) mutable {
				callbacks->post(native_gui_callback_entry{
					.owner = std::move(owner_ref),
					.callback = [
						on_ready = std::move(on_ready),
						text = std::move(text)
					](elem& owner) mutable {
						std::invoke(std::move(on_ready), static_cast<E&>(owner), std::move(text));
					}
				});
			}
		};
	}

	[[nodiscard]] unsigned long long get_current_frame() const noexcept{
		return current_frame_;
	}

	[[nodiscard]] double get_current_time() const{
		return current_time_;
	}

	[[nodiscard]] altitude_t get_max_element_altitude() const noexcept{
		return layer_altitude_record_.get_max();
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
		active_update_elems_state_changes.emplace_back(&p, channel);

	}

	void erase_update(const elem* p, update_channel channel) noexcept {
		assert(is_on_scene_thread(*this));
		//note that the const cast here is safe
		active_update_elems_state_changes.emplace_back(const_cast<elem*>(p), update_channel::none, channel);
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

	/**
	 * @brief Renderer frontend for the current GUI thread.
	 */
	[[nodiscard]] renderer_frontend& renderer() noexcept{
		assert(is_on_scene_thread(*this));
		return renderer_;
	}

	/**
	 * @brief Shared scene resources.
	 */
	[[nodiscard]] scene_resources& resources() const noexcept{
		assert(resources_ != nullptr);
		return *resources_;
	}

	void request_audio_from_scene_proxy(
		const elem& element,
		const sound::play_event event) const{
		assert(is_on_scene_thread(*this));
		input_handler_.request_audio(std::addressof(element), event, sound::request_origin::input_fallback);
	}

	void request_semantic_audio_from_scene_proxy(
		const elem& element,
		const sound::play_event event) const{
		assert(is_on_scene_thread(*this));
		input_handler_.request_semantic_audio(std::addressof(element), event);
	}

	void record_state_audio_delta(
		const elem& element,
		const sound::state_family family,
		const bool before,
		const bool after) const{
		assert(is_on_scene_thread(*this));
		input_handler_.record_state_audio_delta(std::addressof(element), family, before, after);
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
		return input_handler_.get_cursor_pos();
	}

	[[nodiscard]] std::span<elem * const> get_inbounds() const noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.get_inbounds();
	}

	template <std::derived_from<input_handle::key_mapping_interface> Mapping>
	Mapping& register_input_mapping(std::string_view name){
		assert(is_on_scene_thread(*this));
		return input_handler_.template register_input_mapping<Mapping>(name);
	}

	template <std::derived_from<input_handle::key_mapping_interface> Mapping = input_handle::key_mapping_interface>
	[[nodiscard]] Mapping* find_input_mapping(std::string_view name) const noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.template find_input_mapping<Mapping>(name);
	}

	bool erase_input_mapping(std::string_view name){
		assert(is_on_scene_thread(*this));
		return input_handler_.erase_input_mapping(name);
	}

	[[nodiscard]] bool is_mouse_pressed(input_handle::mouse mouse_button_code) const noexcept{
		assert(is_on_scene_thread(*this));
		return input_handler_.is_mouse_pressed(mouse_button_code);
	}

	void capture_mouse(elem& target, input_handle::mouse mouse_button_code, math::vec2 press_scene_pos);

	[[nodiscard]] bool has_scroll_focus() const noexcept{
		return input_handler_.has_scroll_focus();
	}

	[[nodiscard]] bool has_cursor_focus() const noexcept{
		return input_handler_.has_cursor_focus();
	}

	void overwrite_last_inbound_click_quiet(elem* elem) noexcept{
		assert(is_on_scene_thread(*this));
		input_handler_.overwrite_last_inbound_click_quiet(elem);
	}

	void retire_elem(elem* target) noexcept;

	void collect_retired_elements() noexcept;

	[[nodiscard]] react_flow::manager& get_react_flow() noexcept{
		assert(is_on_scene_thread(*this));
		return react_flow_;
	}

	friend struct react_flow_create_access;

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

	/**
	 * @brief Close the overlay containing `overlay_elem`, if present.
	 */
	void close_overlay(const elem* overlay_elem){
		overlay_manager_.truncate(overlay_elem);
		this->request_cursor_update();
	}

private:
	template <typename AddFn>
	decltype(auto) react_flow_add_node_(const elem* owner, AddFn&& add){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"create node not on main ui thread"};
		}

		auto& node = std::invoke(std::forward<AddFn>(add), react_flow_);
		if(owner != nullptr){
			try{
				elem_owned_nodes_.insert({owner, std::addressof(static_cast<react_flow::node&>(node))});
			}catch(...){
				react_flow_.erase_node(node);
				throw;
			}
		}
		return node;
	}

	bool react_flow_erase_node_(react_flow::node& node){
		if(!is_on_scene_thread(*this)){
			throw std::runtime_error{"erase node not on main ui thread"};
		}
		const auto* node_addr = std::addressof(node);
		if(std::ranges::any_of(elem_owned_nodes_, [node_addr](const auto& entry){
			return entry.second == node_addr;
		})){
			throw std::invalid_argument{"cannot manually erase element-owned react-flow node"};
		}
		return react_flow_.erase_node(node);
	}

	void async_push_elem_to_action_pending(elem* e){
		action_queue_.push(e);
	}

#pragma region Events

public:
	void update_cursor_type();

	void update(double delta_in_tick);

	events::dispatch_result handle_input_event(const input_handle::input_event_variant& event);

	events::dispatch_result on_esc();

	void request_cursor_update() noexcept{
		input_handler_.request_cursor_update();
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
/**
 * @brief Main retained GUI scene.
 *
 * A scene owns the element tree root, input focus state, update queues, overlay
 * and tooltip managers, react-flow graph, and the renderer frontend used by GUI
 * drawing. Public methods that mutate GUI state assert the scene/UI thread.
 */
struct scene : scene_base{
	friend elem;
	friend ui_manager;

protected:

	void init_root() const;

public:

	[[nodiscard]] explicit scene(const scene_shared_resources& resources)
		: scene_base(resources){
	}

	[[nodiscard]] scene(scene_resources& resources, renderer_frontend&& renderer)
		: scene_base(resources, std::move(renderer)){
	}

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

	/**
	 * @brief Post an element-owned async task.
	 *
	 * When async posting is enabled, the task is processed on the forked scene
	 * worker and completed back on the GUI thread. Otherwise it is processed
	 * synchronously on the current scene.
	 */
	template <std::derived_from<elem> E, std::invocable<E&> Prov>
	elem_async_task_handle post_elem_async_task(E& e, Prov&& prov){
		if(async_task_queue_){
			return async_task_queue_->post(e, std::forward<Prov>(prov));
		}else{
			std::derived_from<basic_elem_async_task> auto task = std::invoke(std::forward<Prov>(prov), e);
			task.process(*this);
			if(!task.stop_requested() && !task.has_exception()){
				task.on_done(e, *this);
			}
			if(task.has_exception()){
				std::rethrow_exception(task.exception());
			}
			return elem_async_task_handle{};
		}
	}


	virtual std::unique_ptr<scene, scene_submodule::scene_deleter> fork(){
		auto rst = new scene{static_cast<scene_shared_resources>(*this)};
		try{
			rst->drop_and_reset_communicate_async_task_queue_size(get_output_communicate_async_task_queues_size());
		}catch(...){
			delete rst;
			throw;
		}

		return std::unique_ptr<scene, scene_submodule::scene_deleter>{rst};
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

	/**
	 * @brief Allocate a detached element owned by this scene.
	 *
	 * The returned `elem_ptr` must be inserted into a group or otherwise stored.
	 * Most container code should prefer `create_back()` / `emplace_back()` so the
	 * element and its cell metadata are created together.
	 */
	template <std::derived_from<elem> T, typename ...Args>
	[[nodiscard]] elem_ptr create(Args&& ...args){
		return elem_ptr{*this, nullptr, std::in_place_type<T>, std::forward<Args>(args)...};
	}

	/**
	 * @brief Create an overlay element with an initializer function.
	 */
	template <invocable_elem_init_func Fn, typename... Args>
	auto create_overlay(const overlay_layout layout, Fn&& fn, Args&&... args){
		auto result = overlay_manager_.push_back(
			layout,
			elem_ptr{*this, nullptr, std::forward<Fn>(fn), std::forward<Args>(args)...});
		this->request_cursor_update();
		return static_cast<overlay_create_result<elem_init_func_create_t<Fn>>>(result);
	}

	/**
	 * @brief Emplace an overlay element from constructor arguments.
	 */
	template <typename T, typename... Args>
		requires (constructible_elem<T, Args&&...>)
	overlay_create_result<T> emplace_overlay(const overlay_layout layout, Args&&... args){
		auto result = overlay_manager_.push_back(
			layout,
			elem_ptr{*this, nullptr, std::in_place_type<T>, std::forward<Args>(args)...});
		this->request_cursor_update();
		return static_cast<overlay_create_result<T>>(result);
	}
};

bool is_on_scene_thread(const scene_base& scene) noexcept{
	return std::this_thread::get_id() == scene.ui_main_thread_id;
}

}
