module mo_yanxi.gui.elem.flex_wrap;

namespace mo_yanxi::gui{
namespace{
struct flex_child_measure{
	float major_min{};
	float major_max{std::numeric_limits<float>::infinity()};
	float minor_min{};
	float minor_max{std::numeric_limits<float>::infinity()};

	float major_basis{};
	float major_size{};
	float minor_basis{};
	float minor_size{};
	float major_weight{};

	layout::size_category major_type{layout::size_category::passive};
	layout::size_category minor_type{layout::size_category::passive};
};

struct flex_line_measure{
	std::size_t begin{};
	std::size_t end{};
	float major_capture{};
	float passive_major_weight{};
	float minor_capture{};
};

[[nodiscard]] constexpr float clamp_axis(const float value, const float min_value, const float max_value) noexcept{
	return math::clamp(value, min_value, max_value);
}

[[nodiscard]] math::vec2 query_intrinsic_size(
	const cell_adaptor<layout::mastering_cell>& cell,
	std::optional<math::vec2>& cache,
	const std::optional<layout::optional_mastering_extent> bound = std::nullopt
){
	if(bound){
		return cell.element->pre_acquire_size(*bound).value_or(cache.value_or(math::vec2{}));
	}

	if(!cache){
		cache = cell.element->pre_acquire_size({
			std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::infinity()
		}).value_or(math::vec2{});
	}
	return *cache;
}

[[nodiscard]] float resolve_major_basis(
	const cell_adaptor<layout::mastering_cell>& cell,
	const layout::layout_policy policy,
	const float available_major,
	const float major_scaling,
	std::optional<math::vec2>& intrinsic_cache,
	flex_child_measure& measure
){
	auto stated = cell.cell.stated_extent.promote();
	const auto [extent_major, _] = layout::get_extent_ptr(policy);
	const auto [major_target, __] = layout::get_vec_ptr(policy);
	const auto major_state = stated.*extent_major;

	measure.major_type = major_state.type;

	switch(major_state.type){
	case layout::size_category::mastering:
		return clamp_axis(major_state.value * major_scaling, measure.major_min, measure.major_max);
	case layout::size_category::pending:
		return clamp_axis(query_intrinsic_size(cell, intrinsic_cache).*major_target, measure.major_min, measure.major_max);
	case layout::size_category::passive:
		measure.major_weight = std::max(0.f, major_state.value);
		return measure.major_min;
	case layout::size_category::scaling:
		if(std::isfinite(available_major)){
			return clamp_axis(major_state.value * available_major, measure.major_min, measure.major_max);
		}
		return measure.major_min;
	default:
		std::unreachable();
	}
}

[[nodiscard]] float resolve_minor_basis(
	const cell_adaptor<layout::mastering_cell>& cell,
	const layout::layout_policy policy,
	const float major_size,
	const float minor_scaling,
	std::optional<math::vec2>& intrinsic_cache,
	flex_child_measure& measure
){
	const auto [_, extent_minor] = layout::get_extent_ptr(policy);
	const auto [major_target, minor_target] = layout::get_vec_ptr(policy);
	const auto minor_state = cell.cell.stated_extent.*extent_minor;

	measure.minor_type = minor_state.type;

	switch(minor_state.type){
	case layout::size_category::mastering:
		return clamp_axis(minor_state.value * minor_scaling, measure.minor_min, measure.minor_max);
	case layout::size_category::pending:{
		layout::optional_mastering_extent bound{};
		bound.set_major(policy, major_size);
		bound.set_minor_pending(policy);
		return clamp_axis(query_intrinsic_size(cell, intrinsic_cache, bound).*minor_target, measure.minor_min, measure.minor_max);
	}
	case layout::size_category::passive:
		return measure.minor_min;
	case layout::size_category::scaling:
		return clamp_axis(major_size * minor_state.value, measure.minor_min, measure.minor_max);
	default:
		std::unreachable();
	}
}

void build_lines(
	const flex_wrap& wrap,
	const std::span<const cell_adaptor<layout::mastering_cell>> cells,
	std::span<flex_child_measure> child_measures,
	const float available_major,
	std::vector<flex_line_measure>& lines
){
	if(cells.empty()){
		return;
	}

	const auto [pad_major_src, pad_major_dst, _, __] = layout::get_pad_ptr(wrap.get_layout_policy());
	constexpr float layout_epsilon = .01f;

	std::size_t line_begin = 0;
	float line_major_capture{};
	float line_passive_total{};

	for(std::size_t idx = 0; idx < cells.size(); ++idx){
		const auto& cell = cells[idx];
		const auto& measure = child_measures[idx];
		const float child_capture = cell.cell.pad.*pad_major_src + measure.major_basis + cell.cell.pad.*pad_major_dst;

		if(idx != line_begin && std::isfinite(available_major) && line_major_capture + child_capture > available_major + layout_epsilon){
			lines.push_back({line_begin, idx, line_major_capture, line_passive_total, 0.f});
			line_begin = idx;
			line_major_capture = 0.f;
			line_passive_total = 0.f;
		}

		line_major_capture += child_capture;
		if(measure.major_type == layout::size_category::passive){
			line_passive_total += measure.major_weight;
		}
	}

	lines.push_back({line_begin, cells.size(), line_major_capture, line_passive_total, 0.f});
}

[[nodiscard]] float compute_wrapped_minor(
	const flex_wrap& wrap,
	std::span<const cell_adaptor<layout::mastering_cell>> cells,
	float available_major
){
	if(cells.empty()){
		return 0.f;
	}

	const auto policy = wrap.get_layout_policy();
	const auto [major_target, minor_target] = layout::get_vec_ptr(policy);
	const auto [_, __, pad_minor_src, pad_minor_dst] = layout::get_pad_ptr(policy);
	const float major_scaling = wrap.get_scaling().*major_target;
	const float minor_scaling = wrap.get_scaling().*minor_target;

	std::vector<flex_child_measure> child_measures(cells.size());
	std::vector<std::optional<math::vec2>> intrinsic_cache(cells.size());
	std::vector<flex_line_measure> lines;
	lines.reserve(cells.size());

	for(std::size_t idx = 0; idx < cells.size(); ++idx){
		const auto& cell = cells[idx];
		auto& measure = child_measures[idx];
		measure.major_min = cell.element->extent_raw().get_minimum_size().*major_target;
		measure.major_max = cell.element->extent_raw().get_maximum_size().*major_target;
		measure.minor_min = cell.element->extent_raw().get_minimum_size().*minor_target;
		measure.minor_max = cell.element->extent_raw().get_maximum_size().*minor_target;
		measure.major_basis = resolve_major_basis(cell, policy, available_major, major_scaling, intrinsic_cache[idx], measure);
	}

	build_lines(wrap, cells, child_measures, available_major, lines);

	float total_minor{};
	for(auto& line : lines){
		const float remaining_major = std::isfinite(available_major)
			? std::fdim(available_major, line.major_capture)
			: 0.f;
		const float passive_unit = line.passive_major_weight > 0 ? remaining_major / line.passive_major_weight : 0.f;

		for(std::size_t idx = line.begin; idx < line.end; ++idx){
			auto& measure = child_measures[idx];
			const auto& cell = cells[idx];
			measure.major_size = measure.major_type == layout::size_category::passive
				? clamp_axis(measure.major_basis + measure.major_weight * passive_unit, measure.major_min, measure.major_max)
				: measure.major_basis;
			measure.minor_basis = resolve_minor_basis(cell, policy, measure.major_size, minor_scaling, intrinsic_cache[idx], measure);
			line.minor_capture = math::max(
				line.minor_capture,
				cell.cell.pad.*pad_minor_src + measure.minor_basis + cell.cell.pad.*pad_minor_dst
			);
		}

		total_minor += line.minor_capture;
	}

	if(lines.size() > 1){
		total_minor += wrap.get_line_spacing() * static_cast<float>(lines.size() - 1);
	}

	return total_minor;
}
}

std::optional<math::vec2> flex_wrap::pre_acquire_size_impl(layout::optional_mastering_extent extent){
	switch(policy_){
	case layout::layout_policy::hori_major:
		if(extent.width_pending()) return std::nullopt;
		break;
	case layout::layout_policy::vert_major:
		if(extent.height_pending()) return std::nullopt;
		break;
	case layout::layout_policy::none:
		if(extent.fully_mastering()) return extent.potential_extent();
		return std::nullopt;
	default:
		std::unreachable();
	}

	if(children_.empty()){
		return std::nullopt;
	}

	auto potential = extent.potential_extent();
	const auto dep = extent.get_pending();
	const auto [major_target, minor_target] = layout::get_vec_ptr(policy_);
	const auto [major_target_dep, minor_target_dep] = layout::get_vec_ptr<bool>(policy_);

	if(dep.*minor_target_dep){
		if(expand_policy_ == layout::expand_policy::passive){
			potential.*minor_target = this->content_extent().*minor_target;
		} else{
			potential.*minor_target = compute_wrapped_minor(*this, cells_, potential.*major_target);
		}
	}

	if(auto pref = get_prefer_content_extent(); pref && expand_policy_ == layout::expand_policy::prefer){
		potential.max(pref.value());
	}

	return potential;
}

void flex_wrap::layout_elem(){
	if(cells_.empty()){
		if(auto pref = get_prefer_extent(); pref && expand_policy_ == layout::expand_policy::prefer){
			resize(pref.value(), propagate_mask::force_upper);
		}
		return;
	}

	const auto policy = get_layout_policy();
	const auto [major_target, minor_target] = layout::get_vec_ptr(policy);
	const auto [major_target_b, _] = layout::get_vec_ptr<bool>(policy);
	const auto [extent_major, extent_minor] = layout::get_extent_ptr(policy);
	const auto [pad_major_src, pad_major_dst, pad_minor_src, pad_minor_dst] = layout::get_pad_ptr(policy);
	const auto align_pos_minor_mask = policy == layout::layout_policy::hori_major ? align::pos::mask_y : align::pos::mask_x;

	auto content_sz = content_extent();
	float available_major = content_sz.*major_target;
	if(restriction_extent.get_pending().*major_target_b){
		available_major = std::numeric_limits<float>::infinity();
	}

	const float major_scaling = get_scaling().*major_target;
	const float minor_scaling = get_scaling().*minor_target;

	std::vector<flex_child_measure> child_measures(cells_.size());
	std::vector<std::optional<math::vec2>> intrinsic_cache(cells_.size());
	std::vector<flex_line_measure> lines;
	lines.reserve(cells_.size());

	for(std::size_t idx = 0; idx < cells_.size(); ++idx){
		const auto& cell = cells_[idx];
		auto& measure = child_measures[idx];
		measure.major_min = cell.element->extent_raw().get_minimum_size().*major_target;
		measure.major_max = cell.element->extent_raw().get_maximum_size().*major_target;
		measure.minor_min = cell.element->extent_raw().get_minimum_size().*minor_target;
		measure.minor_max = cell.element->extent_raw().get_maximum_size().*minor_target;
		measure.major_basis = resolve_major_basis(cell, policy, available_major, major_scaling, intrinsic_cache[idx], measure);
	}

	build_lines(*this, cells_, child_measures, available_major, lines);

	float total_minor{};
	for(auto& line : lines){
		const float remaining_major = std::isfinite(content_sz.*major_target)
			? std::fdim(content_sz.*major_target, line.major_capture)
			: 0.f;
		const float passive_unit = line.passive_major_weight > 0 ? remaining_major / line.passive_major_weight : 0.f;

		for(std::size_t idx = line.begin; idx < line.end; ++idx){
			auto& measure = child_measures[idx];
			const auto& cell = cells_[idx];
			measure.major_size = measure.major_type == layout::size_category::passive
				? clamp_axis(measure.major_basis + measure.major_weight * passive_unit, measure.major_min, measure.major_max)
				: measure.major_basis;
			measure.minor_basis = resolve_minor_basis(cell, policy, measure.major_size, minor_scaling, intrinsic_cache[idx], measure);
			line.minor_capture = math::max(
				line.minor_capture,
				cell.cell.pad.*pad_minor_src + measure.minor_basis + cell.cell.pad.*pad_minor_dst
			);
		}

		total_minor += line.minor_capture;
	}

	if(lines.size() > 1){
		total_minor += line_spacing_ * static_cast<float>(lines.size() - 1);
	}

	if(expand_policy_ != layout::expand_policy::passive){
		math::vec2 size;
		size.*major_target = content_sz.*major_target;
		size.*minor_target = total_minor;
		size += boarder().extent();

		if(expand_policy_ == layout::expand_policy::prefer){
			size.max(get_prefer_extent().value_or({}));
		}

		size.min(restriction_extent.potential_extent());
		resize(size, propagate_mask::force_upper);
		content_sz = content_extent();
	}

	float line_minor_offset = wrap_reverse_ ? total_minor : 0.f;
	for(std::size_t line_idx = 0; line_idx < lines.size(); ++line_idx){
		auto& line = lines[line_idx];
		float major_offset{};

		if(wrap_reverse_){
			line_minor_offset -= line.minor_capture;
		}

		for(std::size_t idx = line.begin; idx < line.end; ++idx){
			auto& cell = cells_[idx];
			auto& measure = child_measures[idx];

			math::vec2 slot_size{};
			slot_size.*major_target = measure.major_size;
			slot_size.*minor_target = std::fdim(line.minor_capture, cell.cell.pad.*pad_minor_src + cell.cell.pad.*pad_minor_dst);

			const bool minor_align_none = (cell.cell.unsaturate_cell_align & align_pos_minor_mask) == align::pos{};
			measure.minor_size = slot_size.*minor_target;
			if(!cell.cell.saturate && !minor_align_none && measure.minor_type != layout::size_category::passive){
				measure.minor_size = math::min(measure.minor_basis, slot_size.*minor_target);
			}

			math::vec2 actual_size = slot_size;
			actual_size.*minor_target = measure.minor_size;

			math::vec2 slot_origin{};
			slot_origin.*major_target = major_offset + cell.cell.pad.*pad_major_src;
			slot_origin.*minor_target = line_minor_offset + cell.cell.pad.*pad_minor_src;

			const auto align_off = align::get_offset_of(cell.cell.unsaturate_cell_align, actual_size, rect{slot_size});
			cell.cell.allocated_region.src = slot_origin + align_off;
			cell.cell.allocated_region.set_size(actual_size);

			layout::optional_mastering_extent expected{};
			if((cell.cell.stated_extent.*extent_major).pending()){
				expected.set_major_pending(policy);
			}
			if((cell.cell.stated_extent.*extent_minor).pending()){
				expected.set_minor_pending(policy);
			}

			cell.apply(*this, expected);
			if(!is_pos_smooth()){
				cell.cell.update_relative_src(*cell.element, content_src_pos_abs());
			}

			major_offset += cell.cell.pad.*pad_major_src + measure.major_size + cell.cell.pad.*pad_major_dst;
		}

		if(wrap_reverse_){
			line_minor_offset -= line_spacing_;
		} else{
			line_minor_offset += line.minor_capture + line_spacing_;
		}
	}
}
}
