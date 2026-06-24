export module mo_yanxi.gui.cfg.audio_assets;

import std;
import mo_yanxi.log;
export import mo_yanxi.audio.resources;
export import mo_yanxi.gui.sound.manager;

namespace mo_yanxi::gui::cfg{

//TODO this config file needs improve:

export constexpr inline std::string_view default_ui_click_audio_file{"ui-click.mp3"};
export constexpr inline std::string_view secondary_ui_click_audio_file{"ui-click-2.mp3"};

export struct default_ui_sound_assets{
	audio::audio_asset_handle click{};
	audio::audio_asset_handle secondary_click{};
	sound::asset_group_handle group{};

	[[nodiscard]] explicit operator bool() const noexcept{
		return static_cast<bool>(click) && static_cast<bool>(group);
	}
};

export [[nodiscard]] inline std::optional<std::filesystem::path> find_default_audio_asset_dir(
	std::filesystem::path base = std::filesystem::current_path()){
	while(true){
		for(const auto& candidate : {
				base / "assets" / "audio",
				base / "properties" / "assets" / "audio"
			}){
			if(std::filesystem::is_directory(candidate)){
				return candidate;
			}
		}

		const auto parent = base.parent_path();
		if(parent.empty() || parent == base){
			break;
		}
		base = parent;
	}
	return std::nullopt;
}

export [[nodiscard]] inline default_ui_sound_assets load_default_ui_sound_assets(
	audio::audio_resource_manager& audio_resources,
	const std::filesystem::path& audio_dir){
	auto register_default_audio = [&](std::string_view file_name){
		const auto path = audio_dir / std::filesystem::path{std::string{file_name}};
		if(!std::filesystem::exists(path)){
			log::warn({"Audio"}, "default audio asset was not found: {}", path.string());
		}

		auto desc = audio::load_desc::from_file(path);
		desc.debug_name = std::string{file_name};
		return audio_resources.register_audio(
			std::move(desc),
			audio::audio_resource_options{
				.load_priority = audio::default_audio_load_priority,
				.protected_resource = true
			});
	};

	default_ui_sound_assets assets{
		.click = register_default_audio(default_ui_click_audio_file),
		.secondary_click = register_default_audio(secondary_ui_click_audio_file),
		.group = sound::make_asset_group()
	};
	assets.group->set(sound::play_event::on_release, assets.click);
	assets.group->set(sound::play_event::on_toggle_off, assets.secondary_click);
	return assets;
}

export [[nodiscard]] inline default_ui_sound_assets load_default_ui_sound_assets(
	audio::audio_resource_manager& audio_resources){
	const auto audio_dir = find_default_audio_asset_dir();
	if(!audio_dir){
		log::warn({"Audio"}, "default audio assets directory was not found");
		return {};
	}
	return load_default_ui_sound_assets(audio_resources, *audio_dir);
}

export inline void install_default_ui_sound_assets(
	sound::manager& sound_manager,
	const default_ui_sound_assets& assets){
	if(assets.group){
		sound_manager.insert_or_assign(sound::default_asset_group_name, assets.group);
	}
}

}
