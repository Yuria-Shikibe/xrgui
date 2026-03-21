//
// Created by Matrix on 2026/3/18.
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
    // 线程安全的异步队列，用于跨线程或延迟投递
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
    	// 只要还有剩余的时间 delta，就继续处理动作
    	while (delta_in_ticks > 0) {
    		// 1. 如果当前没有正在执行的动作，尝试从队列中提取一个
    		if (!current_action_) {
    			if (auto opt = async_queue.try_consume(); opt.has_value()) {
    				current_action_ = std::move(*opt);
    			} else {
    				// 队列为空，直接结束本次 Tick
    				return true;
    			}
    		}

    		// 2. 执行当前的动作 (此时 current_action_ 已经安全地脱离了队列)
    		delta_in_ticks = current_action_->update(delta_in_ticks, target);

    		// 3. 检查动作是否完成
    		if (delta_in_ticks >= 0) [[unlikely]] {
    			// 动作完成（时间有结余或刚好用完），销毁当前动作，下一轮循环将提取新动作
    			current_action_.reset();
    		} else {
    			// 动作未完成（返回 -1.f 等），时间耗尽，跳出循环等待下一次 Tick
    			break;
    		}
    	}
		return false;
    }
};

} // namespace mo_yanxi::gui