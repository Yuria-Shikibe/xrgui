module;

#include <cassert>

export module mo_yanxi.gui.elem.table;

export import mo_yanxi.gui.layout.policies;
export import mo_yanxi.gui.layout.cell;
export import mo_yanxi.gui.elem.celled_group;
import mo_yanxi.gui.util;
import std;

namespace mo_yanxi::gui{
using table_size_t = std::uint32_t;

export struct table_cell_adaptor : cell_adaptor<layout::mastering_cell>{
	//Against MSVC redefine bug...

	[[nodiscard]] table_cell_adaptor(elem* element, const layout::mastering_cell& cell)
		: cell_adaptor(element, cell){
	}

	[[nodiscard]] table_cell_adaptor() = default;

	bool line_feed() const noexcept{
		return cell.end_line;
	}
};

class layout_line{
public:
	std::vector<layout::stated_extent> size_data{};
};

export struct table;

class table_layout_context{
	using cell_adaptor_type = table_cell_adaptor;

	struct table_head{
		layout::stated_size max_size{};
		float max_pad_src{};
		float max_pad_dst{};

		[[nodiscard]] constexpr float get_captured_size() const noexcept{
			return max_pad_src + max_pad_dst + max_size;
		}

		[[nodiscard]] constexpr bool mastering() const noexcept{
			return max_size.mastering();
		}
	};

	layout::layout_policy policy_{};
	std::span<const std::uint32_t> row_counts_{};
	table_size_t max_major_size_{};
	std::vector<table_head> masters_{};

public:
	[[nodiscard]] table_layout_context() = default;

	[[nodiscard]] table_layout_context(
		const layout::layout_policy policy,
		const table_size_t max_major_size,
		std::span<const std::uint32_t> row_counts
	) :
		policy_(policy),
		row_counts_(row_counts),
		max_major_size_{max_major_size}, masters_(max_major_size + row_counts.size()){
	}

	template <typename T>
	struct visit_result{
		T major;
		T minor;
	};

	struct pre_layout_result{
		math::vec2 captured_extent{};
		math::vector2<table_size_t> masters{};
	};


	[[nodiscard]] constexpr table_size_t max_minor_size() const noexcept{
		return static_cast<table_size_t>(row_counts_.size());
	}

	[[nodiscard]] constexpr table_size_t max_major_size() const noexcept{
		return max_major_size_;
	}

	[[nodiscard]] constexpr auto& at_major(const table_size_t column) noexcept{
		return masters_[column];
	}

	[[nodiscard]] constexpr auto& at_minor(const table_size_t row) noexcept{
		return masters_[max_major_size_ + row];
	}

	[[nodiscard]] constexpr auto& at_major(const table_size_t column) const noexcept{
		return masters_[column];
	}

	[[nodiscard]] constexpr auto& at_minor(const table_size_t row) const noexcept{
		return masters_[max_major_size_ + row];
	}

	[[nodiscard]] constexpr auto& data() const noexcept{
		return masters_;
	}

	[[nodiscard]] std::span<const table_head> get_majors() const noexcept{
		return {masters_.data(), max_major_size_};
	}

	[[nodiscard]] std::span<const table_head> get_minors() const noexcept{
		return {masters_.data() + max_major_size_, max_minor_size()};
	}

	[[nodiscard]] std::span<table_head> get_majors() noexcept{
		return {masters_.data(), max_major_size_};
	}

	[[nodiscard]] std::span<table_head> get_minors() noexcept{
		return {masters_.data() + max_major_size_, max_minor_size()};
	}

	constexpr visit_result<table_head&> operator[](const table_size_t major, const table_size_t minor) noexcept{
		return {at_major(major), at_minor(minor)};
	}

	constexpr visit_result<const table_head&> operator[](const table_size_t major,
	                                                     const table_size_t minor) const noexcept{
		return {at_major(major), at_minor(minor)};
	}

private:
	pre_layout_result layout_masters(std::span<const cell_adaptor_type> cells, math::vec2 scaling) noexcept;

	math::vec2 restricted_allocate_pendings(
		const std::span<const cell_adaptor_type> cells,
		math::vec2 valid_extent,
		pre_layout_result pre_result);

public:
	math::vec2 allocate_cells(
		std::span<const cell_adaptor_type> cells,
		math::vec2 valid_size,
		math::vec2 scaling);

	void place_cells(
		std::span<cell_adaptor_type> cells,
		table& parent,
		math::frect region = {}
	);
};

struct table : universal_group<table_cell_adaptor::cell_type, table_cell_adaptor>{
private:
	bool grid_dirty_{true};
	std::vector<std::uint32_t, mr::heap_allocator<std::uint32_t>> grid_row_counts_{get_heap_allocator()};
	std::uint32_t max_major_size_{0};

	align::pos entire_align_{align::pos::center};
	layout::layout_policy layout_policy_{
		search_parent_layout_policy(false).value_or(layout::layout_policy::hori_major)
	};
	layout::expand_policy expand_policy_{layout::expand_policy::resize_to_fit};

public:
	[[nodiscard]] table(scene& scene, elem* parent)
		: universal_group(scene, parent){
		layout_state.intercept_lower_to_isolated = true;
	}

	void set_entire_align(const align::pos align){
		if(util::try_modify(entire_align_, align)){
			notify_isolated_layout_changed();
		}
	}

	[[nodiscard]] align::pos get_entire_align() const{
		return entire_align_;
	}

	void set_layout_policy(layout::layout_policy policy){
		assert(policy != layout::layout_policy::none && "Not implemented yet");
		if(policy != layout_policy_){
			notify_isolated_layout_changed();
			layout_policy_ = policy;
		}
	}

	void set_expand_policy(layout::expand_policy policy){
		if(policy != expand_policy_){
			notify_isolated_layout_changed();
			expand_policy_ = policy;
		}
	}

	void layout_elem() override{
		layout_directional();
		universal_group::layout_elem();
	}

	table& end_line(){
		if(cells_.empty()) return *this;
		cells_.back().cell.end_line = true;
		grid_dirty_ = true;
		return *this;
	}

	void set_edge_pad(align::spacing pad){
		update_grid_cache();
		if(grid_row_counts_.empty()) return;
		auto end_idx = max_major_size_ - 1;

		bool changed{};
		std::uint32_t cell_idx = 0;
		for(std::uint32_t minor = 0; minor < grid_row_counts_.size(); ++minor){
			std::uint32_t row_len = grid_row_counts_[minor];
			auto line = std::span(cells_).subspan(cell_idx, row_len);

			for(std::uint32_t major = 0; major < row_len; ++major){
				auto& elem = line[major];
				switch(layout_policy_){
				case layout::layout_policy::hori_major :{
					if(minor == 0){
						changed |= util::try_modify(elem.cell.pad.top, pad.top);
					}

					if(minor == grid_row_counts_.size() - 1){
						changed |= util::try_modify(elem.cell.pad.bottom, pad.bottom);
					}

					if(major == 0){
						changed |= util::try_modify(elem.cell.pad.left, pad.left);
					}

					if(major == end_idx || (elem.cell.saturate && row_len == 1)){
						changed |= util::try_modify(elem.cell.pad.right, pad.right);
					}
					break;
				}
				case layout::layout_policy::vert_major :{
					if(minor == 0){
						changed |= util::try_modify(elem.cell.pad.left, pad.left);
					}

					if(minor == grid_row_counts_.size() - 1){
						changed |= util::try_modify(elem.cell.pad.right, pad.right);
					}

					if(major == 0){
						changed |= util::try_modify(elem.cell.pad.top, pad.top);
					}

					if(major == end_idx || (elem.cell.saturate && row_len == 1)){
						changed |= util::try_modify(elem.cell.pad.bottom, pad.bottom);
				}
				}
				default : break;
				}
			}
			cell_idx += row_len;
		}

		if(changed){
			notify_isolated_layout_changed();
		}
	}

	void set_edge_pad(float pad){
		set_edge_pad({pad, pad, pad, pad});
	}


	void clear() noexcept override {
		grid_dirty_ = true;
		universal_group::clear();
	}

	void erase_afterward(std::size_t where) override {
		grid_dirty_ = true;
		universal_group::erase_afterward(where);
	}

	void erase_instantly(std::size_t where) override {
		grid_dirty_ = true;
		universal_group::erase_instantly(where);
	}

protected:
	void on_element_add(table_cell_adaptor& adaptor) override {
		grid_dirty_ = true;
		universal_group::on_element_add(adaptor);
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		if(expand_policy_ == layout::expand_policy::passive) return std::nullopt;

		update_grid_cache();
		if(grid_row_counts_.empty()) return std::nullopt;

		table_layout_context context{layout_policy_, max_major_size_, grid_row_counts_};

		auto size = context.allocate_cells(cells_, extent.potential_extent(), get_scaling());
		if(expand_policy_ == layout::expand_policy::prefer){
			if(const auto v = get_prefer_extent()) size.max(v.value().copy().fdim(boarder_extent()));
		}
		extent.apply(size);

		return extent.potential_extent();
	}

	math::vec2 pre_layout(table_layout_context& context, layout::optional_mastering_extent constrain,
	                      bool size_to_constrain){
		auto size = context.allocate_cells(cells_, constrain.potential_extent(), get_scaling());
		const auto [major_target, minor_target] = layout::get_vec_ptr<>(layout_policy_);
		const auto [dep_major_target, dep_minor_target] = layout::get_vec_ptr<bool>(layout_policy_);

		const auto dep = constrain.get_pending();
		const auto sz = constrain.potential_extent();

		if(!(dep.*dep_major_target)){
			size.*major_target = size_to_constrain ? (sz.*major_target) : content_extent().*major_target;
		}

		if(!(dep.*dep_minor_target)){
			size.*minor_target = size_to_constrain ? (sz.*minor_target) : content_extent().*minor_target;
		}

		return size;
	}

	void layout_directional(){
		update_grid_cache();
		if(grid_row_counts_.empty()) return;

		table_layout_context context{layout_policy_, max_major_size_, grid_row_counts_};

		auto extent = restriction_extent;
		extent.collapse(content_extent());
		const auto size = context.allocate_cells(cells_, extent.potential_extent(), get_scaling());
		const auto off = align::get_offset_of(entire_align_, size, rect{tags::from_extent, {}, content_extent()});
		context.place_cells(cells_, *this, rect{tags::from_extent, off, size});
	}

	void update_grid_cache() {
		if (!grid_dirty_) return;
		grid_row_counts_.clear();
		max_major_size_ = 0;
		std::uint32_t current_row_count = 0;
		for (const auto& cell : cells_) {
			current_row_count++;
			if (cell.line_feed()) {
				grid_row_counts_.push_back(current_row_count);
				max_major_size_ = std::max(max_major_size_, current_row_count);
				current_row_count = 0;
			}
		}
		if (current_row_count > 0) {
			grid_row_counts_.push_back(current_row_count);
			max_major_size_ = std::max(max_major_size_, current_row_count);
		}
		grid_dirty_ = false;
	}

};
}