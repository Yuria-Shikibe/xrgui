module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.compound.data_table;

import std;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.alloc;
import mo_yanxi.math;
import mo_yanxi.typesetting;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.text_render_cache;

import mo_yanxi.graphic.draw.instruction;

import mo_yanxi.snap_shot;
import mo_yanxi.math.vector2;
import mo_yanxi.math.matrix3;
import mo_yanxi.csv;

namespace mo_yanxi::gui::cpd{
export
struct data_table_desc{
private:

	struct entry{
		mr::string data{};
		typesetting::glyph_layout_draw_only glyph_layout{};
		text_render_cache render_cache{};
		bool dirty{true};

		[[nodiscard]] entry() = default;

		bool empty() const noexcept{
			return glyph_layout.elems.empty();
		}

		bool try_update(const typesetting::layout_config& cfg, typesetting::fast_plain_layout_context& ctx, typesetting::tokenized_text& cache){
			if(!dirty)return false;
			dirty = false;
			if(data.empty()){
				glyph_layout.clear();
				return true;
			}
			cache.reset(data, typesetting::tokenize_tag::raw);
			ctx.layout(cache, cfg, glyph_layout);
			render_cache.update_buffer(glyph_layout, graphic::colors::white);
			return true;
		}
	};

	struct head : entry{
		using entry::entry;

		snap_shot<float> actual_size;
	};


	typesetting::fast_plain_layout_context plain_layout_context{std::in_place};
	typesetting::tokenized_text cache{};
	mr::vector<head> table_heads_{};
	mr::vector<entry> table_entries_{};
	// mr::heap_vector<typesetting::glyph_layout_draw_only> index_labels{};
	float entry_height_{80};
	math::vec2 pad_{16, 4};

	snap_shot<math::vec2> extent_{};

public:
	[[nodiscard]] data_table_desc() = default;

	[[nodiscard]] std::size_t get_col_count() const noexcept{
		return table_heads_.size();
	}

	[[nodiscard]] std::size_t get_row_count() const noexcept{
		const auto col = get_col_count();
		if(!col) return 0;
		return (table_entries_.size() + col - 1) / col;
	}

	std::optional<math::vec2> pre_acquire_size(layout::optional_mastering_extent extent){
		auto head_requried_ext = layout_heads();
		head_requried_ext.y += (entry_height_ + pad_.y) * get_row_count();
		extent_ = head_requried_ext;
		return head_requried_ext;
	}

	bool try_update_glyph_layouts(){
		bool any{};
		using namespace typesetting;
		const layout_config cfg{};
		math::vec2 extent{};

		for(auto&& entry : table_heads_){
			if(entry.try_update(cfg, plain_layout_context, cache)){
				entry.actual_size = entry.glyph_layout.extent.x;
				any = true;
			}
			extent.max_y(entry.glyph_layout.extent.y);
			extent.x += pad_.x + entry.actual_size.temp;
		}

		for(auto&& entry : table_entries_){
			any |= entry.try_update(cfg, plain_layout_context, cache);
		}
		extent.y += (entry_height_ + pad_.y) * get_row_count() + pad_.y;
		extent_ = extent;

		return any;
	}

	auto get_entry_grid() const noexcept{
		return std::mdspan{table_entries_.data(), get_row_count(), get_col_count()};
	}

	void draw(gui::renderer_frontend& renderer, math::frect clipspace, math::vec2 draw_offset) const{
		renderer.top_viewport().push_local_transform();

		const auto grid = get_entry_grid();
		const auto num_rows = grid.extent(0);
		const auto num_cols = grid.extent(1);

		// 计算表头的最大高度
		float head_max_height = 0.0f;
		for(const auto& entry : table_heads_){
			head_max_height = math::max(head_max_height, entry.glyph_layout.extent.y);
		}

		math::mat3 mat3{math::mat3_idt};
		math::vec2 current_offset{};

		{
			using namespace graphic::draw::instruction;

			renderer.update_state(fx::batch_draw_mode::def);

			renderer << rect_aabb{
				.v00 = draw_offset,
				.v11 = draw_offset + get_extent(),
				.vert_color = {graphic::colors::dark_gray.create_lerp(graphic::colors::black, .5f).set_a(.4f)},
			};

			state_guard g{renderer, fx::make_blend_write_mask(false)};

			if(num_cols){
				static constexpr graphic::color entry_even_row_color = graphic::colors::dark_gray.create_lerp(graphic::colors::ROYAL, .25f);
				current_offset = {0, head_max_height + pad_.y * .5f};
				for(auto row = 0uz; row < num_rows; ++row){
					auto off = draw_offset + current_offset;
					current_offset.y += entry_height_ + pad_.y;
					if((row & 1) == 0){
						renderer << rect_aabb{
							.v00 = off,
							.v11 = (draw_offset + current_offset  + math::vec2{get_extent().x}),
							.vert_color = {entry_even_row_color}
						};
					}
				}

				auto draw_col_line = [&](math::vec2 offset){
					renderer << line{
						.src = offset,
						.dst = offset + math::vec2{0, get_extent().y},
						.color = {graphic::colors::light_gray, graphic::colors::light_gray},
						.stroke = 2,
						.cap_length = 1,
					};
				};

				auto draw_row_line = [&](math::vec2 offset){
					renderer << line{
						.src = offset,
						.dst = offset + math::vec2{get_extent().x, 0},
						.color = {graphic::colors::light_gray, graphic::colors::light_gray},
						.stroke = 2,
						.cap_length = 1,
					};
				};


				//col separators
				current_offset = {pad_.x * .5f};
				for(auto col = 0uz; col < num_cols; ++col){
					auto off = draw_offset + current_offset;
					off.x -= pad_.x * .5f;

					draw_col_line(off);

					auto& head_entry = table_heads_[col];
					current_offset.x += head_entry.actual_size.temp + pad_.x;
				}
				draw_col_line(draw_offset + math::vec2{get_extent().x});

				//row separators
				draw_row_line(draw_offset);
				current_offset = {0, head_max_height + pad_.y * .5f};
				for(auto row = 0uz; row < num_rows; ++row){
					auto off = draw_offset + current_offset;
					draw_row_line(off);

					current_offset.y += entry_height_ + pad_.y;
				}
				draw_row_line(draw_offset + math::vec2{0, get_extent().y});
			}

		}

		renderer.update_state(fx::batch_draw_mode::msdf);

		constexpr auto get_align_off = [](typesetting::line_alignment a, const math::vec2 layout_size, math::vec2 cell_size) static {
			align::pos p;
			switch(a){
			case typesetting::line_alignment::start : p = align::pos::center_left; break;
			case typesetting::line_alignment::center : p = align::pos::center; break;
			case typesetting::line_alignment::end : p = align::pos::center_right; break;
			default : p = align::pos::center; break;
			}
			return align::get_offset_of(p, layout_size, math::frect{cell_size});
		};

		current_offset = {};
		for(auto col = 0uz; col < num_cols; ++col){
			auto& head_entry = table_heads_[col];
			const math::frect wrap_bound{tags::from_extent, current_offset + draw_offset, {head_entry.actual_size.temp + pad_.x, get_extent().y}};

			auto draw_entry = [&](const entry& e, math::vec2 src_offset, math::vec2 cell_size){
				if(e.empty())return;
				if(!math::frect{tags::from_extent, src_offset, cell_size}.overlap_exclusive(clipspace))return;

				math::vec2 target_size;
				if(cell_size.y >= e.glyph_layout.extent.y){
					target_size = e.glyph_layout.extent;
				}else{
					target_size = align::embed_to(align::scale::fillY, e.glyph_layout.extent, cell_size);
				}

				const math::vec2 offset = src_offset + get_align_off(e.render_cache.get_line_align(), target_size, cell_size);

				mat3.set_rect_transform({}, e.glyph_layout.extent, offset, target_size);
				renderer.top_viewport().set_local_transform(mat3);
				renderer.notify_viewport_changed();
				e.render_cache.push_to_renderer(renderer);
			};


			if(clipspace.overlap_exclusive(wrap_bound)){
				renderer.top_viewport().set_local_transform_idt();
				renderer.push_scissor({.rect =
					{tags::from_extent, draw_offset + current_offset, {head_entry.actual_size.temp + pad_.x, pad_.y + head_max_height + (pad_.y + entry_height_) * get_row_count()}}});

				current_offset += pad_ * .5f;
				draw_entry(head_entry, current_offset + draw_offset, {head_entry.actual_size.temp, head_max_height});

				current_offset.y += head_max_height + pad_.y;

				for(auto row = 0uz; row < num_rows; ++row){
					auto& grid_entry = grid[row, col];
					draw_entry(grid_entry, current_offset + draw_offset, {head_entry.actual_size.temp, entry_height_});

					current_offset.y += entry_height_ + pad_.y;
				}

				renderer.pop_scissor();
			}else{
				current_offset.x += pad_.x * .5f;
			}

			current_offset.y = 0;
			current_offset.x += head_entry.actual_size.temp + pad_.x * .5f;
		}

		renderer.top_viewport().pop_local_transform();
		renderer.notify_viewport_changed();
	}
	static data_table_desc from_csv(const std::filesystem::path& path, char delimiter = ',', mr::heap_allocator<> alloc = {}){
		data_table_desc desc{};

		unsigned col_count{};

		csv::parse_file(path, [&](csv::coord coord, std::string_view sv){
			if (coord.row == 0) {
				// 第 0 行：生成表头并统计标准列数 (col_count)
				col_count++;
				auto& e = desc.table_heads_.emplace_back();
				csv::unescape_csv_field(e.data, sv);
				e.render_cache.set_line_align(typesetting::line_alignment::center);
			} else {
				// 数据行：如果当前列索引超出了表头列数，直接丢弃（处理多出的锯齿列）
				if (coord.col >= col_count) {
					return;
				}

				// 计算当前解析的单元格在一维 table_entries_ 数组中理论上的起始索引
				// 因为数据行是从 coord.row = 1 开始的，所以减去 1
				unsigned expected_index = (coord.row - 1) * col_count + coord.col;

				// 如果数组当前大小落后于理论索引，说明之前的行存在列数不足（短行）
				// 我们需要用空的 entry 进行占位补齐
				while (desc.table_entries_.size() < expected_index) {
					desc.table_entries_.emplace_back();
				}

				// 正常追加当前解析出的单元格
				auto& e = desc.table_entries_.emplace_back();
				csv::unescape_csv_field(e.data, sv);
				e.render_cache.set_line_align(csv::is_numeric(e.data)
												? typesetting::line_alignment::end
												: typesetting::line_alignment::start);
			}
		}, delimiter);

		// 收尾处理：如果整个文件的最后一行是短行，解析回调结束时它还没有被补齐。
		// 这里确保最终的一维数组大小是 col_count 的整数倍。
		if (col_count > 0) {
			while (desc.table_entries_.size() % col_count != 0) {
				desc.table_entries_.emplace_back();
			}
		}

		return desc;
	}

	[[nodiscard]] math::vec2 get_extent() const{
		return extent_.temp;
	}

	bool set_col_temp_size(std::size_t col, float delta_size) noexcept {
		auto& c = table_heads_[col];
		float prev = c.actual_size.temp;
		float next = math::max(c.actual_size.base + delta_size, math::max(4.f, math::min(entry_height_, c.glyph_layout.extent.x)));
		if(util::try_modify(c.actual_size.temp, next)){
			const auto dx = next - prev;
			extent_.temp = extent_.base.copy().add_x(dx);
			return true;
		}
		return false;
	}

	void apply_col_temp_size(std::size_t col) noexcept {
		auto& c = table_heads_[col];
		c.actual_size.apply();
		extent_.apply();
	}

	template <std::invocable<std::size_t> Fn>
	void hit_col_bound(math::vec2 pos, Fn&& fn){
		static constexpr float margin = 16;
		if(!util::contains(pos, extent_.temp, {margin, margin}))return;
		float cur_x{};
		auto cols = get_col_count();
		for(auto i = 0uz; i < cols; ++i){
			auto bound_L = cur_x + table_heads_[i].actual_size.temp - margin;
			auto bound_R = cur_x + table_heads_[i].actual_size.temp + pad_.x + margin;
			if(bound_L <= pos.x && pos.x <= bound_R){
				std::invoke(fn, i);
				break;
			}
			cur_x += table_heads_[i].actual_size.temp + pad_.x;
		}
	}

private:
	math::vec2 layout_heads(){
		using namespace typesetting;
		const layout_config cfg{};
		math::vec2 extent{};
		for(auto&& table_head : table_heads_){
			if(table_head.try_update(cfg, plain_layout_context, cache)){
				table_head.actual_size = table_head.glyph_layout.extent.x;
			}

			extent.max_y(table_head.glyph_layout.extent.y);
			extent.x += pad_.x + table_head.actual_size.temp;
		}
		extent.x += pad_.x;
		return extent;
	}
};

struct interface : scroll_adaptor_apply_interface_schema<data_table_desc>{
	math::vec2 get_extent(const element_type& element) const {
		return element.get_extent();
	}

	void layout_elem(element_type& element) const{
		element.try_update_glyph_layouts();
	}

	std::optional<math::vec2> pre_acq_size(element_type& element, layout::optional_mastering_extent bound) const {
		return element.pre_acquire_size(bound);
	}

	void draw_layer(const element_type& element, const scroll_adaptor_base& scroll_adaptor_base, rect clipSpace, fx::layer_param_pass_t param) const {
		if(!param.is_top())return;
		element.draw(scroll_adaptor_base.renderer(), clipSpace, scroll_adaptor_base.content_src_pos_abs());
	}

};


export
struct data_table : public scroll_adaptor<data_table_desc, interface>{
private:
	static constexpr std::size_t no_modified = std::numeric_limits<std::size_t>::max();
	std::size_t last_modified_col = no_modified;

public:
	[[nodiscard]] data_table(scene& scene, elem* parent)
		: scroll_adaptor(scene, parent, layout::layout_policy::none){
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		if(event.key.on_release() && last_modified_col != no_modified){
			get_item().apply_col_temp_size(last_modified_col);
			last_modified_col = no_modified;
		}
		return scroll_adaptor::on_click(event, aboves);
	}

	events::op_afterwards on_drag(const events::drag event) override{
		if(scroll_.is_dirty()){
			//scroll bar dragging
			return scroll_adaptor::on_drag(event);
		}

		const auto delta = get_scroll_offset();
		auto& table = get_item();

		if(last_modified_col == no_modified){
			table.hit_col_bound(event.src + delta, [&](std::size_t col){
				if(table.set_col_temp_size(col, event.delta().x)){
					last_modified_col = col;
				}
			});
		}

		if(last_modified_col != no_modified){
			if(table.set_col_temp_size(last_modified_col, event.delta().x)){
				notify_isolated_layout_changed();
			}
			return events::op_afterwards::intercepted;
		}

		return scroll_adaptor::on_drag(event);
	}
};

}
