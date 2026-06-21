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
import :async_task;
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
	async_reply<std::string> reply{};

	void set_value(std::string text) &&{
		std::move(reply).set_value(std::move(text));
	}

	void set_error(std::exception_ptr exception) &&{
		std::move(reply).set_error(std::move(exception));
	}

	void set_cancelled() &&{
		std::move(reply).set_cancelled();
	}
};

export
template <typename T, std::derived_from<elem> E, typename ValueFn, typename ErrorFn = std::nullptr_t, typename CancelFn = std::nullptr_t>
[[nodiscard]] async_reply<T> make_gui_reply(
	E& owner,
	ValueFn&& value_fn,
	ErrorFn&& error_fn = nullptr,
	CancelFn&& cancel_fn = nullptr);

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

	void begin_shutdown() noexcept{
		begin_shutdown_impl();
	}

protected:
	virtual void begin_shutdown_impl() noexcept{
	}

	//TODO sound interface
};

struct scene_shared_resources;

export
/**
 * @brief Shared resources owned outside an individual scene instance.
 *
 * A `scene` and its forked async scenes share the heap, style manager, cursor
 * collection, object pool, and native communicator stored here. Access to GUI
 * resource managers is intended from the scene/UI thread.
 */
struct scene_resources{
	friend scene_shared_resources;
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

template <typename Reply, typename Result, bool IsVoid = std::is_void_v<Result>>
struct async_reply_object_for_impl;

template <typename Reply, typename Result>
struct async_reply_object_for_impl<Reply, Result, false> : std::bool_constant<
	!std::is_reference_v<Result>
	&& requires(Reply& reply, Result result, std::exception_ptr exception){
		std::move(reply).set_value(std::move(result));
		std::move(reply).set_error(std::move(exception));
		std::move(reply).set_cancelled();
	}> {};

template <typename Reply, typename Result>
struct async_reply_object_for_impl<Reply, Result, true> : std::bool_constant<
	requires(Reply& reply, std::exception_ptr exception){
		std::move(reply).set_value();
		std::move(reply).set_error(std::move(exception));
		std::move(reply).set_cancelled();
	}> {};

template <typename Reply, typename Result>
concept async_reply_object_for = async_reply_object_for_impl<std::decay_t<Reply>, Result>::value;

template <typename ValueFn, typename E, typename Result, bool IsVoid = std::is_void_v<Result>>
struct gui_reply_value_callback_for_impl;

template <typename ValueFn, typename E, typename Result>
struct gui_reply_value_callback_for_impl<ValueFn, E, Result, false> : std::bool_constant<
	std::invocable<std::decay_t<ValueFn>&&, E&, Result>
	> {};

template <typename ValueFn, typename E, typename Result>
struct gui_reply_value_callback_for_impl<ValueFn, E, Result, true> : std::bool_constant<
	std::invocable<std::decay_t<ValueFn>&&, E&>
	> {};

template <typename ValueFn, typename E, typename Result>
concept gui_reply_value_callback_for = gui_reply_value_callback_for_impl<ValueFn, E, Result>::value;

template <typename ErrorFn, typename E>
concept gui_reply_error_callback_for =
	std::same_as<std::decay_t<ErrorFn>, std::nullptr_t>
	|| std::invocable<std::decay_t<ErrorFn>&&, E&, std::exception_ptr>;

template <typename CancelFn, typename E>
concept gui_reply_cancel_callback_for =
	std::same_as<std::decay_t<CancelFn>, std::nullptr_t>
	|| std::invocable<std::decay_t<CancelFn>&&, E&>;

template <typename ProcessFn>
using forked_scene_process_result_t = std::invoke_result_t<std::decay_t<ProcessFn>&, async_task_context&, scene&>;

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

template <
	std::derived_from<elem> E,
	typename ProcessFn,
	typename Reply,
	typename Result = std::invoke_result_t<ProcessFn&, async_task_context&, scene&>,
	bool IsVoid = std::is_void_v<Result>>
struct forked_scene_request_task;

template <std::derived_from<elem> E, typename ProcessFn, typename Reply, typename Result>
struct forked_scene_request_task<E, ProcessFn, Reply, Result, false> : forked_scene_task_base{
	static_assert(!std::is_reference_v<Result>);

	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS Reply reply_;
	std::optional<Result> result_{};

	[[nodiscard]] forked_scene_request_task(ProcessFn process_fn, Reply reply)
		: process_fn_(std::move(process_fn)),
		  reply_(std::move(reply)){
	}

	void process_impl(async_task_context& context, scene& async_scene) override{
		result_.emplace(std::invoke(process_fn_, context, async_scene));
	}

	void on_done(elem&, scene&) override{
		if(result_.has_value()){
			std::move(reply_).set_value(std::move(*result_));
		}else{
			std::move(reply_).set_cancelled();
		}
	}

	bool on_error(elem&, scene&, std::exception_ptr exception) override{
		std::move(reply_).set_error(std::move(exception));
		return true;
	}
};

template <std::derived_from<elem> E, typename ProcessFn, typename Reply, typename Result>
struct forked_scene_request_task<E, ProcessFn, Reply, Result, true> : forked_scene_task_base{
	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS Reply reply_;
	bool processed_{false};

	[[nodiscard]] forked_scene_request_task(ProcessFn process_fn, Reply reply)
		: process_fn_(std::move(process_fn)),
		  reply_(std::move(reply)){
	}

	void process_impl(async_task_context& context, scene& async_scene) override{
		std::invoke(process_fn_, context, async_scene);
		processed_ = true;
	}

	void on_done(elem&, scene&) override{
		if(processed_){
			std::move(reply_).set_value();
		}else{
			std::move(reply_).set_cancelled();
		}
	}

	bool on_error(elem&, scene&, std::exception_ptr exception) override{
		std::move(reply_).set_error(std::move(exception));
		return true;
	}
};

struct async_async_task_queue{
	using async_task_ptr = allocator_aware_poly_unique_ptr<forked_scene_task_base, mr::heap_allocator<forked_scene_task_base>>;

private:
	ccur::mpsc_queue<async_task_ptr, std::deque<
			async_task_ptr, mr::heap_allocator<async_task_ptr>
		>> async_tasks_pending_{};
	mr::heap_allocator<forked_scene_task_base> task_allocator_{};

	std::atomic<forked_scene_task_base*> current_done_task_{};
	std::deque<async_operation_state> task_runtime_states_{};
	std::unique_ptr<scene, scene_deleter> forked_scene_{};
	std::jthread async_task_thread_{};

public:
	[[nodiscard]] async_async_task_queue(const mr::heap_allocator<async_task_ptr>& alloc,
	                                     std::unique_ptr<scene, scene_deleter>&& scene)
		:
		async_tasks_pending_(alloc),
		task_allocator_(alloc),
		forked_scene_((assert(scene != nullptr), std::move(scene))),
		async_task_thread_([this](std::stop_token&& stop_token){
			exchange_scene_thread(*forked_scene_, std::this_thread::get_id());
			async_tasks_process(std::move(stop_token));
		}){
	}

	[[nodiscard]] std::jthread& get_async_task_thread() noexcept{
		return async_task_thread_;
	}

	~async_async_task_queue(){
		for(auto& state : task_runtime_states_){
			state.mark_cancelled();
		}
		async_task_thread_.request_stop();
		async_tasks_pending_.notify();

		current_done_task_.store(nullptr, std::memory_order_relaxed);
		current_done_task_.notify_one();

		if(async_task_thread_.joinable()
			&& async_task_thread_.get_id() != std::this_thread::get_id()){
			async_task_thread_.join();
		}
		async_tasks_pending_.clear();
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

	[[nodiscard]] async_operation_state& create_task_runtime_state(){
		auto& state = task_runtime_states_.emplace_back();
		state.report_progress(0u, 0u);
		return state;
	}

	void async_tasks_process(std::stop_token stop_token){
		while(!stop_token.stop_requested()){
			auto task = async_tasks_pending_.consume([&] noexcept {
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
		if(auto task = current_done_task_.load(std::memory_order_acquire)){
			auto thread = std::this_thread::get_id();
			auto last = exchange_scene_thread(*forked_scene_, thread);
			std::exception_ptr ui_exception{};
			try{
				elem* owner = task->owner();
				if(owner != nullptr && !task->stop_requested()){
					if(task->has_exception()){
						if(!task->on_error(*owner, *forked_scene_, task->exception())){
							log_async_task_exception(task->exception());
						}
					}else{
						task->on_done(*owner, *forked_scene_);
					}
				}else if(task->has_exception()){
					log_async_task_exception(task->exception());
				}
			}catch(...){
				ui_exception = std::current_exception();
			}
			task->mark_finished();
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
	async_operation_handle post(E& e, Prov&& prov){
		using task_type = std::decay_t<std::invoke_result_t<Prov&&, E&>>;
		static_assert(std::derived_from<task_type, forked_scene_task_base>);
		auto task = std::invoke_r<task_type>(std::forward<Prov>(prov), e);
		elem_ref<> owner_ref{e};
		async_operation_state* task_runtime_state{};
		try{
			task_runtime_state = std::addressof(this->create_task_runtime_state());
			task.bind_async_owner(std::move(owner_ref), elem_ref_access::stop_token(std::addressof(e)), *task_runtime_state);
			async_tasks_pending_.push(
				mo_yanxi::make_allocate_aware_poly_unique<task_type, forked_scene_task_base>(
					task_allocator_, std::move(task)));
		}catch(...){
			if(task_runtime_state != nullptr){
				task_runtime_state->mark_cancelled();
			}
			throw;
		}
		return async_operation_handle{*task_runtime_state};
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

protected:
	[[nodiscard]] mr::heap_handle get_heap() const noexcept{
		return resources_->heap.get();
	}


public:
	template <typename T = std::byte>
	[[nodiscard]] mr::heap_allocator<T> get_heap_allocator() const noexcept {
		return mr::heap_allocator<T>{get_heap()};
	}
};

struct scene_base : scene_shared_resources{
	friend elem;
	friend ui_manager;


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

	[[nodiscard]] bool accepts_gui_tasks_() const noexcept{
		return accepting_gui_tasks_.load(std::memory_order_acquire);
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
	std::atomic_bool accepting_gui_tasks_{true};

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
		begin_shutdown();
		async_task_queue_ = nullptr;
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

	std::size_t output_channel_count() const noexcept{
		return output_communicate_async_task_queues_.size();
	}

	void reset_output_channels(std::size_t total_channels){
		output_communicate_async_task_queues_ = decltype(output_communicate_async_task_queues_){std::allocator_arg, get_heap_allocator(), total_channels};
	}

	[[nodiscard]] call_stream_endpoint_ref get_output_communicate_endpoint(std::size_t channel){
		return call_stream_endpoint_ref{std::addressof(output_communicate_async_task_queues_.at(channel))};
	}

	template <typename Fn, typename... Args>
		requires (std::invocable<Fn&&, Args&&...>)
	[[nodiscard]] bool post_output(std::size_t channel, Fn&& fn, Args&&... args){
		return get_output_communicate_endpoint(channel).try_post(
			std::forward<Fn>(fn),
			std::forward<Args>(args)...);
	}

	void consume_output(std::size_t channel){
		output_communicate_async_task_queues_.at(channel).consume();
	}

	[[nodiscard]] async_sync_endpoint_ref<scene&> get_gui_inbox_endpoint() noexcept{
		return async_sync_endpoint_ref<scene&>{std::addressof(input_communicate_async_task_queue_)};
	}

	[[nodiscard]] associated_async_endpoint_ref<elem> get_elem_gui_task_endpoint() noexcept{
		return associated_async_endpoint_ref<elem>{std::addressof(instant_task_queue_)};
	}

	template <std::invocable<scene&> Fn>
	[[nodiscard]] bool try_post_scene_task(Fn&& fn){
		if(!accepting_gui_tasks_.load(std::memory_order_acquire)){
			return false;
		}
		return get_gui_inbox_endpoint().try_post(std::forward<Fn>(fn));
	}

	template <std::invocable<scene&> Fn>
	[[nodiscard]] bool post_gui(Fn&& fn){
		return this->try_post_scene_task(std::forward<Fn>(fn));
	}

	template <std::derived_from<elem> E, std::invocable<E&> Fn>
	[[nodiscard]] bool post_gui(E& owner, Fn&& fn){
		if(!accepting_gui_tasks_.load(std::memory_order_acquire)){
			return false;
		}
		return get_elem_gui_task_endpoint().try_post(owner, std::forward<Fn>(fn));
	}

	template <std::derived_from<elem> E, std::invocable<> Fn>
	[[nodiscard]] bool post_gui(E& owner, Fn&& fn){
		if(!accepting_gui_tasks_.load(std::memory_order_acquire)){
			return false;
		}
		return get_elem_gui_task_endpoint().try_post(owner, std::forward<Fn>(fn));
	}

	void begin_shutdown() noexcept{
		const bool was_accepting = accepting_gui_tasks_.exchange(false, std::memory_order_acq_rel);
		input_communicate_async_task_queue_.close();
		instant_task_queue_.close();
		if(was_accepting && resources_ != nullptr && resources_->communicator_){
			resources_->communicator_->begin_shutdown();
		}
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

		return native_clipboard_request{
			.reply = gui::make_gui_reply<std::string>(
				owner,
				[on_ready = std::forward<Fn>(on_ready)](E& live_owner, std::string text) mutable {
					std::invoke(std::move(on_ready), live_owner, std::move(text));
				})
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
		(void)this->post_gui(e, std::forward<Fn>(fn));
	}

	template <std::derived_from<elem> E, std::invocable<> Fn>
	void post(E& e, Fn&& fn){
		(void)this->post_gui(e, std::forward<Fn>(fn));
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
	void consume_gui_inbox_from(scene& source){
		input_communicate_async_task_queue_.consume(source);
	}

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

	void enable_forked_scene_tasks(bool enable);

	/**
	 * @brief Run work on the forked scene and complete a one-shot reply on the GUI thread.
	 *
	 * `process_fn(context, async_scene)` runs on the forked scene worker when
	 * async posting is enabled, or synchronously on this scene otherwise. Its
	 * return value is delivered through `reply` only if `owner` is still live.
	 */
	template <std::derived_from<elem> E, typename ProcessFn, typename Reply>
		requires std::invocable<std::decay_t<ProcessFn>&, async_task_context&, scene&>
		      && scene_submodule::async_reply_object_for<
			      Reply,
			      scene_submodule::forked_scene_process_result_t<ProcessFn>>
	async_operation_handle request_forked(E& owner, ProcessFn&& process_fn, Reply&& reply){
		using process_type = std::decay_t<ProcessFn>;
		using reply_type = std::decay_t<Reply>;
		using task_type = scene_submodule::forked_scene_request_task<E, process_type, reply_type>;

		process_type process{std::forward<ProcessFn>(process_fn)};
		reply_type completion{std::forward<Reply>(reply)};

		if(!accepts_gui_tasks_()){
			std::move(completion).set_cancelled();
			return async_operation_handle{};
		}

		if(async_task_queue_){
			return async_task_queue_->post(owner, [
				process = std::move(process),
				completion = std::move(completion)
			](E&) mutable {
				return task_type{std::move(process), std::move(completion)};
			});
		}

		task_type task{std::move(process), std::move(completion)};
		task.process(*this);
		if(!task.stop_requested()){
			if(task.has_exception()){
				(void)task.on_error(owner, *this, task.exception());
			}else{
				task.on_done(owner, *this);
			}
		}
		task.mark_finished();
		return async_operation_handle{};
	}

	template <
		std::derived_from<elem> E,
		typename ProcessFn,
		typename ValueFn,
		typename ErrorFn = std::nullptr_t,
		typename CancelFn = std::nullptr_t>
		requires std::invocable<std::decay_t<ProcessFn>&, async_task_context&, scene&>
		      && scene_submodule::gui_reply_value_callback_for<
			      ValueFn,
			      E,
			      scene_submodule::forked_scene_process_result_t<ProcessFn>>
		      && scene_submodule::gui_reply_error_callback_for<ErrorFn, E>
		      && scene_submodule::gui_reply_cancel_callback_for<CancelFn, E>
	async_operation_handle request_forked(
		E& owner,
		ProcessFn&& process_fn,
		ValueFn&& on_value,
		ErrorFn&& on_error = nullptr,
		CancelFn&& on_cancel = nullptr){
		using result_type = scene_submodule::forked_scene_process_result_t<ProcessFn>;
		auto reply = gui::make_gui_reply<result_type>(
			owner,
			std::forward<ValueFn>(on_value),
			std::forward<ErrorFn>(on_error),
			std::forward<CancelFn>(on_cancel));
		return this->request_forked(owner, std::forward<ProcessFn>(process_fn), std::move(reply));
	}

	//TODO since them share the same heap, using mimalloc instead of operator new?
	virtual std::unique_ptr<scene, scene_submodule::scene_deleter> fork(){
		auto rst = new scene{static_cast<scene_shared_resources>(*this)};
		try{
			rst->reset_output_channels(output_channel_count());
		}catch(...){
			delete rst;
			throw;
		}

		return std::unique_ptr<scene, scene_submodule::scene_deleter>{rst};
	}

	virtual void join(scene&& scene){
		consume_gui_inbox_from(scene);
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

namespace scene_submodule{

template <std::derived_from<elem> E>
struct gui_reply_target{
	scene* target_scene_{};
	std::stop_token owner_lifetime_{};
	elem_ref<E> owner_ref_{};

	[[nodiscard]] explicit gui_reply_target(E& owner)
		: target_scene_(std::addressof(owner.get_scene())),
		  owner_lifetime_(owner.lifetime_stop_token()),
		  owner_ref_(owner){
	}

	gui_reply_target(const gui_reply_target&) = delete;
	gui_reply_target& operator=(const gui_reply_target&) = delete;
	[[nodiscard]] gui_reply_target(gui_reply_target&&) noexcept = default;
	gui_reply_target& operator=(gui_reply_target&&) noexcept = default;

	template <typename Fn>
	void dispatch(Fn&& fn) &&{
		if(owner_lifetime_.stop_requested() || target_scene_ == nullptr){
			return;
		}

		auto run = [
			owner_ref = std::move(owner_ref_),
			owner_lifetime = std::move(owner_lifetime_),
			fn = std::forward<Fn>(fn)
		]() mutable {
			if(owner_lifetime.stop_requested()){
				return;
			}
			if(auto* live_owner = owner_ref.get_live()){
				std::invoke(std::move(fn), *live_owner);
			}
		};

		if(is_on_scene_thread(*target_scene_)){
			std::invoke(std::move(run));
			return;
		}

		(void)target_scene_->post_gui([run = std::move(run)](scene&) mutable {
			std::invoke(std::move(run));
		});
	}
};

}

export
template <typename T, std::derived_from<elem> E, typename ValueFn, typename ErrorFn, typename CancelFn>
[[nodiscard]] async_reply<T> make_gui_reply(
	E& owner,
	ValueFn&& value_fn,
	ErrorFn&& error_fn,
	CancelFn&& cancel_fn){
	static_assert(scene_submodule::gui_reply_value_callback_for<ValueFn, E, T>);
	static_assert(scene_submodule::gui_reply_error_callback_for<ErrorFn, E>);
	static_assert(scene_submodule::gui_reply_cancel_callback_for<CancelFn, E>);

	auto make_target = [&owner]{
		return scene_submodule::gui_reply_target<E>{owner};
	};

	auto error_callback = [
		target = make_target(),
		error_fn = std::forward<ErrorFn>(error_fn)
	](std::exception_ptr exception) mutable {
		if constexpr(!std::same_as<std::decay_t<ErrorFn>, std::nullptr_t>){
			std::move(target).dispatch([
				error_fn = std::move(error_fn),
				exception = std::move(exception)
			](E& live_owner) mutable {
				std::invoke(std::move(error_fn), live_owner, std::move(exception));
			});
		}
	};

	auto cancel_callback = [
		target = make_target(),
		cancel_fn = std::forward<CancelFn>(cancel_fn)
	]() mutable {
		if constexpr(!std::same_as<std::decay_t<CancelFn>, std::nullptr_t>){
			std::move(target).dispatch([cancel_fn = std::move(cancel_fn)](E& live_owner) mutable {
				std::invoke(std::move(cancel_fn), live_owner);
			});
		}
	};

	if constexpr(std::is_void_v<T>){
		return gui::make_async_reply<void>(
			[
				target = make_target(),
				value_fn = std::forward<ValueFn>(value_fn)
			]() mutable {
				std::move(target).dispatch([value_fn = std::move(value_fn)](E& live_owner) mutable {
					std::invoke(std::move(value_fn), live_owner);
				});
			},
			std::move(error_callback),
			std::move(cancel_callback));
	}else{
		return gui::make_async_reply<T>(
			[
				target = make_target(),
				value_fn = std::forward<ValueFn>(value_fn)
			](T value) mutable {
				std::move(target).dispatch([
					value_fn = std::move(value_fn),
					value = std::move(value)
				](E& live_owner) mutable {
					std::invoke(std::move(value_fn), live_owner, std::move(value));
				});
			},
			std::move(error_callback),
			std::move(cancel_callback));
	}
}

bool is_on_scene_thread(const scene_base& scene) noexcept{
	return std::this_thread::get_id() == scene.ui_main_thread_id;
}

}
