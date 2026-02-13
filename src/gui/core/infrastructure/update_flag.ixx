module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.gui.infrastructure:update_flag;

import :elem_ptr;

import mo_yanxi.cache;
import std;

namespace mo_yanxi::gui{

struct update_requirement_set_result{
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

struct update_flag{
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

	constexpr update_requirement_set_result clear_children_update_requirement() noexcept{
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
	constexpr update_requirement_set_result set_child_mark_update_changed(elem* children, bool is_update_required) noexcept{
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

	constexpr update_requirement_set_result set_self_update_required(update_channel channel, update_channel mask = update_channel{~0U}) noexcept{
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


}