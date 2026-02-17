module;

#include <cassert>

export module mo_yanxi.gui.elem.label;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.text_holder;
export import mo_yanxi.font;
export import mo_yanxi.font.typesetting;
export import mo_yanxi.graphic.draw.instruction.recorder;
import mo_yanxi.concurrent.atomic_shared_mutex;

import std;



namespace mo_yanxi::gui{

export
template<>
struct layout_record<font::typesetting::glyph_layout>{
	static void record_glyph_draw_instructions(
		graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
		const font::typesetting::glyph_layout& layout,
		graphic::color color_scl
	);
};

export
struct text_style{
	font::typesetting::layout_policy policy;
	align::pos align;
	float scale;
};

export
struct label;

export
struct sync_label_terminal : react_flow::terminal<std::string>{
	label* label_;

	[[nodiscard]] explicit sync_label_terminal(label& label)
	: label_(std::addressof(label)){
	}

protected:
	void on_update(react_flow::data_pass_t<std::string> data) override;
};


struct label : text_holder<font::typesetting::glyph_layout>{
protected:
	const font::typesetting::parser* parser{&font::typesetting::global_parser};
	font::typesetting::glyph_layout glyph_layout{};

	float scale{1.f};
	bool text_expired{false};
	bool fit_{};

	sync_label_terminal* notifier_{};

public:
	math::vec2 max_fit_scale_bound{math::vectors::constant2<float>::inf_positive_vec2};

	using text_holder::text_holder;

	template <typename StrTy>
		requires (std::constructible_from<std::string_view, const StrTy&>)
	void set_text(StrTy&& str){
		if(glyph_layout.get_text() != std::string_view{std::as_const(str)}){
			glyph_layout.set_text(std::forward<StrTy>(str));

			notify_text_changed();
		}
	}

	void set_typesetting_policy(font::typesetting::layout_policy policy){
		if(glyph_layout.set_policy(policy)){
			if(policy != font::typesetting::layout_policy::reserve) fit_ = false;
			notify_text_changed();
		}
	}

	void set_fit(bool fit = true){
		if(util::try_modify(fit_, fit)){
			if(fit){
				set_typesetting_policy(font::typesetting::layout_policy::reserve);
				glyph_layout.set_clamp_size(math::vectors::constant2<float>::inf_positive_vec2);
			}else{
				set_typesetting_policy(font::typesetting::layout_policy::auto_feed_line);
			}

			text_expired = true;
			notify_isolated_layout_changed();
		}
	}

	void set_scale(float scale){
		if(util::try_modify(this->scale, scale)){
			text_expired = true;
			notify_isolated_layout_changed();
		}
	}

	void layout_elem() override{
		elem::layout_elem();

		if(text_expired){
			auto maxSz = restriction_extent.potential_extent();
			const auto resutlSz = layout_text(maxSz.fdim(boarder_extent()));
			if(resutlSz.updated && !resutlSz.required_extent.equals(content_extent())){
				notify_layout_changed(propagate_mask::force_upper);
			}
		}
	}


	sync_label_terminal& request_receiver(){
		if(notifier_){
			return *notifier_;
		}
		auto& node = get_scene().request_react_node<sync_label_terminal>(*this);
		this->notifier_ = &node;
		return node;
	}

	sync_label_terminal& request_receiver_and_connect(react_flow::node& prev){
		auto& node = request_receiver();
		node.connect_predecessor(prev);
		return node;
	}

	[[nodiscard]] const font::typesetting::parser* get_parser() const noexcept{
		return parser;
	}

	void set_parser(const font::typesetting::parser* const parser){
		this->parser = parser;
	}

protected:
	exclusive_glyph_layout<font::typesetting::glyph_layout> get_glyph_layout() const noexcept final{
		return &glyph_layout;
	}

	void notify_text_changed() final{
		text_expired = true;
		notify_isolated_layout_changed();
	}

	std::string_view get_text() const noexcept override{
		return glyph_layout.get_text();
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;


	bool resize_impl(const math::vec2 size) override{
		if(elem::resize_impl(size)){
			layout_text(content_extent());
			return true;
		}
		return false;
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		if(get_expand_policy() == layout::expand_policy::passive){
			return std::nullopt;
		}

		if (!fit_) {
			const auto text_size = layout_text(extent.potential_extent());
			extent.apply(text_size.required_extent);
		}

		auto ext = extent.potential_extent().inf_to0();

		if(get_expand_policy() == layout::expand_policy::prefer){
			if(auto pref = get_prefer_content_extent())ext.max(*pref);
		}

		return ext;
	}

	bool set_text_quiet(std::string_view text){
		if(glyph_layout.get_text() != text){
			glyph_layout.set_text(std::string{text});
			return true;
		}
		return false;
	}

	[[nodiscard]] math::vec2 get_glyph_draw_extent() const noexcept{
		if(fit_){
			return align::embed_to(align::scale::fit_smaller, glyph_layout.extent(), content_extent().min(max_fit_scale_bound));
		}else{
			return glyph_layout.extent();
		}

	}

	[[nodiscard]] math::vec2 get_glyph_src_abs() const noexcept{
		const auto sz = get_glyph_draw_extent();
		const auto rst = align::get_offset_of(text_entire_align, sz, content_bound_abs());
		return rst;
	}

	[[nodiscard]] math::vec2 get_glyph_src_rel() const noexcept{
		const auto sz = get_glyph_draw_extent();
		const auto rst = align::get_offset_of(text_entire_align, sz, content_bound_rel());
		return rst;
	}

	[[nodiscard]] math::vec2 get_glyph_src_local() const noexcept{
		const auto sz = get_glyph_draw_extent();
		const auto rst = align::get_offset_of(text_entire_align, sz, rect{content_extent()});
		return rst;
	}


	virtual text_layout_result layout_text(math::vec2 bound){
		if(bound.area() < 32) return {};
		if(fit_){
			if(text_expired){
				glyph_layout.clear();
				parser->operator()(glyph_layout, 1);
				update_draw_buffer(glyph_layout);
				text_expired = false;
				return {bound, true};
			}

			return {bound, false};
		}

		//TODO loose the relayout requirement
		if(text_expired){
			glyph_layout.set_clamp_size(bound);
			glyph_layout.clear();
			parser->operator()(glyph_layout, scale);
			update_draw_buffer(glyph_layout);
			text_expired = false;
			return {glyph_layout.extent(), true};
		} else{
			if(glyph_layout.get_clamp_size() != bound){
				glyph_layout.set_clamp_size(bound);
				if(!glyph_layout.extent().equals(bound)){
					glyph_layout.clear();
				}
				if(glyph_layout.empty()){
					parser->operator()(glyph_layout, scale);
					update_draw_buffer(glyph_layout);
					return {glyph_layout.extent(), true};
				}
			}
		}

		return {glyph_layout.extent(), false};;
	}

	[[nodiscard]] std::optional<font::typesetting::layout_pos_t> get_layout_pos(math::vec2 globalPos) const;

	void draw_text() const;
};

/*
export struct async_label;


export
struct async_label_terminal : react_flow::terminal<exclusive_glyph_layout<font::typesetting::glyph_layout>>{
	async_label* label;

	[[nodiscard]] explicit async_label_terminal(async_label& label)
	: label(std::addressof(label)){
	}

	void on_update(const exclusive_glyph_layout<font::typesetting::glyph_layout>& data) override;
};

struct async_label_layout_config{
	math::vec2 bound{math::vectors::constant2<float>::inf_positive_vec2};
	float throughout_scale{1.f};
	font::typesetting::layout_policy layout_policy{font::typesetting::layout_policy::reserve};
};


struct async_label : text_holder<font::typesetting::glyph_layout>{
	friend async_label_terminal;

private:
	enum struct layout_extent_state : std::uint8_t{
		none,
		waiting_correction,
		valid,
	};

	using config_prov_node = react_flow::provider_cached<async_label_layout_config>;
	async_label_terminal* terminal{};
	config_prov_node* config_prov_{};
	layout_extent_state extent_state_{};
	math::vec2 last_layout_extent_{};
	bool scale_text_to_fit{true};

public:

	[[nodiscard]] async_label(scene& scene, elem* parent)
	: text_holder(scene, parent), terminal{&get_scene().request_react_node<async_label_terminal>(*this)}{
	}

	~async_label() override{
		if(terminal)get_scene().erase_independent_react_node(*terminal);
	}

	void set_fit(bool is_scale_text_to_fit){
		if(util::try_modify(scale_text_to_fit, is_scale_text_to_fit)){
			notify_layout_changed(propagate_mask::force_upper);
		}
	}

protected:
	exclusive_glyph_layout<font::typesetting::glyph_layout> get_glyph_layout() const noexcept final{
		return terminal->nothrow_request(false).value_or({});
	}

	void notify_text_changed() final{
		notify_layout_changed(propagate_mask::local | propagate_mask::force_upper);
	}

	void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override;

public:
	config_prov_node* set_as_config_prov(){
		if(!config_prov_){
			config_prov_ = &get_scene().request_react_node<config_prov_node>(*this);
			config_prov_->update_value({
				.layout_policy = font::typesetting::layout_policy::def
			});
		}

		return config_prov_;
	}

	void set_dependency(react_flow::node& glyph_layout_prov, bool link_to_prov_if_any = true){
		if(terminal->get_inputs()[0] != std::addressof(glyph_layout_prov)){
			terminal->disconnect_self_from_context();
			terminal->connect_predecessor(glyph_layout_prov);
			notify_layout_changed(propagate_mask::local | propagate_mask::force_upper);

			if(link_to_prov_if_any && config_prov_){
				config_prov_->connect_successors(glyph_layout_prov);
			}
		}
	}


protected:

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		auto rst = [&, this] -> std::optional<math::vec2>{
			if(get_expand_policy() == layout::expand_policy::passive){
				return std::nullopt;
			}

			if(!terminal){
				if(get_expand_policy() == layout::expand_policy::prefer){
					return get_prefer_content_extent();
				}
				return std::nullopt;
			}

			math::optional_vec2 layout_ext{math::nullopt_vec2<float>};
			if(extent_state_ == layout_extent_state::waiting_correction){
				layout_ext = last_layout_extent_;
			}else{
				if(const auto playout = terminal->request(false)){
					layout_ext = (*playout)->extent();
				}
			}

			if(layout_ext){
				auto ext = extent.potential_extent().inf_to0();
				ext.max(layout_ext);

				if(get_expand_policy() == layout::expand_policy::prefer){
					if(const auto pref = get_prefer_content_extent()){
						ext.max(*pref);
					}
				}

				return ext;
			}


			return std::nullopt;
		}();

		if(config_prov_ && extent_state_ != layout_extent_state::waiting_correction){
			config_prov_->update_value<true>(&async_label_layout_config::bound, extent.potential_extent());
		}

		return rst;
	}

	void layout_elem() override{
		text_holder::layout_elem();
		if(extent_state_ == layout_extent_state::waiting_correction){
			extent_state_ = layout_extent_state::valid;
		}
	}

public:
	[[nodiscard]] std::string_view get_text() const noexcept override{
		if(auto t = get_glyph_layout()){
			return t->get_text();
		}else{
			return {};
		}
	}
};

export
struct label_layout_node : glyph_layout_node<font::typesetting::glyph_layout, async_label_layout_config>{
private:
	const font::typesetting::parser* parser_{&font::typesetting::global_parser};
	float last_scale_{1.f};

public:
	using glyph_layout_node::glyph_layout_node;

protected:

	//This function can only be accessed by one thread (one thread write a glyph layout)
	std::optional<exclusive_glyph_layout<font::typesetting::glyph_layout>> operator()(
		const std::stop_token& stop_token,
		std::string&& str,
		async_label_layout_config&& param
	) override{
		const auto cur = get_backend();
		auto& glayout = cur.layout;

		if(glayout.get_text() != str){
			glayout.set_text(std::move(str));
			glayout.clear();
		}

		if(glayout.get_clamp_size() != param.bound){
			glayout.set_clamp_size(param.bound);
			glayout.clear();
		}

		if(glayout.set_policy(param.layout_policy)){
			glayout.clear();
		}

		if(util::try_modify(last_scale_, param.throughout_scale)){
			glayout.clear();
		}

		if(stop_token.stop_requested())return std::nullopt;

		if(glayout.empty()){
			parser_->operator()(glayout, param.throughout_scale);
		}

		return to_result(cur);
	}

};*/
}
