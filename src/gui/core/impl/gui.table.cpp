module;

#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <gch/small_vector.hpp>
#endif

module mo_yanxi.gui.elem.table;


#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <gch/small_vector.hpp>;
#endif

namespace mo_yanxi::gui{

table_layout_context::pre_layout_result table_layout_context::layout_masters(
	const std::span<const cell_adaptor_type> cells, math::vec2 scaling) noexcept{

	const auto [
		pad_major_src, pad_major_dst,
		pad_minor_src, pad_minor_dst
	] = get_pad_ptr(policy_);

	const auto [extent_major, extent_minor] = get_extent_ptr(policy_);


	std::uint32_t cell_idx = 0;
	for (std::uint32_t idx_minor = 0; idx_minor < row_counts_.size(); ++idx_minor){
		std::uint32_t row_len = row_counts_[idx_minor];
		auto line = cells.subspan(cell_idx, row_len);

		for (std::uint32_t idx_major = 0; idx_major < row_len; ++idx_major){
			const auto& elem = line[idx_major];
			auto [head_major, head_minor] = (*this)[idx_major, idx_minor];

			head_major.max_pad_src = math::max(head_major.max_pad_src, elem.cell.pad.*pad_major_src);
			head_major.max_pad_dst = math::max(head_major.max_pad_dst, elem.cell.pad.*pad_major_dst);

			head_minor.max_pad_src = math::max(head_minor.max_pad_src, elem.cell.pad.*pad_minor_src);
			head_minor.max_pad_dst = math::max(head_minor.max_pad_dst, elem.cell.pad.*pad_minor_dst);

			auto promoted = elem.cell.stated_extent.promote();
			promoted.try_scl(scaling);

			head_major.max_size.try_promote_by((promoted.*extent_major).decay());
			if((promoted.*extent_minor).mastering()){
				head_minor.max_size.try_promote_by(promoted.*extent_minor);
			}
		}
		cell_idx += row_len;
	}

	math::vec2 masterings_captured{};
	math::vector2<table_size_t> known_masterings{};

	{
		const auto [major_target, minor_target] = get_vec_ptr<>(policy_);
		masterings_captured.*major_target = std::ranges::fold_left(get_majors() | std::views::transform(&table_head::get_captured_size), 0.f, std::plus{});
		masterings_captured.*minor_target = std::ranges::fold_left(get_minors() | std::views::transform(&table_head::get_captured_size), 0.f, std::plus{});
	}

	{
		const auto [major_target, minor_target] = get_vec_ptr<table_size_t>(policy_);
		known_masterings.*major_target = std::ranges::count_if(get_majors(), &table_head::mastering);
		known_masterings.*minor_target = std::ranges::count_if(get_minors(), &table_head::mastering);
	}

	return {masterings_captured, known_masterings};
}

math::vec2 table_layout_context::restricted_allocate_pendings(const std::span<const cell_adaptor_type> cells,
                                                              math::vec2 valid_extent, pre_layout_result pre_result){
	math::vec2 passive_usable_extent = valid_extent - pre_result.captured_extent;

	const auto [extent_major, extent_minor] = get_extent_ptr(policy_);
	const auto [major_target, minor_target] = get_vec_ptr<>(policy_);


	struct extent_cache {
		layout::stated_extent request;
		math::vec2 response;
	};
	std::vector<gch::small_vector<extent_cache, 2>> size_cache(cells.size());

	auto get_cached_pre_acquire = [&](std::uint32_t flat_idx, const cell_adaptor_type& elem, const layout::stated_extent& ext) -> std::optional<math::vec2> {
		auto& caches = size_cache[flat_idx];
		for (const auto& c : caches) {
			if (c.request.width == ext.width && c.request.height == ext.height) {
				return c.response;
			}
		}
		if (auto res = elem.element->pre_acquire_size(ext)) {
			caches.push_back({ext, *res});
			return res;
		}
		return std::nullopt;
	};


	{
		bool single_line = max_minor_size() == 1 && std::isfinite(valid_extent.*minor_target);

		constexpr float layout_epsilon = .01f;

		if(passive_usable_extent.*major_target <= layout_epsilon){
			passive_usable_extent.*major_target = 0;
		} else {
			gch::small_vector<float, 8> remain_major_pending_sizes;
			if(!layout::is_size_pending(passive_usable_extent.*major_target)){
				remain_major_pending_sizes.resize(max_major_size());
			}

			std::uint32_t cell_idx = 0;
			for(std::uint32_t idx_minor = 0; idx_minor < row_counts_.size(); ++idx_minor){
				std::uint32_t row_len = row_counts_[idx_minor];
				auto line = cells.subspan(cell_idx, row_len);
				auto& head_minor = at_minor(idx_minor);

				layout::stated_extent line_pending_extent;
				line_pending_extent.*extent_major = {layout::size_category::pending};

				if(head_minor.max_size.mastering()){
					line_pending_extent.*extent_minor = head_minor.max_size;
				} else{
					if(single_line){
						line_pending_extent.*extent_minor = layout::stated_size{
								layout::size_category::mastering, valid_extent.*minor_target
							};
					} else{
						line_pending_extent.*extent_minor = layout::stated_size{layout::size_category::pending};
					}
				}

				for(std::uint32_t idx_major = 0; idx_major < row_len; ++idx_major){
					const auto& elem = line[idx_major];
					auto& head_major = at_major(idx_major);

					if((elem.cell.stated_extent.*extent_major).pending()){
						if(auto size = get_cached_pre_acquire(cell_idx + idx_major, elem, line_pending_extent)){
							if(layout::is_size_pending(passive_usable_extent.*major_target)){
								head_major.max_size.try_promote_by(size.value().*major_target);
							} else{
								remain_major_pending_sizes[idx_major] = std::max(
									size.value().*major_target, remain_major_pending_sizes[idx_major]);
							}

							head_minor.max_size.try_promote_by(
								single_line
									? passive_usable_extent.*minor_target
									: math::min(size.value().*minor_target, passive_usable_extent.*minor_target)
							);
						}
					}
				}
				cell_idx += row_len;
			}

			if(layout::is_size_pending(passive_usable_extent.*major_target)){
				passive_usable_extent.*major_target = 0;
			} else {
				if(float major_sum{std::ranges::fold_left(remain_major_pending_sizes, 0.f, std::plus{})}; major_sum > 0){
					if(const auto ratio = passive_usable_extent.*major_target / major_sum; ratio < 1){
						for(auto [midx, new_] : remain_major_pending_sizes | std::views::enumerate){
							if(new_ > 0) at_major(midx).max_size = {layout::size_category::mastering, new_ * ratio};
						}
						passive_usable_extent.*major_target = 0;
					} else{
						for(auto [midx, new_] : remain_major_pending_sizes | std::views::enumerate){
							if(new_ > 0) at_major(midx).max_size = {layout::size_category::mastering, new_};
						}
						passive_usable_extent.*major_target -= major_sum;
					}
				}
			}
		}
	}

	float total_passives_weight{};
	for(auto& major : get_majors()){
		if(!major.max_size.mastering()){
			assert(major.max_size.type == layout::size_category::passive);
			total_passives_weight += major.max_size.value;
			major.max_size.value *= passive_usable_extent.*major_target;
		}
	}


	float total_minor_weight{};
	std::uint32_t cell_idx = 0;

	for(std::uint32_t idx_minor = 0; idx_minor < row_counts_.size(); ++idx_minor){
		std::uint32_t row_len = row_counts_[idx_minor];
		auto line = cells.subspan(cell_idx, row_len);
		auto& head_minor = at_minor(idx_minor);

		float line_minor_size{};
		line_minor_request line_pending_minor_request{};
		for(std::uint32_t idx_major = 0; idx_major < row_len; ++idx_major){
			const auto& elem = line[idx_major];
			auto& head_major = at_major(idx_major);

			if(!head_major.mastering()){
				head_major.max_size.try_promote_by(head_major.max_size.value / total_passives_weight);
			}

			if(!head_minor.mastering()){
				switch((elem.cell.stated_extent.*extent_minor).type){
				case layout::size_category::scaling :{
					float valid = math::min(head_major.max_size * head_minor.max_size.value,
					                        passive_usable_extent.*minor_target);
					line_pending_minor_request.include(valid);
					line_minor_size = math::max(line_minor_size, valid);
					break;
				}
				case layout::size_category::pending :{
					layout::stated_extent ext;
					ext.*extent_major = head_major.max_size;
					ext.*extent_minor = {layout::size_category::pending};

					if(auto size = get_cached_pre_acquire(cell_idx + idx_major, elem, ext)){
						float valid = math::min(size.value().*minor_target, passive_usable_extent.*minor_target);
						line_pending_minor_request.include(valid);
						line_minor_size = math::max(line_minor_size, valid);
					}
					break;
				}
				default : break;
				}
			}
		}

		if(!head_minor.mastering() && line_pending_minor_request.max_pending_size > 0.f){
			head_minor.max_size.try_promote_by(line_pending_minor_request.max_pending_size);
		}

		passive_usable_extent.*minor_target = std::fdim(passive_usable_extent.*minor_target, line_minor_size);
		line_minor_size = 0.;

		if(!head_minor.mastering()){
			total_minor_weight += head_minor.max_size.value;
		}
		cell_idx += row_len;
	}

	math::vec2 extent{};

	for(const auto& table_head : get_majors()){
		extent.*major_target += table_head.get_captured_size();
	}

	for(auto& table_head : get_minors()){
		if(!table_head.max_size.mastering()){
			table_head.max_size.try_promote_by(
				table_head.max_size.value / total_minor_weight * passive_usable_extent.copy().inf_to0().*minor_target);
		}
		extent.*minor_target += table_head.get_captured_size();
	}

	return extent;
}

math::vec2 table_layout_context::allocate_cells(
	std::span<const cell_adaptor_type> cells,
	math::vec2 valid_size,
	math::vec2 scaling){
	return restricted_allocate_pendings(cells, valid_size, layout_masters(cells, scaling));
}

void table_layout_context::place_cells(const std::span<cell_adaptor_type> cells, table& parent, math::frect region){
	const auto [extent_major, extent_minor] = get_extent_ptr(policy_);
	const auto [major_target, minor_target] = get_vec_ptr<>(policy_);

	const auto align_pos_major_mask = policy_ == layout::layout_policy::hori_major ? align::pos::mask_x : align::pos::mask_y;
	const auto align_pos_minor_mask = policy_ == layout::layout_policy::hori_major ? align::pos::mask_y : align::pos::mask_x;

	auto scaling = parent.get_scaling();
	math::vec2 current_position{};

	std::uint32_t cell_idx = 0;
	for (std::uint32_t idx_minor = 0; idx_minor < row_counts_.size(); ++idx_minor){
		std::uint32_t row_len = row_counts_[idx_minor];
		auto line = cells.subspan(cell_idx, row_len);
		float line_stride{};

		for (std::uint32_t idx_major = 0; idx_major < row_len; ++idx_major){
			auto& elem = line[idx_major];
			auto [head_major, head_minor] = (*this)[idx_major, idx_minor];
			math::vec2 src_off;
			src_off.*major_target = head_major.max_pad_src;
			src_off.*minor_target = head_minor.max_pad_src;

			math::vec2 cell_maximum_size;
			cell_maximum_size.*major_target = head_major.max_size;
			cell_maximum_size.*minor_target = head_minor.max_size;

			math::vec2 dst_off;
			dst_off.*major_target = head_major.max_pad_dst;
			dst_off.*minor_target = head_minor.max_pad_dst;

			if(elem.cell.saturate && row_len == 1){
				cell_maximum_size.*major_target = region.extent().*major_target - (src_off.*major_target + dst_off.*major_target);
			}

			layout::stated_extent ext;
			ext.*extent_major = head_major.max_size;
			ext.*extent_minor = head_minor.max_size;

			if((elem.cell.stated_extent.*extent_minor).pending()){
				ext.*extent_minor = {layout::size_category::pending};
			}

			if((elem.cell.stated_extent.*extent_major).pending() && !elem.cell.saturate){
				ext.*extent_major = {layout::size_category::pending};
			}

			{
				auto cell_actuall_size = cell_maximum_size;

				const bool major_align_none = (elem.cell.unsaturate_cell_align & align_pos_major_mask) == align::pos{};
				const bool minor_align_none = (elem.cell.unsaturate_cell_align & align_pos_minor_mask) == align::pos{};


				if(elem.cell.saturate && row_len == 1 && !(elem.cell.stated_extent.*extent_major).mastering()){
					cell_actuall_size.*major_target *= scaling.*major_target * std::clamp((elem.cell.stated_extent.*extent_major).value, 0.f, 1.f);
				}else if((elem.cell.stated_extent.*extent_major).mastering() && !major_align_none){
					cell_actuall_size.*major_target = scaling.*major_target * (elem.cell.stated_extent.*extent_major).value;
				}

				if((elem.cell.stated_extent.*extent_minor).mastering() && !minor_align_none){
					cell_actuall_size.*minor_target = scaling.*minor_target * (elem.cell.stated_extent.*extent_minor).value;
				}

				elem.cell.allocated_region.src =
					current_position + src_off + region.get_src() +
						align::get_offset_of(elem.cell.unsaturate_cell_align, cell_actuall_size, rect{cell_maximum_size});
				elem.cell.allocated_region.set_size(cell_actuall_size);
				elem.apply(parent, ext);
				if(!parent.is_pos_smooth())elem.cell.update_relative_src(*elem.element, parent.content_src_pos_abs());
			}

			const auto total_off = src_off + dst_off + cell_maximum_size;

			line_stride = math::max(line_stride, total_off.*minor_target);
			current_position.*major_target += total_off.*major_target;
		}

		current_position.*major_target = 0;
		current_position.*minor_target += line_stride;
		line_stride = 0;
		cell_idx += row_len;
	}
}
}
