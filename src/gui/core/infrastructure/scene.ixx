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
import mo_yanxi.audio;

export import mo_yanxi.gui.util;
export import mo_yanxi.gui.style.tree.manager;
export import mo_yanxi.gui.sound.manager;
export import mo_yanxi.audio;

export import mo_yanxi.input_handle;
export import mo_yanxi.input_handle.input_event_queue;
export import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.fx.config;

export import mo_yanxi.react_flow;
export import mo_yanxi.i18n.text_tree.react_flow;

import mo_yanxi.flat_set;
import mo_yanxi.fixed_vector;
import mo_yanxi.double_buffer;

namespace mo_yanxi::gui{
std::thread::id exchange_scene_thread(scene& s, std::thread::id id);

BITMASK_OPS(export, elem_tree_channel);

export struct ui_manager;
export struct elem;

export using i18n_text_root_node = mo_yanxi::i18n::i18n_text_root_node;

namespace util{
void update_insert(elem& e, update_channel channel);
void update_erase(const elem& e, update_channel channel);
}

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
	async_operation_handle request_clipboard(E& owner, Fn&& on_ready);

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
	 * handle and the window-thread output queue.
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

}

struct scene_shared_resources{
	friend scene;

private:
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

private:
	[[nodiscard]] mr::heap_handle get_heap() const noexcept{
		return resources_->heap.get();
	}


public:
	template <typename T = std::byte>
	[[nodiscard]] mr::heap_allocator<T> get_heap_allocator() const noexcept {
		return mr::heap_allocator<T>{get_heap()};
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
struct scene : scene_shared_resources{
	friend elem;
	friend elem_ptr;
	friend ui_manager;
	friend native_communicator;
	friend struct react_flow_create_access;
	friend struct scene_submodule::scene_deleter;
	friend struct scene_submodule::forked_scene_worker;
	friend std::thread::id exchange_scene_thread(scene& s, std::thread::id id);
	friend bool is_on_scene_thread(const scene& scene) noexcept;
	friend void util::update_insert(elem& e, update_channel channel);
	friend void util::update_erase(const elem& e, update_channel channel);

private:
	UI_MAIN_THREAD_ACCESS_ONLY UI_TRANSIENT renderer_frontend renderer_{};
	std::thread::id ui_main_thread_id{std::this_thread::get_id()};

#ifdef SCENE_REFERENCE_COUNT_CHECK
	UI_TRANSIENT std::size_t element_on_this_scene_{};
	struct check_on_destruction{
		scene* scene{nullptr};
		~check_on_destruction(){
			assert(scene->element_on_this_scene_ == 0);
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

	FORCE_INLINE inline void check_ref_count_zero_() noexcept{
#ifdef SCENE_REFERENCE_COUNT_CHECK
		assert(element_on_this_scene_ == 0);
#endif
	}

	[[nodiscard]] bool accepts_gui_tasks_() const noexcept{
		return accepting_gui_tasks_.load(std::memory_order_acquire);
	}

	[[nodiscard]] async_operation_state_ptr create_async_operation_state(){
		if(!async_operation_state_pool_){
			throw std::runtime_error{"scene async operation state pool is unavailable"};
		}
		return async_operation_state_pool_->create_operation();
	}

	UI_TRANSIENT unsigned long long current_frame_{};
	UI_TRANSIENT double current_time_{};

	UI_TRANSIENT elem_tree_channel display_state_changed_channel_{};


	UI_MERGE_ON_JOIN associated_async_sync_task_queue<elem> elem_gui_tasks_{get_heap_allocator()};
	UI_MERGE_ON_JOIN scene_submodule::action_queue action_queue_{get_heap_allocator()};

	async_operation_state_pool_ptr async_operation_state_pool_{std::in_place, get_heap_allocator()};
	std::unique_ptr<scene_submodule::forked_scene_worker> forked_scene_worker_{};
	UI_TRANSIENT scene_submodule::input_state input_handler_{get_heap_allocator()};

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
	};
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY linear_flat_set<mr::heap_vector<update_entry>> active_update_elems_{get_heap_allocator()};
	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY mr::heap_vector<update_entry> active_update_elems_state_changes{get_heap_allocator()};

#pragma endregion

	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY react_flow::manager react_flow_{};

	UI_MERGE_ON_JOIN UI_MAIN_THREAD_ACCESS_ONLY std::unordered_multimap<
		const elem*, react_flow::node*,
		std::hash<const elem*>, std::equal_to<const elem*>,
		mr::heap_allocator<std::pair<const elem* const, react_flow::node*>>>
	elem_owned_nodes_{get_heap_allocator()};

	UI_MERGE_ON_JOIN fixed_vector<call_stream_task_queue, mr::heap_allocator<call_stream_task_queue>> output_channels_{};
	async_sync_task_queue<scene&> gui_inbox_{get_heap_allocator()};
	std::atomic_bool accepting_gui_tasks_{true};

	struct retired_elem_record{
		elem* element{};
	};

	mr::heap_vector<retired_elem_record> retired_elements_{get_heap_allocator<retired_elem_record>()};

	UI_TRANSIENT tooltip::tooltip_manager tooltip_manager_{get_heap_allocator()};
	UI_TRANSIENT overlay_manager overlay_manager_{get_heap_allocator()};
	UI_TRANSIENT cursor_drawer current_cursor_drawers_{};
	UI_TRANSIENT layer_altitude_record layer_altitude_record_{get_heap_allocator<unsigned>()};
	elem* scene_root_{};

	[[nodiscard]] scene() = default;

public:
	explicit(false) scene(scene_resources& resources, renderer_frontend&& renderer);

	virtual ~scene(){
		begin_shutdown();
		forked_scene_worker_ = nullptr;
		tooltip_manager_.clear();
		overlay_manager_.clear();
		collect_retired_elements();
		assert(retired_elements_.empty() && "scene destroyed while retired elements are still externally referenced");
	}

protected:
	[[nodiscard]] explicit scene(const scene_shared_resources& resources)
		: scene_shared_resources(resources){
	}

public:

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
		return output_channels_.size();
	}

	void reset_output_channels(std::size_t total_channels){
		output_channels_ = decltype(output_channels_){std::allocator_arg, get_heap_allocator(), total_channels};
	}

	[[nodiscard]] call_stream_task_queue& output_queue(std::size_t channel){
		return output_channels_.at(channel);
	}

	template <typename Fn, typename... Args>
		requires (std::invocable<Fn&&, Args&&...>)
	[[nodiscard]] bool post_output(std::size_t channel, Fn&& fn, Args&&... args){
		return output_channels_.at(channel).try_post(
			std::forward<Fn>(fn),
			std::forward<Args>(args)...);
	}

	void consume_output(std::size_t channel){
		output_channels_.at(channel).consume();
	}

	template <std::invocable<scene&> Fn>
	[[nodiscard]] bool post_gui(Fn&& fn){
		if(!accepting_gui_tasks_.load(std::memory_order_acquire)){
			return false;
		}
		return gui_inbox_.try_post(std::forward<Fn>(fn));
	}

	template <std::derived_from<elem> E, typename Fn>
		requires (std::invocable<Fn&&, E&> || std::invocable<Fn&&>)
	[[nodiscard]] bool post_gui(E& owner, Fn&& fn){
		if(!accepting_gui_tasks_.load(std::memory_order_acquire)){
			return false;
		}
		return elem_gui_tasks_.try_post(owner, std::forward<Fn>(fn));
	}

	void begin_shutdown() noexcept{
		const bool was_accepting = accepting_gui_tasks_.exchange(false, std::memory_order_acq_rel);
		gui_inbox_.close();
		elem_gui_tasks_.close();
		if(async_operation_state_pool_){
			async_operation_state_pool_->cancel_all();
		}
		for(auto& channel : output_channels_){
			channel.close();
		}
		if(was_accepting && resources_ != nullptr && resources_->communicator_){
			resources_->communicator_->begin_shutdown();
		}
	}

	std::thread::id exchange_thread_id(std::thread::id id) noexcept{
		return std::exchange(ui_main_thread_id, id);
	}

private:
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
			.binding = async_operation_binding{
				elem_ref<>{owner},
				owner.lifetime_stop_token(),
				this->create_async_operation_state()
			},
			.reply = gui::make_gui_reply<std::string>(
				owner,
				[on_ready = std::forward<Fn>(on_ready)](E& live_owner, std::string text) mutable {
					std::invoke(std::move(on_ready), live_owner, std::move(text));
				})
		};
	}

public:
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

private:
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

public:
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

protected:
	elem_tree_channel check_display_state_changed() noexcept{
		assert(is_on_scene_thread(*this));
		return std::exchange(display_state_changed_channel_, elem_tree_channel{});
	}

private:
	[[nodiscard]] auto* get_memory_resource() const noexcept {
		return get_heap();
	}

public:
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

private:
	void retire_elem(elem* target) noexcept;

	void collect_retired_elements() noexcept;

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

private:
	void consume_gui_inbox_from(scene& source){
		gui_inbox_.consume(source);
	}

	void merge(scene&& target){
		target.tooltip_manager_.clear();
		target.overlay_manager_.clear();


		for (auto&& [lhs, rhs] : std::views::zip(output_channels_, target.output_channels_)){
			lhs.merge(std::move(rhs));
		}

		assert(target.scene_root_ == nullptr && "forked scene roots must be moved before scene state is joined");
		target.collect_retired_elements();
		target.check_ref_count_zero_();
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
		elem_gui_tasks_.merge(std::move(target.elem_gui_tasks_));
	}

protected:
	void set_root(elem& root){
		input_handler_.bind_scene_context(*this);
		scene_root_ = std::addressof(root);
		init_root();
	}

	[[nodiscard]] scene_submodule::input_state& inputs() noexcept{
		return input_handler_;
	}

	[[nodiscard]] const scene_submodule::input_state& inputs() const noexcept{
		return input_handler_;
	}

	[[nodiscard]] tooltip::tooltip_manager& tooltips() noexcept{
		return tooltip_manager_;
	}

	[[nodiscard]] const tooltip::tooltip_manager& tooltips() const noexcept{
		return tooltip_manager_;
	}

	[[nodiscard]] overlay_manager& overlays() noexcept{
		return overlay_manager_;
	}

	[[nodiscard]] const overlay_manager& overlays() const noexcept{
		return overlay_manager_;
	}

	rect draw_cursor(){
		assert(is_on_scene_thread(*this));
		return current_cursor_drawers_.draw(*this, resources_->cursor_collection_manager.get_cursor_size());
	}

	virtual void draw_impl(rect clip){
		throw std::runtime_error{"Draw is not impl"};
	}

private:
	void init_root() const;

public:
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
		using task_type = scene_submodule::forked_scene_request_task<process_type, reply_type>;

		process_type process{std::forward<ProcessFn>(process_fn)};
		reply_type completion{std::forward<Reply>(reply)};

		if(!accepts_gui_tasks_()){
			std::move(completion).set_cancelled();
			return async_operation_handle{};
		}

		if(forked_scene_worker_){
			return forked_scene_worker_->post(owner, task_type{std::move(process), std::move(completion)});
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

	virtual std::unique_ptr<scene, scene_submodule::scene_deleter> fork(){
		using allocator_type = mr::heap_allocator<scene>;
		using allocator_traits = std::allocator_traits<allocator_type>;

		auto alloc = get_heap_allocator<scene>();
		auto* rst = allocator_traits::allocate(alloc, 1);
		try{
			::new (static_cast<void*>(rst)) scene(static_cast<scene_shared_resources>(*this));
		}catch(...){
			allocator_traits::deallocate(alloc, rst, 1);
			throw;
		}

		try{
			rst->reset_output_channels(output_channel_count());
		}catch(...){
			std::destroy_at(rst);
			allocator_traits::deallocate(alloc, rst, 1);
			throw;
		}

		return std::unique_ptr<scene, scene_submodule::scene_deleter>{rst};
	}

	virtual void join(scene&& target){
		consume_gui_inbox_from(target);
		merge(std::move(target));
		assert(target.scene_root_ == nullptr && "target scene must drop all element ownership");
		target.check_ref_count_zero_();
	}

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

	auto make_error_callback = [&]<typename Fn>(Fn&& fn){
		if constexpr(!std::same_as<std::decay_t<ErrorFn>, std::nullptr_t>){
			return [
				target = make_target(),
				error_fn = std::forward<Fn>(fn)
			](std::exception_ptr exception) mutable {
				std::move(target).dispatch([
					error_fn = std::move(error_fn),
					exception = std::move(exception)
				](E& live_owner) mutable {
					std::invoke(std::move(error_fn), live_owner, std::move(exception));
				});
			};
		}else{
			(void)fn;
			return nullptr;
		}
	};

	auto make_cancel_callback = [&]<typename Fn>(Fn&& fn){
		if constexpr(!std::same_as<std::decay_t<CancelFn>, std::nullptr_t>){
			return [
				target = make_target(),
				cancel_fn = std::forward<Fn>(fn)
			]() mutable {
				std::move(target).dispatch([cancel_fn = std::move(cancel_fn)](E& live_owner) mutable {
					std::invoke(std::move(cancel_fn), live_owner);
				});
			};
		}else{
			(void)fn;
			return nullptr;
		}
	};

	auto error_callback = make_error_callback(std::forward<ErrorFn>(error_fn));
	auto cancel_callback = make_cancel_callback(std::forward<CancelFn>(cancel_fn));

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

export
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
	return owner.get_scene().request_forked(
		owner,
		std::forward<ProcessFn>(process_fn),
		std::move(reply));
}

bool is_on_scene_thread(const scene& scene) noexcept{
	return std::this_thread::get_id() == scene.ui_main_thread_id;
}

}
