module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.gui.elem.label_v2;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.text_holder;
export import mo_yanxi.font;
export import mo_yanxi.typesetting;
export import mo_yanxi.typesetting.rich_text;
export import mo_yanxi.graphic.draw.instruction.recorder;
import mo_yanxi.concurrent.atomic_shared_mutex;
import std;

namespace mo_yanxi::gui{

export
template <>
struct layout_record<typesetting::glyph_layout>{
	static void record_glyph_draw_instructions(
		graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
		const typesetting::glyph_layout& glyph_layout,
		graphic::color color_scl
	);
};

enum struct change_type{
	none = 0,
	text = 1 << 0,
	max_extent = 1 << 1,
	config = 1 << 2,
};

BITMASK_OPS(, change_type);

export
struct label_v2 : text_holder<typesetting::glyph_layout>{
	struct from_string{
		label_v2* label_;
		void operator()(std::string&& ){

		}
	};


protected:
	std::string raw_string_{};
	typesetting::tokenized_text tokenized_text_{};
	typesetting::glyph_layout glyph_layout_{};
	typesetting::layout_context context_{std::in_place};

	change_type change_mark_{};

	bool fit_{};

	bool is_layout_expired_() const noexcept{
		return change_mark_ != change_type{};
	}
public:
	math::vec2 max_fit_scale_bound{math::vectors::constant2<float>::inf_positive_vec2};

	using text_holder::text_holder;

	template <typename StrTy>
		requires (std::constructible_from<std::string_view, const StrTy&>)
	void set_text(StrTy&& str){
		if(raw_string_ != std::string_view{std::as_const(str)}){
			raw_string_ = std::forward<StrTy>(str);
			change_mark_ |= change_type::text;

			notify_text_changed();
		}
	}

	void set_typesetting_policy(typesetting::layout_config policy){
		if(context_.get_config() != policy){
			context_.set_config(policy);

			change_mark_ |= change_type::config;
			notify_text_changed();
		}
	}

	void set_fit(bool fit = true){
		if(util::try_modify(fit_, fit)){
			change_mark_ |= change_type::max_extent | change_type::config;
			notify_text_changed();
		}
	}


	void layout_elem() override{
		elem::layout_elem();

		if(is_layout_expired_()){
			auto maxSz = restriction_extent.potential_extent();
			const auto resutlSz = layout_text(maxSz.fdim(boarder_extent()));
			if(resutlSz.updated && !resutlSz.required_extent.equals(content_extent())){
				notify_layout_changed(propagate_mask::force_upper);
			}
		}
	}


protected:
	exclusive_glyph_layout<typesetting::glyph_layout> get_glyph_layout() const noexcept final{
		return &glyph_layout_;
	}

	void notify_text_changed() final{
		notify_isolated_layout_changed();
	}

	std::string_view get_text() const noexcept final{
		return raw_string_;
	}

	void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override;

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

		const auto ext = extent.potential_extent().inf_to0();

		return util::select_prefer_extent(get_expand_policy() == layout::expand_policy::prefer, ext, get_prefer_content_extent());
	}

	bool set_text_quiet(std::string_view text){
		if(get_text() != text){
			raw_string_ = text;
			return true;
		}
		return false;
	}

	[[nodiscard]] math::vec2 get_glyph_draw_extent() const noexcept{
		if(fit_){
			return align::embed_to(align::scale::fit_smaller, glyph_layout_.extent, content_extent().min(max_fit_scale_bound));
		}else{
			return glyph_layout_.extent;
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

		if((change_mark_ & change_type::text) != change_type{}){
			tokenized_text_.reset(raw_string_);
		}

		if(fit_){
			if((change_mark_ & change_type::max_extent) != change_type{}){
				if(context_.set_max_extent(math::vectors::constant2<float>::inf_positive_vec2)){

				}else{
					change_mark_ = change_mark_ & ~change_type::max_extent;
				}
			}

			if(is_layout_expired_()){
				change_mark_ = change_type::none;

				if(context_.set_max_extent(math::vectors::constant2<float>::inf_positive_vec2)){
					context_.layout(glyph_layout_, tokenized_text_);
					update_draw_buffer(glyph_layout_);
					return {bound, true};
				}
			}
		}else if(context_.set_max_extent(bound) || is_layout_expired_()){
			context_.layout(glyph_layout_, tokenized_text_);
			update_draw_buffer(glyph_layout_);
			change_mark_ = change_type::none;
			return {glyph_layout_.extent, true};
		}

		return {glyph_layout_.extent, false};
	}

	void draw_text() const;
};

/*
export
struct text_style{
	font::typesetting::layout_policy policy;
	align::pos align;
	float scale;
};

export
struct text_holder : elem{
	using elem::elem;

private:
	layout::expand_policy expand_policy_{};
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>> draw_instr_buffer_{mr::get_default_heap_allocator()};
	std::optional<graphic::color> text_color_scl_{};
protected:
	void set_instr_buffer_allocator(const mr::heap_allocator<std::byte>& alloc){
		draw_instr_buffer_ = {alloc};
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
				update_draw_buffer(*buf);
			}
		}
	}

	[[nodiscard]] virtual std::string_view get_text() const noexcept{
		if(auto l = get_glyph_layout()){
			return l->get_text();
		}else{
			return {};
		}
	}

protected:

	void on_opacity_changed(float previous) override{
		if(const auto buf = get_glyph_layout()){
			update_draw_buffer(*buf);
		}
	}

	virtual exclusive_glyph_layout get_glyph_layout() const noexcept = 0;

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
				update_draw_buffer(*buf);
			}
			return true;
		}
		return false;
	}

	void update_draw_buffer(const typesetting::glyph_layout& glyph_layout){
		record_glyph_draw_instructions(draw_instr_buffer_, glyph_layout, get_text_draw_color());
	}

	void push_text_draw_buffer() const;
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
	void on_update(const std::string& data) override;
};

export
struct text_layout_result{
	math::vec2 required_extent;
	bool updated;
};

struct label : text_holder{
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
	exclusive_glyph_layout get_glyph_layout() const noexcept final{
		return &glyph_layout;
	}

	void notify_text_changed() final{
		text_expired = true;
		notify_isolated_layout_changed();
	}

	std::string_view get_text() const noexcept override{
		return glyph_layout.get_text();
	}

	void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override;


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

export struct async_label;


export
struct async_label_terminal : react_flow::terminal<exclusive_glyph_layout>{
	async_label* label;

	[[nodiscard]] explicit async_label_terminal(async_label& label)
	: label(std::addressof(label)){
	}

	void on_update(const exclusive_glyph_layout& data) override;
};

struct async_label_layout_config{
	math::vec2 bound{math::vectors::constant2<float>::inf_positive_vec2};
	float throughout_scale{1.f};
	font::typesetting::layout_policy layout_policy{font::typesetting::layout_policy::reserve};
};


struct async_label : text_holder{
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
	exclusive_glyph_layout get_glyph_layout() const noexcept final{
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
};

export
struct label_layout_node : react_flow::modifier_transient<exclusive_glyph_layout, async_label_layout_config, std::string>{
private:
	const font::typesetting::parser* parser_{&font::typesetting::global_parser};

	float last_scale_{1.f};
	std::atomic_uint current_idx_{0};
	font::typesetting::glyph_layout layout_[2]{};

	ccur::atomic_shared_mutex shared_mutex_;

public:
	[[nodiscard]] label_layout_node()
	: modifier_transient(react_flow::async_type::async_latest){
	}

protected:
	react_flow::request_pass_handle<exclusive_glyph_layout> request_raw(bool allow_expired) override{
		if(get_dispatched() > 0 && !allow_expired){
			return react_flow::make_request_handle_unexpected<exclusive_glyph_layout>(react_flow::data_state::expired);
		}

		ccur::shared_lock lk{shared_mutex_};
		return react_flow::make_request_handle_expected<exclusive_glyph_layout>(
			exclusive_glyph_layout{layout_ + current_idx_.load(std::memory_order::relaxed), std::move(lk)},
			get_dispatched() > 0);
	}

	//This function can only be accessed by one thread (one thread write a glyph layout)
	std::optional<exclusive_glyph_layout> operator()(
		const std::stop_token& stop_token,
		async_label_layout_config&& param,
		std::string&& str
	) override{


		const unsigned idx = !static_cast<bool>(current_idx_.load(std::memory_order::relaxed));
		auto& glayout = layout_[idx];

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

		shared_mutex_.lock();
		current_idx_.store(idx, std::memory_order::relaxed);
		shared_mutex_.downgrade();

		return std::optional{exclusive_glyph_layout{std::addressof(glayout), ccur::shared_lock{shared_mutex_, std::adopt_lock}}};
	}

	std::optional<exclusive_glyph_layout> operator()(
		const std::stop_token& stop_token, const async_label_layout_config& policy, const std::string& str) override{

		//TODO replace with decay copy (auto{})
		return this->operator()(stop_token, async_label_layout_config(policy), std::string{str});
	}
};*/
}
