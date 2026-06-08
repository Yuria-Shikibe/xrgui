local current_dir = os.scriptdir()
local root_dir = os.projectdir()
local is_host_project = path.absolute(current_dir) == path.absolute(root_dir)
local magic_enum_dir = path.join(current_dir, "external/magic_enum")

if is_host_project then
    set_project("xrgui")
    set_version("0.1.0")

    add_rules("mode.debug", "mode.release")
    set_arch("x64")
    set_encodings("utf-8")

    if os.getenv("GITHUB_ACTIONS") ~= "true" then
        set_symbols("debug", "embed")
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
set_config("spec_mo_yanxi_utility_path", path.join(current_dir, "external/mo_yanxi_vulkan_wrapper/external/mo_yanxi_utility"))

includes("external/**/xmake.lua")

if not is_host_project then
    target("mo_yanxi.vulkan_wrapper.test")
        set_enabled(false)
        set_default(false)
    target_end()
end

add_requires("msdfgen", { configs = { openmp = true, extensions = true } })
add_requires("freetype")
add_requires("harfbuzz", { configs = { cxflags = "-DHB_NO_MT" } })
add_requires("nanosvg", "spirv-reflect", "gtl", "glfw")
add_requires("mimalloc v3.2.8")
add_requires("simdutf", { optional = true })
add_requires("toml++")

if is_host_project then
    add_requires("gtest")
end

rule("media.svg_to_bin")
    set_extensions(".svg")
    on_build_file(function (target, sourcefile, opt)
        import("core.project.config")
        import("utils.binary.bin2c")

        local my_res_inc = path.join(config.builddir(), ".assets", "includes")
        local src_basedir = path.join(current_dir, "properties/assets_raw/gen")
        local rel_path = path.relative(sourcefile, src_basedir)
        local headerfile = path.join(my_res_inc, rel_path .. ".h")

        if not os.isfile(headerfile) or os.mtime(headerfile) < os.mtime(sourcefile) then
            os.mkdir(path.directory(headerfile))
            print("generating.bin2c " .. headerfile)
            bin2c(sourcefile, headerfile)
        end
    end)
rule_end()

local function add_xrgui_target_options()
    set_policy("build.c++.modules", true)
    set_languages("c++latest")
    set_warnings("all", "pedantic")
end

local function add_xrgui_core_deps()
    add_deps("mo_yanxi.utility", "mo_yanxi.vulkan_wrapper", "mo_yanxi.react_flow", {public = true})

    add_packages("msdfgen", {public = true})
    add_packages("freetype", {public = true})
    add_packages("harfbuzz", {public = true})
    add_packages("mimalloc", {public = true})
    add_packages("nanosvg")
    add_packages("gtl", {public = true})
    add_packages("spirv-reflect", {public = true})
    add_packages("simdutf")
    add_packages("toml++", {public = true})

    add_includedirs("./external/VulkanMemoryAllocator/include", {public = true})
    add_includedirs("./external/stb")
    add_includedirs("./external/include")
    add_includedirs("./external/plf_hive")
    add_includedirs("./external/small_vector/source/include")
    add_includedirs(path.join(magic_enum_dir, "include"), {public = true})

    add_defines("MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE", "MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2", {public = true})
    add_defines("MO_YANXI_DATA_FLOW_DISABLE_THREAD_CHECK", {public = true})
    -- msvc 新版好像没这问题了，哪天删了，，，
    add_defines("XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE", {public = true})

    add_files("./external/allocator2d/include/mo_yanxi/allocator2d.ixx", {public = true})
    add_files(path.join(magic_enum_dir, "module/magic_enum.cppm"), {
        public = true,
        defines = "MAGIC_ENUM_USE_STD_MODULE"
    })
    add_files("./src/**.cpp")
    add_files("./src/**.ixx", {public = true})

    --add_cxflags("/wd4267", "/wd4244", "/wd4305", {tools = {"cl", "clang_cl"}})
end

local function add_xrgui_default_stack()
    add_xrgui_core_deps()

    add_packages("glfw", {public = true})
    add_files("src.backends/universal/**.ixx", {public = true})
    add_files("src.backends/universal/**.cpp")
    add_files("src.backends/vulkan/**.ixx", {public = true})
    add_files("src.backends/vulkan/**.cpp")
    add_files("src.backends/vulkan_glfw/**.ixx", {public = true})
    add_files("src.backends/vulkan_glfw/**.cpp")

    add_files("gui.config/**.ixx", {public = true})
    add_files("gui.config/**.cpp")


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

    before_build(function (target)
        import("core.project.config")
        local my_res_inc = path.join(config.builddir(), ".assets", "includes")
        local summary_header_path = path.join(my_res_inc, "assets_summary.h")

        local summary_lines = { "#pragma once\n" }

        local src_basedir = path.join(current_dir, "properties/assets_raw/gen")
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
        os.cp(path.join(current_dir, "properties/assets"), dst_dir)
        os.cp(path.join(current_dir, "properties/vk_layer_settings.txt"), dst_dir)
    end)

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    if is_plat("windows") then
        add_syslinks("advapi32", "imm32", "ole32", "shell32", "user32", {public = true})
    end
end

target("xrgui.default")
    set_kind("object")
    add_xrgui_target_options()

    add_xrgui_default_stack()
target_end()

if is_host_project then
    target("xrgui.example")
        set_kind("binary")
        set_extension(".exe")
        add_xrgui_target_options()

        add_xrgui_default_stack()

        add_files("src.examples/**.ixx", {public = true})
        add_files("src.examples/**.cpp")
    target_end()

    target("xrgui.hello")
        set_kind("binary")
        set_extension(".exe")
        add_xrgui_target_options()

        add_xrgui_default_stack()

        add_files("src.hello/**.cpp")
    target_end()

    target("xrgui.tests")
        set_kind("binary")
        set_extension(".exe")
        add_xrgui_target_options()

        add_deps("mo_yanxi.utility", "mo_yanxi.react_flow")
        add_packages("gtest")
        add_packages("toml++", {public = true})
        add_includedirs(path.join(magic_enum_dir, "include"), {public = true})
        add_files(path.join(magic_enum_dir, "module/magic_enum.cppm"), {
            public = true,
            defines = "MAGIC_ENUM_USE_STD_MODULE"
        })
        add_files("src/util/csv.ixx", {public = true})
        add_files("src/util/fixed_vector.ixx", {public = true})
        add_files("src/util/unicode.ixx", {public = true})
        add_files("src/gui/core/misc/inout_animator.ixx", {public = true})
        add_files("src/gui/core/misc/gui.slider_logic.ixx", {public = true})
        add_files("src/i18n/text_tree.ixx", {public = true})
        add_files("src/i18n/text_tree.react_flow.ixx", {public = true})
        add_files("src/i18n/text_tree.toml.ixx", {public = true})
        add_files("src/i18n/text_tree.toml.cpp")
        add_files("src.tests/**.cpp")
    target_end()
end

if is_host_project then
    local function run_xrgui_doctor()
        import("lib.detect.find_tool")

        local ok = true

        local function status(name, passed, detail)
            if passed then
                cprint("${color.success}[ok]${clear} %s%s", name, detail and (" - " .. detail) or "")
            else
                cprint("${color.error}[missing]${clear} %s%s", name, detail and (" - " .. detail) or "")
                ok = false
            end
        end

        local function info(name, detail)
            cprint("${bright}[info]${clear} %s%s", name, detail and (" - " .. detail) or "")
        end

        local shader_files = os.files(path.join(current_dir, "properties/assets/shader/spv/*.spv"))
        local icon_files = os.files(path.join(current_dir, "properties/assets_raw/gen/icons/**.svg"))

        status("VULKAN_SDK", os.getenv("VULKAN_SDK") ~= nil, os.getenv("VULKAN_SDK") or "install Vulkan SDK 1.4+")

        local cl = find_tool("cl")
        status("MSVC cl", cl ~= nil, cl and cl.program or "run from a Developer PowerShell or install latest MSVC")

        local slangc = find_tool("slangc")
        if #shader_files > 0 and slangc == nil then
            info("slangc", "not on PATH; existing generated shaders are available")
        else
            status("slangc", slangc ~= nil, slangc and slangc.program or "add slangc to PATH or pass xmake xrgui.gen_slang --compiler=...")
        end

        local py = find_tool("py") or find_tool("python")
        if #shader_files > 0 and #icon_files > 0 and py == nil then
            info("Python launcher", "not on PATH; existing generated shaders and icons are available")
        else
            status("Python launcher", py ~= nil, py and py.program or "install Python 3.11+ when regenerating shader/icon assets")
        end

        local node = find_tool("node")
        if #icon_files > 0 and node == nil then
            info("Node.js", "not on PATH; existing generated icons are available")
        else
            status("Node.js", node ~= nil, node and node.program or "install Node.js for icon generation")
        end

        local has_vma = os.isfile(path.join(current_dir, "external/mo_yanxi_vulkan_wrapper/external/VulkanMemoryAllocator/include/vk_mem_alloc.h"))
        status("VulkanMemoryAllocator submodule", has_vma, has_vma and "available" or "run git submodule update --init --recursive")
        local has_vk_wrapper = os.isfile(path.join(current_dir, "external/mo_yanxi_vulkan_wrapper/xmake.lua"))
        status("mo_yanxi_vulkan_wrapper submodule", has_vk_wrapper, has_vk_wrapper and "available" or "run git submodule update --init --recursive")

        status("generated shaders", #shader_files > 0, #shader_files > 0 and (#shader_files .. " spv files") or "run xmake xrgui.gen_slang")

        status("generated icons", #icon_files > 0, #icon_files > 0 and (#icon_files .. " svg files") or "run xmake xrgui.gen_icon")

        if not ok then
            raise("XRGUI doctor found missing requirements")
        end
    end

    task("xrgui.doctor")
        on_run(run_xrgui_doctor)

        set_menu({
            usage = "check toolchain and generated assets for xrgui.quickstart"
        })
    task_end()

    task("xrgui.quickstart")
        on_run(function ()
            import("core.base.option")
            import("core.base.task")

            cprint("${bright}[xrgui.quickstart]${clear} configure MSVC debug build")
            task.run("config", {toolchain = "msvc", mode = "debug"})

            local shader_files = os.files(path.join(current_dir, "properties/assets/shader/spv/*.spv"))
            if #shader_files == 0 then
                cprint("${bright}[xrgui.quickstart]${clear} generate shaders")
                task.run("xrgui.gen_slang")
            end

            local icon_files = os.files(path.join(current_dir, "properties/assets_raw/gen/icons/**.svg"))
            if #icon_files == 0 then
                cprint("${bright}[xrgui.quickstart]${clear} generate icons")
                task.run("xrgui.gen_icon")
            end

            cprint("${bright}[xrgui.quickstart]${clear} run doctor")
            run_xrgui_doctor()
            cprint("${bright}[xrgui.quickstart]${clear} build xrgui.hello")
            task.run("build", {target = "xrgui.hello"})
            if option.get("no-run") then
                return
            end
            cprint("${bright}[xrgui.quickstart]${clear} run xrgui.hello")
            os.execv("xmake", {"run", "xrgui.hello"})
        end)

        set_menu({
            usage = "configure, prepare assets, build, and run xrgui.hello",
            options = {
                {nil, "no-run", "k", nil, "Only prepare and build; do not launch the GUI"}
            }
        })
    task_end()

    task("xrgui.gen_slang")
        on_run(function ()
            import("core.base.option")

            local builder = path.join(current_dir, "properties/build_util/slang_builder.py")
            local user_config = option.get("config")
            local user_output = option.get("output")

            local config_path
            if user_config == "" then
                config_path = path.join(current_dir, "properties/assets_raw/shader/config.toml")
            else
                config_path = path.join(os.curdir(), user_config)
            end

            local output_dir
            if user_output == "" then
                output_dir = path.join(current_dir, "properties/assets/shader/spv")
            else
                output_dir = path.join(os.curdir(), user_output)
            end

            local compiler = option.get("compiler")
            if not compiler or compiler == "" then
                compiler = option.get("complier")
            end

            local args = {
                builder,
                compiler,
                output_dir,
                config_path,
                "-j",
                "30"
            }
            local pass = option.get("pass")
            if pass and pass ~= "" then
                table.join2(args, os.argv(pass))
            end

            os.execv("py", args)
        end)

        set_menu({
            usage = "compile Slang shaders to SPIR-V",
            options = {
                {'c', "compiler", "kv", "", "Path to slangc.exe"},
                {nil, "complier", "kv", "./slang/bin/slangc.exe", "Deprecated alias for --compiler"},
                {'o', "output", "kv", "", "SPIR-V output dir relative to project root"},
                {'f', "config", "kv", "", "Shader build config"},
                {'p', "pass", "kv", "", "Pass through args to the Slang builder"},
            }
        })
    task_end()

    task("xrgui.gen_icon")
        on_run(function ()
            local builder = path.join(current_dir, "properties/assets_raw/svg_normalize.py")
            local input_dir = path.join(current_dir, "properties/assets_raw/icons")
            local output_dir = path.join(current_dir, "properties/assets_raw/gen/icons")

            os.execv("py", {builder, "-i", input_dir, "-o", output_dir})
        end)

        set_menu({
            usage = "generate path normalized icons for msdfgen"
        })
    task_end()

    task("xrgui.switch_mode")
        set_menu({
            usage = "xmake xrgui.switch_mode [mode]",
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
                cprint("${red}错误: 请指定目标模式！例如: xmake xrgui.switch_mode debug${clear}")
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
    task_end()
end

