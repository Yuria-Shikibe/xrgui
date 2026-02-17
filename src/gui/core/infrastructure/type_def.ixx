//
// Created by Matrix on 2026/1/29.
//

export module mo_yanxi.gui.infrastructure:defines;

export import align;
export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;

import std;

namespace mo_yanxi::gui{

export using boarder = align::spacing;
export using vec2 = math::vec2;
export using rect = math::frect;
export using altitude_t = unsigned;

export constexpr std::size_t draw_pass_max_capacity = 8;

}