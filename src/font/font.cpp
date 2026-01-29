module;

#include <cassert>

module mo_yanxi.font;
import mo_yanxi.concurrent.guard;

namespace mo_yanxi::font{
U u;

float font_face_view::get_line_spacing(const math::usize2 sz) const{
	check(face().set_size(sz));
	return normalize_len(face()->size->metrics.height);
}

math::usize2 font_face_view::get_font_pixel_spacing(const math::usize2 sz) const{
	check(face().set_size(sz));
	return {face()->size->metrics.x_ppem, face()->size->metrics.y_ppem};
}
}
