module;

#include <cassert>
#include <vulkan/vulkan.h>

export module mo_yanxi.gui.renderer.frontend;

export import mo_yanxi.gui.fx.config;


export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.user_data_entry;
import binary_trace;

import mo_yanxi.gui.alloc;
import mo_yanxi.type_register;

import mo_yanxi.vk.util.uniform;
import mo_yanxi.byte_pool;


import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.math.matrix3;

import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::gui{
#pragma region VBO_config


export
template <typename L, typename R = unsigned>
	requires (sizeof(L) == sizeof(graphic::draw::instruction::state_tag::major) && sizeof(R) == sizeof(
		graphic::draw::instruction::state_tag::minor))
constexpr graphic::draw::instruction::state_tag make_state_tag(L major, R minor = {}) noexcept{
	return graphic::draw::instruction::make_state_tag(major, minor);
}

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
using gui_reserved_user_data_tuple = std::tuple<ubo_screen_info, ubo_layer_info>;
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


	//screen space to uniform space viewport
	mr::vector<layer_viewport> viewports_{};
	math::mat3 uniform_proj_{};

	std::vector<std::byte, mr::aligned_heap_allocator<std::byte, 16>> cache_instr_buffer_inner_usage_{};
	std::vector<std::byte, mr::aligned_heap_allocator<std::byte, 16>> cache_instr_buffer_external_usage_{};

	byte_pool<mr::aligned_heap_allocator<std::byte, 32>> mem_pool_{};

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

	auto& top_viewport(this auto& self) noexcept{
		return self.viewports_.back();
	}

	byte_pool<mr::aligned_heap_allocator<std::byte, 32>>& get_mem_pool() noexcept{
		return mem_pool_;
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

		if constexpr(instruction::known_instruction<Instr>){
			batch_backend_interface_.push(instruction::make_instruction_head(instr),
				reinterpret_cast<const std::byte*>(&instr));
		} else{
			static constexpr type_identity_index tidx = unstable_type_identity_of<Instr>();
			static constexpr bool vtx_only = fx::is_vertex_stage_only<Instr>;

			std::uint32_t idx;
			if constexpr(vtx_only){
				const auto* ientry = table_vertex_only_[tidx];
				idx = ientry - table_vertex_only_.begin();

				if(idx >= table_vertex_only_.size()){
					throw std::out_of_range("index out of range");
				}
			} else{
				const auto* ientry = table_general_[tidx];
				idx = ientry - table_general_.begin();

				if(idx >= table_general_.size()){
					throw std::out_of_range("index out of range");
				}
			}

			const auto head = instruction::instruction_head{
					.type = instruction::instr_type::uniform_update,
					.payload_size = instruction::get_payload_size<Instr>(),
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

		alignas(16) alignas(Instr) alignas(Args...) std::byte buffer_[sizeof(Instr) + (sizeof(Args) + ... + 32)];
		std::byte* pbuffer;
		if(instr_size > sizeof(buffer_)) [[unlikely]] {
			if(instr_size > cache_instr_buffer_inner_usage_.size()) cache_instr_buffer_inner_usage_.resize(instr_size);
			pbuffer = cache_instr_buffer_inner_usage_.data();
		} else{
			pbuffer = buffer_;
		}

		const auto head = instruction::place_instruction_at(pbuffer, instr, args...);
		batch_backend_interface_.push(head, pbuffer);
	}

private:
	void update_state_(
		const graphic::draw::instruction::state_push_config& config,
		const std::span<const std::byte> payload,
		binary_diff_trace::tag tag,
		unsigned offset){
		using namespace graphic::draw;

		batch_backend_interface_.update_state(config, tag, payload, offset);
	}

public:
	void update_state(
		const graphic::draw::instruction::state_push_config& config,
		const std::span<const std::byte> data_span,
		binary_diff_trace::tag tag,
		unsigned offset = 0){
		using namespace graphic::draw;

		if(config.type == instruction::state_push_type::idempotent){
			state_trace_.push(tag, data_span, offset);
		}

		this->update_state_(config, data_span, tag, offset);
	}

	template <typename Instr>
	void update_state(
		const graphic::draw::instruction::state_push_config& config,
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
		this->update_state(graphic::draw::instruction::state_push_config{},
			equation,
			graphic::draw::instruction::make_state_tag(fx::state_type::set_color_blend_equation, attachment_index));
	}

	void set_blend_equation(const fx::blend_equation& equation, fx::render_target_mask mask){
		mask.for_each_popbit([&](unsigned idx){
			set_blend_equation(equation, idx);
		});
	}

	template <fx::state_type_deducable T>
	void update_state(const T& instr){
		this->update_state(graphic::draw::instruction::state_push_config{
			.type = fx::state_type_deduce<T>::is_idempotent ? graphic::draw::instruction::state_push_type::idempotent : graphic::draw::instruction::state_push_type::non_idempotent
		}, instr, gui::make_state_tag(fx::state_type_deduce<T>::type));
	}

	template <fx::state_type_deducable T, typename MinorTag>
		requires(fx::state_type_deduce<T>::requires_minor_tag)
	void update_state(const T& instr, MinorTag minor_tag, unsigned offset = 0){
		this->update_state(graphic::draw::instruction::state_push_config{
			.type = fx::state_type_deduce<T>::is_idempotent ? graphic::draw::instruction::state_push_type::idempotent : graphic::draw::instruction::state_push_type::non_idempotent
		}, instr, gui::make_state_tag(fx::state_type_deduce<T>::type, minor_tag), offset);
	}

	bool push(const std::span<const graphic::draw::instruction::instruction_head> heads, const std::byte* payload){
		auto cur = payload;
		for(const auto& head : heads){
			batch_backend_interface_.push(head, cur);
			cur += head.payload_size;
		}

		return true;
	}

#pragma endregion

	void resize(const math::frect region){
		if(region_ == region) return;
		region_ = region;
	}

	void init_projection(){
		viewports_.clear();
		viewports_.push_back(layer_viewport{region_, {{region_}}, nullptr, math::mat3_idt});
		uniform_proj_ = math::mat3{}.set_orthogonal(region_.get_src(), region_.extent());

		push(ubo_screen_info{uniform_proj_});
		notify_viewport_changed();
	}

	void notify_viewport_changed(){
		const auto& vp = top_viewport();
		push(ubo_layer_info{vp.get_element_to_root_screen(), vp.top_scissor()});
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

	// 2. 从 局部视口坐标系 到 屏幕像素坐标系
	[[nodiscard]] math::vec2 map_viewport_to_screen_space(this const renderer_frontend& self, math::vec2 vp_pt) noexcept {
		assert(!self.viewports_.empty());
		return self.top_viewport().transform_to_root_screen * vp_pt;
	}

	// 3. (综合快捷方法) 从 局部坐标系 直接到 屏幕像素坐标系
	[[nodiscard]] math::vec2 map_local_to_screen_space(this const renderer_frontend& self, math::vec2 local_pt) noexcept {
		assert(!self.viewports_.empty());
		return self.top_viewport().get_element_to_root_screen() * local_pt;
	}

	// ==========================================
	// 核心功能：获取最终提交给 Vulkan 的绝对状态
	// ==========================================

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
	renderer_frontend* renderer_;

public:
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
			renderer_->update_state_(graphic::draw::instruction::state_push_config{
					.type = graphic::draw::instruction::state_push_type::idempotent
				}, fix_.to_span(renderer_->state_trace_), tag_, fix_.logical_offset);
			renderer_->state_trace_.store_tag(tag_, fix_);
		}
	}

public:
	[[nodiscard]] state_guard(renderer_frontend& renderer,
		const std::span<const std::byte> data, const graphic::draw::instruction::state_tag tag, unsigned offset = 0) :
		guard_base(renderer), tag_(tag), fix_(renderer.state_trace_.load_tag(tag)){
		renderer.update_state(graphic::draw::instruction::state_push_config{
				.type = graphic::draw::instruction::state_push_type::idempotent
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
		state_guard(renderer, value, gui::make_state_tag(fx::state_type_deduce<T>::type, minor_tag), offset){
	}

	template <fx::state_type_deducable T>
	[[nodiscard]] state_guard(renderer_frontend& renderer, const T& value, unsigned offset = 0) :
		state_guard(renderer, value, gui::make_state_tag(fx::state_type_deduce<T>::type), offset){
	}
};


}

#pragma endregion
