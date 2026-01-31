module;

export module mo_yanxi.typesetting;

import std;

export namespace mo_yanxi{

template <unsigned long long N>
struct get_smallest_uint{
	using type =
	std::conditional_t<(N <= 0xFF), std::uint8_t,
		std::conditional_t<(N <= 0xFFFF), std::uint16_t,
			std::conditional_t<(N <= 0xFFFFFFFF), std::uint32_t,
				std::uint64_t>>>;
};

template <unsigned long long N>
using smallest_uint_t = typename get_smallest_uint<N>::type;


template <std::size_t N>
class static_string {
	static_assert(N > 0, "Capacity must be greater than 0");

public:
	static constexpr std::size_t max_size = N - 1;

	using size_type = smallest_uint_t<N>;

	constexpr static_string() noexcept : m_data{0} {}

	constexpr static_string(const char* str) noexcept {
		assign(str);
	}

	constexpr static_string(std::string_view sv) noexcept {
		assign(sv);
	}

	// 核心修改：区分编译期和运行时的赋值逻辑
	constexpr void assign(std::string_view sv) {
		m_size = std::min(sv.length(), max_size);

		if consteval{
			// [编译期] 必须使用循环，因为 memcpy 不是 constexpr
			for (std::size_t i = 0; i < m_size; ++i) {
				m_data[i] = sv[i];
			}
		} else {
			if (m_size > 0) {
				std::memcpy(m_data, sv.data(), m_size);
			}
		}

		// 强制 zero termination
		m_data[m_size] = '\0';
	}

	[[nodiscard]] constexpr std::size_t size() const noexcept {
		return m_size;
	}

	[[nodiscard]] constexpr const char* c_str() const noexcept {
		return m_data;
	}

	[[nodiscard]] constexpr const char* data() const noexcept {
		return m_data;
	}

	constexpr operator std::string_view() const noexcept {
		return std::string_view(m_data, m_size);
	}

private:
	size_type m_size{};
	char m_data[N];
};

template <typename T, typename Cont = std::vector<T>>
struct optional_stack{
	std::stack<T, Cont> stack{};

	[[nodiscard]] optional_stack() = default;

	[[nodiscard]] explicit optional_stack(const std::stack<T, Cont>& stack)
		: stack{stack}{
	}

	void push(const T& val){
		stack.push(val);
	}

	void push(T&& val){
		stack.push(std::move(val));
	}

	std::optional<T> pop_and_get(){
		if(stack.empty()){
			return std::nullopt;
		}

		const std::optional rst{std::move(stack.top())};
		stack.pop();
		return rst;
	}

	void pop(){
		if(!stack.empty()) stack.pop();
	}

	[[nodiscard]] T top(const T defaultVal) const noexcept{
		if(stack.empty()){
			return defaultVal;
		} else{
			return stack.top();
		}
	}

	[[nodiscard]] std::optional<T> top() const noexcept{
		if(stack.empty()){
			return std::nullopt;
		} else{
			return stack.top();
		}
	}
};
}

namespace mo_yanxi::type_setting{
namespace glyph_size{
export{
	// 字体排印标准：1 英寸 = 72 点 (pt)
	inline constexpr float points_per_inch = 72.0f;

	inline constexpr float pt_6 = 6.0f;
	inline constexpr float pt_7 = 7.0f;
	inline constexpr float pt_8 = 8.0f;
	inline constexpr float pt_9 = 9.0f;

	// --- 标准阅读字号 (Web/Print 默认) ---
	// 网页和移动设备上常见的标准默认尺寸
	inline constexpr float pt_10 = 10.0f;
	inline constexpr float pt_11 = 11.0f;
	inline constexpr float pt_12 = 12.0f; // 传统印刷和桌面软件的默认尺寸
	inline constexpr float pt_14 = 14.0f;
	inline constexpr float pt_16 = 16.0f; // 现代网页设计的常见默认尺寸

	// --- 标题和强调字号 ---
	inline constexpr float pt_18 = 18.0f;
	inline constexpr float pt_20 = 20.0f;
	inline constexpr float pt_24 = 24.0f;

	// --- 大标题和展示字号 ---
	inline constexpr float pt_36 = 36.0f;
	inline constexpr float pt_48 = 48.0f;
	inline constexpr float pt_72 = 72.0f; // 恰好等于 1 英寸

	// --- 中文字号体系对应点数 (pt) ---

	// 大字号
	inline constexpr float pt_chu_hao = 42.0f; // 初号
	inline constexpr float pt_xiao_chu = 36.0f; // 小初

	// 标题字号
	inline constexpr float pt_yi_hao = 26.0f; // 一号
	inline constexpr float pt_xiao_yi = 24.0f; // 小一
	inline constexpr float pt_er_hao = 22.0f; // 二号
	inline constexpr float pt_xiao_er = 18.0f; // 小二

	// 正文字号
	inline constexpr float pt_san_hao = 16.0f; // 三号
	inline constexpr float pt_xiao_san = 15.0f; // 小三
	inline constexpr float pt_si_hao = 14.0f; // 四号
	inline constexpr float pt_xiao_si = 12.0f; // 小四
	inline constexpr float pt_wu_hao = 10.5f; // 五号 (常用正文尺寸)

	// 脚注/细小字号
	inline constexpr float pt_xiao_wu = 9.0f; // 小五
	inline constexpr float pt_liu_hao = 7.5f; // 六号
	inline constexpr float pt_xiao_liu = 6.5f; // 小六
	inline constexpr float pt_qi_hao = 5.5f; // 七号
	inline constexpr float pt_ba_hao = 5.0f; // 八号

	inline constexpr float standard_size = pt_xiao_er;
}
}
}
