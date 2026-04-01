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
import mo_yanxi.graphic.draw.instruction.recorder;

import mo_yanxi.snap_shot;
import mo_yanxi.math.vector2;
import mo_yanxi.math.matrix3;
import mo_yanxi.csv;

namespace mo_yanxi::gui::cpd{

export 
struct data_table_config{
	float entry_height{80};
	math::vec2 pad{16, 4};
};
export
struct data_table_desc{
private:
	using instr_recorder = graphic::draw::instruction::draw_record_chunked_storage<mr::unvs_allocator<std::byte>>;

	struct entry{
		mr::string data{};
		typesetting::glyph_layout_draw_only glyph_layout{};
		bool dirty{true};
		typesetting::line_alignment line_alignment{};

		[[nodiscard]] entry() = default;

		bool empty() const noexcept{
			return glyph_layout.elems.empty();
		}

		bool try_update(
			const typesetting::layout_config& cfg,
			typesetting::fast_plain_layout_context& ctx,
			typesetting::tokenized_text& cache){
			if(!dirty) return false;
			dirty = false;
			if(data.empty()){
				glyph_layout.clear();
				return true;
			}
			cache.reset(data, typesetting::tokenize_tag::raw);
			ctx.layout(cache, cfg, glyph_layout);
			return true;
		}

		void record_instructions(instr_recorder& buffer){
			using namespace mo_yanxi::graphic;
			using namespace mo_yanxi::graphic::draw::instruction;

			for(const auto& current_line : glyph_layout.lines){
				auto [line_src, spacing] = current_line.calculate_alignment(
					glyph_layout.extent, line_alignment, typesetting::layout_direction::ltr);

				for(const auto& [idx, val] : std::span{
					    glyph_layout.elems.begin() + current_line.glyph_range.pos, current_line.glyph_range.size
				    } | std::views::enumerate){
					if(!val.texture->view) continue;
					auto start = math::fma(idx, spacing, line_src + val.aabb.src);
					buffer.push(rect_aabb{
							.generic = {val.texture->view},
							.v00 = start,
							.v11 = start + val.aabb.extent(),
							.uv00 = val.texture->uv.v00(),
							.uv11 = val.texture->uv.v11(),
							.vert_color = {val.color},
							.slant_factor_asc = val.slant_factor_asc,
							.slant_factor_desc = val.slant_factor_desc,
							.sdf_expand = -val.weight_offset
						});
				}
			}

			buffer.split(true);
		}
	};

	struct head : entry{
		using entry::entry;

		snap_shot<float> actual_size;
	};


	bool any_changed_{true};
	instr_recorder glyph_instructions{};
	typesetting::fast_plain_layout_context plain_layout_context{std::in_place};
	typesetting::tokenized_text cache{};
	mr::vector<head> table_heads_{};
	mr::vector<entry> table_entries_{};
	// mr::heap_vector<typesetting::glyph_layout_draw_only> index_labels{};
	data_table_config config_{};

	snap_shot<math::vec2> extent_{};

public:
	[[nodiscard]] data_table_desc() = default;

	[[nodiscard]] explicit data_table_desc(const data_table_config& config)
		: config_(config){
	}

	bool check_changed() noexcept{
		return std::exchange(any_changed_, false);
	}

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
		head_requried_ext.y += (config_.entry_height + config_.pad.y) * get_row_count();
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
			extent.x += config_.pad.x + entry.actual_size.temp;
		}

		for(auto&& entry : table_entries_){
			any |= entry.try_update(cfg, plain_layout_context, cache);
		}
		extent.y += (config_.entry_height + config_.pad.y) * get_row_count() + config_.pad.y;
		extent_ = extent;

		glyph_instructions.clear();
		for(auto&& entry : table_heads_){
			entry.record_instructions(glyph_instructions);
		}
		for(auto&& entry : table_entries_){
			entry.record_instructions(glyph_instructions);
		}

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

    // =========================================================================
    // 核心优化 1：计算 O(1) 的垂直行剪枝区间 (Row Fast-Forwarding)
    // 请根据您的 math::frect 实际成员变量替换获取 min_y 和 max_y 的方式
    // 假设可以通过类似 clipspace.v00.y 和 clipspace.v11.y 获取边界
    // =========================================================================
    const float clip_min_y = clipspace.vert_00().y;
    const float clip_max_y = clipspace.vert_11().y;

    const float row_step = config_.entry_height + config_.pad.y;
    const float table_start_y = draw_offset.y + head_max_height + config_.pad.y * 0.5f;

    std::size_t visible_row_start = 0;
    std::size_t visible_row_end = num_rows;

    if (num_rows > 0 && row_step > 0.0f) {
        // 利用边界差值直接计算首尾可见行索引
        float start_idx_f = (clip_min_y - table_start_y) / row_step;
        float end_idx_f   = (clip_max_y - table_start_y) / row_step;

        // 向下取整获取起始行，向上取整获取结束行，并用 std::clamp / math::max 限制在合法区间内
        visible_row_start = math::floor<std::size_t>(math::max(start_idx_f, 0.f));
        visible_row_end   = math::ceil<std::size_t>(math::max(end_idx_f, 0.f));

        visible_row_start = math::min(visible_row_start, num_rows);
        visible_row_end   = math::min(visible_row_end, num_rows);
    }
    // =========================================================================

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
          static constexpr graphic::color entry_even_row_color = graphic::colors::dark_gray.create_lerp(
             graphic::colors::ROYAL, .15f);

          // 优化：仅遍历计算出的可见行范围
          current_offset = {0, table_start_y - draw_offset.y + visible_row_start * row_step};
          for(std::size_t row = visible_row_start; row < visible_row_end; ++row){
             auto off = draw_offset + current_offset;
             current_offset.y += row_step;
             if((row & 1) == 0){
                auto v11 = draw_offset + current_offset + math::vec2{get_extent().x};

                math::frect row_bounds{tags::from_extent, off, math::vec2{v11.x - off.x, v11.y - off.y}};
                if(row_bounds.overlap_exclusive(clipspace)){
                   renderer << rect_aabb{
                         .v00 = off,
                         .v11 = v11,
                         .vert_color = {entry_even_row_color}
                      };
                }
             }
          }

          static constexpr float side_stroke = 1.f;

          // 返回 bool 告知是否发生了渲染，用于早期退出
          auto draw_col_line = [&](math::vec2 offset) -> bool {
             math::frect line_bounds{
                   tags::unchecked, tags::from_extent, offset - math::vec2{side_stroke, 0.0f}, math::vec2{side_stroke * 2, get_extent().y}
                };
             if(!line_bounds.overlap_exclusive(clipspace)) return false;

             renderer << line{
                   .src = offset,
                   .dst = offset + math::vec2{0, get_extent().y},
                   .color = {graphic::colors::light_gray, graphic::colors::light_gray},
                   .stroke = side_stroke * 2,
                   .cap_length = side_stroke,
                };
             return true;
          };

          auto draw_row_line = [&](math::vec2 offset) -> bool {
             math::frect line_bounds{
                   tags::unchecked, tags::from_extent, offset - math::vec2{0.0f, side_stroke}, math::vec2{get_extent().x, side_stroke * 2}
                };
             if(!line_bounds.overlap_exclusive(clipspace)) return false;

             renderer << line{
                   .src = offset,
                   .dst = offset + math::vec2{get_extent().x, 0},
                   .color = {graphic::colors::light_gray, graphic::colors::light_gray},
                   .stroke = side_stroke * 2,
                   .cap_length = side_stroke,
                };
             return true;
          };

          // =========================================================================
          // 核心优化 2：列分割线裁剪追踪 (Column Early-Out)
          // =========================================================================
          bool col_line_seen = false;
          current_offset = {config_.pad.x * .5f};
          for(std::size_t col = 0uz; col < num_cols; ++col){
             auto off = draw_offset + current_offset;
             off.x -= config_.pad.x * .5f;

             bool hit = draw_col_line(off);
             if (hit) {
                 col_line_seen = true;
             } else if (col_line_seen) {
                 // 既然之前进入过裁剪空间，现在没碰到了，说明已经越过右边界，直接终止循环
                 break;
             }

             auto& head_entry = table_heads_[col];
             current_offset.x += head_entry.actual_size.temp + config_.pad.x;
          }
          draw_col_line(draw_offset + math::vec2{get_extent().x}); // 补充最后一根线

          // =========================================================================
          // 核心优化 3：重用可见行索引范围画行分割线
          // =========================================================================
          draw_row_line(draw_offset);
          current_offset = {0, table_start_y - draw_offset.y + visible_row_start * row_step};
          for(std::size_t row = visible_row_start; row < visible_row_end; ++row){
             auto off = draw_offset + current_offset;
             draw_row_line(off);
             current_offset.y += row_step;
          }
          draw_row_line(draw_offset + math::vec2{0, get_extent().y});
       }
    }
    renderer.update_state(fx::batch_draw_mode::msdf);

    constexpr auto get_align_off = [](typesetting::line_alignment a, const math::vec2 layout_size,
                                      math::vec2 cell_size) static{
       align::pos p;
       switch(a){
       case typesetting::line_alignment::start : p = align::pos::center_left;
          break;
       case typesetting::line_alignment::center : p = align::pos::center;
          break;
       case typesetting::line_alignment::end : p = align::pos::center_right;
          break;
       default : p = align::pos::center;
          break;
       }
       return align::get_offset_of(p, layout_size, math::frect{cell_size});
    };

    auto draw_entry = [&](const entry& e, math::vec2 src_offset, math::vec2 cell_size, math::usize2 coord){
       if(e.empty()) return;
       // 这里保留单体级的安全相交测试
       if(!math::frect{tags::from_extent, src_offset, cell_size}.overlap_exclusive(clipspace)) return;

       math::vec2 target_size;
       if(cell_size.y >= e.glyph_layout.extent.y){
          target_size = e.glyph_layout.extent;
       } else{
          target_size = align::embed_to(align::scale::fillY, e.glyph_layout.extent, cell_size);
       }

       const math::vec2 offset = src_offset + get_align_off(e.line_alignment, target_size, cell_size);

       mat3.set_rect_transform({}, e.glyph_layout.extent, offset, target_size);
       renderer.top_viewport().set_local_transform(mat3);
       renderer.notify_viewport_changed();

       auto instr_idx = get_col_count() * coord.y + coord.x;
       auto instr = glyph_instructions[instr_idx];
       renderer.push(instr.heads, instr.data.data());
    };

    bool col_cell_seen = false;
    current_offset = {};
    for(std::size_t col = 0uz; col < num_cols; ++col){
       auto& head_entry = table_heads_[col];
       const math::frect wrap_bound{
             tags::from_extent, current_offset + draw_offset,
             {head_entry.actual_size.temp + config_.pad.x, get_extent().y}
          };

       if(clipspace.overlap_exclusive(wrap_bound)){
          col_cell_seen = true;

          renderer.top_viewport().set_local_transform_idt();
          renderer.push_scissor({
                .rect =
                {
                   tags::from_extent, draw_offset + current_offset,
                   {
                      head_entry.actual_size.temp + config_.pad.x,
                      config_.pad.y + head_max_height + (config_.pad.y + config_.entry_height) * get_row_count()
                   }
                }
             });

          current_offset += config_.pad * .5f;

          // 如果需要画表头，我们仍然调用，内部依然会通过 overlap 测试进行裁剪
          draw_entry(head_entry, current_offset + draw_offset, {head_entry.actual_size.temp, head_max_height},
                     math::usize2(col));

          // =========================================================================
          // 核心优化 4：二维循环降维到局部——跳到当前列真正的起止可见行
          // =========================================================================
          current_offset.y += head_max_height + config_.pad.y;
          current_offset.y += visible_row_start * row_step; // Fast-forward 到开始绘制位置

          for(std::size_t row = visible_row_start; row < visible_row_end; ++row){
             auto& grid_entry = grid[row, col];
             draw_entry(grid_entry, current_offset + draw_offset, {head_entry.actual_size.temp, config_.entry_height},
                        math::usize2(col, row + 1));

             current_offset.y += row_step;
          }

          renderer.pop_scissor();
       } else{
          if (col_cell_seen) {
             // 说明已经画完了处于 clipspace 的所有列，目前进入到了右侧不可见区域，直接掐断整个列循环
             break;
          }
          current_offset.x += config_.pad.x * .5f;
       }

       current_offset.y = 0;
       current_offset.x += head_entry.actual_size.temp + config_.pad.x * .5f;
    }

    renderer.top_viewport().pop_local_transform();
    renderer.notify_viewport_changed();
}

	static data_table_desc from_csv(const std::filesystem::path& path, char delimiter = ',', data_table_config config = {}){
		data_table_desc desc{config};

		unsigned col_count{};

		csv::parse_file(path, [&](csv::coord coord, std::string_view sv){
			if(coord.row == 0){
				// 第 0 行：生成表头并统计标准列数 (col_count)
				col_count++;
				auto& e = desc.table_heads_.emplace_back();
				csv::unescape_csv_field(e.data, sv);
				e.line_alignment = (typesetting::line_alignment::center);
			} else{
				// 数据行：如果当前列索引超出了表头列数，直接丢弃（处理多出的锯齿列）
				if(coord.col >= col_count){
					return;
				}

				// 计算当前解析的单元格在一维 table_entries_ 数组中理论上的起始索引
				// 因为数据行是从 coord.row = 1 开始的，所以减去 1
				unsigned expected_index = (coord.row - 1) * col_count + coord.col;

				// 如果数组当前大小落后于理论索引，说明之前的行存在列数不足（短行）
				// 我们需要用空的 entry 进行占位补齐
				while(desc.table_entries_.size() < expected_index){
					desc.table_entries_.emplace_back();
				}

				// 正常追加当前解析出的单元格
				auto& e = desc.table_entries_.emplace_back();
				csv::unescape_csv_field(e.data, sv);
				e.line_alignment = (csv::is_numeric(e.data)
					                    ? typesetting::line_alignment::end
					                    : typesetting::line_alignment::start);
			}
		}, delimiter);

		// 收尾处理：如果整个文件的最后一行是短行，解析回调结束时它还没有被补齐。
		// 这里确保最终的一维数组大小是 col_count 的整数倍。
		if(col_count > 0){
			while(desc.table_entries_.size() % col_count != 0){
				desc.table_entries_.emplace_back();
			}
		}

		return desc;
	}

	[[nodiscard]] math::vec2 get_extent() const{
		return extent_.temp;
	}

	bool set_col_temp_size(std::size_t col, float delta_size) noexcept{
		auto& c = table_heads_[col];
		float prev = c.actual_size.temp;
		float next = math::max(c.actual_size.base + delta_size,
		                       math::max(4.f, math::min(config_.entry_height, c.glyph_layout.extent.x)));
		if(util::try_modify(c.actual_size.temp, next)){
			const auto dx = next - prev;
			extent_.temp = extent_.base.copy().add_x(dx);
			return true;
		}
		return false;
	}

	void apply_col_temp_size(std::size_t col) noexcept{
		auto& c = table_heads_[col];
		c.actual_size.apply();
		extent_.apply();
	}

	template <std::invocable<std::size_t> Fn>
	void hit_col_bound(math::vec2 pos, Fn&& fn){
		static constexpr float margin = 16;
		if(!util::contains(pos, extent_.temp, {margin, margin})) return;
		float cur_x{config_.pad.x * .5f};
		auto cols = get_col_count();
		for(auto i = 0uz; i < cols; ++i){
			auto bound_L = cur_x + table_heads_[i].actual_size.temp - margin;
			auto bound_R = cur_x + table_heads_[i].actual_size.temp + config_.pad.x + margin;
			if(bound_L <= pos.x && pos.x <= bound_R){
				std::invoke(fn, i);
				break;
			}
			cur_x += table_heads_[i].actual_size.temp + config_.pad.x;
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
			extent.x += config_.pad.x + table_head.actual_size.temp;
		}
		extent.x += config_.pad.x;
		return extent;
	}
};

struct interface : scroll_adaptor_apply_interface_schema<data_table_desc>{
	math::vec2 get_extent(const element_type& element) const{
		return element.get_extent();
	}

	void layout_elem(element_type& element) const{
		if(element.check_changed())element.try_update_glyph_layouts();
	}

	std::optional<math::vec2> pre_acq_size(element_type& element, layout::optional_mastering_extent bound) const{
		return element.pre_acquire_size(bound);
	}

	void draw_layer(const element_type& element, const scroll_adaptor_base& scroll_adaptor_base, rect clipSpace,
	                fx::layer_param_pass_t param) const{
		if(!param.is_top()) return;
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
			table.hit_col_bound(event.src + delta - content_src_offset(), [&](std::size_t col){
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

