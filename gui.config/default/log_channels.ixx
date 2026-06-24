export module mo_yanxi.gui.cfg.builtin.log_channels;

import mo_yanxi.log;

namespace mo_yanxi::gui::cfg::builtin{

export
inline void configure_gui_log_channels(){
	using enum log::terminal_color;

	log::set_channel_color("App", bright_white);
	log::set_channel_color("GUI", bright_cyan);
	log::set_channel_color("Compositor", bright_magenta);
	log::set_channel_color("Vulkan", bright_yellow);
	log::set_channel_color("Assets", bright_green);
	log::set_channel_color("ImageAtlas", bright_blue);
	log::set_channel_color("ShaderC", cyan);
	log::set_channel_color("Freetype", white);
	log::set_channel_color("Thread", bright_black);
	log::set_channel_color("NumericInput", yellow);
	log::set_channel_color("Lifecycle", bright_green);
}

}
