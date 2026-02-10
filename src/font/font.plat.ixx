module;


#if defined(_WIN32)
#include <windows.h>
#include <tchar.h>
#include <shlobj.h>
#elif defined(__linux__)
#include <fontconfig/fontconfig.h>
#elif defined(__APPLE__)
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#endif


export module mo_yanxi.font.plat;

import std;

// 定义返回结构
struct font_descriptor {
    std::string family_name;
    std::filesystem::path file_path;
};

// 辅助函数：Windows 宽字符串转 UTF-8 std::string
#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
}
#endif

// 核心函数：获取系统字体


namespace mo_yanxi::font{
export
[[nodiscard]] std::vector<font_descriptor> get_system_fonts() {
    std::vector<font_descriptor> fonts;

#if defined(_WIN32)
    // --- Windows 实现 (基于注册表) ---
    HKEY h_key;
    // 打开字体注册表键
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &h_key) == ERROR_SUCCESS) {
        DWORD max_value_name_len, max_value_data_len;
        RegQueryInfoKey(h_key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_value_name_len, &max_value_data_len, NULL, NULL);

        // 分配缓冲区
        std::vector<wchar_t> value_name(max_value_name_len + 1);
        std::vector<wchar_t> value_data(max_value_data_len + 1);

        // 获取 Windows 字体目录 (通常是 C:\Windows\Fonts)
        wchar_t win_font_dir[MAX_PATH];
        if (SHGetSpecialFolderPathW(0, win_font_dir, CSIDL_FONTS, FALSE) == FALSE) {
            // 如果获取失败，回退到硬编码
            wcscpy_s(win_font_dir, L"C:\\Windows\\Fonts");
        }
        std::filesystem::path system_font_path(win_font_dir);

        DWORD index = 0;
        while (true) {
            DWORD name_len = max_value_name_len + 1;
            DWORD data_len = max_value_data_len + 1;
            DWORD type;

            LSTATUS status = RegEnumValueW(
                h_key, index,
                value_name.data(), &name_len,
                NULL, &type,
                reinterpret_cast<LPBYTE>(value_data.data()), &data_len
            );

            if (status != ERROR_SUCCESS) break;

            if (type == REG_SZ) {
                std::wstring font_name_w(value_name.data());
                std::wstring file_name_w(value_data.data());

                // 简单的清理：移除结尾的 " (TrueType)" 等标记
                size_t param_pos = font_name_w.find(L" (");
                if (param_pos != std::wstring::npos) {
                    font_name_w = font_name_w.substr(0, param_pos);
                }

                std::filesystem::path full_path;
                // 如果注册表里的路径包含盘符或反斜杠，通常是绝对路径
                if (file_name_w.find(L"\\") != std::wstring::npos || file_name_w.find(L":") != std::wstring::npos) {
                    full_path = file_name_w;
                } else {
                    // 否则是相对路径，位于 C:\Windows\Fonts 下
                    full_path = system_font_path / file_name_w;
                }

                fonts.push_back({ wide_to_utf8(font_name_w), full_path });
            }
            index++;
        }
        RegCloseKey(h_key);
    }

#elif defined(__linux__)
    // --- Linux 实现 (基于 Fontconfig) ---
    FcConfig* config = FcInitLoadConfigAndFonts();
    FcPattern* pat = FcPatternCreate();
    FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, FC_FILE, (char*)0);
    FcFontSet* fs = FcFontList(config, pat, os);

    if (fs) {
        for (int i = 0; i < fs->nfont; i++) {
            FcPattern* font = fs->fonts[i];
            FcChar8* file = NULL;
            FcChar8* family = NULL;

            if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch &&
                FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch) {

                fonts.push_back({
                    std::string(reinterpret_cast<char*>(family)),
                    std::filesystem::path(reinterpret_cast<char*>(file))
                });
            }
        }
        FcFontSetDestroy(fs);
    }
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    FcConfigDestroy(config);

#elif defined(__APPLE__)
    // --- macOS 实现 (基于 CoreText) ---
    CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
    CFArrayRef font_arr = CTFontCollectionCreateMatchingFontDescriptors(collection);

    if (font_arr) {
        CFIndex count = CFArrayGetCount(font_arr);
        for (CFIndex i = 0; i < count; i++) {
            CTFontDescriptorRef descriptor = (CTFontDescriptorRef)CFArrayGetValueAtIndex(font_arr, i);

            // 获取字体名称
            CFStringRef name_ref = (CFStringRef)CTFontDescriptorCopyAttribute(descriptor, kCTFontFamilyNameAttribute);

            // 获取字体 URL (路径)
            CFURLRef url_ref = (CFURLRef)CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute);

            if (name_ref && url_ref) {
                // 转换名称
                char name_buf[256];
                if (CFStringGetCString(name_ref, name_buf, sizeof(name_buf), kCFStringEncodingUTF8)) {
                    // 转换路径
                    char path_buf[1024];
                    if (CFURLGetFileSystemRepresentation(url_ref, true, (UInt8*)path_buf, sizeof(path_buf))) {
                        fonts.push_back({
                            std::string(name_buf),
                            std::filesystem::path(path_buf)
                        });
                    }
                }
            }
            if (name_ref) CFRelease(name_ref);
            if (url_ref) CFRelease(url_ref);
        }
        CFRelease(font_arr);
    }
    CFRelease(collection);
#endif

    return fonts;
}

}