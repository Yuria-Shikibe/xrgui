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

math::vec2 font_face_view::get_line_spacing_vec(const math::usize2 sz) const{
	check(face().set_size(sz));
	return get_line_spacing_vec();
}

math::vec2 font_face_view::get_line_spacing_vec() const{
	//TODO col distance is not good here.
	return {normalize_len(face()->size->metrics.max_advance), normalize_len(face()->size->metrics.height)};

}

math::usize2 font_face_view::get_font_pixel_spacing(const math::usize2 sz) const{
	check(face().set_size(sz));
	return {face()->size->metrics.x_ppem, face()->size->metrics.y_ppem};
}
}
