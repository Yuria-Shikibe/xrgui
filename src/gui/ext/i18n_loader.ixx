export module mo_yanxi.gui.i18n_loader;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.i18n.text_tree.toml;

import mo_yanxi.platform;

namespace mo_yanxi::gui {

export struct i18n_load_options {
	std::filesystem::path bundle_dir{std::filesystem::current_path() / "assets/i18n/bundle"};
	i18n::locale_name default_locale{"en-US"};
	i18n::text_tree_toml_options toml{};
};

export struct i18n_load_result {
	bool found{};
	bool applied{};
	i18n::locale_name requested_locale{};
	i18n::locale_name locale{};
	std::filesystem::path path{};
	std::exception_ptr error{};
};

export struct i18n_loaded_bundle {
	i18n_load_result result{};
	i18n::frozen_text_tree_ptr tree{};
};

export [[nodiscard]] inline i18n_loaded_bundle load_system_locale_i18n_bundle(
	i18n_load_options options = {}) {
	i18n_loaded_bundle bundle{};
	bundle.result.requested_locale.assign(platform::get_system_locale_name());

	try {
		auto loaded = i18n::load_text_tree_locale_bundle(
			bundle.result.requested_locale,
			i18n::locale_text_tree_load_options{
				.bundle_dir = std::move(options.bundle_dir),
				.default_locale = std::move(options.default_locale),
				.toml = std::move(options.toml),
			});
		if(!loaded) {
			return bundle;
		}

		bundle.result.found = true;
		bundle.result.locale = loaded->locale;
		bundle.result.path = std::move(loaded->path);
		bundle.tree = std::move(loaded->tree);
	} catch(...) {
		bundle.result.error = std::current_exception();
	}

	return bundle;
}

export inline i18n_load_result apply_i18n_bundle_to_resources(
	scene_resources& resources,
	i18n_loaded_bundle&& bundle) {
	auto result = std::move(bundle.result);
	if(result.error != nullptr || !bundle.tree) {
		return result;
	}

	auto& root = resources.i18n_prov.node;
	root.update_value(i18n::text_snapshot{
		.tree = std::move(bundle.tree),
		.revision = root.get_raw_cache().revision + 1,
		.locale = std::string{std::string_view{result.locale}},
	});
	result.applied = true;
	return result;
}

export [[nodiscard]] inline async_operation_handle load_scene_i18n_for_system_locale(
	elem& owner,
	i18n_load_options options = {}) {
	return owner.get_scene().request_forked(
		owner,
		[options = std::move(options)](async_task_context& context, scene&) mutable {
			context.report_progress(0u, 1u);
			if(context.stop_requested()) {
				return i18n_loaded_bundle{};
			}

			auto bundle = ::mo_yanxi::gui::load_system_locale_i18n_bundle(std::move(options));
			context.report_progress(1u, 1u);
			return bundle;
		},
		[](elem& live_owner, i18n_loaded_bundle bundle) mutable {
			(void)::mo_yanxi::gui::apply_i18n_bundle_to_resources(
				live_owner.get_scene().resources(),
				std::move(bundle));
		});
}

export [[nodiscard]] inline async_operation_handle load_scene_i18n_for_system_locale(
	scene& scene,
	i18n_load_options options = {}) {
	return ::mo_yanxi::gui::load_scene_i18n_for_system_locale(scene.root(), std::move(options));
}

}
