import std;

import mo_yanxi.i18n.text_tree;
import mo_yanxi.i18n.text_tree.react_flow;
import mo_yanxi.i18n.text_tree.toml;

namespace {

using mo_yanxi::i18n::lookup_status;
using mo_yanxi::i18n::frozen_text_tree;
using mo_yanxi::i18n::i18n_text_root_node;
using mo_yanxi::i18n::i18n_text_subscriber_node;
using mo_yanxi::i18n::text_subscription;
using mo_yanxi::i18n::missing_text_policy;
using mo_yanxi::i18n::node_kind;
using mo_yanxi::i18n::text_tree_builder;
using mo_yanxi::i18n::load_text_tree_toml_file;
using mo_yanxi::i18n::load_text_tree_locale_bundle;
using mo_yanxi::i18n::locale_text_tree_load_options;
using mo_yanxi::i18n::make_locale_fallback_chain;
using mo_yanxi::i18n::find_text_tree_locale_file;
using mo_yanxi::i18n::parse_text_tree_toml;
using mo_yanxi::i18n::locale_name;

static_assert(!std::is_default_constructible_v<frozen_text_tree>);
static_assert(!std::is_move_constructible_v<frozen_text_tree>);
static_assert(!std::is_destructible_v<frozen_text_tree>);

void require(bool value, std::string_view message) {
	if(!value) {
		throw std::runtime_error{std::string{message}};
	}
}

void require_text(const mo_yanxi::i18n::frozen_text_tree& tree, std::string_view path, std::string_view expected) {
	const auto value = tree.find_text(path);
	if(!value || *value != expected) {
		throw std::runtime_error{std::string{"unexpected text at "} + std::string{path}};
	}
}

template <typename Fn>
void require_throw(Fn&& fn, std::string_view message) {
	try {
		std::forward<Fn>(fn)();
	} catch(const std::exception&) {
		return;
	}
	throw std::runtime_error{std::string{message}};
}

void require_file_write(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file{path, std::ios::binary};
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	if(!file) {
		throw std::runtime_error{std::format("failed to write test file {}", path.string())};
	}
}

struct scoped_temp_dir {
	std::filesystem::path path;

	scoped_temp_dir()
		: path(std::filesystem::temp_directory_path()
		       / std::format("xrgui_text_tree_tests_{}", std::chrono::steady_clock::now().time_since_epoch().count())) {
		std::filesystem::create_directories(path);
	}

	~scoped_temp_dir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

void test_basic_paths() {
	text_tree_builder builder;
	builder.set_text("app.title", "XRGUI");
	builder.set_text("app.empty", "");
	builder.make_dir("app.menu");
	const std::array<std::string_view, 2> subtitle_path{"app", "subtitle"};
	const std::array<std::string_view, 2> settings_path{"app", "settings"};
	builder.set_text(subtitle_path, "Segmented");
	builder.make_dir(settings_path);

	auto tree = std::move(builder).freeze();
	require_text(*tree, "app.title", "XRGUI");
	require_text(*tree, "app.empty", "");
	require_text(*tree, "app.subtitle", "Segmented");
	require(tree->lookup("app.settings").kind == node_kind::directory, "segment span paths should create directories");
	require(tree->raw_data() != nullptr && tree->raw_size() != 0, "frozen tree should own one raw data buffer");

	const auto object_address = reinterpret_cast<std::uintptr_t>(tree.get());
	const auto raw_address = reinterpret_cast<std::uintptr_t>(tree->raw_data());
	require(raw_address > object_address, "raw data should be allocated after the frozen_text_tree object");
	require(raw_address % alignof(std::max_align_t) == 0, "raw data should be max_align_t aligned");

	const auto title = tree->find_text("app.title");
	require(title && title->data()[title->size()] == '\0', "stored text should be null terminated");
	require((*tree)["app"]["title"].text() && *(*tree)["app"]["title"].text() == "XRGUI",
	        "operator[] chain should resolve text");

	const auto app = tree->lookup("app");
	require(app && app.kind == node_kind::directory, "app should be a directory");
	require(!tree->find_text("app.menu"), "directory should not resolve as text");
	require(tree->lookup("app..title").status == lookup_status::invalid_path, "double-dot path should be invalid");
	require(tree->lookup("app.title.extra").status == lookup_status::not_namespace, "text cannot have child paths");
}

void test_large_sibling_lookup_uses_name_order() {
	constexpr std::array<std::string_view, 12> names{
		"A0",
		"a1",
		"a10",
		"a2",
		"alpha",
		"alphabet",
		"beta",
		"dash-name",
		"middle",
		"under_score",
		"z9",
		"zz",
	};

	text_tree_builder builder;
	for(const auto name : names) {
		builder.set_text(std::format("root.{}", name), std::format("value-{}", name));
	}

	auto tree = std::move(builder).freeze();
	for(const auto name : names) {
		require_text(*tree, std::format("root.{}", name), std::format("value-{}", name));
	}
	require(tree->lookup("root.a0").status == lookup_status::missing, "missing lower-bound sibling should not resolve");
	require(tree->lookup("root.alph").status == lookup_status::missing, "missing prefix sibling should not resolve");
	require(tree->lookup("root.zzz").status == lookup_status::missing, "missing upper-bound sibling should not resolve");
}

void test_namespace_mounts() {
	text_tree_builder builder;
	const auto common = builder.create_namespace();
	const auto nested = builder.create_namespace();

	builder.set_text(common, "buttons.ok", "OK");
	builder.set_text(nested, "qqq", "deep");
	builder.make_dir(common, "yyy.zzz.ooo");
	builder.mount_namespace(common, "yyy.zzz.ooo.ppp", nested);
	builder.mount_namespace("xxx", common);

	auto tree = std::move(builder).freeze();
	require_text(*tree, "xxx.buttons.ok", "OK");
	require_text(*tree, "xxx.yyy.zzz.ooo.ppp.qqq", "deep");
	require(tree->lookup("xxx").kind == node_kind::directory, "mounted namespace should resolve as a directory");
}

void test_hard_links() {
	text_tree_builder builder;
	builder.set_text("base.leaf", "value");
	builder.make_dir("base.dir");
	builder.set_text("base.dir.child", "child");
	builder.add_hard_link("alias.leaf", "base.leaf");
	builder.add_hard_link("dir_alias", "base.dir");

	auto tree = std::move(builder).freeze();
	require_text(*tree, "alias.leaf", "value");
	require_text(*tree, "dir_alias.child", "child");
}

void test_symbolic_links() {
	text_tree_builder builder;
	builder.set_text("common.ok", "OK");
	builder.set_text("local.value", "local");
	builder.add_symbolic_link("abs.ok", "/common.ok");
	builder.add_symbolic_link("local.ok", "../common.ok");
	builder.add_symbolic_link("local.self", "./value");
	builder.add_symbolic_link("local.common", "../common");

	auto tree = std::move(builder).freeze();
	require_text(*tree, "abs.ok", "OK");
	require_text(*tree, "local.ok", "OK");
	require_text(*tree, "local.self", "local");
	require_text(*tree, "local.common.ok", "OK");
}

void test_tree_pointer_mounts() {
	text_tree_builder child_builder;
	child_builder.set_text("value", "external");
	child_builder.set_text("nested.leaf", "leaf");
	auto child = std::move(child_builder).freeze();
	const auto before_mount_count = child->reference_count();

	text_tree_builder parent_builder;
	parent_builder.mount_tree("external", child);
	auto parent = std::move(parent_builder).freeze();

	require(child->reference_count() == before_mount_count + 1, "mounted tree should be reference counted");
	require(parent->lookup("external").kind == node_kind::tree_pointer, "tree pointer node should keep its kind");
	require_text(*parent, "external.value", "external");
	require((*parent)["external"]["nested"]["leaf"].text() && *(*parent)["external"]["nested"]["leaf"].text() == "leaf",
	        "operator[] chain should cross frozen_text_tree_ptr mounts");
}

void test_errors() {
	require_throw([] {
		text_tree_builder builder;
		builder.set_text("a", "x");
		builder.set_text("a.b", "y");
	}, "text and directory conflict should throw");

	require_throw([] {
		text_tree_builder builder;
		builder.make_dir("a.b");
		builder.add_hard_link("a.b.c", "a");
	}, "hard link cycle should throw");

	require_throw([] {
		text_tree_builder builder;
		const auto root = builder.root_namespace();
		builder.mount_namespace("self", root);
	}, "namespace mount cycle should throw");

	require_throw([] {
		text_tree_builder builder;
		builder.add_symbolic_link("a", "/missing");
		(void)std::move(builder).freeze();
	}, "dangling symbolic link should throw");

	require_throw([] {
		text_tree_builder builder;
		builder.add_symbolic_link("a", "/b");
		builder.add_symbolic_link("b", "/a");
		(void)std::move(builder).freeze();
	}, "symbolic link cycle should throw");

	require_throw([] {
		text_tree_builder builder;
		builder.set_text("bad path", "x");
	}, "invalid ASCII path should throw");
}

void test_toml_text_links_and_mounts() {
	scoped_temp_dir temp;
	require_file_write(temp.path / "common.toml", R"(
[text.common]
ok = "OK"
empty = ""
)");

	require_file_write(temp.path / "en-US.toml", R"(
locale = "en-US"

[text.app]
title = "XRGUI"

[text.buttons]
cancel = "Cancel"

[text.empty_dir]

[links]
"menu.exit" = "/buttons.cancel"

[mounts]
shared = "common.toml"
)");

	auto tree = load_text_tree_toml_file(temp.path / "en-US.toml");
	require_text(*tree, "app.title", "XRGUI");
	require_text(*tree, "buttons.cancel", "Cancel");
	require_text(*tree, "menu.exit", "Cancel");
	require_text(*tree, "shared.common.ok", "OK");
	require_text(*tree, "shared.common.empty", "");
	require(tree->lookup("empty_dir").kind == node_kind::directory, "empty TOML tables should create directories");
}

void test_toml_invalid_values() {
	require_throw([] {
		(void)parse_text_tree_toml(R"(
[text]
answer = 42
)");
	}, "non-string TOML text leaves should throw");

	require_throw([] {
		(void)parse_text_tree_toml(R"(
[links]
"alias.value" = "/missing.value"
)");
	}, "dangling TOML symbolic links should throw");

	require_throw([] {
		(void)parse_text_tree_toml(R"(
unexpected = "value"
)");
	}, "unknown TOML top-level keys should throw");
}

void test_locale_fallback_bundle() {
	const auto chain = make_locale_fallback_chain("zh_Hans_CN.UTF-8", "en-US");
	const std::vector<locale_name> expected_chain{"zh-Hans-CN", "zh-Hans", "zh", "en-US"};
	require(
		chain == expected_chain,
		"locale fallback chain should normalize and strip subtags");

	scoped_temp_dir temp;
	require_file_write(temp.path / "zh.toml", R"(
[text.app]
title = "中文"
)");
	require_file_write(temp.path / "en-US.toml", R"(
[text.app]
title = "English"
)");

	auto file = find_text_tree_locale_file(
		"zh-Hans-CN",
		locale_text_tree_load_options{.bundle_dir = temp.path});
	require(file && std::string_view{file->locale} == "zh", "locale file lookup should use language fallback");

	auto zh = load_text_tree_locale_bundle(
		"zh-Hans-CN",
		locale_text_tree_load_options{.bundle_dir = temp.path});
	require(zh && std::string_view{zh->locale} == "zh", "locale bundle should use language fallback");
	require_text(*zh->tree, "app.title", "中文");

	auto en = load_text_tree_locale_bundle(
		"fr-CA",
		locale_text_tree_load_options{.bundle_dir = temp.path});
	require(en && std::string_view{en->locale} == "en-US", "locale bundle should use default fallback");
	require_text(*en->tree, "app.title", "English");

	auto missing = load_text_tree_locale_bundle(
		"fr-CA",
		locale_text_tree_load_options{.bundle_dir = temp.path / "missing"});
	require(!missing, "locale bundle should return empty optional when no fallback file exists");
}

mo_yanxi::i18n::frozen_text_tree_ptr make_locale_tree(std::string_view title, std::string_view ok, std::string_view quit) {
	text_tree_builder builder;
	builder.set_text("app.title", title);
	builder.set_text("buttons.ok", ok);
	builder.set_text("menu.quit", quit);
	return std::move(builder).freeze();
}

void update_i18n_root(
	i18n_text_root_node& root,
	mo_yanxi::i18n::frozen_text_tree_ptr tree,
	std::string_view locale) {
	root.update_value(mo_yanxi::i18n::text_snapshot{
		.tree = std::move(tree),
		.revision = root.get_raw_cache().revision + 1,
		.locale = std::string{locale},
	});
}

void test_i18n_react_flow_updates() {
	auto en = make_locale_tree("Hello", "OK", "Quit");
	auto zh = make_locale_tree("你好", "确定", "退出");

	mo_yanxi::react_flow::manager manager{mo_yanxi::react_flow::manager_no_async};
	auto& root = manager.add_node<i18n_text_root_node>(en, "en");
	const mo_yanxi::i18n::text_snapshot* observed_snapshot{};
	auto& snapshot_receiver = manager.add_node(mo_yanxi::react_flow::make_listener(
		[&](const mo_yanxi::i18n::text_snapshot* value) {
			observed_snapshot = value;
		}));
	root.connect_successor(snapshot_receiver);
	root.pull_and_push(false);
	require(observed_snapshot == std::addressof(root.get_raw_cache()), "root should push a pointer to its cached snapshot");

	std::string received;
	std::string_view received_view;
	auto& receiver = manager.add_node(mo_yanxi::react_flow::make_listener([&](std::string_view value) {
		received = std::string{value};
		received_view = value;
	}));

	auto& subscriber = mo_yanxi::i18n::bind_i18n_text(manager, root, receiver, "app.title");
	require(received == "Hello", "binding should push the current root text immediately");
	const auto en_title = en->find_text("app.title");
	require(en_title && received_view.data() == en_title->data(), "subscriber should pass a view into the text tree");
	require(root.get_raw_cache().revision == 1, "root constructor should publish revision 1");

	update_i18n_root(root, zh, "zh");
	require(received == "你好", "root update should automatically refresh subscribers");
	require(observed_snapshot == std::addressof(root.get_raw_cache()), "root updates should keep pushing the cached snapshot pointer");
	require(observed_snapshot->revision == root.get_raw_cache().revision, "observed root snapshot pointer should expose current revision");
	const auto zh_title = zh->find_text("app.title");
	require(zh_title && received_view.data() == zh_title->data(), "root update should pass a view into the new tree");
	require(root.get_raw_cache().revision == 2 && root.get_raw_cache().locale == "zh", "root update should advance revision and locale");

	subscriber.set_subscription(text_subscription{.path = "menu.quit"});
	require(received == "退出", "subscriber path change should refresh from the current root");
}

void test_i18n_react_flow_missing_fallbacks() {
	auto en = make_locale_tree("Hello", "OK", "Quit");

	mo_yanxi::react_flow::manager manager{mo_yanxi::react_flow::manager_no_async};
	auto& root = manager.add_node<i18n_text_root_node>(en);

	std::string received;
	auto& receiver = manager.add_node(mo_yanxi::react_flow::make_listener([&](std::string_view value) {
		received = std::string{value};
	}));

	auto& subscriber = mo_yanxi::i18n::bind_i18n_text(
		manager,
		root,
		receiver,
		text_subscription{
			.path = "missing.title",
			.fallback = "Untitled",
			.missing = missing_text_policy::fallback,
		});
	require(received == "Untitled", "missing text should use explicit fallback policy");

	subscriber.set_subscription(text_subscription{
		.path = "missing.title",
		.fallback = "Untitled",
		.missing = missing_text_policy::path,
	});
	require(received == "missing.title", "missing text should be able to expose the path");

	root.update_value_quiet(mo_yanxi::i18n::text_snapshot{});
	subscriber.set_subscription(text_subscription{
		.path = "missing.title",
		.fallback = "Untitled",
		.missing = missing_text_policy::empty,
	});
	require(received.empty(), "empty missing policy should hide null roots");
}

struct dummy_text_target {
	mo_yanxi::react_flow::manager& manager;
	std::string text{};

	template <typename T>
	auto& request_embedded_react_node(T&& node) {
		return manager.add_node(std::forward<T>(node));
	}

	void set_text(std::string_view value) {
		text = value;
	}
};

void test_i18n_target_binding_helper() {
	auto en = make_locale_tree("Hello", "OK", "Quit");
	auto zh = make_locale_tree("你好", "确定", "退出");

	mo_yanxi::react_flow::manager manager{mo_yanxi::react_flow::manager_no_async};
	auto& root = manager.add_node<i18n_text_root_node>(en, "en");
	dummy_text_target target{manager};

	auto& subscriber = mo_yanxi::i18n::bind_i18n_text(root, target, "buttons.ok");
	require(target.text == "OK", "target binding helper should set initial text");

	update_i18n_root(root, zh, "zh");
	require(target.text == "确定", "target binding helper should track root updates");

	subscriber.set_subscription(text_subscription{.path = "menu.quit"});
	require(target.text == "退出", "target binding helper should track subscriber path changes");
}

struct recording_i18n_callback {
	std::string prefix{};
	std::string* received{};
	std::string_view* received_view{};

	void operator()(std::string_view value) {
		*received = prefix + std::string{value};
		*received_view = value;
	}
};

void test_i18n_direct_listener_helper() {
	auto en = make_locale_tree("Hello", "OK", "Quit");
	auto zh = make_locale_tree("你好", "确定", "退出");

	mo_yanxi::react_flow::manager manager{mo_yanxi::react_flow::manager_no_async};
	auto& root = manager.add_node<i18n_text_root_node>(en, "en");

	std::string received;
	std::string_view received_view;
	recording_i18n_callback callback{
		.prefix = "copy:",
		.received = std::addressof(received),
		.received_view = std::addressof(received_view),
	};

	auto& listener = mo_yanxi::i18n::bind_i18n_text_listener(
		manager,
		root,
		text_subscription{.path = "app.title"},
		callback);
	(void)listener;
	require(received == "copy:Hello", "direct i18n listener should pull once after connecting to root");

	const auto en_title = en->find_text("app.title");
	require(en_title && received_view.data() == en_title->data(), "direct i18n listener should pass a text_tree view");

	callback.prefix = "mutated:";
	update_i18n_root(root, zh, "zh");
	require(received == "copy:你好", "direct i18n listener should decay-copy the callback into the node");
}

}

int main() {
	try {
		test_basic_paths();
		test_large_sibling_lookup_uses_name_order();
		test_namespace_mounts();
		test_hard_links();
		test_symbolic_links();
		test_tree_pointer_mounts();
		test_errors();
		test_toml_text_links_and_mounts();
		test_toml_invalid_values();
		test_locale_fallback_bundle();
		test_i18n_react_flow_updates();
		test_i18n_react_flow_missing_fallbacks();
		test_i18n_target_binding_helper();
		test_i18n_direct_listener_helper();
		return 0;
	} catch(const std::exception& error) {
		std::println(std::cerr, "text_tree_tests failed: {}", error.what());
		return 1;
	}
}
