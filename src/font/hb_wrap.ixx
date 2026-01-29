module;

#include <hb.h>

export module hb_wrap;

import std;

namespace mo_yanxi::font::hb{

namespace hb_raii {

/**
 * @brief 通用删除器包装类
 * 使用模板函数指针，确保不同类型的 HarfBuzz 句柄调用正确的 destroy 函数
 */
template <auto DestroyFunction>
struct hb_deleter {
	template <typename T>
	void operator()(T* ptr) const noexcept {
		DestroyFunction(ptr);
	}
};

using buffer_ptr = std::unique_ptr<hb_buffer_t, hb_deleter<hb_buffer_destroy>>;
using blob_ptr   = std::unique_ptr<hb_blob_t,   hb_deleter<hb_blob_destroy>>;
using face_ptr   = std::unique_ptr<hb_face_t,   hb_deleter<hb_face_destroy>>;
using font_ptr   = std::unique_ptr<hb_font_t,   hb_deleter<hb_font_destroy>>;

[[nodiscard]] inline auto make_buffer() -> buffer_ptr {
	return buffer_ptr{hb_buffer_create()};
}

[[nodiscard]] inline auto make_face(hb_blob_t* blob, unsigned int index) -> face_ptr {
	return face_ptr{hb_face_create(blob, index)};
}

} // namespace hb_raii
}