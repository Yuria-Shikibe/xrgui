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
add_requires("mimalloc v2.2.4")

add_requires("glfw")

function set_xrgui_deps()
    add_packages("msdfgen")
    add_packages("freetype")
    add_packages("mimalloc")
    add_packages("nanosvg")
    add_packages("clipper2")

    add_includedirs("external/VulkanMemoryAllocator/include")
    add_includedirs("external/plf_hive")
    add_includedirs("external/small_vector/source/include")
    add_includedirs("external/stb")
    add_includedirs("external/include")

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2")
    add_files("external/allocator2d/include/mo_yanxi/allocator2d.ixx")

    add_deps("mo_yanxi.vulkan_wrapper")
    add_files("src/**.cpp")
    add_files("src/**.ixx")
end

--[[

target("xrgui.core")
set_extension(".exe")
set_kind("binary")
set_languages("c++latest")

set_xrgui_deps()

add_files("main.cpp")
target_end()

]]


target("xrgui")
    set_extension(".exe")
    set_kind("binary")
    set_languages("c++latest")

    set_xrgui_deps()
    add_packages("glfw")

    add_files("main.cpp")
    add_files("src.backends/universal/**.ixx")
    add_files("src.backends/universal/**.cpp")

    add_files("src.backends/vulkan_glfw/**.ixx")
    add_files("src.backends/vulkan_glfw/**.cpp")

    after_build(function (target)
        -- 引入必要的模块
        import("core.base.option")

        -- 1. 获取源目录 (项目根目录/assets)
        local src_dir = path.join(os.projectdir(), "./properties/assets")

        -- 2. 获取目标目录 (可执行文件所在目录)
        local dst_dir = target:targetdir()

        -- 3. 执行复制
        -- os.cp 会自动递归复制文件夹
        print("正在复制资源文件...")
        os.cp(src_dir, dst_dir)
        print("资源已复制到: " .. dst_dir)
    end)
    --add_rules("utils.bin2c", {extensions = {".svg", ".spv"}})
    --add_files("properties/assets/shader/spv/**.spv")
target_end()

task("gen_slang")
    on_run(function ()
        os.exec("py ./properties/build_util/slang_builder.py ./slang/bin/slangc.exe ./properties/assets/shader/spv ./properties/assets/shader/config.json -j 30")
    end)

    set_menu{usage = "compile slang to spirv"}
task_end()
