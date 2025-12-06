module;

#include <cassert>
#include "gch/small_vector.hpp"

module mo_yanxi.gui.elem.table;

namespace mo_yanxi::gui{

//TODO apply scaling
table_layout_context::pre_layout_result table_layout_context::layout_masters(
	const std::span<const cell_adaptor_type> cells, math::vec2 scaling) noexcept{
	auto line_view = cells | std::views::chunk_by([](const cell_adaptor_type& current, const cell_adaptor_type&){
		return !current.line_feed;
	}) | std::views::enumerate;
	const auto [
		pad_major_src, pad_major_dst,
		pad_minor_src, pad_minor_dst
	] = get_pad_ptr(policy_);

	const auto [extent_major, extent_minor] = get_extent_ptr(policy_);

	//Initializing table heads to known maximum size
	for (auto&& [idx_minor, line] : line_view){
		for (auto && [idx_major, elem] : line | std::views::enumerate){
			auto [head_major, head_minor] = (*this)[idx_major, idx_minor];

			head_major.max_pad_src = math::max(head_major.max_pad_src, elem.cell.pad.*pad_major_src);
			head_major.max_pad_dst = math::max(head_major.max_pad_dst, elem.cell.pad.*pad_major_dst);

			head_minor.max_pad_src = math::max(head_minor.max_pad_src, elem.cell.pad.*pad_minor_src);
			head_minor.max_pad_dst = math::max(head_minor.max_pad_dst, elem.cell.pad.*pad_minor_dst);

			//Apply scaling of masterings
			auto promoted = elem.cell.stated_extent.promote();
			promoted.try_scl(scaling);

			head_major.max_size.try_promote_by((promoted.*extent_major).decay());
			if((promoted.*extent_minor).mastering()){
				//maximizing the minor size if mastering
				head_minor.max_size.try_promote_by(promoted.*extent_minor);
			}
		}
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

	auto line_view = cells | std::views::chunk_by([](const cell_adaptor_type& current, const cell_adaptor_type&){
		return !current.line_feed;
	}) | std::views::enumerate;

	const auto [extent_major, extent_minor] = get_extent_ptr(policy_);
	const auto [major_target, minor_target] = get_vec_ptr<>(policy_);

	//TODO when size in major is inf, pre acquire its size and try promote it to master,
	// if failed to get a pre_acquire size to promote the table head, discard it

	{
		//TODO policy to allocate minor size
		bool single_line = max_minor_size() == 1 && std::isfinite(valid_extent.*minor_target);

		gch::small_vector<float, 8> remain_major_pending_sizes;

		if(passive_usable_extent.*major_target < 8 /*lesser should be meaningless ??*/){
			passive_usable_extent.*major_target = 0;
			goto SKIP_REMAIN_MAJOR_CAL;
		}

		if(!layout::is_size_pending(passive_usable_extent.*major_target)){
			//the major dim of extent is limited
			remain_major_pending_sizes.resize(max_major_size());
		}

		for(auto&& [idx_minor, line] : line_view){
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

			for(auto&& [idx_major, elem] : line | std::views::enumerate){
				auto& head_major = at_major(idx_major);

				if((elem.cell.stated_extent.*extent_major).pending()){
					//deduce major size from minor size (or from nothing if minor is pending)
					if(auto size = elem.element->pre_acquire_size(line_pending_extent)){
						if(layout::is_size_pending(passive_usable_extent.*major_target)){
							//major is expandable, directly promote as masterings
							head_major.max_size.try_promote_by(size.value().*major_target);
						} else{
							//major is limited, record to cache and pending
							remain_major_pending_sizes[idx_major] = std::max(
								size.value().*major_target, remain_major_pending_sizes[idx_major]);
						}

						//uses the minimal size to fit minor size, or exhaust all passive usables
						head_minor.max_size.try_promote_by(
							single_line
								? passive_usable_extent.*minor_target
								: math::min(size.value().*minor_target, passive_usable_extent.*minor_target)
						);
					}
				}
			}
		}


		//calculate remain major extent
		if(layout::is_size_pending(passive_usable_extent.*major_target)){
			//skip: all major uses the maximum, no passives remain
			passive_usable_extent.*major_target = 0;
		} else{
			if(float major_sum{std::ranges::fold_left(remain_major_pending_sizes, 0.f, std::plus{})}; major_sum > 0){
				if(const auto ratio = passive_usable_extent.*major_target / major_sum; ratio < 1){
					//requires down scale to fit
					for(auto [midx, new_] : remain_major_pending_sizes | std::views::enumerate){
						if(new_ > 0) at_major(midx).max_size = {layout::size_category::mastering, new_ * ratio};
					}
					passive_usable_extent.*major_target = 0;
				} else{
					//size is sufficient, maintain the major extent
					for(auto [midx, new_] : remain_major_pending_sizes | std::views::enumerate){
						if(new_ > 0) at_major(midx).max_size = {layout::size_category::mastering, new_};
					}
					passive_usable_extent.*major_target -= major_sum;
				}
			}
		}

	SKIP_REMAIN_MAJOR_CAL:
		(void)0;
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
	for(auto&& [idx_minor, line] : line_view){
		auto& head_minor = at_minor(idx_minor);

		float line_minor_size{};
		for(auto&& [idx_major, elem] : line | std::views::enumerate){
			auto& head_major = at_major(idx_major);

			if(!head_major.mastering()){
				head_major.max_size.try_promote_by(head_major.max_size.value / total_passives_weight);
			}

			if(!head_minor.mastering()){
				switch((elem.cell.stated_extent.*extent_minor).type){
				case layout::size_category::scaling :{
					float valid = math::min(head_major.max_size * head_minor.max_size.value,
					                        passive_usable_extent.*minor_target);
					head_minor.max_size.try_promote_by(valid);
					line_minor_size = math::max(line_minor_size, valid);
					break;
				}
				case layout::size_category::pending :{
					layout::stated_extent ext;
					ext.*extent_major = head_major.max_size;
					ext.*extent_minor = {layout::size_category::pending};

					if(auto size = elem.element->pre_acquire_size(ext)){
						float valid = math::min(size.value().*minor_target, passive_usable_extent.*minor_target);

						head_minor.max_size.try_promote_by(valid);
						line_minor_size = math::max(line_minor_size, valid);
					}
					break;
				}
				default : break;
				}
			}
		}

		passive_usable_extent.*minor_target -= line_minor_size;
		assert(passive_usable_extent.*minor_target >= 0);
		line_minor_size = 0.;

		if(!head_minor.mastering()){
			total_minor_weight += head_minor.max_size.value;
		}
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
	auto view = cells | std::views::chunk_by([](const cell_adaptor_type& current, const cell_adaptor_type&){
		return !current.line_feed;
	}) | std::views::enumerate;

	const auto [extent_major, extent_minor] = get_extent_ptr(policy_);
	const auto [major_target, minor_target] = get_vec_ptr<>(policy_);

	auto scaling = parent.get_scaling();
	math::vec2 current_position{};
	for(auto&& [idx_minor, line] : view){
		float line_stride{};

		for(auto&& [idx_major, elem] : line | std::views::enumerate){
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

			if(elem.cell.saturate && std::ranges::size(line) == 1){
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

				if(elem.cell.saturate && std::ranges::size(line) == 1 && !(elem.cell.stated_extent.*extent_major).mastering()){
					cell_actuall_size.*major_target *= scaling.*major_target * std::clamp((elem.cell.stated_extent.*extent_major).value, 0.f, 1.f);
				}else if((elem.cell.stated_extent.*extent_major).mastering() && elem.cell.align != align::pos::none){
					cell_actuall_size.*major_target = scaling.*major_target * (elem.cell.stated_extent.*extent_major).value;
				}

				if((elem.cell.stated_extent.*extent_minor).mastering() && elem.cell.align != align::pos::none){
					cell_actuall_size.*minor_target = scaling.*minor_target * (elem.cell.stated_extent.*extent_minor).value;
				}

				elem.cell.allocated_region.src =
					current_position + src_off + region.get_src() +
						align::get_offset_of(elem.cell.align, cell_actuall_size, rect{cell_maximum_size});
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
	}
}
}
