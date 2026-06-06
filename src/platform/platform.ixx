module;

#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#endif

export module mo_yanxi.platform;

import std;

namespace mo_yanxi::platform {
namespace {
#ifdef _WIN32
thread_local bool com_initialized_by_platform = false;
#endif

[[nodiscard]] std::string decode_uri_path(const std::string_view uri) {
	std::string result;
	result.reserve(uri.size());

	for(std::size_t i = 0; i < uri.size(); ++i) {
		if(uri[i] == '%' && i + 2 < uri.size()) {
			const std::string_view hex = uri.substr(i + 1, 2);
			int value = 0;
			const auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
			if(ec == std::errc{} && ptr == hex.data() + hex.size()) {
				result += static_cast<char>(value);
				i += 2;
				continue;
			}
		}
		result += uri[i];
	}

	return result;
}
#ifdef _WIN32

template <typename T>
void release_com_object(T* object) noexcept {
	if(object != nullptr) {
		object->Release();
	}
}
#endif
}

export void initialize() {
#ifdef _WIN32
	const HRESULT hr_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if(SUCCEEDED(hr_init)) {
		com_initialized_by_platform = true;
		return;
	}

	if(hr_init != RPC_E_CHANGED_MODE) {
		throw std::runtime_error("CoInitializeEx failed");
	}
#endif
}

export void terminate() noexcept {
#ifdef _WIN32
	if(com_initialized_by_platform) {
		CoUninitialize();
		com_initialized_by_platform = false;
	}
#endif
}

export [[nodiscard]] std::optional<std::string> get_environment_variable(const char* name) {
	if(name == nullptr || *name == '\0') {
		return std::nullopt;
	}

#ifdef _WIN32
	char* value{};
	std::size_t value_size{};
	if(::_dupenv_s(&value, &value_size, name) != 0 || value == nullptr) {
		return std::nullopt;
	}

	std::unique_ptr<char, decltype(&std::free)> holder{value, std::free};
	return std::string{value, value_size != 0 ? value_size - 1 : std::strlen(value)};
#else
	if(const char* value = std::getenv(name); value != nullptr) {
		return std::string{value};
	}
	return std::nullopt;
#endif
}

export [[nodiscard]] bool environment_flag_enabled(const char* name) {
	return get_environment_variable(name).value_or(std::string{}) == "1";
}

export [[nodiscard]] constexpr std::string_view get_invalid_filename_chars() noexcept {
#if defined(_WIN32)
	static constexpr auto chars = [] {
		constexpr std::string_view symbols = R"(<>:"/\|?*)";
		std::array<char, 32 + symbols.size()> buffer{};
		auto out = std::ranges::copy(symbols, buffer.begin()).out;
		for(std::uint32_t i = 0; i < 32; ++i) {
			*out++ = static_cast<char>(i);
		}
		return buffer;
	}();

	return std::string_view{chars.data(), chars.size()};
#else
	static constexpr std::array<char, 2> chars{'/', '\0'};
	return std::string_view{chars.data(), chars.size()};
#endif
}

export [[nodiscard]] constexpr std::string_view get_invalid_path_chars() noexcept {
#if defined(_WIN32)
	static constexpr auto chars = [] {
		constexpr std::string_view symbols = R"(<>"|?*)";
		std::array<char, 32 + symbols.size()> buffer{};
		auto out = std::ranges::copy(symbols, buffer.begin()).out;
		for(std::uint32_t i = 0; i < 32; ++i) {
			*out++ = static_cast<char>(i);
		}
		return buffer;
	}();

	return std::string_view{chars.data(), chars.size()};
#else
	static constexpr std::array<char, 1> chars{'\0'};
	return std::string_view{chars.data(), chars.size()};
#endif
}

export struct driver_letters_info {
	std::array<char, 128> buffer{};
	std::uint32_t size{};

	[[nodiscard]] constexpr auto view() const noexcept {
		const std::string_view values{buffer.data(), size};
		return values
			| std::views::split('\0')
			| std::views::filter([](auto&& range) { return !std::ranges::empty(range); })
			| std::views::transform([](auto&& range) {
				return std::string_view(std::ranges::data(range), std::ranges::size(range));
			});
	}
};

export [[nodiscard]] driver_letters_info get_drive_letters() noexcept {
	driver_letters_info info{};
#ifdef _WIN32
	const auto result_size = GetLogicalDriveStringsA(static_cast<DWORD>(info.buffer.size()), info.buffer.data());
	if(result_size > 0 && result_size <= info.buffer.size()) {
		info.size = static_cast<std::uint32_t>(result_size);
	}
#endif
	return info;
}

export [[nodiscard]] std::vector<std::filesystem::path> get_quick_access_folders() {
	std::vector<std::filesystem::path> paths;

#ifdef _WIN32
	IShellItem* home_folder = nullptr;
	if(SUCCEEDED(SHCreateItemFromParsingName(
		   L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}",
		   nullptr,
		   IID_PPV_ARGS(&home_folder)))) {
		std::unique_ptr<IShellItem, decltype(&release_com_object<IShellItem>)> home_holder{
			home_folder,
			release_com_object<IShellItem>
		};

		IEnumShellItems* item_enum = nullptr;
		if(SUCCEEDED(home_folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&item_enum)))) {
			std::unique_ptr<IEnumShellItems, decltype(&release_com_object<IEnumShellItems>)> enum_holder{
				item_enum,
				release_com_object<IEnumShellItems>
			};

			IShellItem* child_item = nullptr;
			while(item_enum->Next(1, &child_item, nullptr) == S_OK) {
				std::unique_ptr<IShellItem, decltype(&release_com_object<IShellItem>)> child_holder{
					child_item,
					release_com_object<IShellItem>
				};

				PWSTR path_str = nullptr;
				if(SUCCEEDED(child_item->GetDisplayName(SIGDN_FILESYSPATH, &path_str))) {
					std::unique_ptr<std::remove_pointer_t<PWSTR>, decltype(&CoTaskMemFree)> path_holder{
						path_str,
						CoTaskMemFree
					};
					paths.emplace_back(path_str);
				}
			}
		}
	}

#elif defined(__linux__)
	if(const char* home_dir = std::getenv("HOME"); home_dir != nullptr) {
		const std::filesystem::path bookmarks_path =
			std::filesystem::path(home_dir) / ".config" / "gtk-3.0" / "bookmarks";
		std::ifstream file(bookmarks_path);

		std::string line;
		while(std::getline(file, line)) {
			if(!line.starts_with("file://")) {
				continue;
			}

			const std::size_t space_pos = line.find(' ');
			const std::string uri = line.substr(7, space_pos != std::string::npos ? space_pos - 7 : std::string::npos);
			std::filesystem::path decoded_path(decode_uri_path(uri));
			if(std::filesystem::exists(decoded_path)) {
				paths.push_back(std::move(decoded_path));
			}
		}
	}

#elif defined(__APPLE__)
	if(const char* home_dir = std::getenv("HOME"); home_dir != nullptr) {
		const std::filesystem::path home(home_dir);
		static constexpr std::array<std::string_view, 6> default_dirs{
			"Desktop",
			"Downloads",
			"Documents",
			"Pictures",
			"Music",
			"Movies"
		};

		for(const auto dir : default_dirs) {
			std::filesystem::path path = home / dir;
			if(std::filesystem::exists(path)) {
				paths.push_back(std::move(path));
			}
		}
	}
#endif

	return paths;
}

}
