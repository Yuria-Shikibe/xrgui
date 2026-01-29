module;

#include <cassert>

module mo_yanxi.font;
import mo_yanxi.concurrent.guard;

namespace mo_yanxi::font{
	U u;


acquire_result font_face::obtain(const glyph_index_t code, const glyph_size_type size){
	assert((size.x != 0 || size.y != 0) && "must at least one none zero");

	{
		ccur::semaphore_acq_guard _{mutex_};
		check(face_.set_size(size.x, size.y));
		if(const auto shot = face_.load_and_get_by_index(code)){
			const bool is_empty = shot.value()->bitmap.width == 0 || shot.value()->bitmap.rows == 0;
			return acquire_result{
				this,
				face_->glyph->metrics,
				graphic::msdf::msdf_glyph_generator{
					is_empty ? nullptr : face_.msdfHdl.get(),
					face_->size->metrics.x_ppem, face_->size->metrics.y_ppem
				}, get_extent(face_)};
		}
	}

	if(fallback){
		assert(fallback != this);
		return fallback->obtain(code, size);
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
