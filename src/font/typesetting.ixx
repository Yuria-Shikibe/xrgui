module;

export module mo_yanxi.typesetting;

import std;

import mo_yanxi.math;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;

export namespace mo_yanxi{

template <typename Cont>
struct optional_stack {
	using value_type = std::ranges::range_value_t<Cont>;

	Cont data{};

	[[nodiscard]] optional_stack() = default;

	// 修改点 3: 构造函数现在接受底层容器
	[[nodiscard]] explicit optional_stack(const Cont& cont)
	   : data{cont}{
	}

	// 添加移动构造支持（可选，但推荐）
	[[nodiscard]] explicit optional_stack(Cont&& cont)
	   : data{std::move(cont)}{
	}

	// 修改点 4: 针对 vector/deque 的 clear
	void clear() noexcept{
		data.clear();
	}

	bool empty() const noexcept{
		return data.empty();
	}

	// 修改点 5: stack.push() -> container.push_back()
	void push(const value_type& val){
		data.push_back(val);
	}

	void push(value_type&& val){
		data.push_back(std::move(val));
	}

	std::optional<value_type> pop_and_get(){
		if(data.empty()){
			return std::nullopt;
		}

		// 修改点 6: stack.top() -> container.back()
		// 注意：先 move 元素，再 pop_back
		std::optional<value_type> rst{std::move(data.back())};
		data.pop_back();
		return rst;
	}

	void pop(){
		// 修改点 7: stack.pop() -> container.pop_back()
		if(!data.empty()) data.pop_back();
	}

	[[nodiscard]] value_type top(const value_type defaultVal) const noexcept{
		if(data.empty()){
			return defaultVal;
		} else{
			return data.back();
		}
	}

	[[nodiscard]] std::optional<value_type> top() const noexcept{
		if(data.empty()){
			return std::nullopt;
		} else{
			return data.back();
		}
	}

	// 修改点 8: 直接返回 back() 的引用
	[[nodiscard]] value_type& top_ref() noexcept{
		return data.back();
	}

	[[nodiscard]] const value_type& top_ref() const noexcept{
		return data.back();
	}

	// 额外建议: 既然直接暴露了 Cont，有时提供底层容器的访问也是有用的
	[[nodiscard]] const Cont& get_container() const noexcept {
		return data;
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



export struct layout_rect{
	float width;
	float ascender;
	float descender;

	[[nodiscard]] constexpr float height() const noexcept{
		return ascender + descender;
	}

	[[nodiscard]] constexpr math::vec2 size() const noexcept{
		return {width, height()};
	}

	[[nodiscard]] constexpr math::frect to_region(math::vec2 src) const noexcept{
		return {tags::from_extent, src.add_y(descender), width, -height()};
	}

	constexpr void max_height(layout_rect region) noexcept{
		ascender = math::max(ascender, region.ascender);
		descender = math::max(descender, region.descender);
	}

	constexpr void scale(float scale) noexcept{
		ascender *= scale;
		descender *= scale;
		width *= scale;
	}
};


export template <typename T>
	requires std::is_arithmetic_v<T>
constexpr T string_cast(std::string_view str, T def = 0){
	T t{def};
	std::from_chars(str.data(), str.data() + str.size(), t);
	return t;
}

export template <typename T>
	requires std::is_arithmetic_v<T>
constexpr std::vector<T> string_cast_seq(const std::string_view str, T def = 0, std::size_t expected = 2){
	const char* begin = str.data();
	const char* end = begin + str.size();

	std::vector<T> result{};
	if(expected) result.reserve(expected);

	while(!expected || result.size() != expected){
		if(begin == end) break;
		T t{def};
		auto [ptr, ec] = std::from_chars(begin, end, t);
		begin = ptr;

		if(ec == std::errc::invalid_argument){
			begin++;
		} else{
			result.push_back(t);
		}
	}

	return result;
}

template <typename T, std::size_t sz>
struct cast_result{
	std::array<T, sz> data;
	typename std::array<T, sz>::size_type size;
};

export template <std::size_t expected_count, typename T>
	requires std::is_arithmetic_v<T>
constexpr cast_result<T, expected_count> string_cast_seq(const std::string_view str, T def){
	const char* begin = str.data();
	const char* end = begin + str.size();

	std::array<T, expected_count> result{};
	std::size_t count{};

	while(count != expected_count && begin != end){
		T t{def};
		auto [ptr, ec] = std::from_chars(begin, end, t);
		begin = ptr;

		if(ec == std::errc::invalid_argument){
			begin++;
		} else{
			result[count++] = t;
		}
	}

	return {result, count};
}

}
