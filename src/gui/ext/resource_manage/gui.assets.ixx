//
// Created by Matrix on 2025/11/25.
//

export module mo_yanxi.gui.assets;

import mo_yanxi.graphic.image_atlas;
import std;

namespace mo_yanxi::gui::assets{

export
void load_default_assets(const std::filesystem::path& svg_root);

export
void generate_default_shapes(graphic::image_atlas& image_atlas);

export
void dispose_generated_shapes();
}
