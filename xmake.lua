includes("external/**/xmake.lua");

local current_dir = os.scriptdir()

add_rules("mode.debug", "mode.release")
set_arch("x64")
set_encodings("utf-8")
set_project("xrgui")
set_symbols("debug")

if is_plat("windows") then
    add_vectorexts("avx", "avx2")

    if is_mode("debug") then
        set_runtimes("MDd")
    else
        set_runtimes("MD")
    end
else
    set_runtimes("c++_shared")
end


add_requires("msdfgen", {
    configs = {
        openmp = true,
        extensions = true,
    }
})

add_requires("freetype")
add_requires("harfbuzz")
add_requires("nanosvg")
add_requires("spirv-reflect")
add_requires("clipper2")
add_requires("mimalloc v2.2.4")
add_requires("glfw")

add_requires("simdutf", {optional = true})

function set_xrgui_deps()
    function join_path(p)
       return path.join(current_dir, p)
    end



    add_deps("mo_yanxi.utility")
    add_deps("mo_yanxi.vulkan_wrapper")

    add_packages("msdfgen", {public = true})
    add_packages("freetype", {public = true})
    add_packages("harfbuzz", {public = true})
    add_packages("mimalloc", {public = true})
    add_packages("nanosvg", {public = true})
    add_packages("clipper2", {public = true})
    add_packages("spirv-reflect", {public = true})

    add_packages("simdutf", {public = true})


    add_includedirs(join_path("./external/VulkanMemoryAllocator/include"), {public = true})
    add_includedirs(join_path("./external/stb"), {public = true})
    add_includedirs(join_path("./external/include"), {public = true})
    add_includedirs(join_path("./external/plf_hive"), {public = true})
    add_includedirs(join_path("./external/small_vector/source/include"), {public = true})

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2", {public = true})
    add_files(join_path("./external/allocator2d/include/mo_yanxi/allocator2d.ixx"), {public = true})



    add_defines("XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE", {public = true})
    add_files(join_path("./src/**.cpp"))
    add_files(join_path("./src/**.ixx"), {public = true})
end


-- target("xrgui.core")
--     set_kind("static")
--     set_languages("c++23")
--     set_policy("build.c++.modules", true)
--
--     set_xrgui_deps()
-- target_end()


target("xrgui.example")
    set_extension(".exe")
    set_kind("binary")
    set_languages("c++23")
    set_policy("build.c++.modules", true)

    set_xrgui_deps()

    set_warnings("all")
    set_warnings("pedantic")

    add_packages("glfw")

    add_files("main.cpp")

    add_files("src.backends/universal/**.ixx", {public = true})
    add_files("src.backends/universal/**.cpp")
    add_files("src.backends/vulkan/**.ixx", {public = true})
    add_files("src.backends/vulkan/**.cpp")
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
        import("core.base.option")

        local path_builder = path.join(current_dir, "./properties/build_util/slang_builder.py");
        local path_config = path.join(current_dir, option.get("config"));
        local path_slangc = option.get("complier");

        os.exec("py " .. path_builder .. " " .. path_slangc .. " " .. option.get("output") .. " " .. path_config .. " -j 30")
    end)

    set_menu({
        usage = "compile slang to spirv",
        options = {
            {'c', "complier", "kv", "./properties/build_util/slang/bin/slangc.exe", "Path to slangc.exe"},
            {'o', "output", "kv", "./properties/assets/shader/spv", "Spirv Output Dir Relative To Directory Root"},
            {'f', "config", "kv", "./properties/assets/shader/config.json", "Shader Build Config"},
        }
    })
task_end()

task("set_mode")
set_menu({
    usage = "xmake build_release",
    description = "Switch to mode and generate cmake",
    options = {
        {'m', "mode", "kv", nil, "Select mode"}
    }
})

on_run(function ()
    import("core.base.task")
    import("core.base.option")

    os.exec("xmake f -m " .. option.get("mode"))
    task.run("gen_ide_hintonly_cmake")

    print("Switch To " .. option.get("mode"))
end)
task_end()