module;

#include <cassert>

module mo_yanxi.font;
import mo_yanxi.concurrent.guard;

namespace mo_yanxi::font{
	U u;


acquire_result font_face::obtain(const char_code code, const glyph_size_type size){
	assert((size.x != 0 || size.y != 0) && "must at least one non zero");

	{
		ccur::semaphore_acq_guard _{mutex_};
		check(face_.set_size(size.x, size.y));
		if(const auto shot = face_.load_and_get(code)){
			if((shot.value()->bitmap.width != 0 && shot.value()->bitmap.rows != 0) || is_space(code)){
				return acquire_result{
					this,
					face_->glyph->metrics,
					graphic::msdf::msdf_glyph_generator{
						face_.msdfHdl,
						face_->size->metrics.x_ppem, face_->size->metrics.y_ppem
					}, get_extent(face_, code)};
			}
		}
	}

	if(fallback){
		assert(fallback != this);
		return fallback->obtain(code, size);
	}

	return acquire_result{};
}

acquire_result font_face::obtain_glyph(const std::uint32_t index, const glyph_size_type size){
	assert((size.x != 0 || size.y != 0) && "must at least one non zero");

	{
		ccur::semaphore_acq_guard _{mutex_};
		check(face_.set_size(size.x, size.y));

		if(auto error = face_.load_glyph(index, FT_LOAD_DEFAULT)){
			// handle error if needed
		} else {
			// Even if bitmap is empty (e.g. space or outline font before rendering),
			// we should return a valid result if we have metrics.
			// FT_LOAD_DEFAULT loads outline but not bitmap for outline fonts.
			// If it's a bitmap font, it loads bitmap.
			// So we check if format is not none.

			bool has_content = (face_->glyph->format != FT_GLYPH_FORMAT_NONE);

			if (has_content) {
				return acquire_result{
					this,
					face_->glyph->metrics,
					graphic::msdf::msdf_glyph_generator{
						face_.msdfHdl,
						face_->size->metrics.x_ppem, face_->size->metrics.y_ppem
					}, get_extent(face_, 0)
				};
			}
		}
	}
	return acquire_result{};
}

float font_face::get_line_spacing(const math::usize2 sz) const{
	ccur::semaphore_acq_guard _{mutex_};

	check(face_.set_size(sz));
	return normalize_len(face_->size->metrics.height);
}

math::usize2 font_face::get_font_pixel_spacing(const math::usize2 sz) const{
	ccur::semaphore_acq_guard _{mutex_};

	check(face_.set_size(sz));
	return {face_->size->metrics.x_ppem, face_->size->metrics.y_ppem};
}

}
