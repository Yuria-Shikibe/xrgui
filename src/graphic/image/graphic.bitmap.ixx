module;

export module mo_yanxi.graphic.bitmap;

export import mo_yanxi.math.vector2;

export import mo_yanxi.bitmap;
import std;

namespace mo_yanxi::graphic{

export using mo_yanxi::bitmap;

export using mo_yanxi::bitmap_view;

export
[[nodiscard]] bitmap load_bitmap(std::string_view path);

export
[[nodiscard]] bitmap load_bitmap(const std::string& path){
	return load_bitmap(std::string_view{path});
}

export
[[nodiscard]] bitmap load_bitmap(const std::filesystem::path& path){
	return load_bitmap(path.string());
}

export
void write_bitmap(const bitmap& bitmap, std::string_view path, bool autoCreateFile = false);

}
