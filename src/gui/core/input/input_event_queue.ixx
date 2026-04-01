//
// Created by Matrix on 2026/4/1.
//

export module input_event_queue;

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

}
