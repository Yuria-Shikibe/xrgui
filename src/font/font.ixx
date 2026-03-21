module;

#if DEBUG_CHECK
#define FT_CONFIG_OPTION_ERROR_STRINGS
#endif

#include <cassert>
#include <ft2build.h>
#include <freetype/freetype.h>
#include <mo_yanxi/adapted_attributes.hpp>
#include <mo_yanxi/enum_operator_gen.hpp>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <msdfgen/msdfgen-ext.h>
#endif


export module mo_yanxi.font;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.graphic.msdf;
import mo_yanxi.handle_wrapper;
import mo_yanxi.concurrent.guard;
import mo_yanxi.msdf_adaptor;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <msdfgen/msdfgen-ext.h>;
#endif

import std;

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

export
template <typename T>
FORCE_INLINE CONST_FN constexpr T get_snapped_size(const T len) noexcept{
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
FORCE_INLINE CONST_FN constexpr T normalize_len(const FT_Pos pos) noexcept{
	return static_cast<T>(pos) / 64.f;
}

export
template <std::floating_point T = float>
FORCE_INLINE CONST_FN constexpr T normalize_len_1616(const FT_Pos pos) noexcept{
	return static_cast<T>(pos) / T(1 << 16);
}

export enum class font_style : std::uint32_t {
	normal = 0,
	bold = 1,
	italic = 2,
	bold_italic = 3
};

export 
constexpr font_style make_font_style(bool italic, bool bold) noexcept {
	if (italic && bold) return font_style::bold_italic;
	if (italic) return font_style::italic;
	if (bold) return font_style::bold;
	return font_style::normal;
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

	[[nodiscard]] FORCE_INLINE constexpr float ascender() const noexcept{
		return horiBearing.y;
	}

	[[nodiscard]] FORCE_INLINE constexpr float descender() const noexcept{
		return size.y - horiBearing.y;
	}

	/**
	 * @param pos Pen position
	 * @param scale
	 * @return [v00, v11]
	 */
	[[nodiscard]] FORCE_INLINE constexpr math::frect place_to(const math::vec2 pos, const math::vec2 scale) const{
		math::vec2 src = pos;
		math::vec2 end = pos;
		src.add(horiBearing.x * scale.x, -horiBearing.y * scale.y);
		end.add(horiBearing.x * scale.x + size.x * scale.x, descender() * scale.y);
		return {tags::unchecked, tags::from_vertex, src, end};
	}

	FORCE_INLINE constexpr bool operator==(const glyph_metrics&) const noexcept = default;
};

export struct font_face_handle;

export struct acquire_result{
	glyph_metrics metrics;
	graphic::msdf::msdf_glyph_generator generator;

	constexpr bool has_drawable_glyph() const noexcept{
		return generator.face != nullptr;
	}

	[[nodiscard]] inline constexpr math::usize2 extent() const noexcept{
		return metrics.size.copy().ceil().as<unsigned>() + (graphic::msdf::sdf_image_boarder * 2);
	}
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
	[[nodiscard]] bool is_bold() const noexcept {
		return handle && (handle->style_flags & FT_STYLE_FLAG_BOLD) != 0;
	}

	[[nodiscard]] bool is_italic() const noexcept {
		return handle && (handle->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
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

	acquire_result obtain(const glyph_index_t code, const glyph_size_type size) const{
		check(set_size(size.x, size.y));
		if(const auto shot = load_and_get_by_index(code, FT_LOAD_NO_HINTING)){
			auto glyph = shot.value();
			const bool is_empty = glyph->bitmap.width == 0 || glyph->bitmap.rows == 0;
			return acquire_result{
					glyph->metrics,
					graphic::msdf::msdf_glyph_generator{
						is_empty ? nullptr : msdfHdl.get(),
						handle->size->metrics.x_ppem, handle->size->metrics.y_ppem
					}
				};
		}

		return {};
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

	bool empty() const noexcept{
		return chain_.empty();
	}

	explicit operator bool() const noexcept{
		return !chain_.empty();
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

export struct styled_font_face_view {
	font_face_view view;
	bool is_bold_satisfied{};
	bool is_italic_satisfied{};

	constexpr explicit operator bool() const noexcept {
		return static_cast<bool>(view);
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

	[[nodiscard]] bool is_bold() const noexcept {
		return loader_handle_.is_bold();
	}

	[[nodiscard]] bool is_italic() const noexcept {
		return loader_handle_.is_italic();
	}

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

export struct font_family {
private:
	static constexpr std::size_t Total = std::to_underlying(font_style::bold_italic) + 1;
	std::vector<const font_face_meta*> flat_metas_{};
	// 记录各段起始点。对于索引 i，其数据范围为 [ offsets_[i], offsets_[i+1] )
	// 对于 font_style::bold_italic (即 3)，结束点为 flat_metas_.size()
	std::array<std::size_t, Total> offsets_{0, 0, 0, 0};

public:
	[[nodiscard]] font_family() = default;

	// 设置特定样式的回退链，内部自动处理后续数据段的偏移
	void set_style(font_style style, std::span<const font_face_meta* const> metas) {
		const auto idx = static_cast<std::size_t>(style);
		const auto start = offsets_[idx];
		const auto end = (idx + 1 < Total) ? offsets_[idx + 1] : flat_metas_.size();

		const auto old_count = end - start;
		const auto new_count = metas.size();

		// 抹去旧区间，插入新数据
		flat_metas_.erase(flat_metas_.begin() + start, flat_metas_.begin() + end);
		flat_metas_.insert(flat_metas_.begin() + start, metas.begin(), metas.end());

		// 如果新旧数量不一致，更新后续样式的偏移点
		if (new_count != old_count) {
			const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(new_count) - static_cast<std::ptrdiff_t>(old_count);
			for (std::size_t i = idx + 1; i < Total; ++i) {
				offsets_[i] = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(offsets_[i]) + diff);
			}
		}
	}

	// 获取特定样式的数据视图
	[[nodiscard]] std::span<const font_face_meta* const> get_style(font_style style) const noexcept {
		const auto idx = static_cast<std::size_t>(style);
		const auto start = offsets_[idx];
		const auto end = (idx < Total - 1) ? offsets_[idx + 1] : flat_metas_.size();

		if (start == end) return {};
		return {flat_metas_.data() + start, end - start};
	}

	[[nodiscard]] bool empty() const noexcept {
		return flat_metas_.empty();
	}
};


}

template <>
struct std::hash<mo_yanxi::font::glyph_identity>{ // NOLINT(*-dcl58-cpp)
	static constexpr std::size_t operator()(const mo_yanxi::font::glyph_identity& key) noexcept{
		auto packed = std::bit_cast<std::uint64_t>(key);

		packed ^= packed >> 30;
		packed *= 0xbf58476d1ce4e5b9ULL;
		packed ^= packed >> 27;
		packed *= 0x94d049bb133111ebULL;
		packed ^= packed >> 31;

		return static_cast<std::size_t>(packed);
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
