module;

#if DEBUG_CHECK
#define FT_CONFIG_OPTION_ERROR_STRINGS
#endif

#include <cassert>
#include <ft2build.h>
#include <freetype/freetype.h>
#include <msdfgen/msdfgen-ext.h>

#include <hb.h>
#include <hb-ft.h>

export module mo_yanxi.font;
import std;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;

import mo_yanxi.graphic.msdf;
import mo_yanxi.handle_wrapper;
import mo_yanxi.concurrent.guard;

namespace mo_yanxi::font{
export constexpr inline math::vec2 font_draw_expand{graphic::msdf::sdf_image_boarder, graphic::msdf::sdf_image_boarder};

void check(FT_Error error);

export struct library : exclusive_handle<FT_Library>{
	[[nodiscard]] explicit(false) library(std::nullptr_t){}

	[[nodiscard]] library(){
		check(FT_Init_FreeType(as_data()));
	}

	~library(){
		if(handle) check(FT_Done_FreeType(handle));
	}

	explicit operator bool() const noexcept{
		return handle != nullptr;
	}

	library(const library& other) = delete;
	library(library&& other) noexcept = default;
	library& operator=(const library& other) = delete;
	library& operator=(library&& other) noexcept = default;
};

export struct hb_buffer_wrapper : exclusive_handle<hb_buffer_t*>{
	[[nodiscard]] hb_buffer_wrapper(){
		handle = hb_buffer_create();
	}

	~hb_buffer_wrapper(){
		if(handle) hb_buffer_destroy(handle);
	}

	hb_buffer_wrapper(const hb_buffer_wrapper& other) = delete;
	hb_buffer_wrapper(hb_buffer_wrapper&& other) noexcept = default;
	hb_buffer_wrapper& operator=(const hb_buffer_wrapper& other) = delete;
	hb_buffer_wrapper& operator=(hb_buffer_wrapper&& other) noexcept = default;
};

inline FT_Library global_free_type_lib_handle;

union U{
	library owner_free_type_lib;
	[[nodiscard]] U(){}
	~U(){}
};
extern U u;

/**
 * @brief
 * @param externals already available handle, or null to initialize one.
 */
export
inline void initialize(FT_Library externals = nullptr){
	if(global_free_type_lib_handle){
		throw std::runtime_error{"Duplicate Freetype library initialization"};
	}
	if(externals){
		global_free_type_lib_handle = externals;
		std::construct_at(&u.owner_free_type_lib, library{nullptr});
	}else{
		std::construct_at(&u.owner_free_type_lib, library{});
		global_free_type_lib_handle = u.owner_free_type_lib;
	}
}

export
inline bool terminate() noexcept{
	if(!global_free_type_lib_handle)return false;
	global_free_type_lib_handle = nullptr;
	if(u.owner_free_type_lib){
		std::destroy_at(&u.owner_free_type_lib);
	}
	return true;
}

export
inline FT_Library get_ft_lib() noexcept{
	return global_free_type_lib_handle;
}

export using char_code = char32_t;
export using glyph_size_type = math::vector2<std::uint16_t>;
export using glyph_raw_metrics = FT_Glyph_Metrics;

export inline bool is_space(char_code code) noexcept{
	return code == U'\0' || code <= std::numeric_limits<signed char>::max() && std::isspace(code);
}

export inline bool is_unicode_separator(char_code c) noexcept{
	// ASCII控制字符（换行/回车/制表符等）
	if(c < 0x80 && std::iscntrl(static_cast<unsigned char>(c))){
		return true;
	}

	// 常见ASCII分隔符
	if(c < 0x80 && (std::ispunct(static_cast<unsigned char>(c)) || c == ' ')){
		return true;
	}

	// 全角标点（FF00-FFEF区块）
	if((c >= 0xFF01 && c <= 0xFF0F) || // ！＂＃＄％＆＇（）＊＋，－．／
		(c >= 0xFF1A && c <= 0xFF20) || //：；＜＝＞？＠
		(c >= 0xFF3B && c <= 0xFF40) || //；＜＝＞？＠［＼］＾＿
		(c >= 0xFF5B && c <= 0xFF65)){
		//｛｜｝～｟...等
		return true;
	}

	// 中文标点（3000-303F区块）
	if((c >= 0x3000 && c <= 0x303F)){
		// 包含　、。〃〄等
		return true;
	}

	return false;
}


export
template <typename T>
constexpr T get_snapped_size(const T len) noexcept{
	if(len == 0) return 0;
	if(len <= static_cast<T>(256)) return 64;
	return 128;
}

export struct glyph_identity{
	std::uint32_t index{};
	glyph_size_type size{};

	constexpr friend bool operator==(const glyph_identity&, const glyph_identity&) noexcept = default;
};

export
template <std::floating_point T = float>
constexpr T normalize_len(const FT_Pos pos) noexcept{
	return std::ldexp(static_cast<T>(pos), -6); // NOLINT(*-narrowing-conversions)
}

export
template <std::floating_point T = float>
constexpr T normalize_len_1616(const FT_Pos pos) noexcept{
	return std::ldexp(static_cast<T>(pos), -16); // NOLINT(*-narrowing-conversions)
}

export
struct glyph_metrics{
	math::vec2 size{};
	math::vec2 horiBearing{};
	math::vec2 vertBearing{};
	math::vec2 advance{};

	[[nodiscard]] constexpr glyph_metrics() = default;

	[[nodiscard]] constexpr explicit(false) glyph_metrics(const glyph_raw_metrics& metrics){
		size.x = normalize_len<float>(metrics.width);
		size.y = normalize_len<float>(metrics.height);
		horiBearing.x = normalize_len<float>(metrics.horiBearingX);
		horiBearing.y = normalize_len<float>(metrics.horiBearingY);
		advance.x = normalize_len<float>(metrics.horiAdvance);
		vertBearing.x = normalize_len<float>(metrics.vertBearingX);
		vertBearing.y = normalize_len<float>(metrics.vertBearingY);
		advance.y = normalize_len<float>(metrics.vertAdvance);

		if(metrics.height == 0 && metrics.horiBearingY == 0){
			size.y = horiBearing.y = vertBearing.y;
		}
	}

	[[nodiscard]] constexpr float ascender() const noexcept{
		return horiBearing.y;
	}

	[[nodiscard]] constexpr float descender() const noexcept{
		return size.y - horiBearing.y;
	}

	/**
	 * @param pos Pen position
	 * @param scale
	 * @return [v00, v11]
	 */
	[[nodiscard]] math::frect place_to(const math::vec2 pos, const math::vec2 scale) const{
		math::vec2 src = pos;
		math::vec2 end = pos;
		src.add(horiBearing.x, -horiBearing.y * scale.y);
		end.add(horiBearing.x + size.x * scale.x, descender() * scale.y);

		return {tags::unchecked, tags::from_vertex, src, end};
	}

	bool operator==(const glyph_metrics&) const noexcept = default;
};

export constexpr inline auto ascii_chars = []() constexpr{
	constexpr std::size_t Size = '~' - ' ' + 1;
	std::array<char_code, Size> charCodes{};
	std::ranges::copy(std::ranges::views::iota(' ', '~' + 1), charCodes.begin());

	return charCodes;
}();

export struct font_face;

struct font_face_handle : exclusive_handle<FT_Face>{
	exclusive_handle_member<msdfgen::FontHandle*> msdfHdl{};
	exclusive_handle_member<hb_font_t*> hbFont{};

	[[nodiscard]] font_face_handle() = default;

	~font_face_handle(){
		if(hbFont) hb_font_destroy(hbFont);
		if(handle) check(FT_Done_Face(handle));
		if(msdfHdl) msdfgen::destroyFont(msdfHdl);
	}

	font_face_handle(const font_face_handle& other) = delete;
	font_face_handle(font_face_handle&& other) noexcept = default;
	font_face_handle& operator=(const font_face_handle& other) = delete;
	font_face_handle& operator=(font_face_handle&& other) noexcept = default;

	explicit font_face_handle(const char* path, const FT_Long index = 0){
		auto lib = get_ft_lib();
		check(FT_New_Face(lib, path, index, &handle));
		msdfHdl = msdfgen::loadFont(graphic::msdf::HACK_get_ft_library_from(&lib), path);
		hbFont = hb_ft_font_create_referenced(handle);
	}

	friend font_face;


	[[nodiscard]] std::string_view get_family_name() const noexcept{
		return {handle->family_name};
	}

	[[nodiscard]] std::string_view get_style_name() const noexcept{
		return {handle->style_name};
	}

	[[nodiscard]] std::string get_fullname() const{
		return std::format("{}.{}", get_family_name(), get_style_name());
	}

	[[nodiscard]] auto get_face_index() const noexcept{
		return handle->face_index;
	}

	[[nodiscard]] FT_UInt index_of(const char_code code) const noexcept{
		return FT_Get_Char_Index(handle, code);
	}

	[[nodiscard]] hb_font_t* get_hb_font() const noexcept{
		return hbFont;
	}

private:
	[[nodiscard]] FT_Error load_glyph(const FT_UInt index, const FT_Int32 loadFlags) const noexcept{
		return FT_Load_Glyph(handle, index, loadFlags);
	}

	[[nodiscard]] FT_Error load_char(const FT_ULong code, const FT_Int32 loadFlags) const noexcept{
		return FT_Load_Char(handle, code, loadFlags);
	}

	FT_Error set_size(const unsigned w, const unsigned h = 0u) const noexcept{
		if((w && handle->size->metrics.x_ppem == w) && (h && handle->size->metrics.y_ppem == h)) return 0;
		return FT_Set_Pixel_Sizes(handle, w, h);
	}

	FT_Error set_size(const math::usize2 size2) const noexcept{
		return set_size(size2.x, size2.y);
	}

	[[nodiscard]] auto get_height() const noexcept{
		return handle->height;
	}

	[[nodiscard]] std::expected<FT_GlyphSlot, FT_Error> load_and_get(
		const char_code index,
		const FT_Int32 loadFlags = FT_LOAD_DEFAULT) const{
		auto glyph_index = index_of(index);
		// Check if glyph index is zero, which means the character is not in the font
		if(index != 0 && glyph_index == 0){
			return std::expected<FT_GlyphSlot, FT_Error>{std::unexpect, FT_Err_Invalid_Character_Code};
		}

		if(auto error = load_glyph(glyph_index, loadFlags)){
			return std::expected<FT_GlyphSlot, FT_Error>{std::unexpect, error};
		}

		return std::expected<FT_GlyphSlot, FT_Error>{std::in_place, handle->glyph};
	}

	[[nodiscard]] FT_GlyphSlot load_and_get_guaranteed(const char_code index, const FT_Int32 loadFlags) const{
		if(const auto error = load_char(index, loadFlags)){
			check(error);
		}

		return handle->glyph;
	}

};

export struct glyph_wrap{
	char_code code{};
	font_face_handle* face{};

	[[nodiscard]] glyph_wrap() = default;

	[[nodiscard]] glyph_wrap(
		const char_code code,
		font_face_handle& face) :
	code{code},
	face(&face){
	}


};

[[nodiscard]] math::usize2 get_extent(const font_face_handle& face, const char_code code) noexcept {
	return {
		face->glyph->bitmap.width + graphic::msdf::sdf_image_boarder * 2,
		face->glyph->bitmap.rows + graphic::msdf::sdf_image_boarder * 2
	};
}
export struct acquire_result{
	font_face* wrap;
	glyph_metrics metrics;
	graphic::msdf::msdf_glyph_generator generator;
	math::usize2 extent;
};

struct font_face{
private:
	font_face_handle face_{};
	mutable std::binary_semaphore mutex_{1};
	mutable std::binary_semaphore msdf_mutex_{1};
public:
	//TODO english char replacement ?
	font_face* fallback{};


	[[nodiscard]] font_face() = default;

	[[nodiscard]] explicit font_face(const char* fontPath)
	: face_{fontPath}{
	}

	[[nodiscard]] explicit font_face(const std::string_view fontPath)
	: face_{fontPath.data()}{
	}

	[[nodiscard]] auto get_msdf_lock() const noexcept {
		return ccur::semaphore_acq_guard{msdf_mutex_};
	}

	[[nodiscard]] acquire_result obtain(const char_code code, const glyph_size_type size);

	[[nodiscard]] acquire_result obtain_glyph(const std::uint32_t index, const glyph_size_type size);

	[[nodiscard]] float get_line_spacing(const math::usize2 sz) const;

	[[nodiscard]] math::usize2 get_font_pixel_spacing(const math::usize2 sz) const;

	[[nodiscard]] acquire_result obtain(const char_code code, const glyph_size_type::value_type size) noexcept{
		return obtain(code, {size, 0});
	}

	[[nodiscard]] acquire_result obtain_snapped(const char_code code, const glyph_size_type::value_type size) noexcept{
		return obtain(code, get_snapped_size(size));
	}

	[[nodiscard]] std::string format(const char_code code, const glyph_size_type size) const{
		return std::format("{}.{}.{:#0X}|{},{}", face_.get_family_name(), face_.get_style_name(),
			std::bit_cast<int>(code), size.x, size.y);
	}

	void shape(hb_buffer_t* buf, glyph_size_type size) const {
		ccur::semaphore_acq_guard _{mutex_};
		check(face_.set_size(size.x, size.y));
		hb_ft_font_changed(face_.get_hb_font());
		hb_shape(face_.get_hb_font(), buf, nullptr, 0);
	}

	[[nodiscard]] const font_face_handle& face() const noexcept{
		return face_;
	}

	font_face(const font_face& other) = delete;
	font_face(font_face&& other) noexcept = delete;
	font_face& operator=(const font_face& other) = delete;
	font_face& operator=(font_face&& other) noexcept = delete;
};

}

template <>
struct std::hash<mo_yanxi::font::glyph_identity>{ // NOLINT(*-dcl58-cpp)
	std::size_t operator()(const mo_yanxi::font::glyph_identity& key) const noexcept{
		return std::hash<std::uint64_t>{}(std::bit_cast<std::uint64_t>(key));
	}
};

module : private;


void mo_yanxi::font::check(FT_Error error){
	if(!error) return;

#if DEBUG_CHECK
	const char* err = FT_Error_String(error);
	std::println(std::cerr, "[Freetype] Error {}: {}", error, err);
#else
	std::println(std::cerr, "[Freetype] Error {}", error);
#endif

	throw std::runtime_error("Freetype Failed");
}
