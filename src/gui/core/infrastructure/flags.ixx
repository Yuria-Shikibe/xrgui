module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.gui.infrastructure:flags;

import :elem_ptr;
import :defines;

import mo_yanxi.meta_programming;
import mo_yanxi.cache;
import std;

namespace mo_yanxi::gui{

struct requirement_set_result{
	bool need_propagate;
	bool is_update_required;

	constexpr explicit operator bool() const noexcept{
		return need_propagate;
	}

	constexpr bool is_required() const noexcept{
		assert(need_propagate);
		return is_update_required;
	}
};

export
enum class update_channel : unsigned{
	none,
	layout = 1 << 0,
	position = 1 << 1,
	draw = 1 << 2,

	custom = 1 << 3,
};

BITMASK_OPS(export, update_channel)

export
struct update_flags{
private:
	unsigned total_children_required_{};

	update_channel self_update_required_channels_{};
	lru_set<elem*, 4> update_required_cache_{};



public:
	constexpr bool is_update_required() const noexcept{
		return is_self_update_required() || is_children_update_required();
	}

	constexpr bool is_self_update_required() const noexcept{
		return self_update_required_channels_ != update_channel::none;
	}

	constexpr bool is_self_update_required(update_channel mask) const noexcept{
		return (self_update_required_channels_ & mask) != update_channel::none;
	}

	constexpr bool is_children_update_required() const noexcept{
		return total_children_required_;
	}

	constexpr requirement_set_result clear_children_update_requirement() noexcept{
		if(!total_children_required_)return {};

		clear_cache();
		if(!is_self_update_required()){
			total_children_required_ = 0;
			return  {true, false};
		}else{
			total_children_required_ = 0;
			return {};
		}
	}

	/**
	 * @brief Requires children should only mark the change when it is really changed.
	 */
	constexpr requirement_set_result set_child_mark_update_changed(elem* children, bool is_update_required) noexcept{
		if(is_update_required){
			update_required_cache_.insert(children);

			const auto last = total_children_required_++;
			if(!last && !is_self_update_required()){
				return {true, true};
			}
		}else{
			update_required_cache_.erase(children);
			assert(total_children_required_ > 0);
			const auto last = total_children_required_--;
			if(last == 1 && !is_self_update_required()){
				return {true, false};
			}
		}

		return {};
	}

	constexpr requirement_set_result set_self_update_required(update_channel channel, update_channel mask = update_channel{~0U}) noexcept{
		if((self_update_required_channels_ & mask) == (channel & mask))return {};
		self_update_required_channels_ = (channel & mask) | (self_update_required_channels_ & ~mask);
		if(is_self_update_required() && !total_children_required_){
			return {true, true};
		}
		if(!is_self_update_required() && !total_children_required_){
			return {true, false};
		}
		return {};
	}

	constexpr bool is_cache_holdable() const noexcept{
		return update_required_cache_.size() <= total_children_required_;
	}

	/**
	 *
	 * @return provide local storage mark to avoid memory access in common children
	 */
	constexpr const lru_set<elem*, 4>& get_marked() const noexcept{
		return update_required_cache_;
	}

	constexpr void clear_cache() noexcept{
		update_required_cache_.clear();
	}

	constexpr bool erase_cache_elem(const elem* e) noexcept {
		return update_required_cache_.erase(const_cast<elem*>(e));
	}
};

export
struct draw_flag{
	using flag_type = smallest_uint_t<(1 << draw_pass_max_capacity) - 1>;
private:
	flag_type is_self_draw_required_{};
	bool is_children_draw_required_{};

	constexpr static unsigned short max_count = 400;

	unsigned short debug_count_{};

public:
	void clear() noexcept{
		is_self_draw_required_ = {};
		is_children_draw_required_ = {};
	}

	bool is_draw_required() const noexcept{
		return is_self_draw_required_ || is_children_draw_required_;
	}

	bool is_children_draw_required() const noexcept{
		return is_children_draw_required_;
	}

	bool is_self_draw_required() const noexcept{
		return is_self_draw_required_;
	}

	requirement_set_result set_self_draw_required(flag_type required, flag_type mask = std::numeric_limits<flag_type>::max()) noexcept{
		if((is_self_draw_required_ & mask) == (required & mask))return {};
		is_self_draw_required_ = (required & mask) | (is_self_draw_required_ & ~mask);
		if(required && !is_children_draw_required()){
			return {true, true};
		}

		if(!required && !is_children_draw_required()){
			return {true, false};
		}

		return {};
	}

	requirement_set_result set_children_draw_required(bool required) noexcept{
		if(is_children_draw_required_ == required)return {};
		is_children_draw_required_ = required;
		if(required && !is_self_draw_required()){
			return {true, true};
		}

		if(!required && !is_self_draw_required()){
			return {true, false};
		}

		return {};
	}

	void update_debug_count() noexcept{
		if(is_self_draw_required()){
			debug_count_ = max_count;
		}else{
			if(debug_count_)--debug_count_;
		}
	}


	float get_debug_count() const noexcept{
		return static_cast<float>(debug_count_) / static_cast<float>(max_count);
	}


};
}