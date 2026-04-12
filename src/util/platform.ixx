module;

#ifdef _WIN32
#define USING_WINDOWS
#endif

#ifdef USING_WINDOWS
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#elif defined(__linux__)
#include <fstream>
#include <cstdlib>


std::string decode_uri(const std::string& uri) {
	std::string result;
	for (size_t i = 0; i < uri.length(); ++i) {
		if (uri[i] == '%' && i + 2 < uri.length()) {
			std::string hex_str = uri.substr(i + 1, 2);
			result += static_cast<char>(std::stoi(hex_str, nullptr, 16));
			i += 2;
		} else {
			result += uri[i];
		}
	}
	return result;
}
#elif defined(__APPLE__)
#include <cstdlib>
#endif

export module mo_yanxi.core.platform;

import std;

namespace mo_yanxi::platform {


export void initialize(){
#ifdef _WIN32
	HRESULT hr_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr_init) && hr_init != RPC_E_CHANGED_MODE) {
		throw std::runtime_error("CoInitializeEx failed");
	}
#endif
}

export void terminate(){
	CoUninitialize();
}

export
[[nodiscard]] constexpr std::string_view get_invalid_filename_chars() noexcept {

#if defined(_WIN32)
	static constexpr auto arr = [](){
		constexpr std::string_view chars = R"(<>:"/\|?*)";
		constexpr auto sz = 32 + std::ranges::size(chars);
		std::array<char, sz> char_array;
		auto rst = std::ranges::copy(chars, char_array.begin());
		for (std::uint32_t i = 0; i < 32; ++i) {
			*rst.out = static_cast<char>(i);
			++rst.out;
		}
		return char_array;
	}();
	return std::string_view(arr);
#else
	// POSIX (Linux, macOS, Unix) 仅禁用正斜杠和空字符
	invalid_chars += '/';
	invalid_chars += '\0';
	return "/\0";
#endif
}

export
[[nodiscard]] constexpr std::string_view get_invalid_path_chars() noexcept {
#if defined(_WIN32)
	// 编译期生成 Windows 路径禁用字符数组
	static constexpr auto arr = [](){
		// 相比文件名，路径允许斜杠 '/'、反斜杠 '\' 和 盘符冒号 ':'
		constexpr std::string_view chars = R"(<>"|?*)";
		constexpr auto sz = 32 + chars.size(); // 32 个控制字符 (0-31) + 特殊符号

		std::array<char, sz> char_array{};
		auto rst = std::ranges::copy(chars, char_array.begin());

		// 填入 ASCII 0 (\0) 到 31 的控制字符
		for (std::uint32_t i = 0; i < 32; ++i) {
			*rst.out = static_cast<char>(i);
			++rst.out;
		}
		return char_array;
	}();

	// 使用 data() 和 size() 构造，防止遇到 '\0' 时被 C 风格字符串隐式截断
	return std::string_view(arr.data(), arr.size());
#else
	// POSIX (Linux, macOS, Unix) 系统中，'/' 是合法的路径分隔符。
	// 整个路径字符串中唯一真正非法的字符只有空字符 '\0'。
	static constexpr std::array<char, 1> arr = {'\0'};
	return std::string_view(arr.data(), arr.size());
#endif
}



export struct driver_letters_info {
	std::array<char, 128> buffer;
	std::uint32_t size;

	[[nodiscard]] constexpr auto view() const noexcept {
		std::string_view sv{buffer.data(), size};
		return sv
			| std::views::split('\0')
			| std::views::filter([](auto&& r) { return !std::ranges::empty(r); })
			| std::views::transform([](auto&& r) {
				return std::string_view(std::ranges::data(r), std::ranges::size(r));
			});
	}
};

export [[nodiscard]] driver_letters_info get_drive_letters() noexcept {
	driver_letters_info info{};
#ifdef USING_WINDOWS
	const auto result_size = GetLogicalDriveStringsA(sizeof(info.buffer), info.buffer.data());
	// 确保成功且没有超出缓冲区大小
	if (result_size > 0 && result_size <= sizeof(info.buffer)) {
		info.size = static_cast<std::uint32_t>(result_size);
	}
#endif
	return info;
}

/**
 * @brief 获取操作系统文件管理器的侧边栏/快速访问文件夹路径列表
 * @return std::vector<std::filesystem::path> 有效的物理文件夹路径集合
 */
export std::vector<std::filesystem::path> get_quick_access_folders() {
    std::vector<std::filesystem::path> paths;

#ifdef _WIN32
    IShellItem* home_folder = nullptr;
    // FOLDERID_HomeFolder 是 Windows 10/11 中“快速访问”的虚拟文件夹 ID
    if (SUCCEEDED(SHCreateItemFromParsingName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}", nullptr, IID_PPV_ARGS(&home_folder)))) {
        IEnumShellItems* item_enum = nullptr;
        // 枚举“快速访问”下的所有子项（即你截图中的那些文件夹）
        if (SUCCEEDED(home_folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&item_enum)))) {
            IShellItem* child_item = nullptr;
            while (item_enum->Next(1, &child_item, nullptr) == S_OK) {
                PWSTR path_str = nullptr;
                // SIGDN_FILESYSPATH 确保只提取具有真实系统路径的项，过滤掉纯虚拟项（如控制面板）
                if (SUCCEEDED(child_item->GetDisplayName(SIGDN_FILESYSPATH, &path_str))) {
                	try{
                		paths.emplace_back(path_str);
                	}catch(...){
                		CoTaskMemFree(path_str);
                		child_item->Release();
                		item_enum->Release();
                		home_folder->Release();
                		throw;
                	}
                	CoTaskMemFree(path_str);
                }
                child_item->Release();
            }
            item_enum->Release();
        }
        home_folder->Release();
    }

#elif defined(__linux__)
    // 大多数现代 Linux 桌面 (GNOME/KDE/XFCE) 使用 GTK 书签标准
    const char* home_dir = std::getenv("HOME");
    if (home_dir) {
        std::filesystem::path bookmarks_path = std::filesystem::path(home_dir) / ".config" / "gtk-3.0" / "bookmarks";
        std::ifstream file(bookmarks_path);
        std::string line;
        while (std::getline(file, line)) {
            if (line.starts_with("file://")) {
                // 书签格式通常是 "file:///path/to/folder Optional Name"
                size_t space_pos = line.find(' ');
                std::string uri = line.substr(7, space_pos != std::string::npos ? space_pos - 7 : std::string::npos);
                std::filesystem::path decoded_path(decode_uri(uri));
                if (std::filesystem::exists(decoded_path)) {
                    paths.push_back(decoded_path);
                }
            }
        }
    }

#elif defined(__APPLE__)
    // 纯 C++ 无法直接读取 macOS Finder 的私有 Sidebar 数据库 (sfl2/sfl3 格式)。
    // 这里提供标准的后备方案。若需真实数据，必须混编 Objective-C++ 调用 NSFileManager/LSSharedFileList。
    const char* home_dir = std::getenv("HOME");
    if (home_dir) {
        std::filesystem::path home(home_dir);
        std::vector<std::string> default_dirs = {"Desktop", "Downloads", "Documents", "Pictures", "Music", "Movies"};
        for (const auto& dir : default_dirs) {
            std::filesystem::path p = home / dir;
            if (std::filesystem::exists(p)) {
                paths.push_back(p);
            }
        }
    }
#endif

    return paths;
}

} // namespace mo_yanxi::core