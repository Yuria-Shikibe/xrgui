module;

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

export module mo_yanxi.log;

import std;
import magic_enum;
import mo_yanxi.platform;

namespace mo_yanxi::log{

export
enum class level : std::uint8_t{
	trace,
	debug,
	info,
	warn,
	error,
	fatal,
	off
};

export
enum class terminal_color : std::uint8_t{
	none = 0,
	black,
	red,
	green,
	yellow,
	blue,
	magenta,
	cyan,
	white,
	bright_black,
	bright_red,
	bright_green,
	bright_yellow,
	bright_blue,
	bright_magenta,
	bright_cyan,
	bright_white
};

export
struct channel_config{
	terminal_color color{};
	bool force_ignore{};
};

export
[[nodiscard]] constexpr std::string_view level_name(const level value) noexcept{
	const std::string_view name = ::magic_enum::enum_name(value);
	if(name.empty()){
		std::unreachable();
	}
	return name;
}

export
[[nodiscard]] constexpr level parse_level(const std::string_view text){
	if(text == "trace") return level::trace;
	if(text == "debug") return level::debug;
	if(text == "info") return level::info;
	if(text == "warn" || text == "warning") return level::warn;
	if(text == "error") return level::error;
	if(text == "fatal") return level::fatal;
	if(text == "off") return level::off;

	throw std::invalid_argument{std::format("invalid log level '{}'", text)};
}

export
struct site{
	std::string_view category;
	std::source_location location;

	[[nodiscard]] explicit(false) site(
		const std::string_view category_,
		const std::source_location location_ = std::source_location::current()) noexcept
		: category{category_},
		  location{location_}{
	}
};

export
struct record{
	level severity{};
	std::string category;
	std::string message;
	terminal_color category_color{};
	std::source_location location;
	std::chrono::steady_clock::duration elapsed{};
	std::thread::id thread_id{};
	std::string stacktrace{};
};

export
class sink{
public:
	virtual ~sink() = default;
	virtual void write(const record& entry) = 0;
};

namespace impl{
[[nodiscard]] inline std::chrono::steady_clock::time_point start_time() noexcept{
	static const auto start = std::chrono::steady_clock::now();
	return start;
}

[[nodiscard]] constexpr std::string_view short_file_name(const std::string_view file_name) noexcept{
	const auto pos = file_name.find_last_of("/\\");
	if(pos == std::string_view::npos){
		return file_name;
	}

	return file_name.substr(pos + 1);
}

[[nodiscard]] inline std::string elapsed_time_text(const std::chrono::steady_clock::duration elapsed){
	const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	const auto minutes = elapsed_ms / 60'000;
	const auto seconds = (elapsed_ms / 1'000) % 60;
	const auto milliseconds = elapsed_ms % 1'000;

	return std::format("{:02}:{:02}.{:03}", minutes, seconds, milliseconds);
}

[[nodiscard]] constexpr char level_letter(const level value) noexcept{
	switch(value){
	case level::trace: return 'T';
	case level::debug: return 'D';
	case level::info: return 'I';
	case level::warn: return 'W';
	case level::error: return 'E';
	case level::fatal: return 'F';
	case level::off: return 'O';
	}

	std::unreachable();
}

[[nodiscard]] constexpr std::string_view ansi_color_code(const terminal_color color) noexcept{
	switch(color){
	case terminal_color::none: return "";
	case terminal_color::black: return "\x1b[30m";
	case terminal_color::red: return "\x1b[31m";
	case terminal_color::green: return "\x1b[32m";
	case terminal_color::yellow: return "\x1b[33m";
	case terminal_color::blue: return "\x1b[34m";
	case terminal_color::magenta: return "\x1b[35m";
	case terminal_color::cyan: return "\x1b[36m";
	case terminal_color::white: return "\x1b[37m";
	case terminal_color::bright_black: return "\x1b[90m";
	case terminal_color::bright_red: return "\x1b[91m";
	case terminal_color::bright_green: return "\x1b[92m";
	case terminal_color::bright_yellow: return "\x1b[93m";
	case terminal_color::bright_blue: return "\x1b[94m";
	case terminal_color::bright_magenta: return "\x1b[95m";
	case terminal_color::bright_cyan: return "\x1b[96m";
	case terminal_color::bright_white: return "\x1b[97m";
	}

	std::unreachable();
}

[[nodiscard]] inline std::string location_text(const std::source_location& location){
	return std::format(
		"{}:{}:{} in {}",
		short_file_name(location.file_name()),
		location.line(),
		location.column(),
		location.function_name());
}

[[nodiscard]] inline std::string stacktrace_text(){
	const auto trace = std::stacktrace::current(2, 32);
	if(trace.empty()){
		return {};
	}

	std::string result{};
	for(const auto& [index, entry] : trace | std::views::enumerate){
		if(!result.empty()){
			result += '\n';
		}

		const std::string file = entry.source_file();
		const auto line = entry.source_line();
		if(!file.empty() && line != 0){
			result += std::format(
				"    #{:02} {}:{} {}",
				index,
				short_file_name(file),
				line,
				entry.description());
		} else{
			result += std::format("    #{:02} {}", index, entry.description());
		}
	}

	return result;
}

inline void write_line(std::ostream& output, const record& entry, const bool use_color){
	output
		<< '['
		<< elapsed_time_text(entry.elapsed)
		<< ' ' << level_letter(entry.severity)
		<< ' ';
	if(use_color && entry.category_color != terminal_color::none){
		output << ansi_color_code(entry.category_color) << entry.category << "\x1b[0m";
	} else{
		output << entry.category;
	}
	output
		<< "] " << entry.message
		<< " @ " << location_text(entry.location)
		<< '\n';
	if(!entry.stacktrace.empty()){
		output << entry.stacktrace << '\n';
	}
}

[[nodiscard]] inline level default_min_level(){
#ifndef NDEBUG
	return level::debug;
#else
	return level::info;
#endif
}

[[nodiscard]] inline std::optional<std::string> environment_variable(const char* name){
	return platform::get_environment_variable(name);
}
}

export
class console_sink final : public sink{
public:
	void write(const record& entry) override{
		if(entry.severity >= level::warn){
			impl::write_line(std::cerr, entry, true);
			return;
		}

		impl::write_line(std::clog, entry, true);
	}
};

export
class file_sink final : public sink{
private:
	std::ofstream output_;

public:
	[[nodiscard]] explicit file_sink(const std::filesystem::path& path)
		: output_{path, std::ios::app}{
		if(!output_){
			throw std::runtime_error{std::format("failed to open log file '{}'", path.string())};
		}
	}

	void write(const record& entry) override{
		impl::write_line(output_, entry, false);
		output_.flush();
	}
};

export
class logger{
private:
	std::atomic<level> min_level_{impl::default_min_level()};
	std::mutex sink_mutex_{};
	std::vector<std::unique_ptr<sink>> sinks_{};
	mutable std::mutex channel_mutex_{};
	std::unordered_map<std::string, channel_config> channels_{};

	[[nodiscard]] channel_config channel_config_for(const std::string_view category) const{
		std::lock_guard lock{channel_mutex_};
		for(const auto& [channel, config] : channels_){
			if(channel == category){
				return config;
			}
		}
		return {};
	}

public:
	[[nodiscard]] logger(){
		sinks_.push_back(std::make_unique<console_sink>());
		if(auto env_level = impl::environment_variable("XRGUI_LOG_LEVEL")){
			this->set_min_level(parse_level(*env_level));
		}
		if(auto env_file = impl::environment_variable("XRGUI_LOG_FILE")){
			if(env_file->empty()){
				throw std::invalid_argument{"XRGUI_LOG_FILE is set but empty"};
			}
			this->add_sink(std::make_unique<file_sink>(*env_file));
		}
	}

	void set_min_level(const level value) noexcept{
		min_level_.store(value, std::memory_order_release);
	}

	[[nodiscard]] level min_level() const noexcept{
		return min_level_.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool enabled(const level severity) const noexcept{
		return severity >= min_level() && min_level() != level::off;
	}

	[[nodiscard]] bool enabled(const level severity, const std::string_view category) const{
		if(!this->enabled(severity)){
			return false;
		}

		return !this->channel_config_for(category).force_ignore;
	}

	void register_channel(std::string category, channel_config config){
		if(category.empty()){
			throw std::invalid_argument{"log channel category cannot be empty"};
		}

		std::lock_guard lock{channel_mutex_};
		channels_.insert_or_assign(std::move(category), config);
	}

	void set_channel_color(std::string category, const terminal_color color){
		if(category.empty()){
			throw std::invalid_argument{"log channel category cannot be empty"};
		}

		std::lock_guard lock{channel_mutex_};
		channels_[std::move(category)].color = color;
	}

	void clear_channel_color(const std::string_view category){
		std::lock_guard lock{channel_mutex_};
		for(auto& [channel, config] : channels_){
			if(channel == category){
				config.color = terminal_color::none;
				return;
			}
		}
	}

	void set_channel_ignored(std::string category, const bool ignored){
		if(category.empty()){
			throw std::invalid_argument{"log channel category cannot be empty"};
		}

		std::lock_guard lock{channel_mutex_};
		channels_[std::move(category)].force_ignore = ignored;
	}

	void add_sink(std::unique_ptr<sink> next_sink){
		if(next_sink == nullptr){
			throw std::invalid_argument{"log sink cannot be null"};
		}

		std::lock_guard lock{sink_mutex_};
		sinks_.push_back(std::move(next_sink));
	}

	void clear_sinks(){
		std::lock_guard lock{sink_mutex_};
		sinks_.clear();
	}

	void write(record entry){
		const channel_config config = this->channel_config_for(entry.category);
		if(!this->enabled(entry.severity) || config.force_ignore){
			return;
		}

		entry.category_color = config.color;
		entry.elapsed = std::chrono::steady_clock::now() - impl::start_time();
		entry.thread_id = std::this_thread::get_id();
		if(entry.severity >= level::warn){
			entry.stacktrace = impl::stacktrace_text();
		}

		std::lock_guard lock{sink_mutex_};
		for(auto& current_sink : sinks_){
			current_sink->write(entry);
		}
	}
};

export
[[nodiscard]] logger& default_logger(){
	static logger instance{};
	return instance;
}

export
inline void set_min_level(const level value) noexcept{
	default_logger().set_min_level(value);
}

export
[[nodiscard]] inline bool enabled(const level severity) noexcept{
	return default_logger().enabled(severity);
}

export
[[nodiscard]] inline bool enabled(const level severity, const std::string_view category){
	return default_logger().enabled(severity, category);
}

export
inline void register_category(std::string category, const channel_config config){
	default_logger().register_channel(std::move(category), config);
}

export
inline void register_channel(std::string category, const channel_config config){
	default_logger().register_channel(std::move(category), config);
}

export
inline void set_category_color(std::string category, const terminal_color color){
	default_logger().set_channel_color(std::move(category), color);
}

export
inline void set_channel_color(std::string category, const terminal_color color){
	default_logger().set_channel_color(std::move(category), color);
}

export
inline void clear_category_color(const std::string_view category){
	default_logger().clear_channel_color(category);
}

export
inline void set_category_ignored(std::string category, const bool ignored = true){
	default_logger().set_channel_ignored(std::move(category), ignored);
}

export
inline void set_channel_ignored(std::string category, const bool ignored = true){
	default_logger().set_channel_ignored(std::move(category), ignored);
}

export
inline void add_sink(std::unique_ptr<sink> next_sink){
	default_logger().add_sink(std::move(next_sink));
}

export
inline void add_file_sink(const std::filesystem::path& path){
	add_sink(std::make_unique<file_sink>(path));
}

export
inline void write(const level severity, const site at, std::string message){
	default_logger().write(record{
		.severity = severity,
		.category = std::string{at.category},
		.message = std::move(message),
		.location = at.location
	});
}

export
template <typename... Args>
void write(const level severity, const site at, std::format_string<Args...> fmt, Args&&... args){
	if(!enabled(severity, at.category)){
		return;
	}

	write(severity, at, std::format(fmt, std::forward<Args>(args)...));
}

export
template <typename... Args>
void trace(const site at, std::format_string<Args...> fmt, Args&&... args){
	write(level::trace, at, fmt, std::forward<Args>(args)...);
}

export
template <typename... Args>
void debug(const site at, std::format_string<Args...> fmt, Args&&... args){
	write(level::debug, at, fmt, std::forward<Args>(args)...);
}

export
template <typename... Args>
void info(const site at, std::format_string<Args...> fmt, Args&&... args){
	write(level::info, at, fmt, std::forward<Args>(args)...);
}

export
template <typename... Args>
void warn(const site at, std::format_string<Args...> fmt, Args&&... args){
	write(level::warn, at, fmt, std::forward<Args>(args)...);
}

export
template <typename... Args>
void error(const site at, std::format_string<Args...> fmt, Args&&... args){
	write(level::error, at, fmt, std::forward<Args>(args)...);
}

export
template <typename... Args>
void fatal(const site at, std::format_string<Args...> fmt, Args&&... args){
	write(level::fatal, at, fmt, std::forward<Args>(args)...);
}

}
