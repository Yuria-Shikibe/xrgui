#ifndef XRGUI_GRAPHICS_DETECTION_H
#define XRGUI_GRAPHICS_DETECTION_H

// 定义 API 的枚举常量（用于后续判断）
#define GRAPHICS_BACKEND_NONE   0
#define GRAPHICS_BACKEND_VULKAN 1
#define GRAPHICS_BACKEND_D3D12  2
#define GRAPHICS_BACKEND_METAL  3
#define GRAPHICS_BACKEND_OPENGL 4

// 确保编译器支持 __has_include
#if defined(__has_include)

    // 1. 检查 Vulkan
    #if __has_include(<vulkan/vulkan.h>)
        #define HAS_VULKAN 1
    #endif

    // 2. 检查 DirectX 12 (通常在 Windows SDK 中)
    #if __has_include(<d3d12.h>)
        #define HAS_D3D12 1
    #endif

    // 3. 检查 Metal (通常在 macOS/iOS 下，需注意 Metal 主要是 Obj-C 接口，但在 C++ 中可检查头文件存在性)
    #if __has_include(<Metal/Metal.h>)
        #define HAS_METAL 1
    #endif

    // 4. 检查 OpenGL (作为示例，包含常见的 GL 头文件)
    #if __has_include(<GL/gl.h>) || __has_include(<OpenGL/gl.h>)
        #define HAS_OPENGL 1
    #endif

#else
    // 如果编译器不支持 __has_include，可以在这里添加回退逻辑或报错
    #warning "Compiler does not support __has_include. Graphics API detection skipped."
#endif

// --------------------------------------------------------------------------
// 自动默认 API 选择逻辑
// --------------------------------------------------------------------------

// 辅助宏：用于计算已检测到的 API 数量
// 预处理器会将未定义的宏视为 0，因此可以直接相加
#define GRAPHICS_API_COUNT ( \
    (defined(HAS_VULKAN) ? 1 : 0) + \
    (defined(HAS_D3D12)  ? 1 : 0) + \
    (defined(HAS_METAL)  ? 1 : 0) + \
    (defined(HAS_OPENGL) ? 1 : 0)   \
)

// 如果尚未指定默认图形 API (DEFAULT_GRAPHICS_API)
#if !defined(DEFAULT_GRAPHICS_API)

    // 如果只检测到一个 API，则自动将其设为默认
    #if GRAPHICS_API_COUNT == 1

        #if defined(HAS_VULKAN)
            #define DEFAULT_GRAPHICS_API GRAPHICS_BACKEND_VULKAN
        #elif defined(HAS_D3D12)
            #define DEFAULT_GRAPHICS_API GRAPHICS_BACKEND_D3D12
        #elif defined(HAS_METAL)
            #define DEFAULT_GRAPHICS_API GRAPHICS_BACKEND_METAL
        #elif defined(HAS_OPENGL)
            #define DEFAULT_GRAPHICS_API GRAPHICS_BACKEND_OPENGL
        #endif

    #else
        // 如果检测到 0 个或 >1 个 API，则不自动定义默认值（或定义为 NONE），
        // 迫使开发者在上层手动选择。
        #define DEFAULT_GRAPHICS_API GRAPHICS_BACKEND_NONE
    #endif

#endif

#endif // GRAPHICS_DETECTION_H