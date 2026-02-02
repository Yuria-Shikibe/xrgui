module;

module mo_yanxi.graphic.bitmap;

import mo_yanxi.io.image;

mo_yanxi::bitmap mo_yanxi::graphic::load_bitmap(const std::string_view path){
	int width, height, bpp;
	const auto ptr = io::image::load_image(path, width, height, bpp, 4);

	mo_yanxi::bitmap bitmap(width, height, reinterpret_cast<const color_bits*>(ptr.get()));
	return bitmap;
}

void mo_yanxi::graphic::write_bitmap(const mo_yanxi::bitmap& bitmap, std::string_view path, bool autoCreateFile){
	if(bitmap.area() == 0) return;
	//TODO...

	std::filesystem::path file{path};
	if(!std::filesystem::exists(file)){
		bool result = false;

		if(autoCreateFile){
			const std::ofstream ofStream(file);
			result = ofStream.is_open();
		}

		if(!std::filesystem::exists(file) || !result) throw std::runtime_error{"Inexist File!"};
	}

	//OPTM ...
	// ReSharper disable once CppTooWideScopeInitStatement
	std::string ext = file.extension().generic_string();

	if(path.ends_with(".bmp")){
		io::image::write_bmp(path, bitmap.width(), bitmap.height(), 4, reinterpret_cast<const unsigned char*>(bitmap.data()));
	}

	io::image::write_png(path, bitmap.width(), bitmap.height(), 4, reinterpret_cast<const unsigned char*>(bitmap.data()));
}
