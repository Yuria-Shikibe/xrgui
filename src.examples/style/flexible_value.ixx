//
// Created by Matrix on 2026/4/27.
//

export module mo_yanxi.react_flow.flexible_value;

import mo_yanxi.react_flow;

namespace mo_yanxi::react_flow{
export
template <typename T>
struct flexible_value : terminal<T>{
	using value_type = T;
private:
	value_type value;

public:

	[[nodiscard]] flexible_value() = default;

	[[nodiscard]] explicit(false) flexible_value(node& node_prov){
		set_value(node_prov);
	}

	[[nodiscard]] explicit(false) flexible_value(const value_type& value)
		: value(value){
	}

	const value_type& get_value() const noexcept{
		return value;
	}

	bool has_provenance() const noexcept{
		return this->get_inputs().front() != nullptr;
	}

	bool set_value(const value_type& pal) noexcept{
		if(!has_provenance()){
			value = pal;
			return true;
		}else{
			return false;
		}
	}

	bool set_value(node& node_prov){
		this->connect_predecessor(node_prov);
		return this->pull_and_push(false);
	}

	const value_type* operator->() const noexcept{
		return &value;
	}

	const value_type& operator*() const noexcept{
		return value;
	}

	flexible_value(const flexible_value& other) : value{other.value}{
		this->copy_inputs(other);
	}

	flexible_value(flexible_value&& other) noexcept = default;

	flexible_value& operator=(const flexible_value& other){
		if(this == &other) return *this;
		this->disconnect_self_from_context();
		this->copy_inputs(other);
		value = other.value;
		return *this;
	}

	flexible_value& operator=(flexible_value&& other) noexcept = default;

protected:
	void on_update(react_flow::data_carrier<value_type>& data) override{
		value = data.get();
	}
};

export
template <typename T>
struct flexible_value_holder : node_holder_portable<flexible_value<T>>{
	using node_holder_portable<flexible_value<T>>::node_holder_portable;


	flexible_value_holder(const flexible_value_holder& other) : node_holder_portable<flexible_value<T>>(other.node){
	}

	flexible_value_holder& operator=(const flexible_value_holder& other){
		if(this == &other) return *this;
		this->node.disconnect_self_from_context();
		this->node = other.node;
		return *this;
	}
};
}