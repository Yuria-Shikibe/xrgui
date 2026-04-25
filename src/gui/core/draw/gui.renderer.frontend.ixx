module;

#include <cassert>
#include <vulkan/vulkan.h>

export module mo_yanxi.gui.renderer.frontend;

export import :color_stack;

export import mo_yanxi.gui.fx.config;


export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.user_data_entry;
import mo_yanxi.binary_trace;

import mo_yanxi.gui.alloc;
import mo_yanxi.type_register;

import mo_yanxi.vk.util.uniform;
import mo_yanxi.byte_pool;


import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.math.matrix3;
import mo_yanxi.math;

import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::gui{
#pragma region VBO_config


struct scissor_gpu_{
	math::vec2 src{};
	math::vec2 dst{};

	//TODO margin is never used
	float margin{};

	std::uint32_t cap[3];
	constexpr friend bool operator==(const scissor_gpu_& lhs, const scissor_gpu_& rhs) noexcept = default;
};

struct scissor_raw_{
	math::frect rect{};
	float margin{};

	[[nodiscard]] scissor_raw_ intersection_with(const scissor_raw_& other) const noexcept{
		return {rect.intersection_with(other.rect), margin};
	}

	void uniform(const math::mat3& mat) noexcept{
		if(rect.area() < 0.05f){
			rect = {};
			return;
		}

		auto src = rect.get_src();
		auto dst = rect.get_end();
		src *= mat;
		dst *= mat;

		rect = {tags::from_vertex, src, dst};
	}

	constexpr friend bool operator==(const scissor_raw_& lhs, const scissor_raw_& rhs) noexcept = default;

	constexpr explicit(false) operator scissor_gpu_() const noexcept{
		return scissor_gpu_{rect.get_src(), rect.get_end(), margin};
	}
};

struct layer_viewport{
	struct transform_pair{
		math::mat3 current;
		math::mat3 accumul;
	};

	math::frect viewport{};
	std::vector<scissor_raw_> scissors{};

	math::mat3 transform_to_root_screen{};
	std::vector<transform_pair> element_local_transform{};

	[[nodiscard]] layer_viewport() = default;

	[[nodiscard]] layer_viewport(
		const math::frect& viewport,
		const scissor_raw_& scissors,
		std::nullptr_t allocator_, //TODO
		const math::mat3& transform_to_root_screen)
		:
		viewport(viewport),
		scissors({scissors}/*, alloc*/),
		transform_to_root_screen(transform_to_root_screen),
		element_local_transform({{math::mat3_idt, math::mat3_idt}}/*, alloc*/){
	}

	[[nodiscard]] scissor_raw_ top_scissor() const noexcept{
		assert(!scissors.empty());
		return scissors.back();
	}

	void pop_scissor() noexcept{
		assert(scissors.size() > 1);
		scissors.pop_back();
	}

	void push_scissor(const scissor_raw_& scissor){
		scissors.push_back(scissor.intersection_with(top_scissor()));
	}

	[[nodiscard]] math::mat3 get_element_to_root_screen() const noexcept{
		assert(!element_local_transform.empty());
		return transform_to_root_screen * element_local_transform.back().accumul;
	}

	void push_local_transform(const math::mat3& mat){
		assert(!element_local_transform.empty());
		auto& last = element_local_transform.back();
		element_local_transform.push_back({mat, last.accumul * mat});
	}
	void push_local_transform(const math::vec2 offset){
		assert(!element_local_transform.empty());
		auto& last = element_local_transform.back();
		auto lm = last.accumul;
		lm.c3.x += offset.x;
		lm.c3.y += offset.y;
		element_local_transform.push_back({auto{math::mat3_idt}.set_translation(offset), lm});
	}

	/**
	 * @brief used to open a new transform layer
	 */
	void push_local_transform(){
		assert(!element_local_transform.empty());
		auto& last = element_local_transform.back();
		element_local_transform.push_back({math::mat3_idt, last.accumul});
	}

	void pop_local_transform() noexcept{
		assert(element_local_transform.size() > 1);
		element_local_transform.pop_back();
	}

	void set_local_transform(const math::mat3& mat) noexcept{
		assert(!element_local_transform.empty() && "cannot set empty transform");
		auto& last = element_local_transform.back();
		last.current = mat;
		if(element_local_transform.size() > 1){
			last.accumul = element_local_transform[element_local_transform.size() - 2].accumul * mat;
		} else{
			last.accumul = mat;
		}
	}

	void set_local_transform_idt() noexcept{
		assert(!element_local_transform.empty() && "cannot set empty transform");
		auto& last = element_local_transform.back();
		last.current = math::mat3_idt;
		if(element_local_transform.size() > 1){
			last.accumul = element_local_transform[element_local_transform.size() - 2].accumul;
		} else{
			last.accumul = math::mat3_idt;
		}
	}

	math::mat3 get_local_transform() const noexcept{
		assert(!element_local_transform.empty() && "cannot set empty transform");
		auto& last = element_local_transform.back();
		return last.current;
	}
};

struct alignas(16) ubo_screen_info{
	using tag_vertex_only = void;
	vk::padded_mat3 screen_to_uniform;
};

struct alignas(16) ubo_layer_info{
	using tag_vertex_only = void;
	vk::padded_mat3 element_to_screen;
	scissor_gpu_ scissor;
};

#pragma endregion

export
using gui_reserved_user_data_tuple = std::tuple<ubo_screen_info, ubo_layer_info, accumulated_state>;

export
struct state_guard;

export
struct renderer_frontend{
	friend state_guard;

private:
	using user_table_type = graphic::draw::data_layout_table<mr::vector<graphic::draw::data_layout_type_aware_entry>>;

	//TODO change it to a pointer-index look up table
	user_table_type table_vertex_only_{};
	user_table_type table_general_{};

	graphic::draw::instruction::batch_backend_interface batch_backend_interface_{};
	math::frect region_{};

	color_stack color_stack_{};

	//screen space to uniform space viewport
	mr::vector<layer_viewport> viewports_{};
	math::mat3 uniform_proj_{};

	std::vector<std::byte, mr::aligned_heap_allocator<std::byte, 16>> cache_instr_buffer_inner_usage_{};

	binary_config_trace state_trace_{};

public:
	[[nodiscard]] renderer_frontend() = default;

	template <typename A1, typename A2>
	[[nodiscard]] explicit renderer_frontend(
		const graphic::draw::data_layout_table<A1>& user_data_table_vertex_only,
		const graphic::draw::data_layout_table<A2>& user_data_table_general,
		const graphic::draw::instruction::batch_backend_interface& batch_backend_interface)
		: table_vertex_only_(user_data_table_vertex_only, user_table_type::allocator_type{})
		, table_general_(user_data_table_general, user_table_type::allocator_type{})
		, batch_backend_interface_(batch_backend_interface){
	}

#pragma region Getter
	[[nodiscard]] math::frect get_region() const noexcept{
		return region_;
	}

	layer_viewport& top_viewport() noexcept{
		return viewports_.back();
	}

	const layer_viewport& top_viewport() const noexcept{
		return viewports_.back();
	}





	fx::scissor get_full_screen_scissor() const noexcept{
		return {region_.src.round<int>(), region_.extent().round<unsigned>()};
	}

	fx::viewport get_full_screen_viewport() const noexcept{
		return {region_.src, region_.extent()};
	}
#pragma endregion

#pragma region Instruction_Push

	template <typename Instr>
		requires std::is_trivially_copyable_v<Instr>
	void push(const Instr& instr){
		using namespace graphic::draw;

		if constexpr (graphic::draw::instruction::known_meta_instruction<Instr, renderer_frontend>){
			std::invoke(instr, *this);
		}else if constexpr(instruction::known_instruction<Instr>){
			batch_backend_interface_.push(instruction::make_instruction_head(instr),
				reinterpret_cast<const std::byte*>(&instr));
		} else{
			static constexpr type_identity_index tidx = unstable_type_identity_of<Instr>();
			static constexpr bool vtx_only = fx::is_vertex_stage_only<Instr>;

			std::uint32_t idx;
			if constexpr(vtx_only){
				const auto* ientry = table_vertex_only_[tidx];
				idx = static_cast<std::uint32_t>(ientry - table_vertex_only_.begin());

				if(idx >= table_vertex_only_.size()){
					throw std::out_of_range("index out of range");
				}
			} else{
				const auto* ientry = table_general_[tidx];
				idx = static_cast<std::uint32_t>(ientry - table_general_.begin());

				if(idx >= table_general_.size()){
					throw std::out_of_range("index out of range");
				}
			}

			const auto head = instruction::instruction_head{
					.type = instruction::instr_type::uniform_update,
					.payload_size = static_cast<std::uint32_t>(instruction::get_payload_size<Instr>()),
					.payload = {.ubo = instruction::user_data_indices{idx, !vtx_only}}
				};
			batch_backend_interface_.push(head, reinterpret_cast<const std::byte*>(&instr));
		}
	}

	template <graphic::draw::instruction::known_instruction Instr, typename... Args>
	void push(const Instr& instr, const Args&... args){
		static_assert(sizeof...(args) > 0);
		using namespace graphic::draw;
		const auto instr_size = instruction::get_payload_size<Instr, Args...>(args...);

		alignas(16) alignas(Instr) alignas(Args...) std::byte buffer_[sizeof(Instr) + (math::align_up(sizeof(Args), 16) + ... + 32)];
		std::byte* pbuffer;
		if constexpr ((std::is_trivially_copyable_v<Args> && ...) && (... && !std::ranges::input_range<Args>)){
			assert(instr_size <= sizeof(buffer_));
			pbuffer = buffer_;
		}else{
			if(instr_size > sizeof(buffer_)) [[unlikely]] {
				if(instr_size > cache_instr_buffer_inner_usage_.size()) cache_instr_buffer_inner_usage_.resize(instr_size);
				pbuffer = cache_instr_buffer_inner_usage_.data();
			} else{
				pbuffer = buffer_;
			}
		}


		const auto head = instruction::place_instruction_at(pbuffer, instr, args...);
		batch_backend_interface_.push(head, pbuffer);
	}


private:
	void update_state_(
		const fx::state_push_config& config,
		const std::span<const std::byte> payload,
		binary_diff_trace::tag tag,
		unsigned offset){
		using namespace graphic::draw;

		batch_backend_interface_.update_state(config, tag, payload, offset);
	}

public:
	void update_state(
		const fx::state_push_config& config,
		const std::span<const std::byte> data_span,
		binary_diff_trace::tag tag,
		unsigned offset = 0){
		using namespace graphic::draw;

		if(config.to_clear.any()){
			state_trace_.clear_mask(config.to_clear);
		}

		if(config.tracks_persistent_state()){
			state_trace_.push(tag, data_span, offset);
		}

		this->update_state_(config, data_span, tag, offset);
	}

	template <fx::directly_emitable_state T>
	void update_state(
		const T& state,
		unsigned offset = 0){
		using namespace graphic::draw;

		const fx::state_push_config config = state;
		const fx::binary_diff_tag tag = state;

		[[maybe_unused]] static constexpr auto crop = [] static {
			if constexpr (std::invocable<T>){
				return std::type_identity<std::invoke_result_t<T>>{};
			}else{
				return std::type_identity<T>{};
			}
		};

		using passed_type = decltype(crop())::type;

		if constexpr (std::is_empty_v<passed_type> || std::is_void_v<passed_type>){
			if(config.to_clear.any()){
				state_trace_.clear_mask(config.to_clear);
			}

			if(config.tracks_persistent_state()){
				state_trace_.push(tag, std::span<const std::byte>{}, offset);
			}

			this->update_state_(config, std::span<const std::byte>{}, tag, offset);
		}else{
				auto submit = [&]<typename Ty>(const Ty& s){
					if(config.to_clear.any()){
						state_trace_.clear_mask(config.to_clear);
					}

					if(config.tracks_persistent_state()){
						state_trace_.push(tag, std::span{reinterpret_cast<const std::byte*>(std::addressof(s)), sizeof(T)}, offset);
					}

				this->update_state_(config, std::span{reinterpret_cast<const std::byte*>(std::addressof(s)), sizeof(T)}, tag, offset);
			};

			if constexpr (std::invocable<T>){
				submit(state());
			}else{
				submit(state);
			}
		}
		
	}

	template <typename Instr>
	void update_state(
		const fx::state_push_config& config,
		const Instr& instr,
		binary_diff_trace::tag tag,
		unsigned offset = 0){
		using namespace graphic::draw;
		static_assert(!instruction::known_instruction<Instr>);

		this->update_state(config, std::span{
				reinterpret_cast<const std::byte*>(std::addressof(instr)),
				sizeof(Instr)
			}, tag, offset);
	}

	void set_blend_equation(const fx::blend_equation& equation, unsigned attachment_index){
		this->update_state(fx::state_push_config{},
			equation,
			fx::make_state_tag(fx::state_type::set_color_blend_equation, attachment_index));
	}

	void set_blend_equation(const fx::blend_equation& equation, fx::render_target_mask mask){
		mask.for_each_popbit([&](unsigned idx){
			set_blend_equation(equation, idx);
		});
	}

	template <fx::state_type_deducable T>
	void update_state(const T& instr){
		this->update_state(fx::state_type_deduce<T>::make_push_config(), instr, fx::make_state_tag(fx::state_type_deduce<T>::type));
	}

	template <fx::state_type_deducable T, typename MinorTag>
		requires(fx::state_type_deduce<T>::requires_minor_tag)
	void update_state(const T& instr, MinorTag minor_tag, unsigned offset = 0){
		this->update_state(fx::state_type_deduce<T>::make_push_config(), instr, fx::make_state_tag(fx::state_type_deduce<T>::type, minor_tag), offset);
	}

	void update_state(fx::batch_draw_mode mode){
		this->update_state({}, mode, fx::make_state_tag(fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT));
	}

	void push(const std::span<const graphic::draw::instruction::instruction_head> heads, const std::byte* payload){
		batch_backend_interface_.push(heads, payload);
	}

	template <graphic::draw::instruction::known_instruction T>
	friend renderer_frontend& operator<<(renderer_frontend& renderer, const T& instr){
		renderer.push(instr);
		return renderer;
	}

	template <graphic::draw::instruction::known_meta_instruction<renderer_frontend> T>
	friend renderer_frontend& operator<<(renderer_frontend& renderer, const T& instr){
		renderer.push(instr);
		return renderer;
	}


#pragma endregion

	void resize(const math::frect region){
		if(region_ == region) return;
		region_ = region;
	}

	void init_timeline_variable(){
		viewports_.clear();
		viewports_.push_back(layer_viewport{region_, {{region_}}, nullptr, math::mat3_idt});
		uniform_proj_ = math::mat3{}.set_orthogonal(region_.get_src(), region_.extent());

		push(ubo_screen_info{uniform_proj_});
		push(color_stack_.top());
		notify_viewport_changed();
	}

	void notify_viewport_changed(){
		const auto& vp = top_viewport();
		push(ubo_layer_info{vp.get_element_to_root_screen(), vp.top_scissor()});
		update_state(fx::scissor{
			.pos = vp.top_scissor().rect.vert_00().round<std::int32_t>(),
			.size = vp.top_scissor().rect.extent().round<std::uint32_t>()
		});
	}

	void push_viewport(const math::frect viewport, scissor_raw_ scissor_raw){
		assert(!viewports_.empty());

		const auto& last = viewports_.back();

		const auto trs = math::mat3{}.set_rect_transform(viewport.src, viewport.extent(), last.viewport.src,
			last.viewport.extent());
		const auto scissor_intersection = scissor_raw.intersection_with(last.top_scissor()).intersection_with(
			{viewport});

		viewports_.push_back(layer_viewport{viewport, {scissor_raw}, nullptr, last.get_element_to_root_screen() * trs});
	}

	[[nodiscard]] color_stack& get_color_stack() noexcept{
		return color_stack_;
	}

	void push_viewport(const math::frect viewport){
		push_viewport(viewport, {viewport});
	}

	void pop_viewport() noexcept{
		assert(viewports_.size() > 1);
		viewports_.pop_back();
	}

	void push_scissor(scissor_raw_ scissor_in_layer_space){
		auto& vp = top_viewport();

		scissor_in_layer_space.uniform(vp.get_element_to_root_screen());

		top_viewport().push_scissor(scissor_in_layer_space);
	}

	void pop_scissor() noexcept{
		top_viewport().pop_scissor();
	}

	[[nodiscard]] math::vec2 map_local_to_viewport_space(this const renderer_frontend& self, math::vec2 local_pt) noexcept {
		assert(!self.viewports_.empty());
		return self.top_viewport().element_local_transform.back().accumul * local_pt;
	}


	[[nodiscard]] math::vec2 map_viewport_to_screen_space(this const renderer_frontend& self, math::vec2 vp_pt) noexcept {
		assert(!self.viewports_.empty());
		return self.top_viewport().transform_to_root_screen * vp_pt;
	}


	[[nodiscard]] math::vec2 map_local_to_screen_space(this const renderer_frontend& self, math::vec2 local_pt) noexcept {
		assert(!self.viewports_.empty());
		return self.top_viewport().get_element_to_root_screen() * local_pt;
	}





	[[nodiscard]] fx::viewport get_absolute_viewport(
		const fx::viewport& local_vp,
		math::vec2(*trans)(const renderer_frontend&, math::vec2) = &renderer_frontend::map_local_to_screen_space) const noexcept {
		const math::vec2 screen_src = std::invoke(trans, *this, local_vp.src);
		const math::vec2 screen_dst = std::invoke(trans, *this, local_vp.src + local_vp.extent);

		return fx::viewport{
			screen_src,
			screen_dst - screen_src
		};
	}

	[[nodiscard]] fx::scissor get_absolute_scissor(
		const fx::scissor& local_sc,
		math::vec2(*trans)(const renderer_frontend&, math::vec2) = &renderer_frontend::map_local_to_screen_space) const noexcept {
		const math::vec2 float_src = local_sc.pos.as<float>();
		const math::vec2 float_dst = local_sc.pos.as<float>() + local_sc.size.as<float>();

		const math::vec2 screen_src = std::invoke(trans, *this, float_src);
		const math::vec2 screen_dst = std::invoke(trans, *this, float_dst);

		const math::i32point2 final_pos = screen_src.round<int>();
		const math::u32size2 final_size = (screen_dst - screen_src).round<unsigned>();

		return fx::scissor{ final_pos, final_size };
	}
};
}

#pragma region Guards

namespace mo_yanxi::gui{
template <typename D>
struct guard_base{
private:
	friend D;
	renderer_frontend* renderer_{};

public:
	[[nodiscard]] guard_base() = default;

	[[nodiscard]] explicit guard_base(renderer_frontend& renderer)
		: renderer_(std::addressof(renderer)){
	}

	guard_base(const guard_base& other) = delete;

	guard_base(guard_base&& other) noexcept
		: renderer_{std::exchange(other.renderer_, {})}{
	}

	guard_base& operator=(const guard_base& other) = delete;

	guard_base& operator=(guard_base&& other) noexcept{
		if(this == &other) return *this;
		if(renderer_){
			static_cast<D*>(this)->pop();
		}
		renderer_ = std::exchange(other.renderer_, {});
		return *this;
	}

	~guard_base(){
		if(renderer_){
			try{
				static_cast<D*>(this)->pop();
			} catch(...){
			}
		}
	}
};

export
struct viewport_guard : guard_base<viewport_guard>{
private:
	friend guard_base;

	void pop() const{
		renderer_->pop_viewport();
		renderer_->notify_viewport_changed();
	}

public:
	[[nodiscard]] viewport_guard(renderer_frontend& renderer, const math::frect viewport,
		const scissor_raw_& scissor_raw) :
		guard_base(renderer){
		renderer.push_viewport(viewport, scissor_raw);
		renderer.notify_viewport_changed();
	}

	[[nodiscard]] viewport_guard(renderer_frontend& renderer, const math::frect viewport) :
		guard_base(renderer){
		renderer.push_viewport(viewport);
		renderer.notify_viewport_changed();
	}
};

export
struct scissor_guard : guard_base<scissor_guard>{
private:
	friend guard_base;

	void pop() const{
		renderer_->pop_scissor();
		renderer_->notify_viewport_changed();
	}

public:
	[[nodiscard]] scissor_guard(renderer_frontend& renderer, const scissor_raw_& scissor_raw) :
		guard_base(renderer){
		renderer.push_scissor(scissor_raw);
		renderer.notify_viewport_changed();
	}
};

export
struct color_guard : guard_base<color_guard>{
private:
	friend guard_base;

	void pop() const{
		renderer_->get_color_stack().pop();
		renderer_->push(renderer_->get_color_stack().top());
	}

public:
	[[nodiscard]] color_guard(renderer_frontend& renderer, const color_modifier& m) :
		guard_base(renderer){
		renderer.get_color_stack().push(m);
		renderer.push(renderer.get_color_stack().top());
	}

	[[nodiscard]] color_guard(renderer_frontend& renderer, const graphic::color& blend_c, float influence) :
		guard_base(renderer){
		renderer.get_color_stack().push_color(blend_c, influence);
		renderer.push(renderer.get_color_stack().top());
	}

	[[nodiscard]] color_guard(renderer_frontend& renderer, const graphic::color& mul_c) :
		guard_base(renderer){
		renderer.get_color_stack().push_multiply_color(mul_c);
		renderer.push(renderer.get_color_stack().top());
	}

	[[nodiscard]] color_guard(renderer_frontend& renderer, const float luma_intensity, float influence) :
		guard_base(renderer){
		renderer.get_color_stack().push_intensity(luma_intensity, influence);
		renderer.push(renderer.get_color_stack().top());
	}
};

export
struct transform_guard : guard_base<transform_guard>{
private:
	friend guard_base;

	void pop() const{
		renderer_->top_viewport().pop_local_transform();
		renderer_->notify_viewport_changed();
	}

public:
	[[nodiscard]] transform_guard(renderer_frontend& renderer, const math::mat3& transform) :
		guard_base(renderer){
		renderer.top_viewport().push_local_transform(transform);
		renderer.notify_viewport_changed();
	}
};


export
struct state_guard : guard_base<state_guard>{
private:
	friend guard_base;
	binary_config_trace::tag tag_;
	binary_config_trace::record_fix fix_;

	void pop() const{
		if(fix_.data){
			renderer_->update_state_(fx::state_push_config{
					.commit_mode = fx::state_commit_mode::accumulate,
					.boundary_mode = fx::state_boundary_mode::defer
				}, fix_.to_span(renderer_->state_trace_), tag_, fix_.logical_offset);
			renderer_->state_trace_.store_tag(tag_, fix_);
		}
	}

public:
	[[nodiscard]] state_guard() = default;

	[[nodiscard]] state_guard(renderer_frontend& renderer,
		const std::span<const std::byte> data, const graphic::draw::instruction::state_tag tag, unsigned offset = 0) :
		guard_base(renderer), tag_(tag), fix_(renderer.state_trace_.load_tag(tag)){
		renderer.update_state(fx::state_push_config{
				.commit_mode = fx::state_commit_mode::accumulate,
				.boundary_mode = fx::state_boundary_mode::defer
			}, data, tag, offset);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	[[nodiscard]] state_guard(renderer_frontend& renderer,
		const T& value, const graphic::draw::instruction::state_tag tag, unsigned offset = 0) :
		state_guard(renderer, std::span{
				reinterpret_cast<const std::byte*>(std::addressof(value)),
				sizeof(T)
			}, tag, offset){
	}

	template <fx::state_type_deducable T, typename MinorTag>
		requires(fx::state_type_deduce<T>::requires_minor_tag)
	[[nodiscard]] state_guard(renderer_frontend& renderer, const T& value, MinorTag minor_tag, unsigned offset = 0) :
		state_guard(renderer, value, fx::make_state_tag(fx::state_type_deduce<T>::type, minor_tag), offset){
	}

	template <fx::state_type_deducable T>
	[[nodiscard]] state_guard(renderer_frontend& renderer, const T& value, unsigned offset = 0) :
		state_guard(renderer, value, fx::make_state_tag(fx::state_type_deduce<T>::type), offset){
	}

	[[nodiscard]] state_guard(renderer_frontend& renderer, fx::batch_draw_mode mode) : state_guard(renderer, mode, fx::make_state_tag(fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT)){
	}


};


}

#pragma endregion
