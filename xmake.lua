set_project("xrgui")
set_version("0.1.0")

set_policy("build.c++.modules", true)
add_rules("mode.debug", "mode.release")

local current_dir = os.scriptdir()
local root_dir = os.projectdir()

if current_dir == root_dir then
    set_arch("x64")
    set_encodings("utf-8")

    if os.getenv("GITHUB_ACTIONS") == "true" then

    else
        set_symbols("debug")
        set_strip("debug")
    end

    add_vectorexts("avx", "avx2")
    set_policy("build.warning", true)

    if is_plat("windows") then
        set_runtimes(is_mode("debug") and "MDd" or "MD")
    else
        set_runtimes("c++_shared")
    end
end

-- used for react flow redirect
set_config("spec_mo_yanxi_utility_path", path.join(current_dir, "./external/mo_yanxi_vulkan_wrapper/external/mo_yanxi_utility"))

includes("external/**/xmake.lua")

add_requires("msdfgen", { configs = { openmp = true, extensions = true } })
add_requires("freetype")
add_requires("harfbuzz", { configs = { cxflags = "-DHB_NO_MT" } })
add_requires("nanosvg", "spirv-reflect", "gtl", "glfw")
add_requires("mimalloc v3.2.8")
add_requires("simdutf", { optional = true })

rule("media.svg_to_bin")
    set_extensions(".svg")
    on_build_file(function (target, sourcefile, opt)
        import("core.project.config")
        import("utils.binary.bin2c")

        local my_res_inc = path.join(config.builddir(), ".assets", "includes")
        local src_basedir = path.join(os.projectdir(), "properties/assets_raw/gen")
        local rel_path = path.relative(sourcefile, src_basedir)
        local headerfile = path.join(my_res_inc, rel_path .. ".h")

        if not os.isfile(headerfile) or os.mtime(headerfile) < os.mtime(sourcefile) then
            os.mkdir(path.directory(headerfile))
            print("generating.bin2c " .. headerfile)
            bin2c(sourcefile, headerfile)
        end
    end)
rule_end()

function set_xrgui_deps()
    add_deps("mo_yanxi.utility", "mo_yanxi.vulkan_wrapper", "mo_yanxi.react_flow")

    add_packages("msdfgen")
    add_packages("freetype")
    add_packages("harfbuzz")
    add_packages("mimalloc")
    add_packages("nanosvg")
    add_packages("gtl")
    add_packages("spirv-reflect")
    add_packages("simdutf")

    add_includedirs("./external/VulkanMemoryAllocator/include")
    add_includedirs("./external/stb")
    add_includedirs("./external/include")
    add_includedirs("./external/plf_hive")
    add_includedirs("./external/small_vector/source/include")

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2", {public = true})
    add_defines("MO_YANXI_DATA_FLOW_DISABLE_THREAD_CHECK", {public = true})

    -- msvc 新版好像没这问题了，哪天删了，，，
    add_defines("XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE", {public = true})

    add_files("./external/allocator2d/include/mo_yanxi/allocator2d.ixx", {public = true})
    add_files("./src/**.cpp")
    add_files("./src/**.ixx", {public = true})

    add_cxflags("/wd4267", "/wd4244", "/wd4305", {tools = {"cl", "clang_cl"}})

--     on_config(function (target)
--         local flags = nil
--         if target:has_tool("cxx", "cl", "clang_cl") then
--             flags = {"/wd4267", "/wd4244", "/wd4305"}
--         elseif target:has_tool("cxx", "gcc", "clang") then
--             flags = "-Wno-conversion"
--         end
--
--         if flags then
--             for _, file in ipairs(os.files("src/**.ixx")) do
--                 local relpath = path.relative(file, os.projectdir())
--                 target:fileconfig_add(relpath, {cxflags = flags, public = true})
--             end
--             for _, file in ipairs(os.files("src/**.cpp")) do
--                 local relpath = path.relative(file, os.projectdir())
--                 target:fileconfig_add(relpath, {cxflags = flags})
--             end
--         end
--     end)

    add_links("shaderc_shared")
end

target("xrgui")
    set_kind("object")
    set_languages("c++latest")
    set_warnings("all", "pedantic")
    set_xrgui_deps()

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    if is_plat("windows") then
        add_syslinks("imm32", {public = true})
    end

    set_enabled(false)

target_end()

target("xrgui.example")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")

    set_warnings("all", "pedantic")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    if is_plat("windows") then
        add_syslinks("imm32", {public = true})
    end

    set_xrgui_deps()
-- 似乎有bug使得msvc总说找不到模块，暂时直接添加代码
--     add_deps("xrgui")
    add_packages("glfw")

    add_files("src.backends/universal/**.ixx", {public = true})
    add_files("src.backends/universal/**.cpp")
    add_files("src.backends/vulkan/**.ixx", {public = true})
    add_files("src.backends/vulkan/**.cpp")
    add_files("src.backends/vulkan_glfw/**.ixx", {public = true})
    add_files("src.backends/vulkan_glfw/**.cpp")
    add_files("src.examples/**.ixx", {public = true})
    add_files("src.examples/**.cpp")


    add_rules("media.svg_to_bin")
    add_files("properties/assets_raw/gen/**.svg")

    on_config(function (target)
        import("core.project.config")
        local my_res_inc = path.join(config.builddir(), ".assets", "includes")

        -- 提前创建目录，防止编译器因为目录不存在而忽略该包含路径
        if not os.isdir(my_res_inc) then
            os.mkdir(my_res_inc)
        end

        -- 确保编译器的搜索路径中包含此目录
        target:add("includedirs", my_res_inc, {public = true})

        local summary_header_path = path.join(my_res_inc, "assets_summary.h")
        if not os.isfile(summary_header_path) then
            io.writefile(summary_header_path, "#pragma once\n// Generated placeholder\n")
        end
    end)

    -- 构建前自动生成汇总头文件 assets_summary.h
    before_build(function (target)
            import("core.project.config")
            local my_res_inc = path.join(config.builddir(), ".assets", "includes")
            local summary_header_path = path.join(my_res_inc, "assets_summary.h")

            local summary_lines = { "#pragma once\n" }

            local src_basedir = path.join(os.projectdir(), "properties/assets_raw/gen")
            local svg_files = os.files(path.join(src_basedir, "**.svg"))

            for _, file in ipairs(svg_files) do
                local rel_path = path.relative(file, src_basedir)
                local inc_path = (rel_path .. ".h"):gsub("\\", "/")

                local dir = path.directory(rel_path)
                local ns_decl, ns_close = "", ""
                if dir and dir ~= "." and dir ~= "" then
                    local ns_name = dir:gsub("[\\/]", "::"):gsub("[%.%-]", "_")
                    ns_decl = "namespace " .. ns_name .. " {\n"
                    ns_close = "} // namespace " .. ns_name .. "\n"
                end

                local var_name = path.filename(rel_path):gsub("[%.%-]", "_")
                table.insert(summary_lines, string.format([[
    %s#if __has_include("%s")
    constexpr inline char %s[] = {
    #include "%s"
    };
    #else
    constexpr inline char %s[] = {};
    #endif
    %s]], ns_decl, inc_path, var_name, inc_path, var_name, ns_close))
            end

            local new_content = table.concat(summary_lines, "\n")
            if io.readfile(summary_header_path) ~= new_content then
                print("updating " .. summary_header_path)
                io.writefile(summary_header_path, new_content)
            end
        end)

    after_build(function (target)
        local dst_dir = target:targetdir()
        os.cp(path.join(os.projectdir(), "properties/assets"), dst_dir)
        os.cp(path.join(os.projectdir(), "properties/vk_layer_settings.txt"), dst_dir)
    end)
target_end()

task("gen_slang")
    on_run(function ()
        import("core.base.option")

        local path_builder = path.join(current_dir, "./properties/build_util/slang_builder.py");
        local path_config = path.join(current_dir, option.get("config"));
        local path_slangc = option.get("complier");
        local pass = option.get("pass");

        os.exec("py " .. path_builder .. " " .. path_slangc .. " " .. option.get("output") .. " " .. path_config .. " -j 30 " .. pass)
    end)

    set_menu({
        usage = "compile slang to spirv",
        options = {
            {'c', "complier", "kv", "./slang/bin/slangc.exe", "Path to slangc.exe"},
            {'o', "output", "kv", "./properties/assets/shader/spv", "Spirv Output Dir Relative To Directory Root"},
            {'f', "config", "kv", "./properties/assets/shader/config.toml", "Shader Build Config"},
            {'p', "pass", "kv", "", "Pass through args to py"},
        }
    })
task_end()

task("gen_icon")
    on_run(function ()
        os.exec("py ./properties/assets_raw/svg_normalize.py -i ./properties/assets_raw/icons -o ./properties/assets_raw/gen/icons")
    end)

    set_menu({
            usage = "generate path normalized icons for msdfgen"
    })
task_end()

task("switch_mode")
    set_menu({
        usage = "xmake switch_mode [mode]",
        description = "切换编译模式。若模式改变，则执行 clean 并重新生成 compile_commands",
        options = {
            {nil, "mode", "v", nil, "目标编译模式 (例如: debug, release)"}
        }
    })

    on_run(function ()
        import("core.project.config")
        import("core.base.option")
        import("core.base.task")

        local target_mode = option.get("mode")
        if not target_mode then
            cprint("${red}错误: 请指定目标模式！例如: xmake switch_mode debug${clear}")
            return
        end

        config.load()
        local current_mode = config.get("mode")

        if current_mode == target_mode then
            cprint("${color.success}当前已经是 %s 模式，无需进行任何操作。${clear}", target_mode)
            return
        end

        cprint("${yellow}正在将模式从 '%s' 切换至 '%s'...${clear}", tostring(current_mode), target_mode)

        task.run("config", {mode = target_mode})
        task.run("clean")
        task.run("project", {kind = "compile_commands"})

        cprint("${color.success}模式切换与清理配置完成！${clear}")
    end)

