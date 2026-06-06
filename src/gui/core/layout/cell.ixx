//

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
	/**
	 * @brief Result yielded by celled container creation helpers.
	 *
	 * A `create_handle<Elem, Cell>` exposes both the newly inserted element and
	 * its container-owned cell metadata. The cell reference is only valid while
	 * the element remains in the same container slot.
	 */
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
	/**
	 * @brief Base metadata shared by every container cell.
	 *
	 * A cell is not an element. It belongs to the parent container and describes
	 * how that parent should allocate, scale, margin, align, and place the child
	 * element during layout.
	 */
	struct basic_cell{
		/**
		 * @brief Parent-content-relative rectangle assigned to the child.
		 */
		rect allocated_region{};

		/**
		 * @brief Alignment inside `allocated_region` when the child is smaller
		 * than the cell's margin-clipped extent.
		 */
		align::pos unsaturate_cell_elem_align{align::pos::center};

		/**
		 * @brief Per-cell scaling multiplied into the child's context scaling.
		 */
		vec2 scaling{1.f, 1.f};

		/**
		 * @brief Space removed from `allocated_region` before the child is placed.
		 */
		align::spacing margin{};





		constexpr vec2 get_relative_src(vec2 actual_extent) const noexcept{
			return allocated_region.get_src() + margin.top_lft() + align::get_offset_of(unsaturate_cell_elem_align, actual_extent, rect{tags::unchecked, allocated_region.extent().fdim(margin.extent())});
		}

		void apply_to(
			const elem& group,
			elem& elem,
			optional_mastering_extent cell_expected_restriction_extent
			)const{
			elem.set_scaling(group.get_scaling() * scaling);
			const auto extent = allocated_region.extent().fdim(margin.extent());

			// Finite axes are bounded by this allocated cell; their numeric values are
			// replaced with the margin-clipped allocated extent. Pending axes are
			// preserved so the child may resolve that direction from content and
			// propagate the requirement upward.
			if(!cell_expected_restriction_extent.width_pending()){
				cell_expected_restriction_extent.set_width(extent.x);
			}

			if(!cell_expected_restriction_extent.height_pending()){
				cell_expected_restriction_extent.set_height(extent.y);
			}

		elem.restriction_extent = cell_expected_restriction_extent;
		elem.resize(extent, propagate_mask::lower);
		elem.set_prefer_extent(extent);
		elem.try_layout();
		}

		bool update_relative_src(elem& elem, math::vec2 parent_content_src) const{
			const auto r1 = elem.set_rel_pos(get_relative_src(elem.extent()));
			const auto r2 = elem.update_abs_src(parent_content_src);
			return r1 || r2;
		}

		bool update_relative_src(elem& elem, math::vec2 parent_content_src, float lerp_alpha) const{
			const auto r1 = elem.set_rel_pos(get_relative_src(elem.extent()), lerp_alpha);
			const auto r2 = elem.update_abs_src(parent_content_src);
			return r1 || r2;
		}

	};

	export
	/**
	 * @brief Cell used by `scaling_stack`.
	 *
	 * `region_scale` is normalized to the parent content extent. Its source and
	 * destination describe the child's region before `region_align` adjusts that
	 * region inside the parent.
	 */
	struct scaled_cell : basic_cell{
		rect region_scale{};
		/**
		 * @brief Alignment applied to the scaled region inside the parent.
		 */
		align::pos region_align{align::pos::center};
	};

	export
	/**
	 * @brief One-axis cell used by `sequence` and `overflow_sequence`.
	 *
	 * The container's layout policy selects which physical axis this size
	 * controls. The other axis is normally the full available parent-content
	 * extent.
	 */
	struct partial_mastering_cell : basic_cell{
		/**
		 * @brief Declared minor-axis size: fixed, passive, scaling, or pending.
		 */
		stated_size stated_size{size_category::passive, 1};

		/**
		 * @brief Padding before and after this child on the sequence advance axis.
		 */
		align::padding1d<float> pad{};

		/**
		 * @brief Set the declared one-axis size directly.
		 */
		auto& set_size(const layout::stated_size stated_size){
			this->stated_size = stated_size;
			return *this;
		}

		/**
		 * @brief Use a fixed pixel size on the sequence advance axis.
		 */
		auto& set_size(const float size){
			this->stated_size = {size_category::mastering, size};
			return *this;
		}

		/**
		 * @brief Ask the child for its content size on the sequence advance axis.
		 */
		auto& set_pending(){
			this->stated_size = {size_category::pending, 1};
			return *this;
		}

		/**
		 * @brief Share remaining sequence space by `weight`.
		 */
		auto& set_passive(float weight = 1.f){
			this->stated_size = {size_category::passive, weight};
			return *this;
		}

		/**
		 * @brief Compute this axis from the other axis by `ratio`.
		 */
		auto& set_from_ratio(float ratio = 1){
			this->stated_size = {size_category::scaling, ratio};
			return *this;
		}

		/**
		 * @brief Set padding before and after the child along the advance axis.
		 */
		auto& set_pad(const align::padding1d<float> pad){
			this->pad = pad;
			return *this;
		}
	};

	export
	/**
	 * @brief Two-axis cell used by `table`.
	 *
	 * Each axis can be fixed, passive, scaling, or pending. `end_line` splits
	 * rows/columns, and `saturate` lets a single cell consume the whole current
	 * line when the table layout can provide that extra space.
	 */
	struct mastering_cell : basic_cell{
		stated_extent stated_extent{};
		align::spacing pad{};
		/**
		 * @brief Align this cell within the row/column slot when the declared
		 * extent is smaller than the table head extent.
		 *
		 * `align::pos::none` forces the cell to use the full head extent.
		 */
		align::pos unsaturate_cell_align{align::pos::center};
		bool end_line{};

		/**
		 * @brief Indicate that the cell should grow to the whole line space.
		 */
		bool saturate{};

		/**
		 * @brief Set both axes to fixed pixel sizes.
		 */
		constexpr mastering_cell& set_size(vec2 sz) noexcept {
			stated_extent.width = {size_category::mastering, sz.x};
			stated_extent.height = {size_category::mastering, sz.y};

			return *this;
		}

		constexpr mastering_cell& set_size(float sz) noexcept {
			return set_size({sz, sz});
		}

		/**
		 * @brief Mark this cell as the last cell of the current table line.
		 */
		constexpr mastering_cell& set_end_line(bool lf = true) noexcept {
			end_line = lf;
			return *this;
		}

		/**
		 * @brief Set per-cell table padding.
		 */
		constexpr mastering_cell& set_pad(align::spacing pad) noexcept {
			this->pad = pad;
			return *this;
		}

		constexpr mastering_cell& set_pad(float pad) noexcept {
			this->pad.set(pad);
			return *this;
		}

		/**
		 * @brief Set the width to a fixed pixel value.
		 */
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

		constexpr mastering_cell& set_pending_weight(vec2 weight) noexcept {
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

		constexpr mastering_cell& set_pending(math::vector2<bool> weight) noexcept {
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

		constexpr mastering_cell& set_pending() noexcept {
			return set_pending({true, true});
		}

	};
}
