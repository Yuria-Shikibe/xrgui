module;

#include <shaderc/shaderc.hpp>

export module mo_yanxi.graphic.shaderc;
import std;
import mo_yanxi.io;

namespace mo_yanxi::graphic {
    struct Includer final : shaderc::CompileOptions::IncluderInterface {

        struct result_t : shaderc_include_result {
            std::string filepath;
            std::string code;
        };

        shaderc_include_result *GetInclude(
            const char *requested_source,
            shaderc_include_type type,
            const char *requesting_source,
            std::size_t include_depth
        ) override {
            std::filesystem::path file{requesting_source};

            result_t& result = *new result_t;

            file = file.parent_path() / requested_source;
            result.filepath = file.string();
            result.code = io::read_string(file.c_str()).value();

            static_cast<shaderc_include_result &>(result) = {
                result.filepath.c_str(),
                result.filepath.length(),
                result.code.data(),
                result.code.size(),
                this
            };

            return &result;
        }

        void ReleaseInclude(shaderc_include_result *data) override {
            delete static_cast<result_t *>(data); // NOLINT(*-pro-type-static-cast-downcast)
        }
    };

    export
    class shader_runtime_compiler {
        static constexpr std::array valid_types{
            std::string_view{"vertex"},
            std::string_view{"fragment"},
            std::string_view{"compute"},
        };


        //--------------------
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

    public:
        shader_runtime_compiler(shaderc_source_language language = shaderc_source_language_glsl) {
            options.SetSourceLanguage(language);
            options.SetWarningsAsErrors();
            options.SetGenerateDebugInfo();
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
            options.SetTargetSpirv(shaderc_spirv_version_1_6);
            options.SetOptimizationLevel(shaderc_optimization_level_performance);
            // options.SetPreserveBindings(true);
            // options.SetWarningsAsErrors();
            if(language == shaderc_source_language_glsl)options.SetForcedVersionProfile(460, shaderc_profile_core);

            options.SetIncluder(std::make_unique<Includer>());
        }

        void add_debug_info(){
            options.SetGenerateDebugInfo();
        }

        void add_marco(const std::string& name, const std::string& val){
            options.AddMacroDefinition(name, val);
        }

        auto compile(const std::span<const char> code, const char *filepath, const char *entry = "main") const {
            const shaderc::SpvCompilationResult result =
                compiler.CompileGlslToSpv(
                    code.data(), code.size(),
                    shaderc_glsl_infer_from_source,
                    filepath, entry, options);

            auto err = result.GetErrorMessage();
            if(!err.empty())std::println(std::cerr, "{}", result.GetErrorMessage());

            std::vector<std::uint32_t> bin{result.begin(), result.end()};

            return bin;
        }

        auto compile(const std::string_view path, const char* entry = "main") const{
            const auto pStr = path.data();
            const auto code = io::read_string(pStr).value();
            return compile(code, pStr, entry);
        }
    };

    export
    struct shader_wrapper {
    private:
        const shader_runtime_compiler* compiler;
        std::filesystem::path outputDirectory{};

    public:
        [[nodiscard]] shader_wrapper(const shader_runtime_compiler& compiler, std::filesystem::path&& outputDirectory) :
            compiler{&compiler},
            outputDirectory{std::move(outputDirectory)} {
            if(!std::filesystem::is_directory(this->outputDirectory)) {
                throw std::invalid_argument("Invalid output directory");
            }
        }

        [[nodiscard]] shader_wrapper(
            const shader_runtime_compiler& compiler,
            const std::filesystem::path& outputDirectory) :
            shader_wrapper{compiler, std::filesystem::path{outputDirectory}}{
        }

        void compile(const std::filesystem::path& file) const {
            const auto rst = compiler->compile(file.string());
            const auto target = outputDirectory / file.filename().string().append(".spv");
            if(rst.empty()){
                std::println("[ShaderC] Skip Shader {} Compile", file.filename().string());
            }else{
                io::write_bytes(target, rst);
            }
        }
    };
}
