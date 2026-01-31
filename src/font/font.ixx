module;

#if DEBUG_CHECK
#define FT_CONFIG_OPTION_ERROR_STRINGS
#endif

#include <cassert>
#include <ft2build.h>
#include <freetype/freetype.h>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <msdfgen/msdfgen-ext.h>
#endif


export module mo_yanxi.font;
import std;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.graphic.msdf;
import mo_yanxi.handle_wrapper;
import mo_yanxi.concurrent.guard;
import mo_yanxi.msdf_adaptor;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <msdfgen/msdfgen-ext.h>;
#endif

namespace mo_yanxi::font{
export constexpr inline math::vec2 font_draw_expand{graphic::msdf::sdf_image_boarder, graphic::msdf::sdf_image_boarder};
void check(FT_Error error);

export struct library : exclusive_handle<FT_Library>{
	[[nodiscard]] explicit(false) library(std::nullptr_t){
	}

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

inline FT_Library global_free_type_lib_handle;

union U{
	library owner_free_type_lib;

	[[nodiscard]] U(){
	}

	~U(){
	}
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
	} else{
		std::construct_at(&u.owner_free_type_lib, library{});
		global_free_type_lib_handle = u.owner_free_type_lib;
	}
}

export
inline bool terminate() noexcept{
	if(!global_free_type_lib_handle) return false;
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
export using glyph_index_t = std::uint32_t;
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
	glyph_index_t index{};
	glyph_size_type size{};

	constexpr friend bool operator==(const glyph_identity&, const glyph_identity&) noexcept = default;
};

export
template <std::floating_point T = float>
constexpr T normalize_len(const FT_Pos pos) noexcept{
	if consteval{
		return static_cast<T>(pos) / 64.f;
	} else{
		return std::ldexp(static_cast<T>(pos), -6);
		// NOLINT(*-narrowing-conversions)
	}
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
		src.add(horiBearing.x * scale.x, -horiBearing.y * scale.y);
		end.add(horiBearing.x * scale.x + size.x * scale.x, descender() * scale.y);
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

export struct font_face_handle;

export struct acquire_result{
	glyph_metrics metrics;
	graphic::msdf::msdf_glyph_generator generator;
	math::usize2 extent;
};

export struct font_face_meta;

struct font_face_handle : exclusive_handle<FT_Face>{
private:
	exclusive_handle_member<msdfgen::FontHandle*> msdfHdl{};
	exclusive_handle_member<const font_face_meta*> origin_meta{};

public:
	[[nodiscard]] font_face_handle() = default;

	~font_face_handle(){
		if(handle) check(FT_Done_Face(handle));
		if(msdfHdl) msdfgen::destroyFont(msdfHdl);
	}

	// 从内存加载，注意 data 必须在 handle 生命周期内有效
	explicit font_face_handle(std::span<const std::byte> data, const font_face_meta* meta, const FT_Long index = 0) : origin_meta(meta){
		auto lib = get_ft_lib();
		check(FT_New_Memory_Face(lib,
			reinterpret_cast<const FT_Byte*>(data.data()),
			static_cast<FT_Long>(data.size()),
			index,
			&handle));
		// msdfgen 采用 FreeType 句柄
		msdfHdl = adopt_msdfgen_hld_and_fuck_msvc(handle);
	}

	font_face_handle(const font_face_handle& other) = delete;
	font_face_handle(font_face_handle&& other) noexcept = default;
	font_face_handle& operator=(const font_face_handle& other) = delete;
	font_face_handle& operator=(font_face_handle&& other) noexcept = default;

	const font_face_meta* get_source() const noexcept{
		return origin_meta;
	}

	msdfgen::FontHandle* get_msdf_handle() const noexcept{
		return msdfHdl;
	}

	explicit font_face_handle(const char* path, const FT_Long index = 0){
		auto lib = get_ft_lib();
		check(FT_New_Face(lib, path, index, &handle));
		msdfHdl = msdfgen::loadFont(graphic::msdf::HACK_get_ft_library_from(&lib), path);
	}

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

	[[nodiscard]] std::expected<FT_GlyphSlot, FT_Error> load_and_get_by_index(
		const glyph_index_t glyph_index,
		const FT_Int32 loadFlags = FT_LOAD_DEFAULT) const{
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

	acquire_result obtain(const glyph_index_t code, const glyph_size_type size){
		check(set_size(size.x, size.y));
		if(const auto shot = load_and_get_by_index(code)){
			const bool is_empty = shot.value()->bitmap.width == 0 ||
				shot.value()->bitmap.rows == 0;
			return acquire_result{
					handle->glyph->metrics,
					graphic::msdf::msdf_glyph_generator{
						is_empty ? nullptr : msdfHdl.get(),
						handle->size->metrics.x_ppem, handle->size->metrics.y_ppem
					},
					get_extent()
				};
		}

		return {};
	}


	[[nodiscard]] math::usize2 get_extent() noexcept{
		return {
				handle->glyph->bitmap.width + graphic::msdf::sdf_image_boarder * 2,
				handle->glyph->bitmap.rows + graphic::msdf::sdf_image_boarder * 2
			};
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

export struct font_face_view;

struct font_face_view{
private:
	// A view is just a span over a thread-local vector of handles
	std::span<font_face_handle> chain_{};

public:
	font_face_view() = default;

	explicit font_face_view(const std::span<font_face_handle>& faces)
		: chain_(faces){
	}

	[[nodiscard]] float get_line_spacing(const math::usize2 sz) const;

	[[nodiscard]] math::vec2 get_line_spacing_vec(const math::usize2 sz) const;
	[[nodiscard]] math::vec2 get_line_spacing_vec() const;
	[[nodiscard]] math::usize2 get_font_pixel_spacing(const math::usize2 sz) const;

	[[nodiscard]] std::string format(const glyph_index_t code, const glyph_size_type size) const{
		return std::format("{}.{}.{:#0X}|{},{}", face().get_family_name(), face().get_style_name(),
			std::bit_cast<int>(code), size.x, size.y);
	}

	[[nodiscard]] const font_face_handle& face() const noexcept{
		return chain_.front();
	}

	[[nodiscard]] font_face_handle& face() noexcept{
		return chain_.front();
	}

	auto begin() noexcept{
		return chain_.begin();
	}

	auto end() noexcept{
		return chain_.end();
	}

	auto begin() const noexcept{
		return chain_.begin();
	}

	auto end() const noexcept{
		return chain_.end();
	}

	std::pair<font_face_handle*, glyph_index_t> find_glyph_of(const char_code code){
		for(auto& chain : chain_){
			const auto index = chain.index_of(code);
			if(index != 0){
				return {&chain, index};
			}
		}
		return {&face(), face().index_of(code)};
	}
};

/**
 * @brief 字体元数据 (Read-Only)
 * 仅包含文件数据，多线程共享，不包含任何 Handle。
 */
export struct font_face_meta {
private:
	std::vector<std::byte> font_data_{};

	// 这是一个专门给 Loading Thread 使用的 Handle。
	// 因为 Loading Thread 是单例/串行的，所以这里存放一个实例是安全的。
	// 它的初始化在 Meta 创建时完成（通常在主线程），之后仅由 Loading Thread 读取/使用。
	font_face_handle loader_handle_{};

public:
	font_face_meta() = default;

	explicit font_face_meta(std::vector<std::byte> data) : font_data_(std::move(data)) {
		// 在 Meta 创建时，立即为加载线程初始化一个专用的 Handle
		// 注意：这里传 nullptr 给 meta 参数，防止递归引用，且 loader handle 不需要回溯
		loader_handle_ = font_face_handle(font_data_, nullptr);
	}

	explicit font_face_meta(const char* path) : font_face_meta(read_file(path)) {}
	explicit font_face_meta(const wchar_t* path) : font_face_meta(read_file(path)) {}
	explicit font_face_meta(const std::filesystem::path& path) : font_face_meta(read_file(path)) {}
	explicit font_face_meta(const std::string_view path) : font_face_meta(read_file(path)) {}
	explicit font_face_meta(const std::string& path) : font_face_meta(read_file(path)) {}

	[[nodiscard]] std::span<const std::byte> data() const noexcept {
		return font_data_;
	}

	// 提供给 sdf_load 使用的接口
	[[nodiscard]] msdfgen::FontHandle* get_loader_msdf_handle() const noexcept {
		return loader_handle_.get_msdf_handle();
	}

private:
	static std::vector<std::byte> read_file(std::ifstream& file){
		if (!file.is_open()) throw std::runtime_error("Failed to open font file");
		auto size = file.tellg();
		std::vector<std::byte> buffer(size);
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(buffer.data()), size);
		return buffer;
	}
	[[nodiscard]] static std::vector<std::byte> read_file(const char* fontpath) {
		std::ifstream file(fontpath, std::ios::binary | std::ios::ate);
		return read_file(file);
	}

	[[nodiscard]] static std::vector<std::byte> read_file(const wchar_t* fontpath) {
		std::ifstream file(fontpath, std::ios::binary | std::ios::ate);
		return read_file(file);
	}

	[[nodiscard]] static std::vector<std::byte> read_file(const std::filesystem::path& fontpath) {
		std::ifstream file(fontpath, std::ios::binary | std::ios::ate);
		return read_file(file);
	}
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
