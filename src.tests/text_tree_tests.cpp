#include <gtest/gtest.h>

import std;

import mo_yanxi.i18n.text_tree;
import mo_yanxi.i18n.text_tree.react_flow;
import mo_yanxi.i18n.text_tree.toml;

namespace {

using mo_yanxi::i18n::find_text_tree_locale_file;
using mo_yanxi::i18n::frozen_text_tree;
using mo_yanxi::i18n::i18n_text_root_node;
using mo_yanxi::i18n::load_text_tree_locale_bundle;
using mo_yanxi::i18n::load_text_tree_toml_file;
using mo_yanxi::i18n::locale_name;
using mo_yanxi::i18n::locale_text_tree_load_options;
using mo_yanxi::i18n::lookup_status;
using mo_yanxi::i18n::make_locale_fallback_chain;
using mo_yanxi::i18n::missing_text_policy;
using mo_yanxi::i18n::node_kind;
using mo_yanxi::i18n::parse_text_tree_toml;
using mo_yanxi::i18n::text_subscription;
using mo_yanxi::i18n::text_tree_builder;

static_assert(!std::is_default_constructible_v<frozen_text_tree>);
static_assert(!std::is_move_constructible_v<frozen_text_tree>);
static_assert(!std::is_destructible_v<frozen_text_tree>);

void expect_text(const mo_yanxi::i18n::frozen_text_tree& tree, std::string_view path, std::string_view expected) {
	SCOPED_TRACE(std::string{path});
	const auto value = tree.find_text(path);
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(expected, *value);
}

void write_test_file(const std::filesystem::path& path, std::string_view text) {
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

mo_yanxi::i18n::frozen_text_tree_ptr make_locale_tree(
	std::string_view title,
	std::string_view ok,
	std::string_view quit) {
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

struct dummy_text_target {
	mo_yanxi::react_flow::manager& manager;
	std::string text{};
	std::size_t embedded_node_requests{};

	void set_text(std::string_view value) {
		text = value;
	}
};

template <typename T>
	requires std::derived_from<std::remove_cvref_t<T>, mo_yanxi::react_flow::node>
auto& react_flow_attach_impl(
	dummy_text_target& target,
	T&& node) {
	++target.embedded_node_requests;
	return target.manager.add_node(std::forward<T>(node));
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

} // namespace

TEST(TextTree, BasicPaths) {
	text_tree_builder builder;
	builder.set_text("app.title", "XRGUI");
	builder.set_text("app.empty", "");
	builder.make_dir("app.menu");
	const std::array<std::string_view, 2> subtitle_path{"app", "subtitle"};
	const std::array<std::string_view, 2> settings_path{"app", "settings"};
	builder.set_text(subtitle_path, "Segmented");
	builder.make_dir(settings_path);

	auto tree = std::move(builder).freeze();
	ASSERT_TRUE(tree);
	expect_text(*tree, "app.title", "XRGUI");
	expect_text(*tree, "app.empty", "");
	expect_text(*tree, "app.subtitle", "Segmented");
	EXPECT_EQ(node_kind::directory, tree->lookup("app.settings").kind);
	EXPECT_NE(nullptr, tree->raw_data());
	EXPECT_NE(0uz, tree->raw_size());

	const auto object_address = reinterpret_cast<std::uintptr_t>(tree.get());
	const auto raw_address = reinterpret_cast<std::uintptr_t>(tree->raw_data());
	EXPECT_GT(raw_address, object_address);
	EXPECT_EQ(0uz, raw_address % alignof(std::max_align_t));

	const auto title = tree->find_text("app.title");
	ASSERT_TRUE(title.has_value());
	EXPECT_EQ('\0', title->data()[title->size()]);
	ASSERT_TRUE((*tree)["app"]["title"].text().has_value());
	EXPECT_EQ("XRGUI", *(*tree)["app"]["title"].text());

	const auto app = tree->lookup("app");
	EXPECT_TRUE(app);
	EXPECT_EQ(node_kind::directory, app.kind);
	EXPECT_FALSE(tree->find_text("app.menu").has_value());
	EXPECT_EQ(lookup_status::invalid_path, tree->lookup("app..title").status);
	EXPECT_EQ(lookup_status::not_namespace, tree->lookup("app.title.extra").status);
}

TEST(TextTree, LargeSiblingLookupUsesNameOrder) {
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
	ASSERT_TRUE(tree);
	for(const auto name : names) {
		expect_text(*tree, std::format("root.{}", name), std::format("value-{}", name));
	}
	EXPECT_EQ(lookup_status::missing, tree->lookup("root.a0").status);
	EXPECT_EQ(lookup_status::missing, tree->lookup("root.alph").status);
	EXPECT_EQ(lookup_status::missing, tree->lookup("root.zzz").status);
}

TEST(TextTree, NamespaceMounts) {
	text_tree_builder builder;
	const auto common = builder.create_namespace();
	const auto nested = builder.create_namespace();

	builder.set_text(common, "buttons.ok", "OK");
	builder.set_text(nested, "qqq", "deep");
	builder.make_dir(common, "yyy.zzz.ooo");
	builder.mount_namespace(common, "yyy.zzz.ooo.ppp", nested);
	builder.mount_namespace("xxx", common);

	auto tree = std::move(builder).freeze();
	ASSERT_TRUE(tree);
	expect_text(*tree, "xxx.buttons.ok", "OK");
	expect_text(*tree, "xxx.yyy.zzz.ooo.ppp.qqq", "deep");
	EXPECT_EQ(node_kind::directory, tree->lookup("xxx").kind);
}

TEST(TextTree, HardLinks) {
	text_tree_builder builder;
	builder.set_text("base.leaf", "value");
	builder.make_dir("base.dir");
	builder.set_text("base.dir.child", "child");
	builder.add_hard_link("alias.leaf", "base.leaf");
	builder.add_hard_link("dir_alias", "base.dir");

	auto tree = std::move(builder).freeze();
	ASSERT_TRUE(tree);
	expect_text(*tree, "alias.leaf", "value");
	expect_text(*tree, "dir_alias.child", "child");
}

TEST(TextTree, SymbolicLinks) {
	text_tree_builder builder;
	builder.set_text("common.ok", "OK");
	builder.set_text("local.value", "local");
	builder.add_symbolic_link("abs.ok", "/common.ok");
	builder.add_symbolic_link("local.ok", "../common.ok");
	builder.add_symbolic_link("local.self", "./value");
	builder.add_symbolic_link("local.common", "../common");

	auto tree = std::move(builder).freeze();
	ASSERT_TRUE(tree);
	expect_text(*tree, "abs.ok", "OK");
	expect_text(*tree, "local.ok", "OK");
	expect_text(*tree, "local.self", "local");
	expect_text(*tree, "local.common.ok", "OK");
}

TEST(TextTree, TreePointerMounts) {
	text_tree_builder child_builder;
	child_builder.set_text("value", "external");
	child_builder.set_text("nested.leaf", "leaf");
	auto child = std::move(child_builder).freeze();
	ASSERT_TRUE(child);
	const auto before_mount_count = child->reference_count();

	text_tree_builder parent_builder;
	parent_builder.mount_tree("external", child);
	auto parent = std::move(parent_builder).freeze();
	ASSERT_TRUE(parent);

	EXPECT_EQ(before_mount_count + 1, child->reference_count());
	EXPECT_EQ(node_kind::tree_pointer, parent->lookup("external").kind);
	expect_text(*parent, "external.value", "external");
	ASSERT_TRUE((*parent)["external"]["nested"]["leaf"].text().has_value());
	EXPECT_EQ("leaf", *(*parent)["external"]["nested"]["leaf"].text());
}

TEST(TextTree, BuilderErrorsThrow) {
	EXPECT_THROW(
		[] {
			text_tree_builder builder;
			builder.set_text("a", "x");
			builder.set_text("a.b", "y");
		}(),
		std::exception);

	EXPECT_THROW(
		[] {
			text_tree_builder builder;
			builder.make_dir("a.b");
			builder.add_hard_link("a.b.c", "a");
		}(),
		std::exception);

	EXPECT_THROW(
		[] {
			text_tree_builder builder;
			const auto root = builder.root_namespace();
			builder.mount_namespace("self", root);
		}(),
		std::exception);

	EXPECT_THROW(
		[] {
			text_tree_builder builder;
			builder.add_symbolic_link("a", "/missing");
			(void)std::move(builder).freeze();
		}(),
		std::exception);

	EXPECT_THROW(
		[] {
			text_tree_builder builder;
			builder.add_symbolic_link("a", "/b");
			builder.add_symbolic_link("b", "/a");
			(void)std::move(builder).freeze();
		}(),
		std::exception);

	EXPECT_THROW(
		[] {
			text_tree_builder builder;
			builder.set_text("bad path", "x");
		}(),
		std::exception);
}

TEST(TextTreeToml, TextLinksAndMounts) {
	scoped_temp_dir temp;
	write_test_file(temp.path / "common.toml", R"(
[text.common]
ok = "OK"
empty = ""
)");

	write_test_file(temp.path / "en-US.toml", R"(
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
	ASSERT_TRUE(tree);
	expect_text(*tree, "app.title", "XRGUI");
	expect_text(*tree, "buttons.cancel", "Cancel");
	expect_text(*tree, "menu.exit", "Cancel");
	expect_text(*tree, "shared.common.ok", "OK");
	expect_text(*tree, "shared.common.empty", "");
	EXPECT_EQ(node_kind::directory, tree->lookup("empty_dir").kind);
}

TEST(TextTreeToml, InvalidValuesThrow) {
	EXPECT_THROW(
		(void)parse_text_tree_toml(R"(
[text]
answer = 42
)"),
		std::exception);

	EXPECT_THROW(
		(void)parse_text_tree_toml(R"(
[links]
"alias.value" = "/missing.value"
)"),
		std::exception);

	EXPECT_THROW(
		(void)parse_text_tree_toml(R"(
unexpected = "value"
)"),
		std::exception);
}

TEST(TextTreeToml, LocaleFallbackBundle) {
	const auto chain = make_locale_fallback_chain("zh_Hans_CN.UTF-8", "en-US");
	const std::vector<locale_name> expected_chain{"zh-Hans-CN", "zh-Hans", "zh", "en-US"};
	EXPECT_EQ(expected_chain, chain);

	scoped_temp_dir temp;
	write_test_file(temp.path / "zh.toml", R"(
[text.app]
title = "中文"
)");
	write_test_file(temp.path / "en-US.toml", R"(
[text.app]
title = "English"
)");

	auto file = find_text_tree_locale_file(
		"zh-Hans-CN",
		locale_text_tree_load_options{.bundle_dir = temp.path});
	ASSERT_TRUE(file.has_value());
	EXPECT_EQ("zh", file->locale);

	auto zh = load_text_tree_locale_bundle(
		"zh-Hans-CN",
		locale_text_tree_load_options{.bundle_dir = temp.path});
	ASSERT_TRUE(zh.has_value());
	EXPECT_EQ("zh", zh->locale);
	expect_text(*zh->tree, "app.title", "中文");

	auto en = load_text_tree_locale_bundle(
		"fr-CA",
		locale_text_tree_load_options{.bundle_dir = temp.path});
	ASSERT_TRUE(en.has_value());
	EXPECT_EQ("en-US", en->locale);
	expect_text(*en->tree, "app.title", "English");

	auto missing = load_text_tree_locale_bundle(
		"fr-CA",
		locale_text_tree_load_options{.bundle_dir = temp.path / "missing"});
	EXPECT_FALSE(missing.has_value());
}

TEST(I18nReactFlow, RootUpdatesSubscribers) {
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
	EXPECT_EQ(std::addressof(root.get_raw_cache()), observed_snapshot);

	std::string received;
	std::string_view received_view;
	auto& receiver = manager.add_node(mo_yanxi::react_flow::make_listener([&](std::string_view value) {
		received = std::string{value};
		received_view = value;
	}));

	auto& subscriber = mo_yanxi::i18n::bind_i18n_text(manager, root, receiver, "app.title");
	EXPECT_EQ("Hello", received);
	const auto en_title = en->find_text("app.title");
	ASSERT_TRUE(en_title.has_value());
	EXPECT_EQ(en_title->data(), received_view.data());
	EXPECT_EQ(1uz, root.get_raw_cache().revision);

	update_i18n_root(root, zh, "zh");
	EXPECT_EQ("你好", received);
	EXPECT_EQ(std::addressof(root.get_raw_cache()), observed_snapshot);
	ASSERT_NE(nullptr, observed_snapshot);
	EXPECT_EQ(root.get_raw_cache().revision, observed_snapshot->revision);
	const auto zh_title = zh->find_text("app.title");
	ASSERT_TRUE(zh_title.has_value());
	EXPECT_EQ(zh_title->data(), received_view.data());
	EXPECT_EQ(2uz, root.get_raw_cache().revision);
	EXPECT_EQ("zh", root.get_raw_cache().locale);

	subscriber.set_subscription(text_subscription{.path = "menu.quit"});
	EXPECT_EQ("退出", received);
}

TEST(I18nReactFlow, MissingFallbackPolicies) {
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
	EXPECT_EQ("Untitled", received);

	subscriber.set_subscription(text_subscription{
		.path = "missing.title",
		.fallback = "Untitled",
		.missing = missing_text_policy::path,
	});
	EXPECT_EQ("missing.title", received);

	root.update_value_quiet(mo_yanxi::i18n::text_snapshot{});
	subscriber.set_subscription(text_subscription{
		.path = "missing.title",
		.fallback = "Untitled",
		.missing = missing_text_policy::empty,
	});
	EXPECT_TRUE(received.empty());
}

TEST(I18nReactFlow, TargetBindingHelperTracksRootAndPathChanges) {
	auto en = make_locale_tree("Hello", "OK", "Quit");
	auto zh = make_locale_tree("你好", "确定", "退出");

	mo_yanxi::react_flow::manager manager{mo_yanxi::react_flow::manager_no_async};
	auto& root = manager.add_node<i18n_text_root_node>(en, "en");
	dummy_text_target target{manager};

	auto& subscriber = mo_yanxi::i18n::bind_i18n_text(root, target, "buttons.ok");
	EXPECT_EQ(1uz, target.embedded_node_requests);
	EXPECT_EQ("OK", target.text);

	update_i18n_root(root, zh, "zh");
	EXPECT_EQ("确定", target.text);

	subscriber.set_subscription(text_subscription{.path = "menu.quit"});
	EXPECT_EQ("退出", target.text);
}

TEST(I18nReactFlow, DirectListenerHelperCopiesCallback) {
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
	EXPECT_EQ("copy:Hello", received);

	const auto en_title = en->find_text("app.title");
	ASSERT_TRUE(en_title.has_value());
	EXPECT_EQ(en_title->data(), received_view.data());

	callback.prefix = "mutated:";
	update_i18n_root(root, zh, "zh");
	EXPECT_EQ("copy:你好", received);
}
