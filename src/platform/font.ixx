module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#elif defined(__linux__)
#include <fontconfig/fontconfig.h>
#elif defined(__APPLE__)
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

export module mo_yanxi.platform.font;

import std;
import mo_yanxi.heterogeneous;

namespace mo_yanxi::platform {

export using system_font_map = std::map<
	std::string,
	std::filesystem::path,
	transparent::string_comparator_of<std::less>>;

namespace {
#ifdef _WIN32
[[nodiscard]] std::string wide_to_utf8(const std::wstring_view value) {
	if(value.empty()) {
		return {};
	}

	const int utf8_size = WideCharToMultiByte(
		CP_UTF8,
		0,
		value.data(),
		static_cast<int>(value.size()),
		nullptr,
		0,
		nullptr,
		nullptr);
	if(utf8_size <= 0) {
		return {};
	}

	std::string result(static_cast<std::size_t>(utf8_size), '\0');
	WideCharToMultiByte(
		CP_UTF8,
		0,
		value.data(),
		static_cast<int>(value.size()),
		result.data(),
		utf8_size,
		nullptr,
		nullptr);
	return result;
}
#endif
}

export [[nodiscard]] std::string get_system_default_font_name() {
#if defined(_WIN32)
	NONCLIENTMETRICSW metrics{};
	metrics.cbSize = sizeof(NONCLIENTMETRICSW);
	if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &metrics, 0)) {
		return wide_to_utf8(metrics.lfMessageFont.lfFaceName);
	}
	return "Segoe UI";

#elif defined(__linux__)
	std::string default_font = "sans-serif";
	FcConfig* config = FcInitLoadConfigAndFonts();
	if(config != nullptr) {
		FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>("sans-serif"));
		FcConfigSubstitute(config, pattern, FcMatchPattern);
		FcDefaultSubstitute(pattern);

		FcResult result{};
		FcPattern* font = FcFontMatch(config, pattern, &result);
		if(font != nullptr) {
			FcChar8* family = nullptr;
			if(FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch) {
				default_font = reinterpret_cast<char*>(family);
			}
			FcPatternDestroy(font);
		}

		FcPatternDestroy(pattern);
		FcConfigDestroy(config);
	}
	return default_font;

#elif defined(__APPLE__)
	std::string default_font = "System";
	CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, nullptr);
	if(font != nullptr) {
		CFStringRef name_ref = CTFontCopyFamilyName(font);
		if(name_ref != nullptr) {
			char name_buffer[256]{};
			if(CFStringGetCString(name_ref, name_buffer, sizeof(name_buffer), kCFStringEncodingUTF8)) {
				default_font = name_buffer;
			}
			CFRelease(name_ref);
		}
		CFRelease(font);
	}
	return default_font;
#else
	return "Arial";
#endif
}

export [[nodiscard]] system_font_map get_system_fonts() {
	system_font_map fonts;

#if defined(_WIN32)
	HKEY key{};
	if(RegOpenKeyExW(
		   HKEY_LOCAL_MACHINE,
		   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
		   0,
		   KEY_READ,
		   &key) == ERROR_SUCCESS) {
		std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> key_holder{key, RegCloseKey};

		DWORD max_value_name_len = 0;
		DWORD max_value_data_len = 0;
		if(RegQueryInfoKeyW(
			   key,
			   nullptr,
			   nullptr,
			   nullptr,
			   nullptr,
			   nullptr,
			   nullptr,
			   nullptr,
			   &max_value_name_len,
			   &max_value_data_len,
			   nullptr,
			   nullptr) != ERROR_SUCCESS) {
			return fonts;
		}

		std::vector<wchar_t> value_name(max_value_name_len + 1);
		std::vector<wchar_t> value_data(max_value_data_len / sizeof(wchar_t) + 2);

		wchar_t windows_font_dir[MAX_PATH]{};
		if(SHGetSpecialFolderPathW(nullptr, windows_font_dir, CSIDL_FONTS, FALSE) == FALSE) {
			wcscpy_s(windows_font_dir, L"C:\\Windows\\Fonts");
		}
		const std::filesystem::path system_font_path(windows_font_dir);

		for(DWORD index = 0;; ++index) {
			DWORD name_len = max_value_name_len + 1;
			DWORD data_len = static_cast<DWORD>(value_data.size() * sizeof(wchar_t));
			DWORD type = 0;

			const LSTATUS status = RegEnumValueW(
				key,
				index,
				value_name.data(),
				&name_len,
				nullptr,
				&type,
				reinterpret_cast<LPBYTE>(value_data.data()),
				&data_len);

			if(status != ERROR_SUCCESS) {
				break;
			}
			if(type != REG_SZ) {
				continue;
			}

			std::wstring font_name(value_name.data(), name_len);
			const std::wstring file_name(value_data.data());

			if(const std::size_t param_pos = font_name.find(L" ("); param_pos != std::wstring::npos) {
				font_name = font_name.substr(0, param_pos);
			}

			std::filesystem::path full_path;
			if(file_name.contains(L"\\") || file_name.contains(L":")) {
				full_path = file_name;
			}else {
				full_path = system_font_path / file_name;
			}

			fonts[wide_to_utf8(font_name)] = std::move(full_path);
		}
	}

#elif defined(__linux__)
	FcConfig* config = FcInitLoadConfigAndFonts();
	FcPattern* pattern = FcPatternCreate();
	FcObjectSet* object_set = FcObjectSetBuild(FC_FAMILY, FC_FILE, static_cast<char*>(nullptr));
	FcFontSet* font_set = FcFontList(config, pattern, object_set);

	if(font_set != nullptr) {
		for(int i = 0; i < font_set->nfont; ++i) {
			FcPattern* font = font_set->fonts[i];
			FcChar8* file = nullptr;
			FcChar8* family = nullptr;

			if(FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch
			   && FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch) {
				fonts[std::string(reinterpret_cast<char*>(family))] =
					std::filesystem::path(reinterpret_cast<char*>(file));
			}
		}
		FcFontSetDestroy(font_set);
	}

	FcObjectSetDestroy(object_set);
	FcPatternDestroy(pattern);
	FcConfigDestroy(config);

#elif defined(__APPLE__)
	CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
	CFArrayRef font_array = CTFontCollectionCreateMatchingFontDescriptors(collection);

	if(font_array != nullptr) {
		const CFIndex count = CFArrayGetCount(font_array);
		for(CFIndex i = 0; i < count; ++i) {
			auto descriptor = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(font_array, i));

			CFStringRef name_ref = static_cast<CFStringRef>(
				CTFontDescriptorCopyAttribute(descriptor, kCTFontFamilyNameAttribute));
			CFURLRef url_ref = static_cast<CFURLRef>(
				CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute));

			if(name_ref != nullptr && url_ref != nullptr) {
				char name_buffer[256]{};
				char path_buffer[1024]{};
				if(CFStringGetCString(name_ref, name_buffer, sizeof(name_buffer), kCFStringEncodingUTF8)
				   && CFURLGetFileSystemRepresentation(url_ref, true, reinterpret_cast<UInt8*>(path_buffer), sizeof(path_buffer))) {
					fonts[std::string(name_buffer)] = std::filesystem::path(path_buffer);
				}
			}

			if(name_ref != nullptr) {
				CFRelease(name_ref);
			}
			if(url_ref != nullptr) {
				CFRelease(url_ref);
			}
		}
		CFRelease(font_array);
	}

	if(collection != nullptr) {
		CFRelease(collection);
	}
#endif

	return fonts;
}

export [[nodiscard]] auto find_family_of(system_font_map& fonts, const std::string_view prefix) {
	auto start_it = fonts.lower_bound(prefix);
	auto upper_prefix = std::string{prefix};

	while(!upper_prefix.empty() && upper_prefix.back() == std::numeric_limits<char>::max()) {
		upper_prefix.pop_back();
	}

	if(upper_prefix.empty()) {
		return std::ranges::subrange(start_it, fonts.end());
	}

	++upper_prefix.back();
	auto end_it = fonts.lower_bound(upper_prefix);
	return std::ranges::subrange{start_it, end_it};
}

}
