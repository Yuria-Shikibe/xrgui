//
// Created by Matrix on 2025/3/13.
//

export module mo_yanxi.gui.layout.cell;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;

export import mo_yanxi.math.rect_ortho;
export import align;

import mo_yanxi.math;
import std;

namespace mo_yanxi::gui::layout{
	export
	template <typename Elem, typename Cell>
	struct cell_create_result{
		Elem& elem;
		Cell& cell; //Dangling Caution

		template <typename T>
			requires (std::derived_from<Elem, T>)
		explicit(false) operator cell_create_result<T, Cell>() const noexcept{
			return {elem, cell};
		}

	};

	export
	template <typename Elem>
	struct cell_creator_base{
		using elem_type = Elem;
	};

	export
	template <typename Create>
	concept cell_creator = requires{
		typename std::remove_cvref_t<Create>::elem_type;
	};

	export
	struct basic_cell{
		rect allocated_region{};

		align::pos align{align::pos::center};
		math::section<vec2> extent_span{{}, math::vectors::constant2<float>::inf_positive_vec2};
		vec2 scaling{1.f, 1.f};
		align::spacing margin{};

		[[nodiscard]] constexpr vec2 clamp_size(vec2 sz) const noexcept{
			return sz.clamp_xy(extent_span.from, extent_span.to);
		}

		constexpr vec2 get_relative_src(vec2 actual_extent) const noexcept{
			return margin.top_lft() + allocated_region.get_src();// + align::get_offset_of(align, actual_extent, allocated_region);
		}

		void apply_to(
			const elem& group,
			elem& elem,
			optional_mastering_extent cell_expected_restriction_extent
			)const{
			elem.set_scaling(group.get_scaling() * scaling);
			const auto extent = allocated_region.extent().fdim(margin.extent());

			if(!cell_expected_restriction_extent.width_pending()){
				cell_expected_restriction_extent.set_width(extent.x);
			}

			if(!cell_expected_restriction_extent.height_pending()){
				cell_expected_restriction_extent.set_height(extent.y);
			}

			elem.restriction_extent = cell_expected_restriction_extent;
			elem.resize(extent, propagate_mask::lower);
			elem.try_layout();
		}

		void update_relative_src(elem& elem, math::vec2 parent_content_src) const{
			elem.set_rel_pos(get_relative_src(allocated_region.extent()));
			elem.update_abs_src(parent_content_src);
		}

		void update_relative_src(elem& elem, math::vec2 parent_content_src, float lerp_alpha) const{
			elem.set_rel_pos(get_relative_src(allocated_region.extent()), lerp_alpha);
			elem.update_abs_src(parent_content_src);
		}

	};

	export
	struct scaled_cell : basic_cell{
		rect region_scale{};
	};

	export
	struct partial_mastering_cell : basic_cell{
		stated_size stated_size{size_category::passive, 1};
		align::padding1d<float> pad{};

		auto& set_size(const layout::stated_size stated_size){
			this->stated_size = stated_size;
			return *this;
		}

		auto& set_size(const float size){
			this->stated_size = {size_category::mastering, size};
			return *this;
		}

		auto& set_pending(){
			this->stated_size = {size_category::pending, 1};
			return *this;
		}

		auto& set_passive(float weight = 1.f){
			this->stated_size = {size_category::passive, weight};
			return *this;
		}

		auto& set_from_ratio(float ratio = 1){
			this->stated_size = {size_category::scaling, ratio};
			return *this;
		}

		auto& set_pad(const align::padding1d<float> pad){
			this->pad = pad;
			return *this;
		}
	};

	export
	struct mastering_cell : basic_cell{
		stated_extent stated_extent{};
		align::spacing pad{};
		bool end_line{};

		/**
		 * @brief indicate the cell to grow to the whole line space
		 */
		bool saturate{};

		constexpr mastering_cell& set_size(vec2 sz) noexcept {
			stated_extent.width = {size_category::mastering, sz.x};
			stated_extent.height = {size_category::mastering, sz.y};

			return *this;
		}

		constexpr mastering_cell& set_size(float sz) noexcept {
			return set_size({sz, sz});
		}

		constexpr mastering_cell& set_end_line(bool lf = true) noexcept {
			end_line = lf;
			return *this;
		}

		constexpr mastering_cell& set_pad(align::spacing pad) noexcept {
			this->pad = pad;
			return *this;
		}

		constexpr mastering_cell& set_pad(float pad) noexcept {
			this->pad.set(pad);
			return *this;
		}

		constexpr mastering_cell& set_width(float sz) noexcept {
			stated_extent.width = {size_category::mastering, sz};

			return *this;
		}

		constexpr mastering_cell& set_height(float sz) noexcept {
			stated_extent.height = {size_category::mastering, sz};

			return *this;
		}

		constexpr mastering_cell& set_height_from_scale(float scale = 1.){
			if(stated_extent.width.type == size_category::scaling){
				throw illegal_layout{};
			}
			stated_extent.height.type = size_category::scaling;
			stated_extent.height.value = scale;
			return *this;
		}

		constexpr mastering_cell& set_width_from_scale(float scale = 1.){
			if(stated_extent.height.type == size_category::scaling){
				throw illegal_layout{};
			}
			stated_extent.width.type = size_category::scaling;
			stated_extent.width.value = scale;
			return *this;
		}

		constexpr mastering_cell& set_height_passive(float weight = 1.){
			stated_extent.height.type = size_category::passive;
			stated_extent.height.value = weight;
			return *this;
		}

		constexpr mastering_cell& set_width_passive(float weight = 1.){
			stated_extent.width.type = size_category::passive;
			stated_extent.width.value = weight;
			return *this;
		}

		constexpr mastering_cell& set_external_weight(vec2 weight) noexcept {
			if(weight.x > 0){
				stated_extent.width.type = size_category::pending;
				stated_extent.width.value = weight.x;
			}

			if(weight.y > 0){
				stated_extent.height.type = size_category::pending;
				stated_extent.height.value = weight.y;
			}

			return *this;
		}

		constexpr mastering_cell& set_external(math::vector2<bool> weight) noexcept {
			if(weight.x){
				stated_extent.width.type = size_category::pending;
				stated_extent.width.value = static_cast<float>(weight.x);
			}

			if(weight.y){
				stated_extent.height.type = size_category::pending;
				stated_extent.height.value = static_cast<float>(weight.y);
			}

			return *this;
		}

		constexpr mastering_cell& set_external() noexcept {
			return set_external({true, true});
		}

	};
}
