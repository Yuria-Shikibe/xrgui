includes("external/**/xmake.lua");

add_rules("mode.debug", "mode.release")
set_arch("x64")
set_encodings("utf-8")
set_project("xrgui")

if is_plat("windows") then
    if is_mode("debug") then
        set_runtimes("MDd")
    else
        set_runtimes("MD")
    end
else
    set_runtimes("c++_shared")
end
add_vectorexts("avx", "avx2")

add_requires("msdfgen", {
    configs = {
        openmp = true,
        extensions = true,
    }
})
add_requires("freetype")
add_requires("nanosvg")
add_requires("clipper2")

target("xrgui")
    set_extension(".exe")
    set_kind("binary")
    set_languages("c++latest")

    add_packages("msdfgen")
    add_packages("freetype")
    add_packages("mimalloc")
    add_packages("nanosvg")
    add_packages("clipper2")


    add_includedirs("external/VulkanMemoryAllocator/include")
    add_includedirs("external/plf_hive")
    add_includedirs("external/small_vector/source/include")
    add_includedirs("external/stb")

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2")
    add_files("external/allocator2d/include/mo_yanxi/allocator2d.ixx")


    add_deps("mo_yanxi.vulkan_wrapper")
    add_files("main.cpp")
    add_files("src/**.cpp")
    add_files("src/**.ixx")
target_end()
