-- 设置项目基本信息
set_project("xrgui")
set_version("0.1.0")

-- 允许 C++ 模块构建与 LTO 策略
set_policy("build.c++.modules", true)

-- 定义基础编译模式
add_rules("mode.debug", "mode.release")

-- 获取当前路径与根路径
local current_dir = os.scriptdir()
local root_dir = os.projectdir()

-- 全局基础配置
if current_dir == root_dir then
    set_arch("x64")
    set_encodings("utf-8")
    set_symbols("debug")
    add_vectorexts("avx", "avx2")
    set_policy("build.warning", true)

    if is_plat("windows") then
        set_runtimes(is_mode("debug") and "MDd" or "MD")
    else
        set_runtimes("c++_shared")
    end
end

-- 保留特定的配置路径
set_config("spec_mo_yanxi_utility_path", path.join(current_dir, "./external/mo_yanxi_vulkan_wrapper/external/mo_yanxi_utility"))

-- 包含外部依赖的 xmake 脚本
includes("external/**/xmake.lua")

-- ---------------------------------------------------------
-- 依赖包定义
-- ---------------------------------------------------------
add_requires("msdfgen", { configs = { openmp = true, extensions = true } })
add_requires("freetype")
add_requires("harfbuzz", { configs = { cxflags = "-DHB_NO_MT" } })
add_requires("nanosvg", "spirv-reflect", "gtl", "glfw")
add_requires("mimalloc v3.2.8")
add_requires("simdutf", { optional = true })

-- ---------------------------------------------------------
-- 自定义规则：SVG 资源处理 (生成 .h)
-- ---------------------------------------------------------
rule("media.svg_to_bin")
    set_extensions(".svg")
    on_build_file(function (target, sourcefile, opt)
        import("core.project.config")
        import("utils.binary.bin2c")

        local my_res_inc = path.join(config.builddir(), ".assets", "includes")
        local src_basedir = path.join(os.projectdir(), "properties/assets/images")
        local rel_path = path.relative(sourcefile, src_basedir)
        local headerfile = path.join(my_res_inc, rel_path .. ".h")

        if not os.isfile(headerfile) or os.mtime(headerfile) < os.mtime(sourcefile) then
            os.mkdir(path.directory(headerfile))
            print("generating.bin2c " .. headerfile)
            bin2c(sourcefile, headerfile)
        end
    end)
rule_end()

-- ---------------------------------------------------------
-- 依赖配置辅助函数 (保留原始 add_packages 结构)
-- ---------------------------------------------------------
function set_xrgui_deps()
    add_deps("mo_yanxi.utility", "mo_yanxi.vulkan_wrapper", "mo_yanxi.react_flow")

    -- 保留原始的 add_packages 列表
    add_packages("msdfgen", {public = true})
    add_packages("freetype", {public = true})
    add_packages("harfbuzz", {public = true})
    add_packages("mimalloc", {public = true})
    add_packages("nanosvg", {public = true})
    add_packages("gtl", {public = true})
    add_packages("spirv-reflect", {public = true})
    add_packages("simdutf", {public = true})

    -- 包含目录与宏定义
    add_includedirs("./external/VulkanMemoryAllocator/include", {public = true})
    add_includedirs("./external/stb", {public = true})
    add_includedirs("./external/include", {public = true})
    add_includedirs("./external/plf_hive", {public = true})
    add_includedirs("./external/small_vector/source/include", {public = true})

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2", {public = true})
    add_defines("MO_YANXI_DATA_FLOW_DISABLE_THREAD_CHECK", {public = true})
    add_defines("XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE", {public = true})

    -- 源文件
    add_files("./external/allocator2d/include/mo_yanxi/allocator2d.ixx", {public = true})
    add_files("./src/**.cpp")
    add_files("./src/**.ixx", {public = true})

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
    add_cxflags("/wd4267", "/wd4244", "/wd4305", {tools = {"cl", "clang_cl"}})
    set_warnings("all", "pedantic")
    set_xrgui_deps()

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    if is_plat("windows") then
        add_syslinks("imm32", {public = true})
    end
target_end()

target("xrgui.lib")
    set_kind("static")
    set_extension(".exe")
    set_languages("c++latest")

    set_warnings("all", "pedantic")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end

    add_deps("xrgui")
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
    add_files("properties/assets/images/**.svg")

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

            local src_basedir = path.join(os.projectdir(), "properties/assets/images")
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
            -- 只有内容变化才写入，避免触发全量重编
            if io.readfile(summary_header_path) ~= new_content then
                print("updating " .. summary_header_path)
                io.writefile(summary_header_path, new_content)
            end
        end)

    -- 编译后资源同步
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

        os.exec("py " .. path_builder .. " " .. path_slangc .. " " .. option.get("output") .. " " .. path_config .. " -j 30")
    end)

    set_menu({
        usage = "compile slang to spirv",
        options = {
            {'c', "complier", "kv", "./slang/bin/slangc.exe", "Path to slangc.exe"},
            {'o', "output", "kv", "./properties/assets/shader/spv", "Spirv Output Dir Relative To Directory Root"},
            {'f', "config", "kv", "./properties/assets/shader/config.toml", "Shader Build Config"},
        }
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
        -- 引入 task 模块，用于调用内置任务或插件
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

        -- 1. 等价于 xmake f --mode=...
        task.run("config", {mode = target_mode})

        -- 2. 等价于 xmake clean
        task.run("clean")

        -- 3. 等价于 xmake project -k compile_commands
        -- 注意：命令行的短选项 -k 对应底层参数长选项 kind
        task.run("project", {kind = "compile_commands"})

        cprint("${color.success}模式切换与清理配置完成！${clear}")
    end)

