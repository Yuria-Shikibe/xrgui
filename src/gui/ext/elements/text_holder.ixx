//
// Created by Matrix on 2026/2/1.
//

export module mo_yanxi.gui.elem.text_holder;

import std;
import mo_yanxi.concurrent.atomic_shared_mutex;
import mo_yanxi.graphic.draw.instruction.recorder;
import mo_yanxi.graphic.color;
import mo_yanxi.gui.infrastructure;

namespace mo_yanxi::gui{

export
struct text_layout_result{
	math::vec2 required_extent;
	bool updated;
};

export
template <typename LayoutType>
struct exclusive_glyph_layout{
	using layout_type = LayoutType;

private:
	const layout_type* layout;
	ccur::shared_lock lock_{};

public:
	[[nodiscard]] exclusive_glyph_layout() = default;

	[[nodiscard]] explicit(false) exclusive_glyph_layout(const layout_type* layout)
	: layout(layout){
	}

	[[nodiscard]] exclusive_glyph_layout(const layout_type* layout, ccur::shared_lock&& lock)
	: layout(layout), lock_(std::move(lock)){}

	explicit operator bool() const noexcept{
		return layout != nullptr;
	}

	const layout_type* operator->() const noexcept{
		return layout;
	}

	const layout_type& operator*() const noexcept{
		return *layout;
	}
};

void push(gui::renderer_frontend& r, const graphic::draw::instruction::draw_record_storage<mr::heap_allocator<>>& buf);

export
template <typename LayoutType>
struct layout_record{
	static void record_glyph_draw_instructions(
		graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
		const LayoutType& glyph_layout,
		graphic::color color_scl
	){
		static_assert(false);
	}
};

export
template <typename LayoutType>
struct text_holder : elem{
	using layout_type = LayoutType;

	text_holder(scene& scene, elem* parent)
		: elem(scene, parent){
	}

private:
	layout::expand_policy expand_policy_{};
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<>> draw_instr_buffer_{mr::get_default_heap_allocator()};
	std::optional<graphic::color> text_color_scl_{};

protected:
	void set_instr_buffer_allocator(const mr::heap_allocator<std::byte>& alloc){
		draw_instr_buffer_ = {alloc};
	}

	bool has_drawable_text() const noexcept{
		return !draw_instr_buffer_.heads().empty();
	}

public:
	align::pos text_entire_align{align::pos::top_left};

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void set_expand_policy(const layout::expand_policy expand_policy){
		if(util::try_modify(expand_policy_, expand_policy)){
			notify_text_changed();
		}
	}

	[[nodiscard]] std::optional<graphic::color> get_text_color_scl() const{
		return text_color_scl_;
	}

	void set_text_color_scl(const std::optional<graphic::color>& text_color_scl){
		if(util::try_modify(this->text_color_scl_, text_color_scl)){
			if(const auto buf = get_glyph_layout()){
				this->update_draw_buffer(*buf);
			}
		}
	}

	[[nodiscard]] virtual std::string_view get_text() const noexcept = 0;

protected:

	void on_opacity_changed(float previous) override{
		if(const auto buf = get_glyph_layout()){
			this->update_draw_buffer(*buf);
		}
	}

	virtual exclusive_glyph_layout<LayoutType> get_glyph_layout() const noexcept = 0;

	virtual void notify_text_changed() = 0;

	virtual graphic::color get_text_draw_color() const noexcept{
		auto color = text_color_scl_.value_or(graphic::colors::white);
		color.mul_a(get_draw_opacity());
		if(is_disabled()){
			color.mul_a(.5f);
		}
		return color;
	}

	bool set_disabled(bool isDisabled) override{
		if(elem::set_disabled(isDisabled)){
			if(const auto buf = get_glyph_layout()){
				this->update_draw_buffer(*buf);
			}
			return true;
		}
		return false;
	}

	void update_draw_buffer(const LayoutType& glyph_layout){
		layout_record<LayoutType>::record_glyph_draw_instructions(draw_instr_buffer_, glyph_layout, get_text_draw_color());
	}

	void push_text_draw_buffer() const{
		push(get_scene().renderer(), draw_instr_buffer_);
	}
};

/*
export
template <typename LayoutType, typename Param>
struct glyph_layout_node : react_flow::async_node<
		react_flow::descriptor<exclusive_glyph_layout<LayoutType>>,
		react_flow::descriptor<std::string, {true}, std::string_view>,
		react_flow::descriptor<Param>>{
	using layout_type = LayoutType;

private:
	using base_type = react_flow::async_node<react_flow::descriptor<exclusive_glyph_layout<LayoutType>>,
		react_flow::descriptor<std::string, {true}, std::string_view>,
		react_flow::descriptor<Param>>;

	std::atomic_uint current_idx_{0};
	LayoutType layout_[2]{};
	ccur::atomic_shared_mtx shared_mutex_;

public:
	[[nodiscard]] glyph_layout_node()
	: base_type(react_flow::async_type::async_latest){
	}

protected:
	struct work_param{
		layout_type& layout;
		unsigned index;
	};

	work_param get_backend() noexcept {
		const unsigned idx = !static_cast<bool>(current_idx_.load(std::memory_order::relaxed));
		return {layout_[idx], idx};
	}

	auto to_result(const work_param& param) noexcept {
		shared_mutex_.lock();
		current_idx_.store(param.index, std::memory_order::relaxed);
		shared_mutex_.downgrade();

		return std::optional{exclusive_glyph_layout{std::addressof(param.layout), ccur::shared_lock{shared_mutex_, std::adopt_lock}}};
	}

	react_flow::request_pass_handle<exclusive_glyph_layout<layout_type>> request_raw(bool allow_expired) final{
		if(this->get_dispatched() > 0 && !allow_expired){
			return react_flow::make_request_handle_unexpected<exclusive_glyph_layout<layout_type>>(react_flow::data_state::expired);
		}

		ccur::shared_lock lk{shared_mutex_};
		return react_flow::make_request_handle_expected<exclusive_glyph_layout<layout_type>>(
			exclusive_glyph_layout{layout_ + current_idx_.load(std::memory_order::relaxed), std::move(lk)},
			this->get_dispatched() > 0);
	}

	// This function can only be accessed by one thread (one thread write a glyph layout)
	 std::optional<exclusive_glyph_layout<layout_type>> operator()(
	 	const std::stop_token& stop_token,
	 	std::string&& str,
	 	Param&& param
	 ) override = 0;

	 std::optional<exclusive_glyph_layout<layout_type>> operator()(
	 	const std::stop_token& stop_token, const std::string& str, const Param& policy) override{

	 	//TODO replace with decay copy (auto{})
	 	return this->operator()(stop_token, std::string{str}, Param(policy));
	 }
};
*/

}
