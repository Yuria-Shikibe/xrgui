export module mo_yanxi.gui.elem.image_frame;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.assets.image_regions;
export import mo_yanxi.gui.region_drawable;
export import mo_yanxi.gui.region_drawable.derives;
export import mo_yanxi.graphic.color;
export import mo_yanxi.math;
export import align;

import std;

namespace mo_yanxi::gui{
[[nodiscard]] math::vec2 get_expected_size(
	const drawable_base& drawable,
	const image_display_style& style,
	const math::vec2 bound) noexcept{
	if(const auto sz = drawable.get_preferred_extent()){
		return align::embed_to(style.scaling, sz, bound);
	}

	return bound;
}


export
struct image_frame : public elem{
	image_display_style default_style{};

protected:
	std::size_t current_frame_index_{};
	mr::heap_vector<styled_drawable> drawables_{};

public:
	[[nodiscard]] image_frame(scene& scene, elem* parent)
	: elem(scene, parent){
	}

	std::size_t get_drawable_size() const noexcept{
		return drawables_.size();
	}

	bool is_drawable_empty() const noexcept{
		return drawables_.empty();
	}

	template <std::derived_from<drawable_base> Ty, typename... T>
		requires (std::constructible_from<Ty, T...>)
	void set_drawable(const std::size_t idx, image_display_style style, T&&... args){
		drawables_.resize(math::max(drawables_.size(), idx + 1));
		drawables_[idx] = styled_drawable{
			style,
			mo_yanxi::make_allocate_aware_poly_unique<Ty, drawable_base>(this->get_heap_allocator<Ty>(), std::forward<T>(args)...)
		};
	}

	template <std::derived_from<drawable_base> Ty, typename... T>
		requires (std::constructible_from<Ty, T...>)
	void set_drawable(const std::size_t idx, T&&... args){
		this->set_drawable<Ty>(idx, default_style, std::forward<T>(args)...);
	}

	template <std::derived_from<drawable_base> Ty, typename... T>
		requires (std::constructible_from<Ty, T...>)
	void set_drawable(T&&... args){
		this->set_drawable<Ty>(0, std::forward<T>(args)...);
	}

	[[nodiscard]] std::size_t get_index() const noexcept{
		return current_frame_index_;
	}

	virtual bool set_index(std::size_t idx) {
		if(idx >= drawables_.size()){
			throw std::out_of_range("set_frame_index");
		}

		//TODO notify layout change if needed?
		if(util::try_modify(current_frame_index_, idx)){
			return true;
			// notify_layout_changed();
		}
		return false;
		// return current_frame_index_;
	}

protected:
	void try_swap_image(const std::size_t ldx, const std::size_t rdx){
		if(ldx >= drawables_.size() || rdx >= drawables_.size()) return;

		std::swap(drawables_[ldx], drawables_[rdx]);
	}

	void draw_content_impl(const rect clipSpace) const override{
		elem::draw_content_impl(clipSpace);
		auto drawable = get_region();
		if(!drawable || !drawable->drawable) return;
		const auto sz = get_expected_size(*drawable->drawable, drawable->style, content_extent());
		const auto off = align::get_offset_of(default_style.align, sz, content_bound_abs());

		//TODO support stylized color
		graphic::color scl{graphic::colors::white};
		scl.mul_a(get_draw_opacity());
		drawable->drawable->draw(get_scene().renderer(), math::raw_frect{off, sz}, scl);
	}

	[[nodiscard]] const styled_drawable* get_region() const noexcept{
		if(current_frame_index_ < drawables_.size()){
			return &drawables_[current_frame_index_];
		}
		return nullptr;
	}
};


export struct check_box;

struct check_box_receiver : react_flow::terminal<std::size_t>{
private:
	check_box* check_box_;

public:
	[[nodiscard]] explicit check_box_receiver(check_box& check_box)
	: terminal(react_flow::propagate_behavior::eager), check_box_(&check_box){
	}

protected:
	void on_update(const std::size_t& data) override;
};

struct check_box : image_frame{
	using image_frame::image_frame;
private:
	using node_provider_type = react_flow::provider_cached<std::size_t>;
	using node_terminal_type = check_box_receiver;

	node_provider_type* node_prov_;
	node_terminal_type* node_rcvr_;

public:
	[[nodiscard]] check_box(scene& scene, elem* parent)
	: image_frame(scene, parent){
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
	}

	bool set_index(std::size_t idx) override{
		if(image_frame::set_index(idx)){
			if(node_prov_){
				node_prov_->update_value(idx);
			}
			return true;
		}
		return false;
	}

	node_provider_type& request_provider(){
		return request_and_cache_node(&check_box::node_prov_, react_flow::propagate_behavior::eager);
	}

	node_terminal_type& request_terminal(){
		return request_and_cache_node(&check_box::node_rcvr_);
	}

	//TODO tooltip dropdown

protected:
	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		auto rst = image_frame::on_click(event, aboves);
		if(get_drawable_size() <= 1)return rst;

		if(!event.key.on_release())return events::op_afterwards::intercepted;

		if(!event.within_elem(*this))return events::op_afterwards::intercepted;

		set_index((current_frame_index_ + 1) % drawables_.size());

		return events::op_afterwards::intercepted;
	}
};

void check_box_receiver::on_update(const std::size_t& data){
	this->check_box_->set_index(data);
}

export
template <std::derived_from<drawable_base> T>
struct image_frame_single : public elem{
	using drawable_type = T;

	image_display_style style{};

protected:
	T drawable_{};

public:
	[[nodiscard]] image_frame_single(scene& scene, elem* group,
		drawable_type drawable = {},
		const image_display_style& style = {})
	: elem(scene, group), style(style), drawable_(std::move(drawable)){
	}

	void set_drawable(T icon) noexcept{
		drawable_ = std::move(icon);
	}

protected:
	void draw_content_impl(const rect clipSpace) const override{
		draw_style();

		auto sz = gui::get_expected_size(drawable_, style, content_extent());
		auto off = align::get_offset_of(style.align, sz, content_bound_abs());

		//TODO support stylized color
		graphic::color scl{graphic::colors::white};
		scl.mul_a(get_draw_opacity());
		drawable_.draw(get_scene().renderer(), math::raw_frect{off, sz}, scl);
	}
};

export
struct icon_frame : image_frame_single<drawable_image<>>{
	[[nodiscard]] icon_frame(scene& scene, elem* group, const assets::image_region_borrow& icon = {},
		const image_display_style& style = {})
	: image_frame_single<drawable_image<>>(scene, group, drawable_image<>{icon}, style){
	}
};



export
struct row_separator : image_frame_single<drawable_row_patch<>>{
	[[nodiscard]] row_separator(
		scene& scene, elem* group,
		const assets::row_patch& patch = assets::builtin::get_separator_row_patch(),
		const image_display_style& style = {align::scale::stretch, align::pos::center})
	: image_frame_single(scene, group, drawable_row_patch<>{patch}, style){
		set_style();
	}
};

}
