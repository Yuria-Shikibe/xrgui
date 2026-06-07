export module mo_yanxi.i18n.text_tree.toml;

import std;
export import mo_yanxi.i18n.text_tree;

namespace mo_yanxi::i18n {

export using locale_name = std::string;

export struct text_tree_toml_options {
	std::filesystem::path base_dir{};
	bool allow_mounts{true};
	std::uint32_t max_mount_depth{16};
};

export struct locale_text_tree_load_options {
	std::filesystem::path bundle_dir{std::filesystem::current_path() / "assets/i18n/bundle"};
	locale_name default_locale{"en-US"};
	text_tree_toml_options toml{};
};

export struct locale_text_tree_file {
	locale_name locale{};
	std::filesystem::path path{};
};

export struct locale_text_tree_load_result {
	frozen_text_tree_ptr tree{};
	locale_name locale{};
	std::filesystem::path path{};
};

export [[nodiscard]] frozen_text_tree_ptr parse_text_tree_toml(
	std::string_view source,
	const text_tree_toml_options& options = {});

export [[nodiscard]] frozen_text_tree_ptr load_text_tree_toml_file(
	const std::filesystem::path& path,
	const text_tree_toml_options& options = {});

export [[nodiscard]] locale_name normalize_locale_name(std::string_view locale);

export [[nodiscard]] std::vector<locale_name> make_locale_fallback_chain(
	std::string_view locale,
	std::string_view default_locale = "en-US");

export [[nodiscard]] std::optional<locale_text_tree_file> find_text_tree_locale_file(
	std::string_view locale,
	const locale_text_tree_load_options& options = {});

export [[nodiscard]] std::optional<locale_text_tree_load_result> load_text_tree_locale_bundle(
	std::string_view locale,
	const locale_text_tree_load_options& options = {});

}
