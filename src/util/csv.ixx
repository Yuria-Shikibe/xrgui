export module mo_yanxi.csv;

import std;

namespace mo_yanxi::csv{
export
constexpr inline bool is_numeric(std::string_view sv) noexcept {
	std::size_t start = 0;
	std::size_t end = sv.size();


	while (start < end && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r' || sv[start] == '\n')) {
		++start;
	}


	while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' || sv[end - 1] == '\n')) {
		--end;
	}

	// 如果全部都是空格，或者原字符串为空
	if (start == end) return false;

	std::size_t i = start;

	// 3. 处理可选的正负号
	if (sv[i] == '+' || sv[i] == '-') {
		++i;
	}

	// 只有符号没有数字是不合法的（例如 "+" 或 "-"）
	if (i == end) return false;

	bool has_digits = false;
	bool has_dot = false;
	bool has_exponent = false;

	// 4. 核心状态机扫描
	for (; i < end; ++i) {
		char c = sv[i];

		if (c >= '0' && c <= '9') {
			has_digits = true;
		} else if (c == '.') {
			// 不能有多个小数点，且小数点不能在科学计数法的 e/E 之后
			if (has_dot || has_exponent) return false;
			has_dot = true;
		} else if (c == 'e' || c == 'E') {
			// 指数符号前必须有数字，且不能有多个指数符号
			if (has_exponent || !has_digits) return false;
			has_exponent = true;
			has_digits = false; // 指数后面必须紧跟新的数字

			// 处理指数后面可选的正负号 (例如 e-5, E+10)
			if (i + 1 < end && (sv[i + 1] == '+' || sv[i + 1] == '-')) {
				++i;
			}
		} else {
			// 遇到任何其他字符，立刻判定为非法
			return false;
		}
	}

	// 必须以数字结尾，防止出现 "123e" 或仅仅是 "." 的情况
	return has_digits;
}


export
template <typename Alloc>
inline void unescape_csv_field(std::basic_string<char, std::char_traits<char>, Alloc>& target, std::string_view field){
	if(const std::size_t first_quote = field.find('"'); first_quote == std::string_view::npos){
		target = field;
		return;
	}

	target.resize_and_overwrite(field.size(), [&](char* buf, std::size_t /* max_size */){
		std::size_t read_pos = 0;
		std::size_t write_pos = 0;

		while(read_pos < field.size()){
			std::size_t next_quote = field.find('"', read_pos);

			if(next_quote == std::string_view::npos){
				std::size_t len = field.size() - read_pos;
				std::memcpy(buf + write_pos, field.data() + read_pos, len);
				write_pos += len;
				break;
			}

			std::size_t len = next_quote - read_pos;
			if(len > 0){
				std::memcpy(buf + write_pos, field.data() + read_pos, len);
				write_pos += len;
			}

			buf[write_pos++] = '"';
			read_pos = next_quote + 1;
			if(read_pos < field.size() && field[read_pos] == '"'){
				read_pos++;
			}
		}

		return write_pos;
	});
}

export struct coord{
	std::size_t row, col;
};

template <typename Func>
concept csv_callback = std::invocable<Func, coord, std::string_view>;

template <csv_callback Callback>
inline void parse_memory(std::string_view data, Callback&& callback, char delimiter = ','){
	std::size_t current_row = 0;
	std::size_t current_col = 0;
	bool in_quotes = false;
	std::size_t field_start = 0;

	auto yield_field = [&](std::size_t end_pos){
		std::string_view field = data.substr(field_start, end_pos - field_start);


		if(field.size() >= 2 && field.front() == '"' && field.back() == '"'){
			field = field.substr(1, field.size() - 2);
		}
		callback(coord{current_row, current_col}, field);
	};

	for(std::size_t i = 0; i < data.size(); ++i){
		char c = data[i];

		if(c == '"'){
			in_quotes = !in_quotes;
		} else if(c == delimiter && !in_quotes){
			yield_field(i);
			current_col++;
			field_start = i + 1;
		} else if((c == '\n' || c == '\r') && !in_quotes){
			bool is_rn = (c == '\r' && i + 1 < data.size() && data[i + 1] == '\n');
			yield_field(i);

			current_row++;
			current_col = 0;


			if(is_rn){
				++i;
			}
			field_start = i + 1;
		}
	}


	if(field_start < data.size() || (!data.empty() && data.back() == delimiter)){
		yield_field(data.size());
	}
}

export
template <csv_callback Callback>
inline void parse_file(const std::filesystem::path& file_path, Callback&& callback, char delimiter = ','){
	std::ifstream file(file_path, std::ios::binary | std::ios::ate);
	if(!file.is_open()){
		throw std::runtime_error("failed to open file: " + file_path.string());
	}

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::string buffer;
	buffer.resize_and_overwrite(size, [&](char* ptr, std::size_t sz){
		file.read(ptr, size);
		return size;
	});
	if(file){
		csv::parse_memory(std::string_view{buffer}, std::forward<Callback>(callback), delimiter);
	}
}
}
