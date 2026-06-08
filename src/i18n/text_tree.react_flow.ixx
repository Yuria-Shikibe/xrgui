export module mo_yanxi.i18n.text_tree.react_flow;

import std;

export import mo_yanxi.i18n.text_tree;
export import mo_yanxi.react_flow;
export import mo_yanxi.react_flow.common;

namespace mo_yanxi::i18n{
export enum class missing_text_policy : std::uint8_t{
	empty,
	path,
	fallback,
	fallback_then_path,
};

export struct text_snapshot{
	frozen_text_tree_ptr tree{};
	std::uint64_t revision{};
	std::string locale{};
};

export struct text_subscription{
	std::string path{};
	std::string fallback{};
	missing_text_policy missing{missing_text_policy::fallback_then_path};

	bool operator==(const text_subscription&) const noexcept = default;
};

export [[nodiscard]] inline std::string_view missing_i18n_text(
	const text_subscription& subscription){
	switch(subscription.missing){
	case missing_text_policy::empty : return {};
	case missing_text_policy::path : return subscription.path;
	case missing_text_policy::fallback : return subscription.fallback;
	case missing_text_policy::fallback_then_path : return subscription.fallback.empty()
		                                                      ? subscription.path
		                                                      : subscription.fallback;
	default : std::unreachable();
	}
}

export [[nodiscard]] inline std::string_view resolve_i18n_text(
	const text_snapshot& snapshot,
	const text_subscription& subscription){
	if(!snapshot.tree || subscription.path.empty()){
		return missing_i18n_text(subscription);
	}

	if(const auto text = snapshot.tree->find_text(subscription.path)){
		return *text;
	}
	return missing_i18n_text(subscription);
}

export [[nodiscard]] inline std::string_view resolve_i18n_text(
	const text_snapshot* snapshot,
	const text_subscription& subscription){
	return snapshot == nullptr ? missing_i18n_text(subscription) : resolve_i18n_text(*snapshot, subscription);
}

export struct i18n_text_subscription_state{
	[[nodiscard]] i18n_text_subscription_state() = default;

	[[nodiscard]] explicit i18n_text_subscription_state(text_subscription subscription)
		: subscription_(std::move(subscription)){
	}

	[[nodiscard]] const text_subscription& subscription() const noexcept{
		return subscription_;
	}

	[[nodiscard]] std::string_view resolve(const text_snapshot* snapshot) const{
		return resolve_i18n_text(snapshot, subscription_);
	}

	bool assign_subscription(text_subscription subscription){
		const bool changed = subscription_ != subscription;
		if(changed){
			subscription_ = std::move(subscription);
		}
		return changed;
	}

private:
	text_subscription subscription_{};
};

struct i18n_text_snapshot_pointer{
	[[nodiscard]] const text_snapshot* operator()(const text_snapshot& snapshot) const noexcept{
		return std::addressof(snapshot);
	}
};

export struct i18n_text_root_node
	: react_flow::provider_cached<text_snapshot, const text_snapshot*, i18n_text_snapshot_pointer>{
	[[nodiscard]] i18n_text_root_node() = default;

	[[nodiscard]] explicit i18n_text_root_node(react_flow::propagate_type propagate_type)
		: provider_cached(propagate_type){
	}

	[[nodiscard]] explicit i18n_text_root_node(
		frozen_text_tree_ptr tree,
		std::string_view locale = {})
		: i18n_text_root_node(react_flow::propagate_type::eager, std::move(tree), locale){
	}

	[[nodiscard]] i18n_text_root_node(
		react_flow::propagate_type propagate_type,
		frozen_text_tree_ptr tree,
		std::string_view locale = {})
		: provider_cached(propagate_type){
		provider_cached::update_value_quiet(text_snapshot{
				.tree = std::move(tree),
				.revision = 1,
				.locale = std::string{locale},
			});
	}
};

export struct i18n_text_subscriber_node : react_flow::modifier<
		react_flow::descriptor<std::string_view>, react_flow::descriptor<const text_snapshot*>>{
	[[nodiscard]] i18n_text_subscriber_node() = default;

	[[nodiscard]] explicit i18n_text_subscriber_node(
		text_subscription subscription,
		react_flow::propagate_type propagate_type = react_flow::propagate_type::eager)
		: modifier(propagate_type),
		  subscription_state_(std::move(subscription)){
	}

	[[nodiscard]] explicit i18n_text_subscriber_node(
		std::string_view path,
		std::string_view fallback = {},
		missing_text_policy missing = missing_text_policy::fallback_then_path,
		react_flow::propagate_type propagate_type = react_flow::propagate_type::eager)
		: i18n_text_subscriber_node(
			text_subscription{
				.path = std::string{path},
				.fallback = std::string{fallback},
				.missing = missing,
			},
			propagate_type){
	}

	[[nodiscard]] const text_subscription& subscription() const noexcept{
		return subscription_state_.subscription();
	}

	void set_subscription(text_subscription subscription, bool pull_now = true){
		const bool changed = subscription_state_.assign_subscription(std::move(subscription));
		if(pull_now && changed){
			(void)pull_and_push(true);
		}
	}

protected:
	react_flow::data_carrier<std::string_view> operator()(
		const text_snapshot* snapshot) override{
		return subscription_state_.resolve(snapshot);
	}

private:
	i18n_text_subscription_state subscription_state_{};
};

export [[nodiscard]] inline i18n_text_subscriber_node make_i18n_text_subscriber(
	text_subscription subscription,
	react_flow::propagate_type propagate_type = react_flow::propagate_type::eager){
	return i18n_text_subscriber_node{std::move(subscription), propagate_type};
}

export template <typename Fn>
struct i18n_text_listener_callback{
	i18n_text_subscription_state subscription_state;
	Fn fn;

	void operator()(react_flow::data_carrier<const text_snapshot*>& snapshot){
		std::invoke(fn, subscription_state.resolve(snapshot.get()));
	}
};

export template <typename Fn>
struct i18n_text_listener
	: react_flow::listener<i18n_text_listener_callback<std::decay_t<Fn>>, const text_snapshot*>{
	using callback_type = i18n_text_listener_callback<std::decay_t<Fn>>;
	using base = react_flow::listener<callback_type, const text_snapshot*>;

	[[nodiscard]] i18n_text_listener(
		react_flow::propagate_type propagate_type,
		callback_type callback)
		: base(propagate_type, std::move(callback)){
	}

	[[nodiscard]] const text_subscription& subscription() const noexcept{
		return this->fn.subscription_state.subscription();
	}

	void set_subscription(text_subscription subscription, bool pull_now = true){
		const bool changed = this->fn.subscription_state.assign_subscription(std::move(subscription));
		if(pull_now && changed){
			(void)this->pull_and_push(true);
		}
	}
};

export template <typename Fn>
	requires std::invocable<std::decay_t<Fn>&, std::string_view>
[[nodiscard]] auto make_i18n_text_listener(
	text_subscription subscription,
	Fn&& fn,
	react_flow::propagate_type propagate_type = react_flow::propagate_type::eager){
	using callback_type = std::decay_t<Fn>;
	using adapter_type = i18n_text_listener_callback<callback_type>;
	return i18n_text_listener<callback_type>{
			propagate_type,
			adapter_type{
				.subscription_state = i18n_text_subscription_state{std::move(subscription)},
				.fn = callback_type{std::forward<Fn>(fn)},
			},
		};
}

template <typename Listener>
Listener& connect_i18n_text_listener(
	i18n_text_root_node& root,
	Listener& listener){
	root.connect_successor(listener);
	(void)listener.pull_and_push(true);
	return listener;
}

export template <typename Fn>
	requires std::invocable<std::decay_t<Fn>&, std::string_view>
auto& bind_i18n_text_listener(
	react_flow::manager& manager,
	i18n_text_root_node& root,
	text_subscription subscription,
	Fn&& fn,
	react_flow::propagate_type propagate_type = react_flow::propagate_type::eager){
	auto& listener = manager.add_node(make_i18n_text_listener(
		std::move(subscription),
		std::forward<Fn>(fn),
		propagate_type));
	return i18n::connect_i18n_text_listener(root, listener);
}

export inline i18n_text_subscriber_node& bind_i18n_text(
	react_flow::manager& manager,
	i18n_text_root_node& root,
	react_flow::type_aware_node<std::string_view>& receiver,
	text_subscription subscription){
	auto& subscriber = manager.add_node<i18n_text_subscriber_node>(std::move(subscription));
	react_flow::connect_chain(root, subscriber, receiver);
	(void)subscriber.pull_and_push(true);
	return subscriber;
}

export inline i18n_text_subscriber_node& bind_i18n_text(
	react_flow::manager& manager,
	i18n_text_root_node& root,
	react_flow::type_aware_node<std::string_view>& receiver,
	std::string_view path){
	return bind_i18n_text(
		manager,
		root,
		receiver,
		text_subscription{.path = std::string{path}});
}

export
template <typename Target, std::invocable<Target&, std::string_view> ApplyFn>
auto& bind_i18n_text(
	i18n_text_root_node& root,
	Target& target,
	text_subscription subscription,
	ApplyFn&& apply){
	auto& listener = react_flow::attach(
		target,
		i18n::make_i18n_text_listener(
			std::move(subscription),
			[&target, apply = std::forward<ApplyFn>(apply)](std::string_view value) mutable{
				std::invoke(apply, target, value);
			}));
	return i18n::connect_i18n_text_listener(root, listener);
}

export template <typename Target>
auto& bind_i18n_text(
	i18n_text_root_node& root,
	Target& target,
	text_subscription subscription){
	return i18n::bind_i18n_text(
		root,
		target,
		std::move(subscription),
		[](Target& text_target, std::string_view value){
			text_target.set_text(value);
		});
}

export template <typename Target>
auto& bind_i18n_text(
	i18n_text_root_node& root,
	Target& target,
	std::string_view path){
	return i18n::bind_i18n_text(
		root,
		target,
		text_subscription{.path = std::string{path}});
}
}
