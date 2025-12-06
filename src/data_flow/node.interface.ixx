module;

#include <cassert>

#include <mo_yanxi/enum_operator_gen.hpp>

#ifndef MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
#define MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK 1
#endif

#ifndef MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK
#define MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK 1
#endif

export module mo_yanxi.react_flow:node_interface;
import std;

import mo_yanxi.type_register;
export import mo_yanxi.react_flow.data_storage;

namespace mo_yanxi::react_flow{
using data_type_index = type_identity_index;

export struct manager;

export struct socket_type_pair{
	data_type_index input;
	data_type_index output;
};

struct invalid_node : std::invalid_argument{
	[[nodiscard]] explicit invalid_node(const std::string& msg)
	: invalid_argument(msg){
	}

	[[nodiscard]] explicit invalid_node(const char* msg)
	: invalid_argument(msg){
	}
};


ENUM_COMPARISON_OPERATORS(data_state, export)

//TODO return both data and state
export
template <typename T>
using request_pass_handle = request_result<data_package_optimal<T>>;

export
template <typename T>
[[nodiscard]] request_pass_handle<T> make_request_handle_unexpected(data_state error) noexcept{
	return request_pass_handle<T>{error};
}

export
template <typename T>
[[nodiscard]] request_pass_handle<T> make_request_handle_expected(T&& data, bool isExpired) noexcept{
	return request_pass_handle<T>(isExpired, std::in_place, std::forward<T>(data));
}

export
template <typename T>
[[nodiscard]] request_pass_handle<T> make_request_handle_expected_ref(const T& data, bool isExpired) noexcept{
	return request_pass_handle<T>(isExpired, data);
}

export enum struct async_type : std::uint8_t{
	/**
	 * @brief the modifier is synchronized
	 */
	none,

	/**
	 * @brief When a new update is arrived and the previous is not done yet, cancel the previous
	 */
	async_latest,

	/**
	 * @brief When a new update is arrived and the previous is not done yet, just dispatch again
	 */
	async_all,

	def = async_latest
};

export enum struct propagate_behavior : std::uint8_t{
	eager,
	lazy,
	pulse
};

export enum struct data_pending_state : std::uint8_t{
	done,
	expired,
	waiting_pulse
};


template <std::ranges::input_range Rng>
	requires (std::is_scoped_enum_v<std::ranges::range_value_t<Rng>>)
std::ranges::range_value_t<Rng> merge_data_state(const Rng& states) noexcept{
	return std::ranges::max(states, std::ranges::less{}, [](const auto v){
		return std::to_underlying(v);
	});
}

template <typename T>
void update_state_enum(T& state, T other) noexcept{
	state = T{std::max(std::to_underlying(state), std::to_underlying(other))};
}

export
template <typename T>
struct terminal;

template <typename Ret, typename... Args>
struct modifier_base;

export
template <typename T>
struct intermediate_cache;

export struct node;

export
using node_ptr = node*;

export
struct successor_entry{
	std::size_t index;
	node_ptr entity;

	[[nodiscard]] successor_entry() = default;

	[[nodiscard]] successor_entry(const std::size_t index, node& entity)
	: index(index),
	entity(std::addressof(entity)){
	}

	[[nodiscard]] node* get() const noexcept{
		return entity;
	}

	template <typename T>
	void update(manager& manager, const T& data) const;

	void update(manager& manager, const void* data, data_type_index checker) const;

	void mark_updated() const;
};


/**
 * @brief 判断在 n0 的 output 中加入 n1 后是否会形成环
 *
 * @param self 边的起始节点
 * @param successors 边的目标节点
 * @return 如果会形成环，返回 true, 否则返回 false
 */
export
bool is_ring_bridge(const node* self, const node* successors);

struct node{
	friend successor_entry;
	friend manager;

	template <typename Ret, typename... Args>
	friend struct modifier_base;

	template <typename T>
	friend struct intermediate_cache;

protected:
	propagate_behavior data_propagate_type_{};
	data_pending_state data_pending_state_{};

public:
	[[nodiscard]] node() = default;

	[[nodiscard]] explicit node(propagate_behavior data_propagate_type)
	: data_propagate_type_(data_propagate_type){
	}

	virtual ~node() = default;

	[[nodiscard]] propagate_behavior get_propagate_type() const noexcept{
		return data_propagate_type_;
	}

	[[nodiscard]] bool is_data_expired() const noexcept{
		return data_pending_state_ != data_pending_state::done;
	}

#pragma region ConncectionInterface
public:
	[[nodiscard]] virtual data_type_index get_out_socket_type_index() const noexcept{
		return nullptr;
	}

	[[nodiscard]] virtual std::span<const data_type_index> get_in_socket_type_index() const noexcept{
		return {};
	}

	[[nodiscard]] virtual data_state get_data_state() const noexcept{
		return data_state::failed;
	}

	[[nodiscard]] virtual std::span<const node_ptr> get_inputs() const noexcept{
		return {};
	}

	[[nodiscard]] virtual std::span<const successor_entry> get_outputs() const noexcept{
		return {};
	}

	bool connect_successors_unchecked(const std::size_t slot_of_successor, node& post){
		if(connect_successors_impl(slot_of_successor, post)){
			post.connect_predecessor_impl(slot_of_successor, *this);
			return true;
		}
		return false;
	}

	bool connect_predecessor_unchecked(const std::size_t slot_of_successor, node& prev){
		return prev.connect_successors_unchecked(slot_of_successor, *this);
	}

	bool connect_successors(const std::size_t slot, node& post){
#if MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK
		if(is_ring_bridge(this, &post)){
			throw invalid_node{"ring detected"};
		}
#endif


		if(!std::ranges::contains(post.get_in_socket_type_index(), get_out_socket_type_index())){
			throw invalid_node{"Node type NOT match"};
		}

		return connect_successors_unchecked(slot, post);
	}

	bool connect_predecessor(const std::size_t slot, node& prev){
		return prev.connect_successors(slot, *this);
	}

	bool connect_successors(node& post){
#if MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK
		if(is_ring_bridge(this, &post)){
			throw invalid_node{"ring detected"};
		}
#endif

		const auto rng = post.get_in_socket_type_index();
		if(auto itr = std::ranges::find(rng, get_out_socket_type_index()); itr != rng.end()){
			return connect_successors_unchecked(std::ranges::distance(rng.begin(), itr), post);
		} else{
			throw invalid_node{"Failed To Find Slot"};
		}
	}

	bool connect_predecessor(node& prev){
		return prev.connect_successors(*this);
	}

	bool disconnect_successors(const std::size_t slot, node& post) noexcept{
		if(erase_successors_single_edge(slot, post)){
			post.erase_predecessor_single_edge(slot, *this);
			return true;
		}
		return false;
	}

	bool disconnect_predecessor(const std::size_t slot, node& prev) noexcept{
		return prev.disconnect_successors(slot, *this);
	}


	bool disconnect_successors(node& post){
		const auto rng = post.get_in_socket_type_index();
		if(auto itr = std::ranges::find(rng, get_out_socket_type_index()); itr != rng.end()){
			auto slot = std::ranges::distance(rng.begin(), itr);
			if(erase_successors_single_edge(slot, post)){
				post.erase_predecessor_single_edge(slot, *this);
				return true;
			}
		} else{
			throw invalid_node{"Failed To Find Slot"};
		}

		return false;
	}

	bool disconnect_predecessor(node& prev){
		return prev.disconnect_successors(*this);
	}

	virtual void disconnect_self_from_context() noexcept{

	}

	[[nodiscard]] virtual bool is_isolated() const noexcept{
		return false;
	}


public:
	virtual bool erase_successors_single_edge(std::size_t slot, node& post) noexcept{
		return false;
	}

	virtual void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept{
	}

protected:
	//TODO disconnected conflicted
	virtual bool connect_successors_impl(std::size_t slot, node& post){
		return false;
	}

	virtual void connect_predecessor_impl(std::size_t slot, node& prev){
	}

#pragma endregion


protected:
	void push_update(this const auto& self, manager& manager, const void* data, data_type_index idx) {
		if(self.data_propagate_type_ == propagate_behavior::eager){
			for(const successor_entry& successor : self.get_outputs()){
				successor.update(manager, data, idx);
			}
		} else{
			std::ranges::for_each(self.get_outputs(), &successor_entry::mark_updated);
		}
	}

	virtual void on_pulse_received(manager& m){
		return;
	}

	/**
	 * @brief update the node
	 *
	 * The data flow is from source to terminal (push)
	 *
	 * @param manager
	 * @param in_data_pass_by_copy ptr to const data, provided by parent
	 */
	virtual void on_push(manager& manager, std::size_t from_index, const void* in_data_pass_by_copy){

	}

	/**
	 *
	 * @brief Only notify the data has changed, used for lazy nodes. Should be mutually exclusive with update when propagate.
	 *
	 */
	virtual void mark_updated(std::size_t from_index) noexcept{
		data_pending_state_ = data_pending_state::expired;
		std::ranges::for_each(get_outputs(), &successor_entry::mark_updated);
	}


	static bool try_insert(std::vector<successor_entry>& successors, std::size_t slot, node& next){
		if(std::ranges::find_if(successors, [&](const successor_entry& e){
			return e.index == slot && e.entity == &next;
		}) != successors.end()){
			return false;
		}

		successors.emplace_back(slot, next);
		return true;
	}

	static bool try_erase(std::vector<successor_entry>& successors, const std::size_t slot, const node& next) noexcept{
		return std::erase_if(successors, [&](const successor_entry& e){
			return e.index == slot && e.entity == &next;
		});
	}
};

export
template <typename T>
struct type_aware_node : node{
	using node::node;

	[[nodiscard]] data_type_index get_out_socket_type_index() const noexcept final{
		return unstable_type_identity_of<T>();
	}

	template <typename S>
	std::optional<T> request(this S& self, bool allow_expired){
		if(auto rst = self.request_raw(allow_expired)){
			return rst.value().fetch();
		} else{
			return std::nullopt;
		}
	}

	template <typename S>
	std::expected<T, data_state> request_stated(this S& self, bool allow_expired){
		if(auto rst = self.request_raw(allow_expired)){
			if(auto optal = rst.value().fetch()){
				return std::expected<T, data_state>{optal.value()};
			}
			return std::unexpected{data_state::failed};
		} else{
			return std::unexpected{rst.error()};
		}
	}

	template <typename S>
	std::optional<T> nothrow_request(this S& self, bool allow_expired) noexcept try{
		if(auto rst = self.request_raw(allow_expired)){
			return rst.value().fetch();
		}
		return std::nullopt;
	} catch(...){
		return std::nullopt;
	}

	template <typename S>
	std::expected<T, data_state> nothrow_request_stated(this S& self, bool allow_expired) noexcept try{
		if(auto rst = self.request_raw(allow_expired)){
			if(auto optal = rst.value().fetch()){
				return std::expected<T, data_state>{optal.value()};
			}
		}
		return std::unexpected{data_state::failed};
	} catch(...){
		return std::unexpected{data_state::failed};
	}

	/**
	 * @brief Pull data from parent node
	 *
	 * The data flow is from terminal to source (pull)
	 *
	 * This member function is not const as it may update the internal cache
	 *
	 * @param allow_expired
	 */
	virtual request_pass_handle<T> request_raw(bool allow_expired) = 0;
};

export
template <typename T>
type_aware_node<T>& node_type_cast(node& node) noexcept(!MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK){
#if MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
	auto idt = node.get_out_socket_type_index();
	if( idt != unstable_type_identity_of<T>()){
		throw invalid_node{"Node type NOT match"};
	}
#endif
	return static_cast<type_aware_node<T>&>(node);
}


export
template <typename T>
struct provider_general : type_aware_node<T>{
	static constexpr data_type_index node_data_type_index = unstable_type_identity_of<T>();
	friend manager;

	[[nodiscard]] provider_general() = default;


	[[nodiscard]] explicit provider_general(manager& manager) : manager_(std::addressof(manager)){
	}

protected:
	[[nodiscard]] explicit provider_general(manager& manager, propagate_behavior propagate_type)
	: type_aware_node<T>{propagate_type}, manager_(std::addressof(manager)){
	}
public:

	void update_value(T&& value){
		this->update_value_unchecked(std::move(value));
	}

	void update_value(const T& value){
		this->update_value_unchecked(T{value});
	}

	[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
		return std::span{&node_data_type_index, 1};
	}


	void disconnect_self_from_context() noexcept final{
		for(const auto& successor : successors){
			successor.entity->disconnect_predecessor(successor.index, *this);
		}
		successors.clear();
	}

	[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
		return successors;
	}

private:
	void update_value_unchecked(T&& value){
		this->on_push(std::addressof(value));
	}

	bool connect_successors_impl(const std::size_t slot, node& post) final{
		post.erase_predecessor_single_edge(slot, *post.get_inputs()[slot]);
		return node::try_insert(successors, slot, post);
	}

	bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
		return node::try_erase(successors, slot, post);
	}

protected:
	manager* manager_{};
	std::vector<successor_entry> successors{};

	virtual void on_push(void* in_data_pass_by_move){
		for (auto && successor : successors){
			successor.update(*manager_, *static_cast<const T*>(in_data_pass_by_move));
		}
	}
};

export
template <typename T>
struct terminal_cached;

template <typename T>
struct terminal : type_aware_node<T>{
private:
	node_ptr parent{};

protected:
	[[nodiscard]] explicit terminal(propagate_behavior data_propagate_type)
	: type_aware_node<T>(data_propagate_type){
	}

public:
	[[nodiscard]] terminal() = default;


	static constexpr data_type_index node_data_type_index = unstable_type_identity_of<T>();

	[[nodiscard]] std::span<const node_ptr> get_inputs() const noexcept final{
		return {&parent, 1};
	}

	[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
		return std::span{&node_data_type_index, 1};
	}

	[[nodiscard]] data_state get_data_state() const noexcept override{
		return this->parent->get_data_state();
	}

	void check_expired_and_update(bool allow_expired){
		if(!this->is_data_expired()) return;

		if(auto rst = this->request(allow_expired)){
			this->on_update(rst.value().fetch());
		}
	}

	void disconnect_self_from_context() noexcept final{
		if(parent){
			parent->erase_successors_single_edge(0, *this);
			parent = nullptr;
		}
	}

	request_pass_handle<T> request_raw(const bool allow_expired) override{
		assert(parent != nullptr);
		return node_type_cast<T>(*parent).request_raw(allow_expired);
	}

protected:
	void connect_predecessor_impl(const std::size_t slot, node& prev) final{
		assert(slot == 0);
		if(parent){
			const auto rng = parent->get_outputs();
			const auto idx = std::ranges::distance(rng.begin(), std::ranges::find(rng, this, &successor_entry::get));
			parent->erase_successors_single_edge(idx, *this);
		}
		parent = std::addressof(prev);
		if(auto dat = this->nothrow_request(true)){
			this->on_update(*dat);
		}
	}

	void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
		assert(slot == 0);
		if(parent == std::addressof(prev)) parent = {};
	}


	virtual void on_update(const T& data){

	}

	void mark_updated(const std::size_t from_index) noexcept final{
		assert(from_index == 0);
		this->data_pending_state_ = data_pending_state::expired;
	}

	friend terminal_cached<T>;

	void on_push(manager& manager, const std::size_t from_index, const void* in_data_pass_by_copy) override {
		assert(from_index == 0);
		this->data_pending_state_ = data_pending_state::done;
		this->on_update(*static_cast<const T*>(in_data_pass_by_copy));
	}
};


export
template <typename T>
struct terminal_cached : terminal<T>{
	[[nodiscard]] const T& request_cache(){
		update_cache();
		return cache_;
	}

	[[nodiscard]] data_state get_data_state() const noexcept override{
		if(this->is_data_expired()) return data_state::expired;
		return data_state::fresh;
	}

	[[nodiscard]] explicit terminal_cached(propagate_behavior data_propagate_type)
	: terminal<T>(data_propagate_type){
	}

	[[nodiscard]] terminal_cached() = default;

protected:
	void on_update(const T& data) override{
		terminal<T>::on_update(data);
	}

	request_pass_handle<T> request_raw(const bool allow_expired) final{
		update_cache();

		if(!this->is_data_expired() || allow_expired){
			return react_flow::make_request_handle_expected_ref(cache_);
		} else{
			return react_flow::make_request_handle_unexpected<T>(data_state::expired);
		}
	}

	void on_pulse_received(manager& m) override{
		if(this->data_pending_state_ != data_pending_state::waiting_pulse)return;
		this->data_pending_state_ = data_pending_state::done;
		this->on_update(cache_);
	}

	void on_push(manager& manager, const std::size_t from_index, const void* in_data_pass_by_copy) override{
		switch(this->data_propagate_type_){
		case propagate_behavior::eager:
			this->data_pending_state_ = data_pending_state::done;
			cache_ = *static_cast<const T*>(in_data_pass_by_copy);
			this->on_update(cache_);
			break;
		case propagate_behavior::lazy:
			this->data_pending_state_ = data_pending_state::expired;
			break;
		case propagate_behavior::pulse:
			this->data_pending_state_ = data_pending_state::waiting_pulse;
			cache_ = *static_cast<const T*>(in_data_pass_by_copy);
			break;
		default: std::unreachable();
		}
	}

private:
	T cache_{};

	void update_cache(){
		if(this->is_data_expired()){
			if(auto rst = this->template request<T>(false)){
				this->data_pending_state_ = false;
				this->on_update(*rst.value());
			}
		}
	}
};


bool is_reachable(const node* start_node, const node* target_node, std::unordered_set<const node*>& visited) {
	if(start_node == nullptr)return false;
	if (start_node == target_node) {
		return true;
	}

	visited.insert(start_node);

	for (auto& neighbor : start_node->get_inputs()) {
		if (!visited.contains(neighbor)) {
			if (is_reachable(neighbor, target_node, visited)) {
				return true;
			}
		}
	}

	return false;
}

template <typename T>
void successor_entry::update(manager& manager, const T& data) const{
	assert(unstable_type_identity_of<T>() == entity->get_in_socket_type_index()[index]);
	entity->on_push(manager, index, std::addressof(data));
}

void successor_entry::update(manager& manager, const void* data, data_type_index checker) const{
#if MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
	if(entity->get_in_socket_type_index()[index] != checker){
		throw invalid_node{"Type Mismatch on update"};
	}
#endif
	entity->on_push(manager, index, data);
}

void successor_entry::mark_updated() const{
	entity->mark_updated(index);
}

bool is_ring_bridge(const node* self, const node* successors) {
	if (self == successors) {
		return true;
	}

	std::unordered_set<const node*> visited;
	return is_reachable(self, successors, visited);
}

}
