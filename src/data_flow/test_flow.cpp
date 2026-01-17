#include <iostream>
#include <cassert>
#include <vector>
#include <charconv>

import mo_yanxi.react_flow;
import std;

namespace mo_yanxi::react_flow::test {

void test_flow() {
    manager manager{};

    struct modifier_str_to_num : modifier_transient<int, std::string> {
        using modifier_transient::modifier_transient;
    protected:
        std::optional<int> operator()(const std::stop_token& stop_token, const std::string& arg) override {
            int val{};
            auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), val);
            if (ec == std::errc{}) {
                return val * 10;
            }
            return std::nullopt;
        }
    };

    struct modifier_num_to_num : modifier_argument_cached<int, int> {
        using modifier_argument_cached::modifier_argument_cached;
    protected:
        std::optional<int> operator()(const std::stop_token& stop_token, const int& arg) override {
            return -arg;
        }
    };

    struct recorder : terminal<int> {
        std::vector<int> received_values;

        void on_update(const int& data) override {
            terminal::on_update(data);
            received_values.push_back(data);
            std::cout << "Recorder received: " << data << std::endl;
        }
    };

    auto& p = manager.add_node<provider_cached<std::string>>();
    auto& m0 = manager.add_node<modifier_str_to_num>(async_type::none);
    auto& m1 = manager.add_node<modifier_num_to_num>(async_type::none, true); // lazy = true
    auto& t1 = manager.add_node<recorder>();

    connect_chain(std::initializer_list<std::initializer_list<node*>>{
        {&p, &m0, &m1, &t1}
    });

    manager.push_posted_act([&] {
        p.update_value("123");
    });

    manager.update();

    // t1 should be expired but not updated yet because m1 is lazy.
    if(t1.is_data_expired()){
         t1.check_expired_and_update(true);
    }

    if (t1.received_values.empty() || t1.received_values.back() != -1230) {
        std::cerr << "Test failed: Expected -1230, got " << (t1.received_values.empty() ? "empty" : std::to_string(t1.received_values.back())) << std::endl;
        std::exit(1);
    }

    std::cout << "Test passed!" << std::endl;
}

}

int main() {
    mo_yanxi::react_flow::test::test_flow();
    return 0;
}
