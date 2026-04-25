module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.compound.numeric_input_area;

import std;
import mo_yanxi.gui.elem.text_edit;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.flipper;
import mo_yanxi.gui.elem.arrow_elem;
import mo_yanxi.snap_shot;

import mo_yanxi.math;

namespace mo_yanxi::gui::cpd{
template <typename T>
consteval std::size_t get_max_chars_size() {
	if constexpr (std::is_floating_point_v<T>) {
		/* 对于浮点数：最大有效位数 + 符号位 + 小数点 + 'e' + 指数符号 + 最大指数位数 (约4位) */
		return std::numeric_limits<T>::max_digits10 + 10;
	} else if constexpr (std::is_integral_v<T>) {
		/*
		 *	对于整数：
		 *	digits10 是可以无损表示的十进制位数
		 *	+1 涵盖最后一位可能的额外数字（例如 8位整数 digits10 为 2，但最大可达 128 三位数）
		 *	+1 涵盖负数可能的负号 '-'
		 *	+1 作为额外的安全冗余
		 */
		return std::numeric_limits<T>::digits10 + 3;
	} else {
		return 32;
	}
}

template <typename CharT, typename T, typename ParamT>
constexpr bool string_to_arithmetic_impl(std::basic_string_view<CharT> sv, T& value, ParamT param) noexcept {
	if constexpr (std::same_as<CharT, char>) {
		auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value, param);
		return ec == std::errc{};
	} else {
		static constexpr std::size_t static_buf_size = get_max_chars_size<T>();

		if (sv.size() > static_buf_size) {
			return false;
		}

		char stack_buf[static_buf_size];

		auto is_out_of_bounds = [](CharT c) {
			return c > static_cast<CharT>(std::numeric_limits<char>::max());
		};

		if (std::ranges::any_of(sv, is_out_of_bounds)) {
			return false;
		}

		std::ranges::copy(sv | std::views::transform([](auto c) { return static_cast<char>(c); }), std::ranges::begin(stack_buf));

		auto [ptr, ec] = std::from_chars(stack_buf, stack_buf + sv.size(), value, param);
		return ec == std::errc{};
	}
}

template <typename CharT, std::integral T>
constexpr bool parse_string_to_arithmetic(std::basic_string_view<CharT> sv, T& value, int base = 10) noexcept {
	return cpd::string_to_arithmetic_impl(sv, value, base);
}

template <typename CharT, std::floating_point T>
constexpr bool parse_string_to_arithmetic(std::basic_string_view<CharT> sv, T& value, std::chars_format fmt = std::chars_format::general) noexcept {
	return cpd::string_to_arithmetic_impl(sv, value, fmt);
}

template <typename T, std::invocable<std::string_view> Fn>
	requires std::is_arithmetic_v<T>
constexpr decltype(auto) process_arithmetic_to_string(T value, Fn&& f) noexcept(std::is_nothrow_invocable_v<Fn&&, std::string_view>) {
	static constexpr std::size_t buf_size = get_max_chars_size<T>();
	char buffer[buf_size + 1] ADAPTED_INDETERMINATE;
	auto [ptr, ec] = std::to_chars(buffer, buffer + buf_size, value);
	if (ec == std::errc{}) {
		*ptr = '\0';

		std::string_view sv(buffer, ptr - buffer);
		return std::invoke(std::forward<Fn>(f), sv);
	} else {
		std::println(std::cerr, "Error: std::to_chars conversion failed.\n");
		std::terminate();
	}
}

struct general_arrow  : arrow_elem<general_arrow>{
protected:
	bool decr_{};

public:
	[[nodiscard]] general_arrow(scene& scene, elem* parent, bool decr)
		: arrow_elem<general_arrow>(scene, parent),
		  decr_(decr){
		interactivity = interactivity_flag::enabled;
	}


	float get_arrow_angle() const noexcept{
		return decr_ ? - math::pi : 0;
	}

};

enum struct drag_state : std::uint8_t{
	none,
	standard,
	precise
};

export
template <typename T>
struct numeric_input_area : flipper<2>{
	using value_type = T;

private:
	struct arrow : general_arrow{
		using general_arrow::general_arrow;

		numeric_input_area& get_area() const{
			const elem& self = *this;
			return self.parent_ref<sequence, true>().parent_ref<numeric_input_area<T>, true>();
		}

		events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
			if(!is_disabled() && event.key.on_release() && event.within_elem(*this)){
				numeric_input_area& area = get_area();
				auto current = area.get_current_value();
				auto step = area.move_step_;

				value_type next_val = current;

				if(decr_){
					// 防下溢处理（针对无符号整数 std::uint32_t 等）并结合 from 钳制
					if (current >= step && (current - step) >= area.valid_rng.from) {
						next_val = current - step;
					} else {
						next_val = area.valid_rng.from;
					}
				}else{
					// 防上溢处理并结合 to 钳制
					if ((area.valid_rng.to - current) >= step) {
						next_val = current + step;
					} else {
						next_val = area.valid_rng.to;
					}
				}

				area.set_value(next_val);
			}
			return events::op_afterwards::intercepted;
		}
	};

	struct numeric_text_edit : text_edit{
		numeric_input_area& get_area() const{
			return parent_ref<numeric_input_area, true>();
		}

		[[nodiscard]] numeric_text_edit(scene& scene, elem* parent)
			: text_edit(scene, parent){
		}


		void action_enter() override{
			update_();
		}

		void on_last_clicked_changed(bool isFocused) override{
			text_edit::on_last_clicked_changed(isFocused);

			if(!isFocused){
				update_();
				get_area().switch_to(0);
			}
		}
	private:
		void update_(){
			value_type val ADAPTED_INDETERMINATE;
			if(cpd::parse_string_to_arithmetic(get_text(), val)){
				get_area().update_value_from_text_edit(val, get_text());
				get_area().switch_to(0);
			}else{
				set_input_invalid();
			}
		}
	};

	math::section<value_type> valid_rng{std::numeric_limits<value_type>::lowest(), std::numeric_limits<value_type>::max()};
	value_type cur_{};
	value_type move_step_{value_type(1)};

	snap_shot<float> drag_temp_move_value_{};
	value_type drag_temp_value_{};

	drag_state drag_state_{};
	float drag_sensitivity_{10};

	text_edit* text_edit_;
	direct_label* label_;

	std::array<arrow*, 2> get_arrows() const noexcept{
		auto rng = elem_cast<sequence, false>(*candidates_[0]).exposed_children();
		return {&elem_cast<arrow, false>(*rng.front()), &elem_cast<arrow, false>(*rng.back())};
	}

	void update_arrow_(value_type val){
		auto arrows = get_arrows();
		arrows.front()->set_disabled(val <= valid_rng.from);
		arrows.back()->set_disabled(val >= valid_rng.to);
	}

	void update_value_logically(value_type val){
		if(util::try_modify(cur_, val)){
			this->on_changed(val);
		}

		this->update_arrow_(val);
	}

	void update_value_from_text_edit(value_type val, std::u32string_view text){
		auto v = std::clamp(val, valid_rng.from, valid_rng.to);
		if(val == v){
			label_->set_tokenized_text({text, typesetting::tokenize_tag::raw});
			this->update_value_logically(v);
		}else{
			set_value(v);
		}
	}

	value_type get_drag_temp_val_() const noexcept{
		if constexpr (std::is_integral_v<value_type>) {
			// 将偏移量转为双精度浮点计算，避免计算过程中的截断与溢出
			double raw_offset = (static_cast<double>(drag_temp_move_value_.temp) / drag_sensitivity_) * static_cast<double>(move_step_);
			if (raw_offset > 0) {
				// 在 double 层面进行判断，彻底避免大额度 raw_offset 转为整型时的溢出回绕
				double max_allowed_offset = static_cast<double>(std::numeric_limits<value_type>::max()) - static_cast<double>(drag_temp_value_);
				if (raw_offset >= max_allowed_offset) {
					return valid_rng.to;
				}
				value_type offset = static_cast<value_type>(raw_offset);
				return valid_rng.clamp(drag_temp_value_ + offset);
			} else if (raw_offset < 0) {
				double max_abs_offset = static_cast<double>(drag_temp_value_) - static_cast<double>(std::numeric_limits<value_type>::lowest());
				double abs_raw_offset = -raw_offset;
				if (abs_raw_offset >= max_abs_offset) {
					return valid_rng.from;
				}
				value_type abs_offset = static_cast<value_type>(abs_raw_offset);
				return valid_rng.clamp(drag_temp_value_ - abs_offset);
			}
			return valid_rng.clamp(drag_temp_value_);
		} else {
			// 浮点数不易发生典型的溢出回绕，直接计算并钳制
			value_type offset = static_cast<value_type>((drag_temp_move_value_.temp / drag_sensitivity_) * static_cast<float>(move_step_));
			return valid_rng.clamp(drag_temp_value_ + offset);
		}
	}

	void set_text_display_(value_type val){
		cpd::process_arithmetic_to_string(val, [this](std::string_view sv){
			this->label_->set_tokenized_text({sv, typesetting::tokenize_tag::raw});
		});
	}

public:
	virtual void on_changed(value_type val){

	}

	void set_value(value_type val) {
		val = std::clamp(val, valid_rng.from, valid_rng.to);
		if(get_current_value() == val)return;
		this->set_text_display_(val);
		this->update_value_logically(val);
	}

	void set_value_no_propagate(value_type val) {
		val = std::clamp(val, valid_rng.from, valid_rng.to);
		if(get_current_value() == val)return;
		cur_ = val;
		this->set_text_display_(val);
		this->update_arrow_(val);
	}

	value_type get_current_value() const noexcept{
		return cur_;
	}

	[[nodiscard]] numeric_input_area(scene& scene, elem* parent)
		: flipper<2>(scene, parent){

		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
		
		set_expand_policy(layout::expand_policy::passive);
		this->create(0, [this](sequence& display){
			display.template_cell.set_pad({4, 4});
			display.set_layout_spec(layout::layout_specifier::fixed(layout::layout_policy::vert_major));
			display.set_style();
			display.set_expand_policy(layout::expand_policy::passive);
			display.create_back([this](arrow& a){
				a.set_min_extent({40, 0});
				a.config.margin = 2;
				a.set_self_boarder(gui::boarder{.left = 2}.set_vert(2));
				util::sync_set_elem_style(a, style::family_variant::base_only);
			}, true).cell().set_size({layout::size_category::scaling, .56f});
			display.create_back([this](direct_label& label){
				label_ = &label;
				label.text_entire_align = align::pos::center;
				label.set_expand_policy(layout::expand_policy::passive);
				label.set_style();
				label.set_fit_type(label_fit_type::scl);
			});
			display.create_back([this](arrow& a){
				a.set_min_extent({40, 0});
				a.config.margin = 2;
				a.set_self_boarder(gui::boarder{.right = 2}.set_vert(2));
				util::sync_set_elem_style(a, style::family_variant::base_only);
			}, false).cell().set_size({layout::size_category::scaling, .56f});
		});

		this->create(1, [this](numeric_text_edit& edit){
			text_edit_ = &edit;
			edit.set_view_type(text_edit_view_type::align_y);
			edit.set_expand_policy(layout::expand_policy::passive);
			util::sync_set_elem_style(edit, style::family_variant::base_only);
		});

		this->set_value(get_current_value());
		this->set_text_display_(get_current_value());
		this->update_arrow_(get_current_value());

	}

public:

#pragma region FilpperOverrides
	events::op_afterwards on_drag(const events::drag event) override{
		auto raw_mov = event.delta().x;

		// 拖拽初始触发
		if(raw_mov && drag_state_ == drag_state::none){
			drag_state_ = drag_state::standard;
			drag_temp_value_ = get_current_value();
			drag_temp_move_value_.base = 0; // 重置基准偏移
			drag_temp_move_value_.temp = 0;
		}

		if(drag_state_ != drag_state::none){
			static constexpr float precise_scl = 0.1f;
			bool want_precise = event.key.match(input_handle::act::ignore, input_handle::mode::shift);

			if(want_precise && drag_state_ != drag_state::precise){
				// Standard -> Precise
				drag_state_ = drag_state::precise;
				// 为了让切换瞬间导出的结果不变，调整 base，抵消比例变化带来的落差
				drag_temp_move_value_.base = drag_temp_move_value_.temp - raw_mov * precise_scl;
			}
			else if(!want_precise && drag_state_ != drag_state::standard){
				// Precise -> Standard
				drag_state_ = drag_state::standard;
				drag_temp_move_value_.base = drag_temp_move_value_.temp - raw_mov;
			}

			// 计算最新的 temp
			float mov = raw_mov;
			if(drag_state_ == drag_state::precise){
				mov *= precise_scl;
			}

			drag_temp_move_value_.temp = drag_temp_move_value_.base + mov;
			this->set_text_display_(get_drag_temp_val_());
		}

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		if(event.key.on_release()){
			if(drag_state_ != drag_state::none){
				this->set_value(get_drag_temp_val_());
				drag_state_ = drag_state::none;
			}else{
				{
					switch_to(1);
					auto& edit = at<text_edit>(1);
					edit.apply_edit([this](std::u32string& s){
						cpd::process_arithmetic_to_string(get_current_value(), [&](std::string_view sv){
							s.assign_range(sv);
						});
						return true;
					});
					edit.on_last_clicked_changed(true);
					edit.action_select_all();
					get_scene().overwrite_last_inbound_click_quiet(&edit);
				}
			}

		}
		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_esc() override{
		if(get_current_active_index() == 1){
			switch_to(0);
			return events::op_afterwards::intercepted;
		}
		return events::op_afterwards::fall_through;
	}
#pragma endregion
	style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override{
		style::cursor_style rst{style::cursor_type::textarea};

		value_type val;
		if(drag_state_ != drag_state::none){
			val = get_drag_temp_val_();
		}else{
			val = get_current_value();
		}
		if(val > this->valid_rng.from)rst.push_dcor(style::cursor_decoration_type::to_left);
		if(val < this->valid_rng.to)rst.push_dcor(style::cursor_decoration_type::to_right);
		return rst;
	}
};
}