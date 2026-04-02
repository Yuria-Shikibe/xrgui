//
// Created by Matrix on 2026/4/1.
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

	cursor_inbound,
	cursor_move,

	frame_split,
};

export
struct input_event_variant{
	input_event_type type;
	union{
		key_set input_key;
		math::vec2 cursor;
		char32_t input_char;
		bool is_inbound;
		std::chrono::duration<double> frame_delta_time;
	};
};

export
struct input_event_queue{
private:
	ccur::mpsc_double_buffer<input_event_variant> buffer_{};

public:
	void push_key(const key_set k){
		buffer_.push(input_event_variant{
			.type = input_event_type::input_key,
			.input_key = k
		});
	}

	void push_mouse(const key_set k){
		buffer_.push(input_event_variant{
			.type = input_event_type::input_mouse,
			.input_key = k
		});
	}

	void push_scroll(const math::vec2 cursor){
		buffer_.push(input_event_variant{
			.type = input_event_type::input_scroll,
			.cursor = cursor
		});
	}

	void push_u32(const char32_t val){
		buffer_.push(input_event_variant{
			.type = input_event_type::input_u32,
			.input_char = val
		});
	}

	void push_cursor_inbound(const bool inbound){
		buffer_.push(input_event_variant{
			.type = input_event_type::cursor_inbound,
			.is_inbound = inbound
		});
	}

	void push_cursor_move(const math::vec2 cursor){
		buffer_.push(input_event_variant{
			.type = input_event_type::cursor_move,
			.cursor = cursor
		});
	}

	void push_frame_split(const std::chrono::duration<double> dt){
		buffer_.push(input_event_variant{
			.type = input_event_type::frame_split,
			.frame_delta_time = dt
		});
	}

	template <std::invocable<std::span<const input_event_variant>> Fn>
	void consume(Fn&& fn){
		if(auto p = buffer_.fetch()){
			std::invoke(fn, std::span{*p});
		}
	}
};

}
