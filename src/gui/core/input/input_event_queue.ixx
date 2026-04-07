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
	// 消费者专属的本地缓存，没有任何竞态条件
	std::vector<input_event_variant> consumer_cache_{};

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
		// 1. 从 MPSC 队列中拉取最新事件，追加到本地缓存
		if(auto p = buffer_.fetch()){
			consumer_cache_.insert(consumer_cache_.end(), p->begin(), p->end());
		}

		// 如果当前没有任何积压事件，直接返回
		if(consumer_cache_.empty()){
			return;
		}

		// 2. 从后向前查找最后一个 frame_split
		auto last_split_it = std::find_if(
			consumer_cache_.rbegin(),
			consumer_cache_.rend(),
			[](const input_event_variant& e){
				return e.type == input_event_type::frame_split;
			}
		);

		// 3. 如果找到了至少一个 frame_split
		if(last_split_it != consumer_cache_.rend()){
			// reverse_iterator 的 base() 会返回对应正向元素的*下一个*位置
			// 这恰好是我们需要的 span 结束位置，包含了该 frame_split 本身
			auto consume_end_it = last_split_it.base();

			// 构造 span 并交付消费
			std::invoke(fn, std::span<const input_event_variant>{
				            consumer_cache_.begin(),
				            consume_end_it
			            });

			// 4. 清理已被消费的事件，未消费的事件（处于 consume_end_it 到 end() 之间）会被自动左移保留
			consumer_cache_.erase(consumer_cache_.begin(), consume_end_it);
		}
		// 如果一个 frame_split 都不存在，则不进行动作，所有数据安全保留在 consumer_cache_ 中
	}
};
}
