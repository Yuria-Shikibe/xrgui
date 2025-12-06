export module mo_yanxi.react_flow;

export import :node_interface;
export import :manager;
export import :nodes;

import std;
//TODO support multi async consumer and better scheduler?

namespace mo_yanxi::react_flow{

export
template <std::ranges::input_range Rng = std::initializer_list<node*>>
void connect_chain(const Rng& chain){
	if constexpr (std::ranges::range<std::ranges::range_const_reference_t<Rng>>){
		std::ranges::for_each(chain, connect_chain<std::ranges::range_const_reference_t<Rng>>);
	}else{
		for (auto && [l, r] : chain | std::views::adjacent<2>){
			if constexpr (std::same_as<decltype(l), node&>){
				l.connect_successors(r);
			}else if(std::same_as<decltype(*l), node&>){
				(*l).connect_successors(*r);
			}

		}
	}

}

void example(){
	manager manager{};

	struct modifier_str_to_num : modifier_transient<int, std::string>{
		using modifier_transient::modifier_transient;
	protected:
		std::optional<int> operator()(const std::stop_token& stop_token, const std::string& arg) override{
			int val{};

			for(int i = 0; i < 4; ++i){
				if(stop_token.stop_requested()){
					return std::nullopt;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), val);
			if(ec == std::errc{}){
				return val * 10;
			}
			return std::nullopt;
		}
	};

	struct modifier_num_to_num : modifier_argument_cached<int, int>{
		using modifier_argument_cached::modifier_argument_cached;
	protected:
		std::optional<int> operator()(const std::stop_token& stop_token, const int& arg) override{
			return -arg;
		}
	};

	struct printer : terminal<int>{
		std::string prefix;

		[[nodiscard]] explicit printer(const std::string& prefix)
		: prefix(prefix){
		}



		void on_update(const int& data) override{
			terminal::on_update(data);

			std::println(std::cout, "{}: {}", prefix, data);
			std::cout.flush();
		}
	};

	auto& p  = manager.add_node<provider_cached<std::string>>();
	auto& m0 = manager.add_node<modifier_str_to_num>(async_type::async_latest);
	auto& m1 = manager.add_node<modifier_num_to_num>(async_type::none, true);
	auto& t0 = manager.add_node<printer>("Str To Num(delay 5s)");
	auto& t1 = manager.add_node<printer>("Negate of Num");

	connect_chain(std::initializer_list<std::initializer_list<node*>>{
		{&p, &m0, &t0},
		{&m0, &m1, &t1}
	});

	std::atomic_flag exit_flag{};

	auto thd = std::jthread([&](std::stop_token t){
		while(!t.stop_requested()){
			std::string str;
			std::cin >> str;

			if(str == "/exit"){
				exit_flag.test_and_set(std::memory_order::relaxed);
				break;
			}

			manager.push_posted_act([&, s = std::move(str)] mutable {
				p.update_value(std::move(s));
			});
		}
	});

	while(!exit_flag.test(std::memory_order::relaxed)){
		manager.update();

		if(t1.is_data_expired()){
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			t1.on_update(t1.request(true).value());
		}
	}
}

}
