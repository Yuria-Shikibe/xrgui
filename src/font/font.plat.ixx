export module mo_yanxi.font.plat;

import std;
export import mo_yanxi.platform.font;

namespace mo_yanxi::font {

export using system_font_map = platform::system_font_map;

export [[nodiscard]] std::string get_system_default_font_name() {
	return platform::get_system_default_font_name();
}

export [[nodiscard]] system_font_map get_system_fonts() {
	return platform::get_system_fonts();
}

export [[nodiscard]] auto find_family_of(system_font_map& fonts, const std::string_view prefix) {
	return platform::find_family_of(fonts, prefix);
}

}
