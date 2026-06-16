export module mo_yanxi.gui.global;

export import mo_yanxi.gui.infrastructure;
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
		const events::dispatch_result status = f.handle_input_event(ev);
		if(status == events::dispatch_result::unhandled){
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
