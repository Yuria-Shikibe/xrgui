module;

#include <toml++/toml.hpp>

module mo_yanxi.i18n.text_tree.toml;

import std;
import magic_enum;
import mo_yanxi.io;

namespace mo_yanxi::i18n {
namespace {

enum class toml_section : std::uint8_t {
	locale,
	text,
	links,
	mounts,
};

struct parse_context {
	text_tree_builder& builder;
	const text_tree_toml_options& options;
	std::uint32_t mount_depth{};
};

struct path_segment_stack {
	std::array<std::string_view, frozen_text_tree::max_path_depth> segments{};
	std::size_t size{};

	[[nodiscard]] bool empty() const noexcept {
		return size == 0;
	}

	[[nodiscard]] std::span<const std::string_view> view() const noexcept {
		return {segments.data(), size};
	}

	[[nodiscard]] std::size_t depth() const noexcept {
		return size;
	}

	void push(std::string_view segment) {
		if(segment.empty()) {
			throw std::invalid_argument{"invalid empty i18n TOML path segment"};
		}
		if(size >= segments.size()) {
			throw std::invalid_argument{"i18n TOML path depth limit exceeded"};
		}
		segments[size++] = segment;
	}

	void push_key(std::string_view key) {
		while(true) {
			const auto dot = key.find('.');
			const auto segment = dot == std::string_view::npos ? key : key.substr(0, dot);
			push(segment);
			if(dot == std::string_view::npos) {
				return;
			}
			key.remove_prefix(dot + 1);
		}
	}

	void restore(std::size_t depth) noexcept {
		size = depth;
	}
};

struct scoped_path_segment {
	path_segment_stack& path;
	std::size_t old_depth;

	scoped_path_segment(path_segment_stack& path, std::string_view segment)
		: path(path),
		  old_depth(path.depth()) {
		path.push_key(segment);
	}

	scoped_path_segment(const scoped_path_segment&) = delete;
	scoped_path_segment& operator=(const scoped_path_segment&) = delete;

	~scoped_path_segment() {
		path.restore(old_depth);
	}
};

[[nodiscard]] std::string_view enum_name_or_unknown(auto value) noexcept {
	const std::string_view name = ::magic_enum::enum_name(value);
	return name.empty() ? std::string_view{"unknown"} : name;
}

[[nodiscard]] std::string describe_node_type(toml::node_type type) {
	std::ostringstream out;
	out << type;
	return std::move(out).str();
}

[[nodiscard]] std::string describe_path(toml_section section, std::span<const std::string_view> path) {
	const auto section_name = enum_name_or_unknown(section);
	if(path.empty()) {
		return std::string{section_name};
	}

	std::size_t path_size{};
	for(const auto segment : path) {
		path_size += segment.size();
	}
	std::string result;
	result.reserve(section_name.size() + 1 + path_size + path.size() - 1);
	result.append(section_name);
	for(const auto segment : path) {
		result.push_back('.');
		result.append(segment);
	}
	return result;
}

[[nodiscard]] std::string_view as_string_or_throw(
	const toml::node& node,
	toml_section section,
	std::span<const std::string_view> path) {
	if(const auto* value = node.as_string()) {
		return value->get();
	}

	throw std::invalid_argument{
		std::format(
			"i18n TOML value '{}' must be a string, got {}",
			describe_path(section, path),
			describe_node_type(node.type()))};
}

void validate_top_level_keys(const toml::table& table) {
	for(const auto& [key, value] : table) {
		(void)value;
		const std::string_view key_name{key.str()};
		if(!::magic_enum::enum_cast<toml_section>(key_name)) {
			throw std::invalid_argument{std::format("unknown i18n TOML top-level key '{}'", key_name)};
		}
	}
}

void parse_text_table(parse_context& context, const toml::table& table, path_segment_stack& path) {
	if(table.empty() && !path.empty()) {
		context.builder.make_dir(path.view());
		return;
	}

	for(const auto& [key, node] : table) {
		const scoped_path_segment segment{path, key.str()};
		if(const auto* child_table = node.as_table()) {
			parse_text_table(context, *child_table, path);
		} else {
			context.builder.set_text(path.view(), as_string_or_throw(node, toml_section::text, path.view()));
		}
	}
}

template <typename Fn>
void parse_string_leaf_table(
	const toml::table& table,
	toml_section section,
	path_segment_stack& path,
	Fn&& fn) {
	for(const auto& [key, node] : table) {
		const scoped_path_segment segment{path, key.str()};
		if(const auto* child_table = node.as_table()) {
			parse_string_leaf_table(*child_table, section, path, std::forward<Fn>(fn));
		} else {
			std::invoke(fn, path.view(), as_string_or_throw(node, section, path.view()));
		}
	}
}

[[nodiscard]] frozen_text_tree_ptr load_text_tree_toml_file_impl(
	const std::filesystem::path& path,
	const text_tree_toml_options& options,
	std::uint32_t mount_depth);

void parse_mounts(parse_context& context, const toml::table& table) {
	if(!context.options.allow_mounts) {
		throw std::invalid_argument{"i18n TOML mounts are disabled"};
	}
	if(context.mount_depth >= context.options.max_mount_depth) {
		throw std::invalid_argument{"i18n TOML mount depth limit exceeded"};
	}

	path_segment_stack path;
	parse_string_leaf_table(table, toml_section::mounts, path, [&](std::span<const std::string_view> mount_path, std::string_view relative_file) {
		const auto mounted_path = context.options.base_dir / std::filesystem::path{relative_file};
		auto mounted_options = context.options;
		mounted_options.base_dir = mounted_path.parent_path();
		auto mounted_tree = load_text_tree_toml_file_impl(mounted_path, mounted_options, context.mount_depth + 1);
		context.builder.mount_tree(mount_path, std::move(mounted_tree));
	});
}

[[nodiscard]] frozen_text_tree_ptr parse_text_tree_toml_impl(
	std::string_view source,
	const text_tree_toml_options& options,
	std::uint32_t mount_depth) {
	toml::table table{};
	try {
		table = toml::parse(source);
	} catch(const toml::parse_error& error) {
		throw std::invalid_argument{std::format("invalid i18n TOML: {}", error.description())};
	}

	validate_top_level_keys(table);

	if(auto locale = table[enum_name_or_unknown(toml_section::locale)]) {
		if(locale.as_string() == nullptr) {
			throw std::invalid_argument{"i18n TOML 'locale' must be a string"};
		}
	}

	text_tree_builder builder;
	parse_context context{
		.builder = builder,
		.options = options,
		.mount_depth = mount_depth,
	};

	if(auto text = table[enum_name_or_unknown(toml_section::text)]) {
		const auto* text_table = text.as_table();
		if(text_table == nullptr) {
			throw std::invalid_argument{"i18n TOML 'text' must be a table"};
		}

		path_segment_stack path;
		parse_text_table(context, *text_table, path);
	}

	if(auto links = table[enum_name_or_unknown(toml_section::links)]) {
		const auto* links_table = links.as_table();
		if(links_table == nullptr) {
			throw std::invalid_argument{"i18n TOML 'links' must be a table"};
		}

		path_segment_stack path;
		parse_string_leaf_table(*links_table, toml_section::links, path, [&](std::span<const std::string_view> link_path, std::string_view target) {
			context.builder.add_symbolic_link(link_path, target);
		});
	}

	if(auto mounts = table[enum_name_or_unknown(toml_section::mounts)]) {
		const auto* mounts_table = mounts.as_table();
		if(mounts_table == nullptr) {
			throw std::invalid_argument{"i18n TOML 'mounts' must be a table"};
		}
		parse_mounts(context, *mounts_table);
	}

	return std::move(builder).freeze();
}

[[nodiscard]] frozen_text_tree_ptr load_text_tree_toml_file_impl(
	const std::filesystem::path& path,
	const text_tree_toml_options& options,
	std::uint32_t mount_depth) {
	auto effective_options = options;
	if(effective_options.base_dir.empty()) {
		effective_options.base_dir = path.parent_path();
	}
	auto source = io::read_string(path);
	if(!source) {
		throw std::runtime_error{std::format("failed to read i18n TOML file '{}'", path.string())};
	}
	return parse_text_tree_toml_impl(*source, effective_options, mount_depth);
}

}

frozen_text_tree_ptr parse_text_tree_toml(
	std::string_view source,
	const text_tree_toml_options& options) {
	return parse_text_tree_toml_impl(source, options, 0);
}

frozen_text_tree_ptr load_text_tree_toml_file(
	const std::filesystem::path& path,
	const text_tree_toml_options& options) {
	return load_text_tree_toml_file_impl(path, options, 0);
}

locale_name normalize_locale_name(std::string_view locale) {
	if(locale.empty()) {
		return {};
	}

	const auto encoding_pos = locale.find_first_of(".@");
	if(encoding_pos != std::string_view::npos) {
		locale = locale.substr(0, encoding_pos);
	}
	if(locale == "C" || locale == "POSIX") {
		return {};
	}

	auto result = locale_name{locale};
	std::size_t size{};
	bool previous_separator = false;
	for(char c : result) {
		if(c == '_' || c == '-') {
			if(size != 0 && !previous_separator) {
				result[size++] = '-';
				previous_separator = true;
			}
			continue;
		}
		result[size++] = c;
		previous_separator = false;
	}
	if(size != 0 && result[size - 1] == '-') {
		--size;
	}
	result.resize(size);
	return result;
}

template <typename Fn>
bool visit_locale_fallback_chain(
	std::string_view locale,
	std::string_view default_locale,
	Fn&& fn) {
	auto normalized = normalize_locale_name(locale);
	const auto normalized_default = normalize_locale_name(default_locale);
	bool emitted_default = false;

	while(!std::string_view{normalized}.empty()) {
		if(normalized == normalized_default) {
			emitted_default = true;
		}
		const auto normalized_view = std::string_view{normalized};
		if(std::invoke(fn, normalized)) {
			return true;
		}

		const auto dash = normalized_view.rfind('-');
		if(dash == std::string::npos) {
			break;
		}
		normalized.assign(normalized_view.substr(0, dash));
	}

	if(!std::string_view{normalized_default}.empty() && !emitted_default) {
		return std::invoke(fn, normalized_default);
	}
	return false;
}

std::vector<locale_name> make_locale_fallback_chain(
	std::string_view locale,
	std::string_view default_locale) {
	std::vector<locale_name> chain;
	visit_locale_fallback_chain(locale, default_locale, [&](locale_name candidate) {
		chain.push_back(candidate);
		return false;
	});
	return chain;
}

std::optional<locale_text_tree_file> find_text_tree_locale_file(
	std::string_view locale,
	const locale_text_tree_load_options& options) {
	std::optional<locale_text_tree_file> result;
	visit_locale_fallback_chain(locale, options.default_locale, [&](locale_name candidate) {
		auto path = options.bundle_dir / std::filesystem::path{std::string_view{candidate}};
		path += ".toml";

		std::error_code ec;
		if(!std::filesystem::is_regular_file(path, ec)) {
			return false;
		}

		result = locale_text_tree_file{
			.locale = candidate,
			.path = std::move(path),
		};
		return true;
	});
	return result;
}

std::optional<locale_text_tree_load_result> load_text_tree_locale_bundle(
	std::string_view locale,
	const locale_text_tree_load_options& options) {
	auto file = find_text_tree_locale_file(locale, options);
	if(!file) {
		return std::nullopt;
	}

	auto toml_options = options.toml;
	toml_options.base_dir = file->path.parent_path();
	return locale_text_tree_load_result{
		.tree = load_text_tree_toml_file(file->path, toml_options),
		.locale = std::move(file->locale),
		.path = std::move(file->path),
	};
}

}
