module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:scene_input;

import :elem_ptr;
import :events;
import :cursor;
import :tooltip_manager;
import :dialog_manager;

import std;
import mo_yanxi.input_handle;
import mo_yanxi.input_handle.input_event_queue;
import mo_yanxi.gui.alloc;
import mo_yanxi.gui.sound.manager;
import mo_yanxi.math.vector2;
import mo_yanxi.double_buffer;
import mo_yanxi.flat_set;

namespace mo_yanxi::gui{

enum struct mouse_capture_owner : std::uint8_t{
	none,
	ui,
	passthrough
};

struct mouse_state{
	math::optional_vec2<float> src{math::nullopt_vec2<float>};
	mouse_capture_owner owner{mouse_capture_owner::none};
	elem* target{};

	void reset(const math::vec2 pos, const mouse_capture_owner owner_value, elem* target_value = nullptr) noexcept{
		src = pos;
		owner = owner_value;
		target = target_value;
	}

	void clear() noexcept{
		src.reset();
		owner = mouse_capture_owner::none;
		target = nullptr;
	}

	[[nodiscard]] bool is_ui_owned() const noexcept{
		return src.has_value() && owner == mouse_capture_owner::ui;
	}

	[[nodiscard]] bool is_passthrough_owned() const noexcept{
		return src.has_value() && owner == mouse_capture_owner::passthrough;
	}

	[[nodiscard]] elem* capture_target() const noexcept{
		return is_ui_owned() ? target : nullptr;
	}

	explicit operator bool() const noexcept{
		return src.has_value();
	}
};

namespace scene_submodule{

enum struct input_key_result{
	handled,
	unhandled,
	esc_required
};

struct input_state;

struct scene_input_dispatcher{
private:
	input_state& state_;

	[[nodiscard]] static events::gui_event_type key_event_type(input_handle::act action) noexcept;
	[[nodiscard]] static events::gui_event_type pointer_event_type(input_handle::act action) noexcept;

public:
	[[nodiscard]] explicit scene_input_dispatcher(input_state& state) noexcept;

	[[nodiscard]] input_key_result dispatch_key_input(input_handle::key_set key) const;
	[[nodiscard]] events::dispatch_result dispatch_text_input(char32_t val) const;
	[[nodiscard]] events::dispatch_result dispatch_ime_composition(
		const input_handle::ime_composition_event& event) const;
	[[nodiscard]] events::dispatch_result dispatch_scroll(math::vec2 scroll) const;
	[[nodiscard]] events::dispatch_result dispatch_pointer_button(input_handle::key_set key) const;
	[[nodiscard]] events::dispatch_result dispatch_pointer_drag(
		const mouse_state& state,
		std::uint16_t button_code,
		std::span<elem* const> path) const;
	[[nodiscard]] events::dispatch_result dispatch_cursor_move(
		std::span<elem* const> path,
		std::span<const math::vec2, 2> local_points) const;
};

struct input_state{
	friend scene_input_dispatcher;

	struct cursor_update_result{
		events::dispatch_result result;
		style::cursor_style style;
	};

private:
	struct audio_request{
		const elem* element{};
		sound::play_event event{};
		sound::request_origin origin{};
		std::uint64_t sequence{};
	};

	struct state_audio_delta{
		const elem* element{};
		sound::state_family family{};
		bool before{};
		bool after{};
		bool on_event_route{};
		std::uint64_t sequence{};
	};

	struct net_state_delta{
		const elem* element{};
		sound::state_family family{};
		bool before{};
		bool after{};
		bool on_event_route{};
		std::uint64_t sequence{};
	};

public:
	struct audio_request_transaction{
		const input_state* owner{};

		[[nodiscard]] explicit audio_request_transaction(const input_state& target) noexcept;

		audio_request_transaction(const audio_request_transaction&) = delete;
		audio_request_transaction& operator=(const audio_request_transaction&) = delete;

		[[nodiscard]] audio_request_transaction(audio_request_transaction&& other) noexcept;

		audio_request_transaction& operator=(audio_request_transaction&& other);

		~audio_request_transaction() noexcept(false);
	};

	struct audio_route_scope{
		const input_state* owner{};
		std::span<elem* const> previous_route{};

		[[nodiscard]] audio_route_scope(const input_state& target, std::span<elem* const> route) noexcept;

		audio_route_scope(const audio_route_scope&) = delete;
		audio_route_scope& operator=(const audio_route_scope&) = delete;

		[[nodiscard]] audio_route_scope(audio_route_scope&& other) noexcept;

		audio_route_scope& operator=(audio_route_scope&& other) noexcept;

		~audio_route_scope();
	};

private:
	using state_delta_allocator = std::allocator_traits<mr::heap_allocator<elem*>>::rebind_alloc<state_audio_delta>;
	using net_delta_allocator = std::allocator_traits<mr::heap_allocator<elem*>>::rebind_alloc<net_state_delta>;
	using mouse_mask_type = std::uint16_t;

	static constexpr std::size_t mouse_button_count = std::to_underlying(input_handle::mouse::Count);

	std::array<mouse_state, mouse_button_count> mouse_states_{};
	mouse_mask_type mouse_pressed_mask_{};
	mouse_mask_type passthrough_mouse_mask_{};

	input_handle::input_manager<scene&> inputs_{};
	double_buffer<mr::heap_vector<elem*>> inbounds_{};
	linear_flat_set<mr::heap_vector<elem*>> cursor_event_active_elems_{};

	mutable std::optional<audio_request> input_audio_fallback_{};
	mutable std::optional<audio_request> semantic_audio_request_{};
	mutable mr::heap_vector<state_audio_delta> state_audio_deltas_;
	mutable mr::heap_vector<net_state_delta> net_state_deltas_;
	mutable std::uint32_t audio_request_transaction_depth_{};
	mutable std::uint64_t audio_request_sequence_{};
	mutable std::span<elem* const> current_audio_route_{};
	mutable bool audio_transaction_had_route_{};

	elem* focus_scroll_{nullptr};
	elem* focus_cursor_{nullptr};
	elem* focus_key_{nullptr};
	elem* last_inbound_click_{nullptr};

	bool request_cursor_update_{};

	[[nodiscard]] static mouse_mask_type mouse_bit(std::uint16_t button_code) noexcept{
		assert(button_code < mouse_button_count);
		return static_cast<mouse_mask_type>(mouse_mask_type{1} << button_code);
	}

	void set_mouse_capture(
		std::uint16_t button_code,
		math::vec2 press_scene_pos,
		mouse_capture_owner owner,
		elem* target = nullptr) noexcept;
	void clear_mouse_capture(std::uint16_t button_code) noexcept;
	void clear_all_mouse_captures() noexcept;

public:
	explicit input_state(const mr::heap_allocator<elem*>& alloc) :
		inbounds_{alloc},
		cursor_event_active_elems_{alloc},
		state_audio_deltas_{state_delta_allocator{alloc}},
		net_state_deltas_{net_delta_allocator{alloc}} {
	}

	void begin_audio_request_transaction() const noexcept;
	void end_audio_request_transaction() const;

	[[nodiscard]] audio_request_transaction make_audio_request_transaction() const noexcept{
		return audio_request_transaction{*this};
	}

	[[nodiscard]] audio_route_scope make_audio_route_scope(std::span<elem* const> route) const noexcept{
		return audio_route_scope{*this, route};
	}

	void request_audio(const elem* element, sound::play_event event, sound::request_origin origin) const;
	void request_semantic_audio(const elem* element, sound::play_event event) const;
	void record_state_audio_delta(const elem* element, sound::state_family family, bool before, bool after) const;
	void update_elem_cursor_state(float delta_in_tick, tooltip::tooltip_manager& tooltip) noexcept;
	void play_audio_for_handled(const elem* element, sound::play_event event) const;

	void drop_event_focus(const elem* target) noexcept;
	void drop_elem(const elem* target) noexcept;

	void request_cursor_update() noexcept{
		request_cursor_update_ = true;
	}

	[[nodiscard]] bool cursor_update_requested() const noexcept{
		return request_cursor_update_;
	}

	void overwrite_last_inbound_click_quiet(elem* elem) noexcept{
		last_inbound_click_ = elem;
	}

	void switch_last_inbound_click(elem* elem);

	void input_inbound(bool is_inbound){
		inputs_.set_inbound(is_inbound);
	}

	void inform_input(input_handle::key_set key){
		inputs_.inform(key);
	}

	void inform_cursor_move(math::vec2 pos){
		inputs_.cursor_move_inform(pos);
	}

	void update_bindings(float delta_in_tick){
		inputs_.update(delta_in_tick);
	}

	void bind_scene_context(scene& target){
		inputs_.main_binds.set_context(std::ref(target));
	}

	[[nodiscard]] math::vec2 get_cursor_pos() const noexcept{
		return inputs_.cursor_pos();
	}

	[[nodiscard]] bool is_cursor_inbound() const noexcept{
		return inputs_.is_cursor_inbound();
	}

	[[nodiscard]] std::span<elem* const> get_inbounds() const noexcept{
		return inbounds_.get_cur();
	}

	[[nodiscard]] bool is_mouse_pressed() const noexcept{
		return mouse_pressed_mask_ != 0;
	}

	[[nodiscard]] bool is_mouse_pressed(input_handle::mouse mouse_button_code) const noexcept{
		return (mouse_pressed_mask_ & mouse_bit(std::to_underlying(mouse_button_code))) != 0;
	}

	[[nodiscard]] bool has_passthrough_mouse_capture() const noexcept{
		return passthrough_mouse_mask_ != 0;
	}

	[[nodiscard]] bool is_scroll_focus(const elem* target) const noexcept{
		return focus_scroll_ == target;
	}

	[[nodiscard]] bool is_key_focus(const elem* target) const noexcept{
		return focus_key_ == target;
	}

	[[nodiscard]] bool is_cursor_focus(const elem* target) const noexcept{
		return focus_cursor_ == target;
	}

	[[nodiscard]] bool has_scroll_focus() const noexcept{
		return focus_scroll_ != nullptr;
	}

	[[nodiscard]] bool has_cursor_focus() const noexcept{
		return focus_cursor_ != nullptr;
	}

	[[nodiscard]] elem* esc_focus_candidate() const noexcept{
		return focus_key_ ? focus_key_ : focus_cursor_;
	}

	[[nodiscard]] bool contains_inbound(const elem* target) const noexcept{
		return std::ranges::contains(get_inbounds(), target);
	}

	void set_scroll_focus(elem* element, bool focus) noexcept;
	void set_key_focus(elem* element, bool focus);
	void switch_key_focus(elem* element);
	void try_swap_focus();
	void swap_focus(elem* newFocus);

	void capture_mouse(elem& target, input_handle::mouse mouse_button_code, math::vec2 press_scene_pos);

	template <std::derived_from<input_handle::key_mapping_interface> Mapping>
	Mapping& register_input_mapping(std::string_view name){
		return inputs_.template register_sub_input<Mapping>(name);
	}

	template <std::derived_from<input_handle::key_mapping_interface> Mapping = input_handle::key_mapping_interface>
	[[nodiscard]] Mapping* find_input_mapping(std::string_view name) const noexcept{
		auto* mapping = inputs_.find_sub_input(name);
		if constexpr (std::same_as<Mapping, input_handle::key_mapping_interface>){
			return mapping;
		}else{
			return dynamic_cast<Mapping*>(mapping);
		}
	}

	bool erase_input_mapping(std::string_view name){
		return inputs_.erase_sub_input(name);
	}

	input_key_result handle_key_input(input_handle::key_set key);

	events::dispatch_result handle_text_input(char32_t val);
	events::dispatch_result handle_ime_composition(const input_handle::ime_composition_event& event);
	events::dispatch_result handle_scroll(math::vec2 scroll);
	events::dispatch_result handle_mouse_input(input_handle::key_set k);
	void on_focus_lost();

	void update_inbounds();

	cursor_update_result update_cursor(overlay_manager& overlays, tooltip::tooltip_manager& tooltips, elem& scene_root);

	style::cursor_style get_cursor_style(math::vec2 cursor_local_pos) const;

	style::cursor_style get_cursor_style() const;

private:
	void flush_audio_request_() const;
};

}

}
