module;

#include <cstdio>

export module mo_yanxi.gui.examples.default_config.colored_cerr;

import std;

namespace mo_yanxi{

class colored_cerr : public std::streambuf {
private:
	std::streambuf* source;
	std::vector<char> buffer;
	static constexpr std::size_t BUF_SIZE = 1024; // 1KB 缓冲区

	const char* RED_START = "\033[31m";
	const char* COLOR_RESET = "\033[0m";

	// 内部刷新逻辑
	bool flush_to_source() {
		std::ptrdiff_t n = pptr() - pbase();
		if (n <= 0) return true;

		// 批量注入颜色：[开始颜色][数据][结束颜色]
		if (source->sputn(RED_START, 5) != 5) return false;
		if (source->sputn(pbase(), n) != n) return false;
		if (source->sputn(COLOR_RESET, 4) != 4) return false;

		pbump(static_cast<int>(-n)); // 重置指针
		return source->pubsync() == 0;
	}

protected:
	// 当缓冲区满时调用
	virtual int_type overflow(int_type c) override {
		if (!flush_to_source()) return EOF;
		if (c != EOF) {
			*pptr() = static_cast<char>(c);
			pbump(1);
		}
		return c;
	}

	// 当调用 std::endl 或 flush 时调用
	virtual int sync() override {
		return flush_to_source() ? 0 : -1;
	}

public:
	explicit colored_cerr(std::streambuf* s) : source(s), buffer(BUF_SIZE) {
		// 设置缓冲区区域：开始、当前、结束
		setp(buffer.data(), buffer.data() + buffer.size());
	}

	~colored_cerr() override {
		sync();
	}
};

// 自动初始化器
struct global_cerr_optimizer {
	std::streambuf* original_buf;
	colored_cerr* optimized_buf;

	global_cerr_optimizer() {
		original_buf = std::cerr.rdbuf();
		optimized_buf = new colored_cerr(original_buf);
		std::cerr.rdbuf(optimized_buf);
	}

	~global_cerr_optimizer() {
		std::cerr.rdbuf(original_buf);
		delete optimized_buf;
	}
};

export
auto make_colored_errc(){
	std::optional<global_cerr_optimizer> _;

	using namespace mo_yanxi;

	if(auto ptr = std::getenv("COLORED"); ptr != nullptr && std::strcmp(ptr, "0") == 0){

	} else{
		_.emplace();
	}

	return _;
}

}