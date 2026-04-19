module;

#include <cassert>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.backend.vulkan.pipeline_manager;

export import mo_yanxi.backend.vulkan.pipeline_info;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.gui.renderer.frontend;

import std;
import mo_yanxi.utility;
import mo_yanxi.vk.record_context;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.shader_reflect;
import mo_yanxi.vk.util.uniform;
import mo_yanxi.vk.util;
import mo_yanxi.vk;
import mo_yanxi.gui.alloc;


namespace mo_yanxi::backend::vulkan{
using namespace gui;

using user_data_table = graphic::draw::data_layout_table<>;

struct stage_binding_spec : vk::binding_spec {
	VkShaderStageFlags stage_flags;
	VkDescriptorBindingFlags binding_flags;
	bool static_length;
};

export
struct descriptor_create_config{
	user_data_table user_data_table{};
	std::vector<stage_binding_spec> specs{};
};

template <std::ranges::input_range Rng>
	requires (std::convertible_to<std::ranges::range_value_t<Rng>, stage_binding_spec>)
[[nodiscard]] vk::descriptor_layout create_layout(VkDevice device, const Rng& specs){
	return vk::descriptor_layout(device, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
		[&](vk::descriptor_layout_builder& builder){
			for(const stage_binding_spec& spec : specs){
				builder.push(spec.binding, spec.type, spec.stage_flags, spec.static_length ? spec.count : 1,
					spec.binding_flags);
			}
		});
}

export
struct descriptor_slots{
private:
	std::vector<vk::binding_spec> binding_specs_{};
	vk::descriptor_layout user_descriptor_layout_{};
	vk::dynamic_descriptor_buffer user_descriptor_buffer_{};

public:
	[[nodiscard]] descriptor_slots() = default;

	template <std::ranges::forward_range Rng = std::initializer_list<stage_binding_spec>>
	[[nodiscard]] descriptor_slots(
		vk::allocator_usage allocator,
		const Rng& specs)
		:
		binding_specs_{std::from_range, specs},
		user_descriptor_layout_(vulkan::create_layout(allocator.get_device(), specs)), user_descriptor_buffer_(allocator, user_descriptor_layout_, user_descriptor_layout_.binding_count(), binding_specs_){

	}

	[[nodiscard]] descriptor_slots(
		vk::allocator_usage allocator,
		const descriptor_create_config& specs)
		: descriptor_slots{allocator, specs.specs}{

	}

	const vk::dynamic_descriptor_buffer& dbo() const noexcept{
		return user_descriptor_buffer_;
	}

	// void bind(std::invocable<std::uint32_t, const vk::descriptor_mapper&> auto group_binder){
	// 	vk::descriptor_mapper m{user_descriptor_buffer_};
	//
	// 	for(std::uint32_t i = 0; i < user_descriptor_buffer_.get_chunk_count(); ++i){
	// 		std::invoke(group_binder, i, m);
	// 	}
	// }

	vk::dynamic_descriptor_mapper get_mapper() noexcept{
		return vk::dynamic_descriptor_mapper{user_descriptor_buffer_};
	}

	[[nodiscard]] VkDescriptorSetLayout descriptor_set_layout() const noexcept{
		return user_descriptor_layout_;
	}
};

// -------------------------------------------------------------------------
// 管线相关配置 (从原代码移植)
// -------------------------------------------------------------------------

export
struct descriptor_use_entry{
	std::uint32_t source;
	std::uint32_t target;
};

struct create_param{
	VkDevice device;
	std::span<const VkDescriptorSetLayout> descriptor_set_layouts{};
};

export
std::vector<VkPushConstantRange> make_push_constants(VkShaderStageFlagBits stage, std::initializer_list<std::uint32_t> size){
	std::vector<VkPushConstantRange> rst;
	rst.reserve(size.size());
	std::uint32_t cur_offset = 0;
	for (auto s : size){
		auto aligned = vk::align_up<std::uint32_t>(s, 4);
		rst.emplace_back(stage, cur_offset, aligned);
		cur_offset += aligned;
	}
	return rst;
}

constexpr std::uint32_t get_used_cap(std::span<const VkPushConstantRange> push_constants) noexcept {
	std::uint32_t rst{};
	for (auto && push_constant : push_constants){
		rst = std::max(vk::align_up(push_constant.offset + push_constant.size, 4U), rst);
	}
	return rst;
}

struct general_config{
	std::vector<VkPushConstantRange> push_constants{};

	std::uint32_t get_used_cap() const noexcept {
		return vulkan::get_used_cap(push_constants);
	}
};

#pragma region Graphic
export
struct graphic_pipeline_data;

export
struct dynamic_blending_config{
private:

	unsigned blending_state_count{};
	std::vector<VkPipelineColorBlendAttachmentState> blend_states{};

public:
	[[nodiscard]] dynamic_blending_config() = default;

	[[nodiscard]] dynamic_blending_config(
		unsigned attachments_count, unsigned blending_state_count,
		const VkPipelineColorBlendAttachmentState& def = {})
		: blending_state_count(attachments_count), blend_states(blending_state_count * attachments_count, def){
	}

	[[nodiscard]] unsigned get_blending_state_count() const noexcept{
		return blending_state_count;
	}

	[[nodiscard]] unsigned get_attachment_count() const noexcept{
		return blend_states.size() / blending_state_count;
	}

	template <typename S>
	auto& operator[](this S& self, const unsigned attachment_idx, const unsigned blending_state_idx) noexcept{
		assert(blending_state_idx < self.blending_state_count);
		return self.blend_states[attachment_idx * self.blending_state_count + blending_state_idx];
	}

	template <typename S>
	[[nodiscard]] auto get_state_of(this S& self, const unsigned attachment_idx) noexcept{
		return std::span{self.blend_states.data() + attachment_idx * blending_state_count, blending_state_count};
	}

	template <typename S>
	[[nodiscard]] auto get_state_range(this S& self) noexcept {
		return self.blend_states | std::views::chunk(self.blending_state_count);
	}
};

export
enum struct blend_dynamic_flags{
	none,
	enable = 1 << 0,
	equation = 1 << 1,
	write_flag = 1 << 2,
};

BITMASK_OPS(export, blend_dynamic_flags);

export
struct option_blending_state{

	std::vector<VkPipelineColorBlendAttachmentState> default_blending_settings{};
	blend_dynamic_flags flags;

	bool is_dynamic_enable() const noexcept{
		return (flags & blend_dynamic_flags::enable) != blend_dynamic_flags::none;
	}

	bool is_dynamic_equation() const noexcept{
		return (flags & blend_dynamic_flags::equation) != blend_dynamic_flags::none;
	}

	bool is_dynamic_write_flag() const noexcept{
		return (flags & blend_dynamic_flags::write_flag) != blend_dynamic_flags::none;
	}

	void apply_to_template(vk::graphic_pipeline_template& gtp) const{
		if(default_blending_settings.size() != gtp.attachment_formats.size()){
			throw std::invalid_argument("Invalid settings for default_blending_settings, blending setting count mismatch");
		}

		gtp.set_vertex_info(vertex_def_desc);

		gtp.attachment_blend_states = default_blending_settings;

		if(is_dynamic_enable()){
			gtp.dynamic_states.push_back(VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT);
		}

		if(is_dynamic_equation()){
			gtp.dynamic_states.push_back(VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT);
		}

		if(is_dynamic_write_flag()){
			gtp.dynamic_states.push_back(VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT);
		}

	}
};

export
struct graphic_pipeline_option{
	bool enables_multisample{};
	mask_usage mask_usage_type{};
	fx::render_target_mask default_target_attachments{};
	fx::render_target_mask input_attachments_mask{};
	option_blending_state blend_state{};

	std::vector<descriptor_use_entry> used_descriptor_sets{};


	bool is_partial_target() const noexcept{
		return default_target_attachments.any() && !default_target_attachments.all();
	}
};

struct graphic_pipeline_data{
	vk::pipeline_layout pipeline_layout{};
	vk::pipeline pipeline{};

	graphic_pipeline_option option{};
	std::uint32_t push_constant_request_size{};

	[[nodiscard]] graphic_pipeline_data() = default;
};

export
struct graphic_pipeline_create_config{

	struct config {

        // 替换原本的 vector<VkPipelineShaderStageCreateInfo>
        std::vector<vk::shader_stage_bundle> shader_bundles;

        graphic_pipeline_option option{};
        std::function<void(vk::pipeline&, VkPipelineLayout, const create_param&, const draw_attachment_create_info&)> creator{};


        void create(
            graphic_pipeline_data& data, const create_param& param,
            const draw_attachment_create_info& attachments) const{

            // 1. 自动执行反射，推导 Push Constants
        	graphic::push_constant_merge_context psh_ctx{};
        	for (const auto& bundle : shader_bundles){
        		auto stage_flags = bundle.create_info.stage;
        		psh_ctx.append(bundle.bytecode, stage_flags);
        	}

            data.push_constant_request_size = get_used_cap(psh_ctx.get_result());
            data.pipeline_layout = {param.device, 0, param.descriptor_set_layouts, psh_ctx.get_result()};
            data.option = option;

            if (creator) {
                creator(data.pipeline, data.pipeline_layout, param, attachments);
            } else {
                vk::graphic_pipeline_template gtp{};

                gtp.set_shaders(shader_bundles | std::views::transform(&vk::shader_stage_bundle::create_info) | std::ranges::to<std::vector>());

            	if(attachments.enables_multisample() && option.enables_multisample){
            		gtp.set_multisample(attachments.multisample, 1, option.enables_multisample);
            	}

            	option.default_target_attachments.for_each_popbit([&](unsigned i){
					gtp.push_color_attachment_format(attachments.attachments[i].attachment.format);
				});

            	if(option.mask_usage_type == mask_usage::write){
            		gtp.push_color_attachment_format(VK_FORMAT_R8_UNORM);
            	}

            	option.blend_state.apply_to_template(gtp);

            	data.pipeline = vk::pipeline{
            		data.pipeline_layout.get_device(), data.pipeline_layout,
					VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
					gtp
				};
            }
        }
    };

	std::vector<config> configurator{};
	std::vector<descriptor_create_config> descriptor_create_info{};

	[[nodiscard]] graphic_pipeline_create_config() = default;

	[[nodiscard]] graphic_pipeline_create_config(
		std::vector<config> configurator,
		std::vector<descriptor_create_config> descriptor_create_info = {})
		: configurator(std::move(configurator)),
		descriptor_create_info(std::move(descriptor_create_info)){
	}

	std::size_t size() const noexcept{
		return configurator.size();
	}

	void create(std::size_t idx, graphic_pipeline_data& data, const create_param& arg, const draw_attachment_create_info& info) const{
		configurator[idx].create(data, arg, info);
	}

	const config& operator[](std::size_t index) const noexcept{
		return configurator[index];
	}

	config& operator[](std::size_t index) noexcept{
		return configurator[index];
	}

	auto generate_push_constants(){
		return configurator | std::views::transform([psh_ctx = graphic::push_constant_merge_context{}](const config& c) mutable {
			   psh_ctx.clear();
			   for (const auto& bundle : c.shader_bundles) {
				   psh_ctx.append(bundle.bytecode, bundle.create_info.stage);
			   }

			   auto result_ranges = std::move(psh_ctx).get_result();
			   std::ranges::sort(result_ranges, byte_less{});
			   return result_ranges;
		   });
	}
};

#pragma endregion

#pragma region Compute

export
struct compute_pipeline_blit_inout_config{
	struct entry{
		std::uint32_t binding;
		std::uint32_t resource_index;
		VkDescriptorType type;
	};

private:
	std::size_t input_count_{};
	std::vector<entry> entries_{};

public:
	[[nodiscard]] compute_pipeline_blit_inout_config() = default;

	template <
		std::ranges::input_range DrawAttachments = std::initializer_list<entry>,
		std::ranges::input_range BlitAttachments = std::initializer_list<entry>>
	[[nodiscard]] compute_pipeline_blit_inout_config(const DrawAttachments& in, const BlitAttachments& outRng) :
		entries_([&]{
			std::vector<entry> rng{std::from_range, in};
			input_count_ = rng.size();
			rng.append_range(outRng);
			return rng;
		}()){}

	auto begin() const noexcept{
		return entries_.begin();
	}

	auto end() const noexcept{
		return entries_.end();
	}

	[[nodiscard]] std::size_t get_input_count() const noexcept{
		return input_count_;
	}

	[[nodiscard]] std::size_t get_output_count() const noexcept{
		return entries_.size() - input_count_;
	}

	[[nodiscard]] std::span<const entry> get_input_entries() const noexcept{
		return {entries_.data(), input_count_};
	}

	[[nodiscard]] std::span<const entry> get_output_entries() const noexcept{
		return {entries_.begin() + input_count_, entries_.end()};
	}

	[[nodiscard]] bool is_compatible_with(
		this const compute_pipeline_blit_inout_config& lhs,
		const compute_pipeline_blit_inout_config& rhs) noexcept{
		if(lhs.get_input_count() != rhs.get_input_count() || lhs.get_output_count() != rhs.get_output_count()){
			return false;
		}

		static constexpr auto is_entry_match = [](const entry& a, const entry& b) static{
			return a.binding == b.binding && a.type == b.type;
		};

		const bool inputs_compatible = std::ranges::is_permutation(
			lhs.get_input_entries(),
			rhs.get_input_entries(),
			is_entry_match
		);

		if(!inputs_compatible) return false;

		const bool outputs_compatible = std::ranges::is_permutation(
			lhs.get_output_entries(),
			rhs.get_output_entries(),
			is_entry_match
		);

		return outputs_compatible;
	}

	vk::descriptor_layout_builder make_layout_builder() const{
		vk::descriptor_layout_builder b;
		for (const auto & value : entries_){
			b.push(value.binding, value.type, VK_SHADER_STAGE_COMPUTE_BIT);
		}
		return b;
	}
};

export
struct compute_pipeline_option{
	compute_pipeline_blit_inout_config inout{};
	std::vector<descriptor_use_entry> used_descriptor_sets{};
};

export
struct compute_pipeline_data{
	vk::pipeline_layout pipeline_layout{};
	vk::pipeline pipeline{};

	compute_pipeline_option option{};

	[[nodiscard]] compute_pipeline_data() = default;
};

export
struct compute_pipeline_create_config{

	struct config{
		vk::shader_stage_bundle shader_bundle{};

		compute_pipeline_option option{};

		void create(compute_pipeline_data& data, const create_param& param) const{
			graphic::push_constant_merge_context psh_ctx{};
			psh_ctx.append(shader_bundle.bytecode, shader_bundle.create_info.stage);

			auto push_constants = psh_ctx.get_result();

			data.pipeline_layout = {param.device, 0, param.descriptor_set_layouts, push_constants};
			data.pipeline = vk::pipeline{
				param.device,
				data.pipeline_layout,
				VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
				shader_bundle.create_info
			};
			data.option = option;
		}
	};


	std::vector<config> configurator{};
	std::vector<compute_pipeline_blit_inout_config> inout_predefines{};
	std::vector<descriptor_create_config> descriptor_create_info{};

	[[nodiscard]] compute_pipeline_create_config() = default;

	[[nodiscard]] explicit(false) compute_pipeline_create_config(std::vector<config> configurator,
		std::vector<compute_pipeline_blit_inout_config> inout_predefines = {},
		std::vector<descriptor_create_config> descriptor_create_info = {})
		: configurator(std::move(configurator)),
		inout_predefines(std::move(inout_predefines)),
		descriptor_create_info(std::move(descriptor_create_info)){
	}

	std::size_t size() const noexcept{
		return configurator.size();
	}

	void create(std::size_t idx, compute_pipeline_data& data, const create_param& arg) const{
		configurator[idx].create(data, arg);
	}

	const config& operator[](std::size_t index) const noexcept{
		return configurator[index];
	}
};

#pragma endregion

export
class uniform_buffer_manager{
private:
	user_data_table merged_user_data_table_{};
	std::vector<std::byte> ubo_data_cache_{};
	vk::uniform_buffer uniform_buffer_{};

public:
	[[nodiscard]] uniform_buffer_manager() = default;

	//TODO loose the allocator constrain
	template <std::ranges::input_range Rng>
		requires (std::same_as<std::ranges::range_const_reference_t<Rng>, const user_data_table&>)
	[[nodiscard]] uniform_buffer_manager(const vk::allocator_usage& allocator, const Rng& rng) : merged_user_data_table_([&]{
		user_data_table table{};
		for(const auto& t : rng){
			table.append(t);
		}
		return table;
	}()), ubo_data_cache_(merged_user_data_table_.required_capacity()), uniform_buffer_(allocator, merged_user_data_table_.required_capacity()){}

};

// -------------------------------------------------------------------------
// 管线管理器 (新封装)
// -------------------------------------------------------------------------

template <typename PipeTy>
struct pipeline_manager_base{
protected:
	uniform_buffer_manager ubo_manager{};
	mr::vector<descriptor_slots> custom_descriptors{};

	mr::vector<PipeTy> pipelines{};

	[[nodiscard]] pipeline_manager_base() = default;

	[[nodiscard]] pipeline_manager_base(
		const vk::allocator_usage& allocator,
		const std::vector<descriptor_create_config>& descriptor_create_configs) :
		ubo_manager{
			allocator, descriptor_create_configs | std::views::transform(&descriptor_create_config::user_data_table)
		}{

		custom_descriptors.reserve(descriptor_create_configs.size());
		for(const auto& descriptor_config : descriptor_create_configs){
			custom_descriptors.push_back(descriptor_slots(
				allocator, descriptor_config
			));
		}
	}



public:
	template <typename S>
	[[nodiscard]] auto get_pipelines(this S& self) noexcept{
		return std::span{self.pipelines};
	}

	template <typename S>
	[[nodiscard]] auto& get_custom_descriptor(this S& self, std::size_t index){
		assert(index < self.custom_descriptors.size());
		return self.custom_descriptors[index];
	}

	[[nodiscard]] std::size_t get_custom_descriptor_count() const noexcept{
		return custom_descriptors.size();
	}
};
export
class graphic_pipeline_manager : public pipeline_manager_base<graphic_pipeline_data>{
private:
	struct input_attachment_descriptor_mock{
		vk::descriptor_layout layout{};
		vk::descriptor_buffer buffer{};
	};
	// 增加此容器，确保自动生成的 Dummy Layout 在管理器销毁前保持存活
	std::vector<input_attachment_descriptor_mock> input_attachment_descriptor_settings_{};
	vk::descriptor_layout mask_descriptor_set_layout_{};

	// 新增：Push Constant 兼容性表 (N * N 平铺的一维数组)
	std::vector<bool> push_constant_compatibility_table_{};

public:
	[[nodiscard]] graphic_pipeline_manager() = default;

	template <std::ranges::input_range T = std::initializer_list<std::span<const VkDescriptorSetLayout>>>
		requires (
			std::ranges::input_range<std::ranges::range_reference_t<T>> &&
			std::convertible_to<std::ranges::range_reference_t<std::ranges::range_reference_t<T>>, VkDescriptorSetLayout>
			)
	graphic_pipeline_manager(
		const vk::allocator_usage& allocator,
		graphic_pipeline_create_config&& create_group,
		const T& auxiliary_layouts,
		const draw_attachment_create_info& draw_attachment_config
		) : pipeline_manager_base{allocator, create_group.descriptor_create_info}, mask_descriptor_set_layout_(
			    allocator.get_device(),
			    VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			    [&](vk::descriptor_layout_builder& builder){
				    builder.push_seq(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
			    }
		    ){

		std::size_t n = create_group.size();
		pipelines.resize(n);

		{//push constant compatibility check

			// 1. 预先提取所有管线的 Push Constants 用于对比 (结合了之前的排序优化)


			// 2. 初始化兼容性表 (仅下三角，大小：N * (N - 1) / 2)
			if (n > 1) {
				auto all_push_constants = create_group.generate_push_constants() | std::ranges::to<std::vector>();

				push_constant_compatibility_table_.resize(n * (n - 1) / 2);
				// 从第1行开始，因为第0行没有下三角元素
				for (std::size_t i = 1; i < n; ++i) {
					// 列索引 j 永远小于 行索引 i
					for (std::size_t j = 0; j < i; ++j) {
						bool is_compatible = std::ranges::equal(
							all_push_constants[i], all_push_constants[j], byte_equal{}
						);
						// 使用等差数列求和公式映射一维索引
						push_constant_compatibility_table_[i * (i - 1) / 2 + j] = is_compatible;
					}
				}
			}

		}

		mr::vector<VkDescriptorSetLayout> layouts_buffer;

		for(const auto& [idx, pipe, layouts] : std::views::zip(std::views::iota(0uz), pipelines, auxiliary_layouts)){
			layouts_buffer.clear();
			layouts_buffer.append_range(layouts);

			auto& group = create_group[static_cast<std::size_t>(idx)];
			const auto& option = group.option;

			for(const auto& [src, dst] : option.used_descriptor_sets){
				layouts_buffer.push_back(custom_descriptors.at(src).descriptor_set_layout());
			}

			if(std::uint32_t input_attachments_count = option.input_attachments_mask.popcount()){
				input_attachment_descriptor_mock mock{vk::descriptor_layout{
					allocator.get_device(),
					VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
					[&](vk::descriptor_layout_builder& builder){
						for(std::uint32_t i = 0; i < input_attachments_count; ++i){
							builder.push_seq(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
						}
					}
				}};
				mock.buffer = vk::descriptor_buffer{
					allocator, mock.layout, mock.layout.binding_count()
				};
				layouts_buffer.push_back(mock.layout);
				input_attachment_descriptor_settings_.push_back(std::move(mock));
			}else{
				input_attachment_descriptor_settings_.emplace_back();
			}

			if(option.mask_usage_type != mask_usage::ignore){
				layouts_buffer.push_back(mask_descriptor_set_layout_);
			}

			create_group.create(idx, pipe, create_param{
				.device = allocator.get_device(),
				.descriptor_set_layouts = layouts_buffer
			}, draw_attachment_config);
		}
	}

	graphic_pipeline_manager(
			const vk::allocator_usage& allocator,
			graphic_pipeline_create_config&& create_group,
			std::span<const VkDescriptorSetLayout> auxiliary_layouts,
			const draw_attachment_create_info& draw_attachment_config
		) : graphic_pipeline_manager(allocator, std::move(create_group), std::views::repeat(auxiliary_layouts, create_group.size()), draw_attachment_config){}

	[[nodiscard]] const vk::descriptor_layout& get_mask_descriptor_set_layout() const noexcept{
		return mask_descriptor_set_layout_;
	}

	[[nodiscard]] std::span<input_attachment_descriptor_mock> get_input_attachment_mock_descriptor() {
		return input_attachment_descriptor_settings_;
	}

	[[nodiscard]] bool is_push_constant_compatible(std::uint32_t pipeline_a, std::uint32_t pipeline_b) const noexcept {
		if (pipeline_a == pipeline_b) return true;

		auto [col, row] = std::minmax(pipeline_a, pipeline_b);

		return push_constant_compatibility_table_[row * (row - 1) / 2 + col];
	}
};

export
class compute_pipeline_manager : public pipeline_manager_base<compute_pipeline_data>{
private:
	std::vector<vk::descriptor_layout> blit_inout_layouts_{};
	std::vector<compute_pipeline_blit_inout_config> inout_predefines_{};


public:
	[[nodiscard]] compute_pipeline_manager() = default;

	template <std::ranges::input_range T = std::initializer_list<const std::span<const VkDescriptorSetLayout>>>
		requires (
			std::ranges::input_range<std::ranges::range_reference_t<T>> &&
			std::convertible_to<std::ranges::range_reference_t<std::ranges::range_reference_t<T>>, VkDescriptorSetLayout>
		)
	compute_pipeline_manager(
		const vk::allocator_usage& allocator,
		const compute_pipeline_create_config& create_group,
		const T& auxiliary_layouts
	) : pipeline_manager_base{allocator, create_group.descriptor_create_info}, inout_predefines_(create_group.inout_predefines){

		blit_inout_layouts_.reserve(create_group.size());
		for (const auto & configurator : create_group.configurator){
			const auto& cfg = configurator.option.inout;
			blit_inout_layouts_.push_back(vk::descriptor_layout{allocator.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, cfg.make_layout_builder()});
		}

		// 2. 初始化管线
		pipelines.resize(create_group.size());
		mr::vector<VkDescriptorSetLayout> layouts_buffer;

		for(const auto& [idx, pipe, layouts] : std::views::zip(std::views::iota(0uz), pipelines, auxiliary_layouts)){
			// 2.2 记录该管线使用的自定义 Descriptor Sets


			// 2.3 绑定自定义 Layouts (根据 source 索引从 custom_descriptors_ 获取)
			layouts_buffer.clear();
			layouts_buffer.append_range(layouts);
			layouts_buffer.push_back(blit_inout_layouts_[idx]);

			//TODO sort
			for(const auto& [src, dst] : create_group[static_cast<std::size_t>(idx)].option.used_descriptor_sets){
				layouts_buffer.push_back(custom_descriptors.at(src).descriptor_set_layout());
			}

			create_group.create(idx, pipe, create_param{
				.device = allocator.get_device(),
				.descriptor_set_layouts = layouts_buffer
			});
		}

	}
	template <std::ranges::input_range T = std::initializer_list<const std::span<const VkDescriptorSetLayout>>>
		requires (
			std::ranges::input_range<std::ranges::range_reference_t<T>> &&
			std::convertible_to<std::ranges::range_reference_t<std::ranges::range_reference_t<T>>, VkDescriptorSetLayout>
		)
	compute_pipeline_manager(
		const vk::allocator_usage& allocator,
		const compute_pipeline_create_config& create_group
	) : compute_pipeline_manager{allocator, create_group, std::views::repeat(std::span<const VkDescriptorSetLayout>{})}{}

	[[nodiscard]] std::span<const vk::descriptor_layout> get_inout_layouts() const noexcept{
		return blit_inout_layouts_;
	}

	[[nodiscard]] std::span<const compute_pipeline_blit_inout_config> get_inout_defines() const noexcept{
		return inout_predefines_;
	}

	[[nodiscard]] bool is_inout_compatible(std::uint32_t pipeline_index, std::uint32_t inout_define_index) const noexcept{
		return get_pipelines()[pipeline_index].option.inout.is_compatible_with(inout_predefines_[inout_define_index]);
	}

	void append_descriptor_buffers(graphic::draw::record_context<>& ctx, std::size_t index) const {
		auto& pipe = pipelines[index];
		for (const auto & used_descriptor_set : pipe.option.used_descriptor_sets){
			ctx.push(used_descriptor_set.target, custom_descriptors[used_descriptor_set.source].dbo());
		}
	}
};
}
