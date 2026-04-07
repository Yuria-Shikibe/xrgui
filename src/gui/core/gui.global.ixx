export module mo_yanxi.gui.global;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.style.manager;
export import mo_yanxi.input_handle.input_event_queue;

import std;

namespace mo_yanxi::gui::global{

export inline ui_manager manager{0};
export inline input_handle::input_event_queue event_queue{};

export
void initialize(){
	std::destroy_at(&manager);
	std::construct_at(&manager);
}

export
void terminate() noexcept {
	std::destroy_at(&manager);
	std::construct_at(&manager, 0);
}

export
template <std::invocable<input_handle::input_event_variant> UnhandledFn>
void consume(scene& f, std::span<const input_handle::input_event_variant> events, UnhandledFn&& fn) {
	using namespace input_handle;
	for(const auto& ev : events){
		// 默认假设事件被拦截（比如内部的系统更新事件不向下游传递）
		events::op_afterwards status = events::op_afterwards::intercepted;

		switch(ev.type){
		case input_event_type::input_key:
			status = f.on_key_input(ev.input_key);
			break;
		case input_event_type::input_mouse:
			status = f.on_mouse_input(ev.input_key);
			break;
		case input_event_type::input_scroll:
			status = f.on_scroll(ev.cursor);
			break;
		case input_event_type::input_u32:
			status = f.on_unicode_input(ev.input_char);
			break;
		case input_event_type::cursor_move:
			status = f.on_cursor_move(ev.cursor);
			break;

			// 下面这两个事件通常属于 UI 框架内部的驱动事件，无需传递给下游的相机/角色控制逻辑
		case input_event_type::cursor_inbound:
			f.on_inbound_changed(ev.is_inbound);
			status = events::op_afterwards::intercepted;
			break;
		case input_event_type::frame_split:
			f.update(std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 60>>>(ev.frame_delta_time).count());
			status = events::op_afterwards::intercepted;
			break;
		}

		// 【核心逻辑】如果 UI 没有拦截该事件，则保存到下游缓冲区
		if(status == events::op_afterwards::fall_through){
			std::invoke(fn, ev);
		}
	}
}

export
template <std::invocable<input_handle::input_event_variant> Fn>
std::chrono::duration<double> consume_current_input(scene& scene, Fn&& fn){
	std::chrono::duration<double> total_dt{0};

	event_queue.consume([&](std::span<const input_handle::input_event_variant> span){
		for (const auto& event : span) {
			if (event.type == input_handle::input_event_type::frame_split) {
				total_dt += event.frame_delta_time;
			}
		}
		global::consume(scene, span, std::forward<Fn>(fn));
	});

	return total_dt;
}

}