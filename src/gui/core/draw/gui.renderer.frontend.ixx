module;

#include <cassert>
#include <vulkan/vulkan.h>

export module mo_yanxi.gui.renderer.frontend;

export import mo_yanxi.gui.fx.config;


export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.user_data_entry;

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

struct scissor{
	math::vec2 src{};
	math::vec2 dst{};

	//TODO margin is never used
	float margin{};

	std::uint32_t cap[3];
	constexpr friend bool operator==(const scissor& lhs, const scissor& rhs) noexcept = default;
};

struct scissor_raw{
	math::frect rect{};
	float margin{};

	[[nodiscard]] scissor_raw intersection_with(const scissor_raw& other) const noexcept{
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

	constexpr friend bool operator==(const scissor_raw& lhs, const scissor_raw& rhs) noexcept = default;

	constexpr explicit(false) operator scissor() const noexcept{
		return scissor{rect.get_src(), rect.get_end(), margin};
	}
};

struct layer_viewport{
	struct transform_pair{
		math::mat3 current;
		math::mat3 accumul;
	};

	math::frect viewport{};
	std::vector<scissor_raw> scissors{};

	math::mat3 transform_to_root_screen{};
	std::vector<transform_pair> element_local_transform{};

	[[nodiscard]] layer_viewport() = default;

	[[nodiscard]] layer_viewport(
		const math::frect& viewport,
		const scissor_raw& scissors,
		std::nullptr_t allocator_, //TODO
		const math::mat3& transform_to_root_screen)
		:
		viewport(viewport),
		scissors({scissors}/*, alloc*/),
		transform_to_root_screen(transform_to_root_screen),
		element_local_transform({{math::mat3_idt, math::mat3_idt}}/*, alloc*/){
	}

	[[nodiscard]] scissor_raw top_scissor() const noexcept{
		assert(!scissors.empty());
		return scissors.back();
	}

	void pop_scissor() noexcept{
		assert(scissors.size() > 1);
		scissors.pop_back();
	}

	void push_scissor(const scissor_raw& scissor){
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
		}else{
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
	scissor scissor;

};

#pragma endregion

export
using gui_reserved_user_data_tuple = std::tuple<ubo_screen_info, ubo_layer_info>;

export
struct renderer_frontend{
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

	[[nodiscard]] math::frect get_region() const noexcept{
		return region_;
	}

	auto& top_viewport(this auto& self) noexcept{
		return self.viewports_.back();
	}

	byte_pool<mr::aligned_heap_allocator<std::byte, 32>>& get_mem_pool() noexcept{
		return mem_pool_;
	}

	template <typename Instr>
		requires std::is_trivially_copyable_v<Instr>
	void push(const Instr& instr){
		using namespace graphic::draw;

		if constexpr (instruction::known_instruction<Instr>){
			batch_backend_interface_.push(instruction::make_instruction_head(instr), reinterpret_cast<const std::byte*>(&instr));
		}else{
			static constexpr type_identity_index tidx = unstable_type_identity_of<Instr>();
			static constexpr bool vtx_only = fx::is_vertex_stage_only<Instr>;

			std::uint32_t idx;
			if constexpr (vtx_only){
				const auto* ientry = table_vertex_only_[tidx];
				idx = ientry - table_vertex_only_.begin();

				if(idx >= table_vertex_only_.size()){
					throw std::out_of_range("index out of range");
				}
			}else{
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

	template <graphic::draw::instruction::known_instruction Instr, typename ...Args>
	void push(const Instr& instr, const Args& ...args) {
		static_assert (sizeof...(args) > 0);
		using namespace graphic::draw;
		const auto instr_size = instruction::get_payload_size<Instr, Args...>(args...);

		alignas(16) alignas(Instr) alignas(Args...) std::byte buffer_[sizeof(Instr) + (sizeof(Args) + ... + 32)];
		std::byte* pbuffer;
		if(instr_size > sizeof(buffer_)) [[unlikely]] {
			if(instr_size > cache_instr_buffer_inner_usage_.size())cache_instr_buffer_inner_usage_.resize(instr_size);
			pbuffer = cache_instr_buffer_inner_usage_.data();
		}else{
			pbuffer = buffer_;
		}

		const auto head = instruction::place_instruction_at(pbuffer, instr, args...);
		batch_backend_interface_.push(head, pbuffer);
	}

	template <typename Instr>
	void update_state(
		const graphic::draw::instruction::state_push_config& config,
		binary_diff_trace::tag tag,
		const Instr& instr, unsigned offset = 0){
		using namespace graphic::draw;
		static_assert(!instruction::known_instruction<Instr>);

		batch_backend_interface_.update_state(config,
			tag,
			std::span{
				reinterpret_cast<const std::byte*>(std::addressof(instr)),
				sizeof(Instr)
			}, offset);
	}

	template <fx::draw_state_config_deduceable Instr>
	void update_state(const graphic::draw::instruction::state_push_config& config, const Instr& instr){
		this->update_state(config, graphic::draw::instruction::state_tag{fx::draw_state_index_deduce_v<Instr>}, instr);
	}

	template <fx::draw_state_config_deduceable Instr>
	void update_state(const Instr& instr){
		this->update_state(graphic::draw::instruction::state_push_config{}, {fx::draw_state_index_deduce_v<Instr>}, instr);
	}

	void undo_state(graphic::draw::instruction::state_tag tag){
		this->update_state(graphic::draw::instruction::state_push_config{.type = graphic::draw::instruction::state_push_type::undo}, tag, std::span<const std::byte>{});
	}

	bool push(const std::span<const graphic::draw::instruction::instruction_head> heads, const std::byte* payload){
		auto cur = payload;
		for (const auto & head : heads){
			batch_backend_interface_.push(head, cur);
			cur += head.payload_size;
		}

		return true;
	}


	void resize(const math::frect region){
		if(region_ == region)return;
		region_ = region;
	}

	void init_projection(){
		viewports_.clear();
		viewports_.push_back(layer_viewport{region_, {{region_}}, nullptr, math::mat3_idt});
		uniform_proj_ = math::mat3{}.set_orthogonal(region_.get_src(), region_.extent());

		push(ubo_screen_info{uniform_proj_});
		notify_viewport_changed();
	}

	void notify_viewport_changed() {
		const auto& vp = top_viewport();
		push(ubo_layer_info{vp.get_element_to_root_screen(), vp.top_scissor()});
	}

	void push_viewport(const math::frect viewport, scissor_raw scissor_raw){
		assert(!viewports_.empty());

		const auto& last = viewports_.back();

		const auto trs = math::mat3{}.set_rect_transform(viewport.src, viewport.extent(), last.viewport.src, last.viewport.extent());
		const auto scissor_intersection = scissor_raw.intersection_with(last.top_scissor()).intersection_with({viewport});

		viewports_.push_back(layer_viewport{viewport, {scissor_raw}, nullptr, last.get_element_to_root_screen() * trs});
	}

	void push_viewport(const math::frect viewport){
		push_viewport(viewport, {viewport});
	}

	void pop_viewport() noexcept{
		assert(viewports_.size() > 1);
		viewports_.pop_back();
	}

	void push_scissor(scissor_raw scissor_in_layer_space){
		auto& vp = top_viewport();

		scissor_in_layer_space.uniform(vp.get_element_to_root_screen());

		top_viewport().push_scissor(scissor_in_layer_space);
	}

	void pop_scissor() noexcept {
		top_viewport().pop_scissor();
	}

	template <typename T = std::byte>
	T* acquire_buffer(std::size_t size, bool clear = false){
		if(clear) [[unlikely]] cache_instr_buffer_external_usage_.clear();
		cache_instr_buffer_external_usage_.resize(size * sizeof(T));
		return reinterpret_cast<T*>(cache_instr_buffer_external_usage_.data());
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
			static_cast<D*>(this)->pop();
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
	[[nodiscard]] viewport_guard(renderer_frontend& renderer, const math::frect viewport, const scissor_raw& scissor_raw) :
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
	[[nodiscard]] scissor_guard(renderer_frontend& renderer, const scissor_raw& scissor_raw) :
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
struct mode_guard : guard_base<mode_guard>{
private:
	friend guard_base;

	void pop() const{
		renderer_->update_state(fx::draw_config{fx::draw_mode::COUNT_or_fallback});
	}

public:
	[[nodiscard]] mode_guard(renderer_frontend& renderer, const fx::draw_config& param) :
	guard_base(renderer){
		renderer.update_state(param);
	}

};

}

#pragma endregion