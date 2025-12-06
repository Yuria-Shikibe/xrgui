module;

export module mo_yanxi.react_flow:manager;

import :node_interface;
import mo_yanxi.utility;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.algo;

namespace mo_yanxi::react_flow{

export struct manager;

struct async_task_base{
friend manager;

public:
	[[nodiscard]] async_task_base() = default;

	virtual ~async_task_base() = default;

	virtual void execute(){

	}

	virtual void on_finish(manager& manager){

	}

	virtual node* get_owner_if_node() noexcept{
		return nullptr;
	}
};

//TODO allocator?

export struct manager_no_async_t{
};

export constexpr inline manager_no_async_t manager_no_async{};

struct node_deleter{
	static void operator()(node* pnode) noexcept{
		pnode->disconnect_self_from_context();
		delete pnode;
	}
};

struct node_p : std::unique_ptr<node, node_deleter>{
	template <std::derived_from<node> T, typename... Args>
		requires (std::constructible_from<T, Args&&...>)
	explicit(false) node_p(std::in_place_type_t<T>, Args&& ...args) : std::unique_ptr<node, node_deleter>(
		new T(std::forward<Args>(args)...), node_deleter{}){
	}
};

export struct manager{
private:
	std::vector<node_p> nodes_anonymous_{};
	std::vector<node*> pulse_subscriber_{};
	std::unordered_set<node*> expired_nodes{};

	using async_task_queue = ccur::mpsc_queue<std::move_only_function<void()>>;
	async_task_queue pending_received_updates_{};
	async_task_queue::container_type recycled_queue_container_{};

	ccur::mpsc_queue<std::unique_ptr<async_task_base>> pending_async_modifiers_{};
	std::mutex done_mutex_{};
	std::vector<std::unique_ptr<async_task_base>> done_[2]{};

	std::mutex async_request_mutex_{};
	std::vector<std::packaged_task<void()>> async_request_[2]{};


	std::jthread async_thread_{};

	template <std::derived_from<node> T, typename ...Args>
	[[nodiscard]] node_p make_node(Args&& ...args){
		return mo_yanxi::back_redundant_construct<node_p, 1>(std::in_place_type<T>, *this, std::forward<Args>(args)...);
	}

	void process_node(node& node){
		if(node.get_propagate_type() == propagate_behavior::pulse){
			pulse_subscriber_.push_back(&node);
		}
	}

public:
	[[nodiscard]] manager() : async_thread_([](std::stop_token stop_token, manager& manager){
		execute_async_tasks(std::move(stop_token), manager);
	}, std::ref(*this)) {}

	[[nodiscard]] explicit manager(manager_no_async_t){}

	template <std::derived_from<node> T, typename ...Args>
	T& add_node(Args&& ...args){
		auto& ptr = nodes_anonymous_.emplace_back(this->make_node<T>(std::forward<Args>(args)...));
		this->process_node(*ptr);
		return static_cast<T&>(*ptr);
	}

	template <std::derived_from<node> T>
	T& add_node(T&& node){
		auto& ptr = nodes_anonymous_.emplace_back(this->make_node<T>(std::move(node)));
		this->process_node(*ptr);
		return static_cast<T&>(*ptr);
	}

	template <std::derived_from<node> T>
	T& add_node(const T& node){
		auto& ptr = nodes_anonymous_.emplace_back(this->make_node<T>(node));
		this->process_node(*ptr);
		return static_cast<T&>(*ptr);
	}

	void clear_isolated() noexcept{
		try{
			for (auto && node : nodes_anonymous_){
				if(node->is_isolated()){
					expired_nodes.insert(node.get());
				}
			}
		} catch(...){
			return; //end garbage collection directly
		}
	}

	bool erase_node(node& n) noexcept
	try{
		return expired_nodes.insert(std::addressof(n)).second;
	} catch(const std::bad_alloc&){
		pending_async_modifiers_.erase_if([&](const std::unique_ptr<async_task_base>& ptr){
			return ptr->get_owner_if_node() == &n;
		});
		{
			std::lock_guard lg{done_mutex_};
			algo::erase_unique_if_unstable(done_[1], [&](const std::unique_ptr<async_task_base>& ptr){
				return ptr->get_owner_if_node() == &n;
			});
		}
		algo::erase_unique_unstable(pulse_subscriber_, &n);
		return algo::erase_unique_if_unstable(nodes_anonymous_, [&](const node_p& ptr){
			return ptr.get() == &n;
		});
	}



	/**
	 * @brief Called from OTHER thread that need do sth on the main data flow thread.
	 */
	template <std::invocable<> Fn>
		requires (std::move_constructible<std::remove_cvref_t<Fn>>)
	void push_posted_act(Fn&& fn){
		pending_received_updates_.emplace(std::forward<Fn>(fn));
	}

	void push_task(std::unique_ptr<async_task_base> task){
		if(async_thread_.joinable()){
			pending_async_modifiers_.push(std::move(task));
		}else{
			task->execute();
			done_[1].push_back(std::move(task));
		}
	}

	void update(){
		if(!expired_nodes.empty()){

			{
				std::lock_guard lg{done_mutex_};
				std::erase_if(done_[1], [&](const std::unique_ptr<async_task_base>& ptr){
					return expired_nodes.contains(ptr->get_owner_if_node());
				});
			}

			pending_async_modifiers_.erase_if([&](const std::unique_ptr<async_task_base>& ptr){
				return expired_nodes.contains(ptr->get_owner_if_node());
			});
			algo::erase_unique_if_unstable(nodes_anonymous_, [&](const node_p& ptr){
				return expired_nodes.contains(ptr.get());
			});
			algo::erase_unique_if_unstable(pulse_subscriber_, [&](node* ptr){
				return expired_nodes.contains(ptr);
			});

			expired_nodes.clear();
		}

		for (const auto & pulse_subscriber : pulse_subscriber_){
			pulse_subscriber->on_pulse_received(*this);
		}

		if(pending_received_updates_.swap(recycled_queue_container_)){
			for (auto&& move_only_function : recycled_queue_container_){
				move_only_function();
			}
			recycled_queue_container_.clear();
		}

		{

			{
				std::lock_guard lg{done_mutex_};
				if(done_[1].empty()){
					goto RET;
				}
				std::swap(done_[0], done_[1]);
			}

			for (auto && task_base : done_[0]){
				task_base->on_finish(*this);
			}
			done_[0].clear();

			RET:
			(void)0;
		}
	}

	~manager(){
		if(async_thread_.joinable()){
			async_thread_.request_stop();
			pending_async_modifiers_.notify();
			async_thread_.join();
		}
	}

private:
	static void execute_async_tasks(std::stop_token stop_token, manager& manager){
		while(!stop_token.stop_requested()){
			auto&& task = manager.pending_async_modifiers_.consume([&stop_token]{
				return stop_token.stop_requested();
			});

			if(task){
				task.value()->execute();
				std::lock_guard lg{manager.done_mutex_};
				manager.done_[1].push_back(std::move(task.value()));
			}else{
				return;
			}
		}
	}
};

}