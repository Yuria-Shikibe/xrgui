module;

#include <cassert>
#include <hb.h>
#include <hb-ft.h>

module mo_yanxi.font;
import mo_yanxi.concurrent.guard;

namespace mo_yanxi::font{
	U u;

hb_font_t* font_face::get_hb_font() const {
    if (hb_font_) return hb_font_.get();

    // Lock is held by caller of shape, but we also need to ensure creation is safe.
    // However, since get_hb_font is const and modifies mutable member, it should handle its own safety or rely on external lock.
    // shape() holds the mutex.
    // We create hb_font_ lazily.
    // hb_ft_font_create_referenced references the FT_Face.

    // We need to be careful: creating hb_font reads FT_Face.
    // The mutex_ protects FT_Face.
    // If we are inside shape(), we hold mutex_.

    if (!hb_font_) {
         hb_font_t* hb_ft_font = hb_ft_font_create_referenced(face_.handle);
         hb_font_ = hb_font_handle(hb_ft_font);
    }
    return hb_font_.get();
}

void font_face::shape(hb_buffer_t* buf, const glyph_size_type size) const {
    ccur::semaphore_acq_guard _{mutex_};

    // Ensure size is set on FT_Face because hb_ft_font uses it for metrics?
    // Actually hb_ft uses the current size of the FT_Face.
    check(face_.set_size(size.x, size.y));

    hb_font_t* font = get_hb_font();
    // Update load flags if necessary.
    // hb_ft_font_set_load_flags(font, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING); // Example

    hb_shape(font, buf, nullptr, 0);
}

acquire_result font_face::obtain(const char_code code, const glyph_size_type size){
	assert((size.x != 0 || size.y != 0) && "must at least one none zero");

	{
		ccur::semaphore_acq_guard _{mutex_};
		check(face_.set_size(size.x, size.y));

		if(const auto shot = face_.load_and_get(code)){
            // For glyph indices, we assume they are valid glyphs if load succeeds.
            // is_space check might fail if code is a glyph index (it will return false).
            // But that's fine, we want the glyph.
            // However, if it's a space character (real space), we might want to return empty glyph.
            // But if we use indices, the "space" char is mapped to "space" glyph index.
            // Does space glyph have bitmap? Usually no.
            // So bitmap.width/rows will be 0.

            bool is_valid_bitmap = (shot.value()->bitmap.width != 0 && shot.value()->bitmap.rows != 0);

			if(is_valid_bitmap || is_space(code) || is_glyph_index_code(code)){
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
