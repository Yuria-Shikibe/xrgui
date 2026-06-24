//

//

export module mo_yanxi.input_handle.input_event_queue;

import std;
import mo_yanxi.input_handle;
import mo_yanxi.math.vector2;
import mo_yanxi.concurrent.mpsc_double_buffer;

namespace mo_yanxi::input_handle{
export
enum struct input_event_type : std::uint8_t{
	input_key,
	input_mouse,
	input_scroll,
	input_u32,
	input_ime_composition,

	cursor_inbound,
	cursor_move,
	focus_lost,

	frame_split,
};

export
using raw_input_event_type = input_event_type;

export
enum struct ime_composition_event_type : std::uint8_t{
	begin,
	update,
	commit,
	cancel,
};

export
struct ime_composition_event{
	ime_composition_event_type type{};
	std::u32string text{};
	std::uint32_t cursor{};
};

export
struct input_event_variant{
	input_event_type type{};

	key_set input_key{};
	math::vec2 cursor{};
	char32_t input_char{};
	bool is_inbound{};
	std::chrono::duration<double> frame_delta_time{};
	ime_composition_event ime_composition{};
	std::chrono::steady_clock::time_point timestamp{};
};

export
using raw_input_event = input_event_variant;

export
struct input_event_queue{
private:
	ccur::mpsc_double_buffer<input_event_variant> buffer_{};

	std::vector<input_event_variant> consumer_cache_{};

public:
	inline void push(raw_input_event event){
		if(event.timestamp == std::chrono::steady_clock::time_point{}){
			event.timestamp = std::chrono::steady_clock::now();
		}
		buffer_.push(std::move(event));
	}

	inline void push_key(const key_set k){
		push(input_event_variant{
				.type = input_event_type::input_key,
				.input_key = k
			});
	}

	inline void push_mouse(const key_set k){
		push(input_event_variant{
				.type = input_event_type::input_mouse,
				.input_key = k
			});
	}

	inline void push_scroll(const math::vec2 cursor){
		push(input_event_variant{
				.type = input_event_type::input_scroll,
				.cursor = cursor
			});
	}

	inline void push_u32(const char32_t val){
		push(input_event_variant{
				.type = input_event_type::input_u32,
				.input_char = val
			});
	}

	inline void push_ime_composition(ime_composition_event event){
		push(input_event_variant{
				.type = input_event_type::input_ime_composition,
				.ime_composition = std::move(event)
			});
	}

	inline void push_cursor_inbound(const bool inbound){
		push(input_event_variant{
				.type = input_event_type::cursor_inbound,
				.is_inbound = inbound
			});
	}

	inline void push_cursor_move(const math::vec2 cursor){
		push(input_event_variant{
				.type = input_event_type::cursor_move,
				.cursor = cursor
			});
	}

	inline void push_focus_lost(){
		push(input_event_variant{
				.type = input_event_type::focus_lost
			});
	}

	inline void push_frame_split(const std::chrono::duration<double> dt){
		push(input_event_variant{
				.type = input_event_type::frame_split,
				.frame_delta_time = dt
			});
	}

	template <std::invocable<std::span<const input_event_variant>> Fn>
	void consume(Fn&& fn){

		if(auto p = buffer_.fetch()){
			consumer_cache_.insert(consumer_cache_.end(), p->begin(), p->end());
		}


		if(consumer_cache_.empty()){
			return;
		}


		auto last_split_it = std::find_if(
			consumer_cache_.rbegin(),
			consumer_cache_.rend(),
			[](const input_event_variant& e){
				return e.type == input_event_type::frame_split;
			}
		);


		if(last_split_it != consumer_cache_.rend()){


			auto consume_end_it = last_split_it.base();


			std::invoke(fn, std::span<const input_event_variant>{
				            consumer_cache_.begin(),
				            consume_end_it
			            });


			consumer_cache_.erase(consumer_cache_.begin(), consume_end_it);
		}

	}
};

export
using input_sink = input_event_queue;
}
