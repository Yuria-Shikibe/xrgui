includes("external/**/xmake.lua");

add_rules("mode.debug", "mode.release")
set_arch("x64")
set_encodings("utf-8")
set_project("xrgui")
set_symbols("debug")

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
add_requires("mimalloc v2.2.4")


add_requires("glfw")

function set_xrgui_deps()
    add_packages("msdfgen", {public = true})
    add_packages("freetype", {public = true})
    add_packages("mimalloc", {public = true})
    add_packages("nanosvg", {public = true})
    add_packages("clipper2", {public = true})


    add_includedirs("external/VulkanMemoryAllocator/include", {public = true})
    add_includedirs("external/plf_hive", {public = true})
    add_includedirs("external/small_vector/source/include", {public = true})
    add_includedirs("external/stb", {public = true})
    add_includedirs("external/include", {public = true})

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2", {public = true})
    add_files("external/allocator2d/include/mo_yanxi/allocator2d.ixx", {public = true})

    add_deps("mo_yanxi.vulkan_wrapper")
    add_files("src/**.cpp")
    add_files("src/**.ixx", {public = true})
end


target("xrgui.core")
    set_kind("static")
    set_languages("c++latest")
    set_policy("build.c++.modules", true)


    set_xrgui_deps()
target_end()

target("xrgui.example")
    set_extension(".exe")
    set_kind("binary")
    set_languages("c++latest")
    set_policy("build.c++.modules", true)

    set_xrgui_deps()
    add_packages("glfw")

    add_files("main.cpp")

    add_files("src.backends/universal/**.ixx", {public = true})
    add_files("src.backends/universal/**.cpp")
    add_files("src.backends/vulkan_glfw/**.ixx", {public = true})
    add_files("src.backends/vulkan_glfw/**.cpp")
    add_files("src.examples/**.ixx", {public = true})
    add_files("src.examples/**.cpp")

    after_build(function (target)
        import("core.base.option")

        local src_dir = path.join(os.projectdir(), "./properties/assets")

        local dst_dir = target:targetdir()

        print("正在复制资源文件...")
        os.cp(src_dir, dst_dir)
        print("资源已复制到: " .. dst_dir)
    end)
target_end()

task("gen_slang")
    on_run(function ()
        os.exec("py ./properties/build_util/slang_builder.py ./slang/bin/slangc.exe ./properties/assets/shader/spv ./properties/assets/shader/config.json -j 30")
    end)

    set_menu{usage = "compile slang to spirv"}
task_end()
