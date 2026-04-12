//

//

export module mo_yanxi.gui.action.queue;

import std;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.action;

namespace mo_yanxi::gui {

export
template <typename Target, typename Cont = std::deque<action::action_ptr<Target>>>
struct mpsc_action_queue {
private:

    ccur::mpsc_queue<action::action_ptr<Target>, Cont> async_queue{};
    action::action_ptr<Target> current_action_{};

public:
    mpsc_action_queue() = default;

	void clear() noexcept{
		current_action_.reset();
		async_queue.dump();
	}

    /**
     * @brief 线程安全的异步投递动作，适用于其他系统/线程向此队列添加动作
     */
    template <std::derived_from<action::action<Target>> ActionType, typename... Args>
        requires (std::constructible_from<ActionType, const mr::heap_allocator<ActionType>&, Args&&...>)
    ActionType& push_action(mr::heap_allocator<ActionType> alloc, Args&&... args) {
        return static_cast<ActionType&>(*async_queue.emplace(
            std::in_place_type<ActionType>,
            std::move(alloc),
            std::forward<Args>(args)...
        ));
    }

    /**
     * @brief 驱动队列更新
     */
	bool update_action(float delta_in_ticks, Target& target) {

    	while (delta_in_ticks > 0) {

    		if (!current_action_) {
    			if (auto opt = async_queue.try_consume(); opt.has_value()) {
    				current_action_ = std::move(*opt);
    			} else {

    				return true;
    			}
    		}


    		delta_in_ticks = current_action_->update(delta_in_ticks, target);


    		if (delta_in_ticks >= 0) [[unlikely]] {

    			current_action_.reset();
    		} else {

    			break;
    		}
    	}
		return false;
    }

	bool empty() const noexcept{
		return async_queue.empty();
	}

	bool is_consuming() const noexcept{
		return current_action_ != nullptr;
	}
};

}