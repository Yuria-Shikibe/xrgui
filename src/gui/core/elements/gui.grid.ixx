module;

#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <gch/small_vector.hpp>
#endif

export module mo_yanxi.gui.elem.grid;

import mo_yanxi.gui.elem.celled_group;
import mo_yanxi.math;
import std;


#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <gch/small_vector.hpp>;
#endif

namespace mo_yanxi::gui{

export enum struct grid_extent_type : std::uint8_t{
	src_extent,
	src_dst,
	margin,
	dst_extent, //Reversed src_extent
};

enum struct fallback_strategy : std::uint8_t{
	shrink_or_hide,
	hide,
};



export
struct grid_capture_size{
	fallback_strategy fall{};
	grid_extent_type type{};
	std::uint16_t desc[2]{};
};

export
struct grid_cell : layout::basic_cell{
	math::vector2<grid_capture_size> extent{};

	grid_cell& set_extent(const math::vector2<grid_capture_size> extent) noexcept {
		this->extent = extent;
		return *this;
	}
};

enum struct grid_spec_type{
	uniformed_mastering,
	uniformed_passive,
	uniformed_scaling,
	all_mastering,
	all_passive,
	all_scaling,
	all_pending,
	mixed,
};

template <layout::size_category retTag>
	requires (retTag != layout::size_category::pending)
struct uniformed_extent{
	std::uint32_t extent{};
	float value{};
	align::padding1d<float> pad{};

	[[nodiscard]] std::uint32_t size() const noexcept{
		return extent;
	}

	[[nodiscard]] layout::stated_size load_to(std::span<layout::stated_size> target) const noexcept{
		assert(target.size() == size());

		std::ranges::fill(target, layout::stated_size{retTag, value});
		return layout::stated_size{retTag, extent * value};
	}

	layout::stated_size operator[](unsigned idx) const noexcept{
		assert(idx < extent);
		return layout::stated_size{retTag, value};
	}

	align::padding1d<float> pad_at(unsigned idx) const noexcept{
		return pad;
	}
};

template <typename T>
using container_t = mr::vector<T>;

struct variable_extent_entry{
	float value;
	align::padding1d<float> pad;
};

template <layout::size_category retTag>
	requires (retTag != layout::size_category::pending)
struct variable_extent{

	container_t<variable_extent_entry> value{};

	[[nodiscard]] std::uint32_t size() const noexcept{
		return value.size();
	}

	[[nodiscard]] layout::stated_size load_to(std::span<layout::stated_size> target) const noexcept{
		assert(target.size() == size());
		layout::stated_size rst{retTag, 0};
		std::ranges::transform(value, target.begin(), [&](const variable_extent_entry& e){
			rst.value += e.value;
			return layout::stated_size{retTag, e.value};
		});
		return rst;
	}

	layout::stated_size operator[](unsigned idx) const noexcept{
		return layout::stated_size{retTag, value[idx].value};
	}

	align::padding1d<float> pad_at(unsigned idx) const noexcept{
		return value[idx].pad;
	}
};

export using grid_uniformed_mastering = uniformed_extent<layout::size_category::mastering>;

//TODO uniformed passive is meaningless with the value field...
export struct grid_uniformed_passive{
	std::uint32_t extent{};
	align::padding1d<float> pad{};

	[[nodiscard]] std::uint32_t size() const noexcept{
		return extent;
	}

	[[nodiscard]] layout::stated_size load_to(std::span<layout::stated_size> target) const noexcept{
		assert(target.size() == size());

		std::ranges::fill(target, layout::stated_size{layout::size_category::passive, 1});
		return layout::stated_size{layout::size_category::passive, static_cast<float>(extent)};
	}

	layout::stated_size operator[](unsigned idx) const noexcept{
		assert(idx < extent);
		return layout::stated_size{layout::size_category::passive, 1};
	}

	align::padding1d<float> pad_at(unsigned idx) const noexcept{
		return pad;
	}
};


export using grid_uniformed_scaling = uniformed_extent<layout::size_category::scaling>;


export using grid_all_mastering = variable_extent<layout::size_category::mastering>;
export using grid_all_passive = variable_extent<layout::size_category::passive>;
export using grid_all_scaling = variable_extent<layout::size_category::scaling>;

export
struct grid_all_pending{
	std::uint32_t extent{};
	align::padding1d<float> pad{};

	[[nodiscard]] std::uint32_t size() const noexcept{
		return extent;
	}

	[[nodiscard]] layout::stated_size load_to(std::span<layout::stated_size> target) const noexcept{
		assert(target.size() == size());
		std::ranges::fill(target, layout::stated_size{layout::size_category::pending, 0});
		return layout::stated_size{layout::size_category::pending, 0};
	}

	layout::stated_size operator[](unsigned idx) const noexcept{
		assert(idx < extent);
		return layout::stated_size{layout::size_category::pending, 0};
	}

	[[nodiscard]] align::padding1d<float> pad_at(unsigned idx) const noexcept{
		return pad;
	}
};

export
struct grid_mixed{
	struct entry{
		layout::stated_size value;
		align::padding1d<float> pad;
	};
	container_t<entry> heads;

	[[nodiscard]] std::uint32_t size() const noexcept{
		return heads.size();
	}

	[[nodiscard]] layout::stated_size load_to(std::span<layout::stated_size> target) const noexcept{
		assert(target.size() == size());

		std::ranges::copy(heads | std::views::transform(&entry::value), target.data());
		layout::stated_size rst{layout::size_category::pending};
		for (const auto & stated_size : heads){
			if(rst.type == layout::size_category::pending){
				rst.type = stated_size.value.type;
				rst.value = stated_size.value.value;
				continue;
			}

			if(rst.type != stated_size.value.type)return layout::stated_size{layout::size_category::pending};
			rst.value += stated_size.value.value;
		}

		return rst;
	}


	layout::stated_size operator[](unsigned idx) const noexcept{
		return heads[idx].value;
	}


	[[nodiscard]] align::padding1d<float> pad_at(unsigned idx) const noexcept{
		return heads[idx].pad;
	}
};

export
struct grid_dim_spec{
	using variant_t = std::variant<
		grid_uniformed_mastering,
		grid_uniformed_passive,
		grid_uniformed_scaling,
		grid_all_mastering,
		grid_all_passive,
		grid_all_scaling,
		grid_all_pending,
		grid_mixed
	>;

	variant_t spec{};

	[[nodiscard]] grid_spec_type type() const noexcept{
		return static_cast<grid_spec_type>(spec.index());
	}

	[[nodiscard]] std::uint32_t size() const noexcept{
		return std::visit([](const auto& v){ return v.size(); }, spec);
	}

	[[nodiscard]] layout::stated_size load_to(std::span<layout::stated_size> target) const noexcept{
		return std::visit([&](const auto& v){ return v.load_to(target); }, spec);
	}

	layout::stated_size operator[](unsigned idx) const noexcept{
		return std::visit([idx](const auto& v){ return v[idx]; }, spec);
	}

	[[nodiscard]] align::padding1d<float> pad_at(unsigned idx) const noexcept{
		return std::visit([idx](const auto& v){ return v.pad_at(idx); }, spec);
	}

	bool operator==(const grid_dim_spec&) const noexcept = default;
};

using elem_extent = math::vector2<grid_capture_size>;

constexpr math::based_section<unsigned> find_largest_non_overlapping_section_1d(
	math::section<unsigned> self,
	math::section<unsigned> obstacle) noexcept{


	const auto left_len = (obstacle.from > self.from) ? (obstacle.from - self.from) : 0;


	const auto right_len = (self.to > obstacle.to) ? (self.to - obstacle.to) : 0;



	if(left_len >= right_len){
		return {self.from, left_len};
	} else{
		return {obstacle.to, right_len};
	}
}


struct cell_grid_state{
	math::rect_ortho<unsigned> ideal{};
	math::rect_ortho<unsigned> actual{};
	gch::small_vector<unsigned, 4, mr::heap_allocator<unsigned>> conflicted{};

	[[nodiscard]] cell_grid_state() = default;

	[[nodiscard]] cell_grid_state(const math::rect_ortho<unsigned>& ideal,
		const mr::heap_allocator<unsigned>& alloc)
	: ideal(ideal),
	actual(ideal),
	conflicted(alloc){
	}
};

void generate_layouts(
	mr::heap_vector<cell_grid_state>& storage,
	std::span<const cell_adaptor<grid_cell>> elements,
	const math::usize2 grid_size
){
	storage.resize(std::ranges::size(elements));
	std::ranges::transform(
		elements, storage.begin(), [&](const  cell_adaptor<grid_cell>& elem){
			static constexpr auto calculate_span = [](
				const grid_capture_size& cap,
				unsigned dim_size) -> math::based_section<unsigned>{
				unsigned start = 0, length = 0;
				switch(cap.type){
				case grid_extent_type::src_extent : start = cap.desc[0];
					length = cap.desc[1];
					break;
				case grid_extent_type::src_dst : start = cap.desc[0];
					length = (cap.desc[1] > cap.desc[0]) ? (cap.desc[1] - cap.desc[0]) : 0;
					break;
				case grid_extent_type::margin : start = cap.desc[0];
					if(dim_size > static_cast<unsigned>(cap.desc[0] + cap.desc[1])){
						length = dim_size - cap.desc[0] - cap.desc[1];
					}
					break;
				case grid_extent_type::dst_extent : length = cap.desc[1];
					if(dim_size > static_cast<unsigned>(cap.desc[0] + cap.desc[1])){
						start = dim_size - cap.desc[0] - cap.desc[1];
					}
					break;
				}

				if(start > dim_size) start = dim_size;
				if(start + length > dim_size){
					length = dim_size - start;
				}
				return {start, length};
			};

			auto [x_start, x_len] = calculate_span(elem.cell.extent.x, grid_size.x);
			auto [y_start, y_len] = calculate_span(elem.cell.extent.y, grid_size.y);

			return cell_grid_state{
					math::rect_ortho{tags::unchecked, x_start, y_start, x_len, y_len}, storage.get_allocator()
				};
		});

	const auto beg = storage.begin();
	const auto end = storage.end();
	for(auto cur_itr = beg; cur_itr != end; ++cur_itr){
		auto& current = *cur_itr;
		auto& current_rect = current.actual;

		if(current_rect.is_zero_area()){
			continue;
		}

		const elem_extent& elem_spec = elements[std::ranges::distance(beg, cur_itr)].cell.extent;
		for(auto obstacle_itr = beg; obstacle_itr != cur_itr; ++obstacle_itr){
			const auto& placed_rect = obstacle_itr->actual;

			if(current_rect.overlap_exclusive(placed_rect)){

				const bool x_overlaps = current_rect.get_src_x() < placed_rect.get_end_x() && current_rect.get_end_x() >
					placed_rect.get_src_x();
				const bool y_overlaps = current_rect.get_src_y() < placed_rect.get_end_y() && current_rect.get_end_y() >
					placed_rect.get_src_y();


				if((x_overlaps && elem_spec.x.fall == fallback_strategy::hide) ||
					(y_overlaps && elem_spec.y.fall == fallback_strategy::hide)){
					current_rect.set_size(0, 0);
					break;
					}

				if(x_overlaps && !y_overlaps){

					auto [new_x, new_w] = find_largest_non_overlapping_section_1d(
						{current_rect.get_src_x(), current_rect.get_end_x()},
						{placed_rect.get_src_x(), placed_rect.get_end_x()}
					);
					current_rect.src.x = new_x;
					current_rect.set_width(new_w);
				} else if(y_overlaps && !x_overlaps){

					auto [new_y, new_h] = find_largest_non_overlapping_section_1d(
						{current_rect.get_src_y(), current_rect.get_end_y()},
						{placed_rect.get_src_y(), placed_rect.get_end_y()}
					);
					current_rect.src.y = new_y;
					current_rect.set_height(new_h);
				} else{

					current_rect = math::rect::find_largest_non_edge_exclusive_intersecting_rect<true>(
						current_rect, placed_rect);
				}

				if(current_rect.is_zero_area()){
					break;
				}
			}
		}
	}
}

export
struct grid : universal_group<grid_cell>{
private:
	math::vector2<grid_dim_spec> extent_spec_{};
	mr::heap_vector<float> grid_table_head_{get_heap_allocator<float>()};

	layout::expand_policy expand_policy_{};

	mr::heap_vector<cell_grid_state> cell_layout_state_{};
	bool need_relayout_head_{true};

public:
	//TODO entire align when there are spare space

	[[nodiscard]] grid(scene& scene, elem* parent)
	: universal_group<grid_cell>(scene, parent){

	}

	[[nodiscard]] grid(scene& scene, elem* parent, math::vector2<grid_dim_spec>&& extent_spec)
	: universal_group(scene, parent), extent_spec_(std::move(extent_spec)){}

	void set_extent_spec(math::vector2<grid_dim_spec>&& extent_spec){
		extent_spec_ = std::move(extent_spec);
		need_relayout_head_ = true;
		cell_layout_state_.clear();
		grid_table_head_.clear();
		notify_isolated_layout_changed();
	}

	[[nodiscard]] math::usize2 grid_extent() const noexcept{
		return {extent_spec_.x.size(), extent_spec_.y.size()};
	}

	[[nodiscard]] layout::stated_extent grid_head_at(const unsigned x, const unsigned y) const noexcept{
		return {extent_spec_.x[x], extent_spec_.y[y]};
	}

	void on_element_add(adaptor_type& adaptor) override{
		generate_layouts(cell_layout_state_, cells_, {extent_spec_.x.size(), extent_spec_.y.size()});

		need_relayout_head_ = true;
		notify_isolated_layout_changed();

		universal_group::on_element_add(adaptor);
	}

	void clear() noexcept override {
		universal_group::clear();
		need_relayout_head_ = true;
	}

	void erase_afterward(std::size_t where) override{
		universal_group::erase_afterward(where);
		need_relayout_head_ = true;
	}

	void erase_instantly(std::size_t where) override{
		universal_group::erase_instantly(where);
		need_relayout_head_ = true;
	}

	void layout_elem() override{
		math::vec2 target_extent = extent();

		if(expand_policy_ != layout::expand_policy::passive){
			math::vec2 calc_extent = target_extent;

			if(need_relayout_head_){
				
				auto rst = update_table_head_size(content_extent());
				need_relayout_head_ = true; 
				calc_extent = rst.mastering_size + boarder_extent();

				
				if(rst.has_passive_x) calc_extent.x = std::max(calc_extent.x, target_extent.x);
				if(rst.has_passive_y) calc_extent.y = std::max(calc_extent.y, target_extent.y);
			}

			
			if(expand_policy_ == layout::expand_policy::prefer){
				if(auto ext = get_prefer_extent()){
					calc_extent.max(ext.value());
				}
			}

			resize(calc_extent, propagate_mask::force_upper);
		}

		
		if(need_relayout_head_){
			update_table_head_size(content_extent());
		}

		place_cells();
		universal_group::layout_elem();
	}

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void set_expand_policy(const layout::expand_policy expand_policy){
		if(util::try_modify(expand_policy_, expand_policy)){
			notify_isolated_layout_changed();
		}
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		if(expand_policy_ == layout::expand_policy::passive) return std::nullopt;

		math::vec2 req = content_extent();

		if(need_relayout_head_){
			auto rst = update_table_head_size(extent.potential_extent().inf_to0());
			need_relayout_head_ = true; 
			req = rst.mastering_size;
		}

		if(expand_policy_ == layout::expand_policy::prefer){
			if(auto ext = get_prefer_content_extent()){
				req.max(ext.value());
			}
		}

		return req;
	}

private:
	bool resize_impl(const math::vec2 size) override{
		if(universal_group::resize_impl(size)){
			need_relayout_head_ = true;
			return true;
		}
		return false;
	}

	void place_cells(){
		if(cells_.empty() || grid_table_head_.empty())return;
		const auto columns = extent_spec_.x.size();
		const auto rows = extent_spec_.y.size();
		if(columns == 0 || rows == 0)return;
		mr::vector<float> offset(columns + 1 + rows + 1);
		const auto col_extent_span = std::span{offset}.first(columns + 1);
		const auto row_extent_span = std::span{offset}.last(rows + 1);

		for(std::uint32_t col = 0; col < columns; ++col){
			col_extent_span[col + 1] = col_extent_span[col] + grid_table_head_[col];
		}

		for(std::uint32_t row = 0; row < rows; ++row){
			row_extent_span[row + 1] = row_extent_span[row] + grid_table_head_[row + columns];
		}

		for(std::uint32_t i = 0; i < cells_.size(); ++i){
			auto& cell = cells_[i];
			auto& state = cell_layout_state_[i];

			
			const float x_pre = (state.actual.get_src_x() == 0) ? 0.0f :
				extent_spec_.x.pad_at(state.actual.get_src_x()).pre;
			const float y_pre = (state.actual.get_src_y() == 0) ? 0.0f :
				extent_spec_.y.pad_at(state.actual.get_src_y()).pre;

			
			
			
			const float x_post = (state.actual.get_end_x() == columns) ? 0.0f :
				extent_spec_.x.pad_at(state.actual.get_end_x() - 1).post;
			const float y_post = (state.actual.get_end_y() == rows) ? 0.0f :
				extent_spec_.y.pad_at(state.actual.get_end_y() - 1).post;

			const math::vec2 src{
				col_extent_span[state.actual.get_src_x()] + x_pre,
				row_extent_span[state.actual.get_src_y()] + y_pre
			};
			const math::vec2 dst{
				col_extent_span[state.actual.get_end_x()] - x_post,
				row_extent_span[state.actual.get_end_y()] - y_post
			};

			if(state.actual.is_zero_area()){
				//TODO change display state here?
				//TODO move this to update?
				cell.element->invisible = true;
			}

			const auto ext = dst.copy().fdim(src);
			cell.cell.allocated_region = rect{tags::unchecked, tags::from_extent, src, ext};
			cell.apply(*this, cell.cell.allocated_region.extent());
			if(!is_pos_smooth())cell.cell.update_relative_src(*cell.element, content_src_pos_abs());
		}
	}

	struct table_layout_result{
		math::vec2 mastering_size;
		bool has_passive_x;
		bool has_passive_y;
	};

	table_layout_result update_table_head_size(
		math::vec2 valid_extent
	){
		need_relayout_head_ = false;
		const auto columns = extent_spec_.x.size();
		const auto rows = extent_spec_.y.size();
		if(columns == 0 || rows == 0){
			grid_table_head_.clear();
			return {{}, false, false};
		}
		mr::vector<layout::stated_size> table_head{columns + rows};
		const auto col_span = std::span{table_head}.first(columns);
		const auto row_span = std::span{table_head}.last(rows);

		auto w = extent_spec_.x.load_to(col_span);
		auto h = extent_spec_.y.load_to(row_span);

		struct collapse_result{
			float mastering;
			float passive;
		};

		if(w.type == layout::size_category::scaling && h.type == layout::size_category::scaling){
			throw layout::illegal_layout{};
		}

		static constexpr auto collapse_scaling_by_master = [](
						float ratio,
						layout::stated_size major_sum, layout::stated_size minor_sum,
						std::span<layout::stated_size> major_span, std::span<layout::stated_size> minor_span) -> collapse_result {
			switch(major_sum.type){
			case layout::size_category::mastering:
				return {major_sum.value, 0};
			case layout::size_category::passive:
				return {0, major_sum.value};
			case layout::size_category::scaling: [[fallthrough]];
			case layout::size_category::pending:{
				collapse_result result{};
				for(auto&& sz : major_span){
					if(sz.type == layout::size_category::scaling){
						if(minor_sum.type == layout::size_category::scaling){
							throw layout::illegal_layout{};
						}

						auto rst = sz;
						for(auto tgt : minor_span){
							switch(tgt.type){
							case layout::size_category::mastering:{
								tgt.value *= sz.value;
								rst.try_promote_by(tgt);
								break;
							}
							case layout::size_category::passive:{
								tgt.value *= sz.value * ratio;
								if(!rst.mastering()){
									rst.value = std::max(rst.value, tgt.value);
									rst.type = layout::size_category::passive;
								}
								break;
							}
							default: break;
							}
						}

						sz = rst;
					}

				switch(sz.type){
				case layout::size_category::mastering:
					result.mastering += sz.value;
					break;
				case layout::size_category::passive:
					result.passive += sz.value;
					break;
				case layout::size_category::pending: break;
				default: std::unreachable();
				}
				}
				return result;
			}

			default: std::unreachable();
			}
		};

		const math::vector2 result{
			collapse_scaling_by_master(valid_extent.x / valid_extent.y, w, h, col_span, row_span),
			collapse_scaling_by_master(valid_extent.y / valid_extent.x, h, w, row_span, col_span)
		};

		math::vec2 mastering_size = {result.x.mastering, result.y.mastering};
		for(std::uint32_t i = 0; i < columns; ++i){
			mastering_size.x += extent_spec_.x.pad_at(i).length();
		}
		for(std::uint32_t i = 0; i < rows; ++i){
			mastering_size.y += extent_spec_.y.pad_at(i).length();
		}

		const auto cPrev = extent_spec_.x.pad_at(0).pre;
		const auto cPost = extent_spec_.x.pad_at(columns - 1).post;
		const auto rPrev = extent_spec_.y.pad_at(0).pre;
		const auto rPost = extent_spec_.y.pad_at(rows - 1).post;
		mastering_size.x -= cPrev + cPost;
		mastering_size.y -= rPrev + rPost;

		grid_table_head_.resize(table_head.size());

		// Resolve pending columns (content-driven widths) before passive distribution
		mr::vector<float> pending_col_content_widths(columns, 0.f);
		float pending_cols_total_content_width = 0;
		for(std::uint32_t col = 0; col < columns; ++col){
			if(!col_span[col].pending()) continue;
			float max_width = 0;
			for(std::uint32_t ci = 0; ci < cells_.size(); ++ci){
				auto& state = cell_layout_state_[ci];
				if(state.actual.get_src_x() <= col && col < state.actual.get_end_x()){
					if(auto sz = cells_[ci].element->pre_acquire_size({layout::pending_size, layout::pending_size})){
						max_width = std::max(max_width, sz->x);
					}
				}
			}
			pending_col_content_widths[col] = max_width;
			pending_cols_total_content_width += max_width;
		}
		mastering_size.x += pending_cols_total_content_width;

		auto passives_usable = valid_extent.copy().fdim(mastering_size);
		auto passive_unit = (passives_usable / math::vec2{result.x.passive, result.y.passive}).nan_to0();

		for(std::uint32_t i = 0; i < columns; ++i){
			if(col_span[i].pending()){
				grid_table_head_[i] = pending_col_content_widths[i] + extent_spec_.x.pad_at(i).length();
			} else{
				grid_table_head_[i] = (col_span[i].mastering() ? col_span[i].value : col_span[i].value * passive_unit.x) + extent_spec_.x.pad_at(i).length();
			}
		}
		grid_table_head_.front() -= cPrev;
		grid_table_head_[columns - 1] -= cPost;

		for(std::uint32_t i = 0; i < rows; ++i){
			if(row_span[i].pending()){
				grid_table_head_[columns + i] = extent_spec_.y.pad_at(i).length();
			} else{
				grid_table_head_[columns + i] = (row_span[i].mastering() ? row_span[i].value : row_span[i].value * passive_unit.y) + extent_spec_.y.pad_at(i).length();
			}
		}
		grid_table_head_[columns] -= rPrev;
		grid_table_head_.back() -= rPost;

		// Resolve pending rows
		float pending_rows_total_content_height = 0;
		for(std::uint32_t row = 0; row < rows; ++row){
			if(!row_span[row].pending()) continue;
			float pending_height = 0;
			for(std::uint32_t ci = 0; ci < cells_.size(); ++ci){
				auto& state = cell_layout_state_[ci];
				if(state.actual.get_src_y() <= row && row < state.actual.get_end_y()){
					float known_width = 0;
					for(std::uint32_t c = state.actual.get_src_x(); c < state.actual.get_end_x(); ++c){
						known_width += grid_table_head_[c];
					}
					if(auto sz = cells_[ci].element->pre_acquire_size({known_width, layout::pending_size})){
						float confirmed_height = 0;
						for(std::uint32_t r = state.actual.get_src_y(); r < state.actual.get_end_y(); ++r){
							if(r == row) continue;
							confirmed_height += grid_table_head_[columns + r];
						}
						float overflow = std::max(0.f, sz->y - confirmed_height);
						pending_height = std::max(pending_height, overflow);
					}
				}
			}
			grid_table_head_[columns + row] = pending_height + extent_spec_.y.pad_at(row).length();
			pending_rows_total_content_height += pending_height;
		}
		if(row_span[0].pending()){
			grid_table_head_[columns] -= rPrev;
		}
		if(row_span[rows - 1].pending()){
			grid_table_head_.back() -= rPost;
		}
		mastering_size.y += pending_rows_total_content_height;

		return {mastering_size, result.x.passive > 0.0f, result.y.passive > 0.0f};
	}
};
}
