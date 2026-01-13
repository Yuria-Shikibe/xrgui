module;
#include <hb.h>
#include <hb-ft.h>

export module mo_yanxi.font.hb_wrapper;
import std;

namespace mo_yanxi::font {

    export struct hb_buffer_deleter {
        void operator()(hb_buffer_t* b) const noexcept { if (b) hb_buffer_destroy(b); }
    };
    export using hb_buffer_handle = std::unique_ptr<hb_buffer_t, hb_buffer_deleter>;

    export struct hb_font_deleter {
        void operator()(hb_font_t* f) const noexcept { if (f) hb_font_destroy(f); }
    };
    export using hb_font_handle = std::unique_ptr<hb_font_t, hb_font_deleter>;

    export struct hb_blob_deleter {
        void operator()(hb_blob_t* b) const noexcept { if (b) hb_blob_destroy(b); }
    };
    export using hb_blob_handle = std::unique_ptr<hb_blob_t, hb_blob_deleter>;

    export hb_buffer_handle create_hb_buffer() {
        return hb_buffer_handle(hb_buffer_create());
    }

}
