module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <cassert>

export module mo_yanxi.hb.typesetting;

import std;
import mo_yanxi.font;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.math;
import mo_yanxi.font.manager;
import mo_yanxi.hb.wrap;
import mo_yanxi.graphic.image_region;
import mo_yanxi.cache;

namespace mo_yanxi::font::hb{
export enum struct layout_direction{
	deduced, ltr, rtl, ttb, btt
};

export enum struct linefeed{
	LF, CRLF
};

export enum struct content_alignment{
	start, // 左对齐(LTR) / 上对齐(TTB)
	center, // 居中
	end, // 右对齐(LTR) / 下对齐(TTB)
	justify // 两端对齐 (均匀分布)
};

export struct layout_result{
	math::frect aabb;
	glyph_borrow texture;
	// 可以扩展以支持颜色等富文本属性
	// graphic::color color;
};

export struct glyph_layout{
	std::vector<layout_result> elems;
	math::vec2 extent;
};

// 辅助函数
font_ptr create_harfbuzz_font(const font_face_handle& face){
	hb_font_t* font = hb_ft_font_create_referenced(face);
	return font_ptr{font};
}

constexpr float normalize_hb_pos(hb_position_t pos){
	return static_cast<float>(pos) / 64.0f;
}

export struct layout_config{
	layout_direction direction;
	math::vec2 max_extent = math::vectors::constant2<float>::inf_positive_vec2;
	// 基础字号，若富文本未指定则使用此值
	glyph_size_type font_size;
	linefeed line_feed_type;
	content_alignment align = content_alignment::start;

	float tab_scale = 4.f;

	float line_spacing_scale = 1.25f;
	float line_spacing_fixed_distance = 0.f;

	char32_t wrap_indicator_char = U'\u2925';

	constexpr bool has_wrap_indicator() const noexcept{
		return wrap_indicator_char;
	}
};

struct layout_block{
	std::vector<layout_result> glyphs;
	math::vec2 total_advance{};
	math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

	// [新增] 暂存该 Block 内部的最大 Metrics
	float block_ascender = 0.f;
	float block_descender = 0.f;

	void clear(){
		glyphs.clear();
		total_advance = {};
		pos_min = math::vectors::constant2<float>::inf_positive_vec2;
		pos_max = -math::vectors::constant2<float>::inf_positive_vec2;
		block_ascender = 0.f;
		block_descender = 0.f;
	}

	void push_back(math::frect glyph_region, const layout_result& glyph){
		glyphs.push_back(glyph);
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
	}

	void push_front(math::frect glyph_region, const layout_result& glyph, math::vec2 glyph_advance){
		for(auto&& layout_result : glyphs){
			layout_result.aabb.move(glyph_advance);
		}
		glyphs.insert(glyphs.begin(), glyph);

		pos_min += glyph_advance;
		pos_max += glyph_advance;
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());

		total_advance += glyph_advance;
	}

	math::frect get_local_draw_bound(){
		if(glyphs.empty()){ return {}; }
		return {tags::unchecked, tags::from_vertex, pos_min, pos_max};
	}
};

// --- 内部：排版上下文封装 ---
struct layout_context{
	struct line_record{
		std::size_t start_index; // 对应 results.elems 的起始下标
		std::size_t count; // 本行包含的字形数量
		float line_advance; // 本行在主轴上的实际长度
	};

	struct indicator_cache{
		glyph_borrow texture; // 纹理引用
		math::frect glyph_aabb; // 位于原点(0,0)时的相对AABB
		math::vec2 advance; // 绘制后笔触需要移动的距离
	};

private:
	font_manager& manager;
	font_face_view base_view; // 仅作为初始参考
	layout_config config;
	const type_setting::tokenized_text* full_text;

	lru_cache<font_face_handle*, font_ptr, 4> hb_cache_;
	hb::buffer_ptr hb_buffer_;

	glyph_layout results;
	math::vec2 pen{};
	math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};

	// --- 动态行距状态 ---
	// 不再存储单一的 line_spacing，而是存储当前行的最大上延和下延（或宽度）
	// 对于水平排版：x = ascender, y = descender (absolute values)
	// 对于垂直排版：x = left_half_width, y = right_half_width (或者统称为 thickness)
	float current_line_ascender = 0.f;
	float current_line_descender = 0.f;
	float default_line_thickness = 0.f; // 用于空行的回退高度

	float prev_baseline_y = 0.f;
	float prev_descent = 0.f;

	// 用于空格宽度的基准（从默认字体获取）
	float default_space_width = 0.f;

	hb_direction_t target_hb_dir = HB_DIRECTION_LTR;

	float math::vec2::* major_p = &math::vec2::x;
	float math::vec2::* minor_p = &math::vec2::y;

	std::vector<line_record> lines_;
	std::size_t current_line_start_idx_ = 0;

	layout_block current_block{};

	indicator_cache cached_indicator_;

	type_setting::rich_text_context rich_text_context_;
	type_setting::tokenized_text::token_iterator last_iterator;

public:
	layout_context(
		font_manager& m, font_face_view& v,
		const layout_config& c, const type_setting::tokenized_text& t
	) : manager(m)
		, base_view(v)
		, config(c)
		, full_text(&t)
		, last_iterator(t.get_init_token()){
		hb_buffer_ = hb::make_buffer();
		results.elems.reserve(full_text->get_text().size());

		// 1. 确定书写方向
		if(config.direction != layout_direction::deduced){
			switch(config.direction){
			case layout_direction::ltr : target_hb_dir = HB_DIRECTION_LTR;
				break;
			case layout_direction::rtl : target_hb_dir = HB_DIRECTION_RTL;
				break;
			case layout_direction::ttb : target_hb_dir = HB_DIRECTION_TTB;
				break;
			case layout_direction::btt : target_hb_dir = HB_DIRECTION_BTT;
				break;
			default : break;
			}
		} else{
			hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text->get_text().data()),
				static_cast<int>(full_text->get_text().size()), 0, -1);
			hb_buffer_guess_segment_properties(hb_buffer_.get());
			target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
			if(target_hb_dir == HB_DIRECTION_INVALID){
				target_hb_dir = HB_DIRECTION_LTR;
			}
		}

		// 2. 初始化轴向和基准度量
		// 使用 config.font_size 作为基准来计算空格宽度和默认行高
		const auto snapped_base_size = get_snapped_size_vec(config.font_size);
		auto& primary_face = base_view.face();
		(void)primary_face.set_size(snapped_base_size);
		// 预加载空格以获取度量
		FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

		// 计算基准缩放 (用于将 font metrics 转换到 config.font_size)
		// 注意：FreeType metrics 通常是 26.6 格式，normalize_len 已处理除以 64
		// 但这里我们使用 primary_face 设置为 snapped_size，所以需要缩放到 config.font_size
		math::vec2 base_scale_factor = config.font_size.as<float>() / snapped_base_size.as<float>();

		if(is_vertical_()){
			major_p = &math::vec2::y;
			minor_p = &math::vec2::x;

			// 垂直排版：宽度作为“行高”
			default_line_thickness = normalize_len(primary_face->size->metrics.max_advance) * base_scale_factor.x;
			// 垂直模式空格高度
			default_space_width = default_line_thickness;
		} else{
			major_p = &math::vec2::x;
			minor_p = &math::vec2::y;

			// 水平排版：Ascender + Descender (Height)
			default_line_thickness = normalize_len(primary_face->size->metrics.height) * base_scale_factor.y;
			default_space_width = normalize_hb_pos(primary_face->glyph->advance.x) * base_scale_factor.x;
		}

		// 初始化当前行的 metrics，避免第一行没有内容时换行出错
		current_line_ascender = is_vertical_()
			                        ? default_line_thickness / 2.f
			                        : normalize_len(primary_face->size->metrics.ascender) * base_scale_factor.y;
		current_line_descender = is_vertical_()
			                         ? default_line_thickness / 2.f
			                         : normalize_len(primary_face->size->metrics.descender) * base_scale_factor.y;

		// 3. 处理换行指示符 (Wrap Indicator)
		// 注意：指示符的大小应该跟随其插入点的上下文，但这里作为配置项，我们暂时用 Base Size 初始化它
		if(config.has_wrap_indicator()){
			auto [face, index] = base_view.find_glyph_of(config.wrap_indicator_char);
			if(index){
				glyph_identity id{index, snapped_base_size};
				glyph g = manager.get_glyph_exact(*face, id);

				const auto& m = g.metrics();
				const auto advance = m.advance * base_scale_factor;

				math::vec2 adv{};
				adv.*major_p = advance.*major_p * (is_vertical_() ? -1 : 1);

				math::frect local_aabb = m.place_to({}, base_scale_factor);

				cached_indicator_ = indicator_cache{
						std::move(g),
						local_aabb,
						adv
					};
			} else{
				config.wrap_indicator_char = 0;
			}
		}
	}

	bool is_reversed_() const noexcept{
		return target_hb_dir == HB_DIRECTION_RTL || target_hb_dir == HB_DIRECTION_BTT;
	}

	bool is_vertical_() const noexcept{
		return target_hb_dir == HB_DIRECTION_TTB || target_hb_dir == HB_DIRECTION_BTT;
	}

	// 获取当前上下文请求的字体大小，并对齐到 snap 值
	[[nodiscard]] glyph_size_type get_current_snapped_size() const noexcept{
		auto req_size = rich_text_context_.get_size(config.font_size.as<float>());
		return get_snapped_size_vec(req_size);
	}

	static glyph_size_type get_snapped_size_vec(glyph_size_type v) noexcept{
		return {get_snapped_size(v.x), get_snapped_size(v.y)};
	}

	static glyph_size_type get_snapped_size_vec(math::vec2 v) noexcept{
		return get_snapped_size_vec(v.as<glyph_size_type::value_type>());
	}

	void commit_line_state() noexcept{
		const float current_line_width = std::abs(pen.*major_p);
		const std::size_t current_count = results.elems.size() - current_line_start_idx_;

		if(current_count > 0 || current_line_width > 0){
			lines_.push_back({current_line_start_idx_, current_count, current_line_width});
		}
		current_line_start_idx_ = results.elems.size();
	}

	// 动态换行逻辑
	void advance_line() noexcept{
		commit_line_state();

		// [新增] 在重置前，保存当前行的基线和下延，供下一行排版时参考
		prev_baseline_y = pen.*minor_p;
		// 如果当前行是空的（使用默认高度），则估算一个下延
		if(current_line_ascender + current_line_descender < 0.001f){
			prev_descent = default_line_thickness / 2.0f;
		} else{
			prev_descent = current_line_descender;
		}

		// 1. 计算当前行的实际高度/厚度
		float actual_thickness = current_line_ascender + current_line_descender;
		if(actual_thickness < 0.001f){
			actual_thickness = default_line_thickness;
		}

		// 2. 应用行距缩放和固定间距
		float step = actual_thickness * config.line_spacing_scale + config.line_spacing_fixed_distance;

		// 3. 移动笔触
		pen.*minor_p += step;
		pen.*major_p = 0;

		// 4. 重置行 metrics 为下一行做准备
		// 重置为 0，等待新行的 glyphs 来撑开它
		current_line_ascender = 0.f;
		current_line_descender = 0.f;
	}

	bool flush_block(){
		if(current_block.glyphs.empty()) return true;

		const auto get_max_extent = [&](math::frect region){
			if(min_bound.is_Inf()){
				return region.extent();
			} else{
				return region.copy().expand_by({tags::unchecked, tags::from_vertex, min_bound, max_bound}).extent();
			}
		};

		const auto global_region = current_block.get_local_draw_bound().copy().move(pen);

		// 检查主轴溢出
		if(const auto target_max_major = get_max_extent(global_region).*major_p;
			target_max_major > config.max_extent.*major_p){
			// --- 发生溢出，执行换行 ---

			// 1. 提交当前行 (此时 current_line_ascender/descender 尚未包含 current_block)
			//    这保证了上一行不会被这个溢出的 block 撑大。
			advance_line();

			// 2. 处理 Wrap Indicator (如果有)
			if(config.has_wrap_indicator()){
				const auto& ind = cached_indicator_;
				// 注意：这里可能需要更新 block metrics 来包含 indicator 的大小
				// 但 indicator 通常较小，且逻辑较复杂，暂时假设它不影响行高或单独处理
				current_block.push_front(
					ind.glyph_aabb,
					{ind.glyph_aabb.copy().expand(font_draw_expand), cached_indicator_.texture},
					ind.advance);
			}

			// 3. 将 Block 的 Metrics 应用到 *新* 的一行
			//    因为 advance_line() 已经重置了 current_line_ascender/descender 为 0
			current_line_ascender = current_block.block_ascender;
			current_line_descender = current_block.block_descender;

			// 4. 二次检查：换行后是否依然溢出 (比如单个单词超长或垂直溢出)
			const auto region_new = current_block.get_local_draw_bound().copy().move(pen);
			const auto extent_new = get_max_extent(region_new);

			if(extent_new.*major_p > config.max_extent.*major_p || extent_new.*minor_p > config.max_extent.*minor_p){
				return false;
			}

			min_bound.min(region_new.vert_00());
			max_bound.max(region_new.vert_11());
		} else{
			// --- 未溢出，留在当前行 ---

			// 检查副轴溢出 (通常是垂直高度超限)
			if(get_max_extent(global_region).*minor_p > config.max_extent.*minor_p){
				return false;
			}

			// [新增] 动态行距修正：检查当前 Block 的 Ascender 是否会撞到上一行
			// 仅当不是第一行时才需要检查
			if(!lines_.empty()){
				const float new_ascender = std::max(current_line_ascender, current_block.block_ascender);

				// 计算需要的安全距离：上一行下延 + 当前行上延 + 间距系数
				// 这里使用 line_spacing_scale 来保证行间距
				const float required_gap = (prev_descent + new_ascender) * config.line_spacing_scale + config.
					line_spacing_fixed_distance;

				// 当前基线与上一行基线的距离
				const float current_dist = pen.*minor_p - prev_baseline_y;

				// 如果当前距离不足，需要下移
				if(current_dist < required_gap){
					const float shift_down = required_gap - current_dist;

					// 1. 移动笔触（基线）
					pen.*minor_p += shift_down;

					// 2. 回溯修正：移动当前行已排版的所有字形
					math::vec2 shift_vec{};
					shift_vec.*minor_p = shift_down;

					for(std::size_t i = current_line_start_idx_; i < results.elems.size(); ++i){
						results.elems[i].aabb.move(shift_vec);
					}
				}
			}

			// [关键] 将 Block 的 Metrics 合并入当前行
			current_line_ascender = std::max(current_line_ascender, current_block.block_ascender);
			current_line_descender = std::max(current_line_descender, current_block.block_descender);

			min_bound.min(global_region.vert_00());
			max_bound.max(global_region.vert_11());
		}

		// 应用位置
		for(auto&& item : current_block.glyphs){
			item.aabb.move(pen);
		}
		results.elems.append_range(current_block.glyphs | std::views::as_rvalue);
		pen += current_block.total_advance;

		current_block.clear();
		return true;
	}

	hb_font_t* get_hb_font_(font_face_handle* face) noexcept{
		if(auto ptr = hb_cache_.get(face)){
			return ptr->get();
		}
		auto new_hb_font = create_harfbuzz_font(*face);
		hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
		const auto rst = new_hb_font.get();
		hb_cache_.put(face, std::move(new_hb_font));
		return rst;
	}

	math::vec2 move_pen(math::vec2 pen, math::vec2 advance) const noexcept{
		// 根据 HarfBuzz 方向移动笔触
		if(target_hb_dir == HB_DIRECTION_LTR){
			pen.x += advance.x;
			pen.y -= advance.y;
		} else if(target_hb_dir == HB_DIRECTION_RTL){
			pen.x -= advance.x;
			pen.y -= advance.y;
		} else if(target_hb_dir == HB_DIRECTION_TTB){
			pen.x += advance.x;
			pen.y -= advance.y;
		} else if(target_hb_dir == HB_DIRECTION_BTT){
			pen.x += advance.x;
			pen.y += advance.y;
		}
		return pen;
	}

	// 处理 Text Run (一段连续的、相同富文本属性的文本)
	bool process_text_run(
		std::size_t start,
		std::size_t length,
		font_face_handle* face){
		hb_buffer_clear_contents(hb_buffer_.get());
		hb_buffer_add_utf32(
			hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text->get_text().data()),
			static_cast<int>(full_text->get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
		hb_buffer_set_direction(hb_buffer_.get(), target_hb_dir);
		hb_buffer_guess_segment_properties(hb_buffer_.get());

		// 1. 获取当前富文本状态
		const auto req_size_vec = rich_text_context_.get_size(config.font_size.as<float>()).round<
			glyph_size_type::value_type>();
		const auto snapped_size = get_snapped_size_vec(req_size_vec);
		const auto rich_offset = rich_text_context_.get_offset(); // 用户自定义的偏移 (如上下标)

		// 2. 设置字体大小 (这会影响 face 的内部 metrics)
		// 注意：face 是共享的 handle，修改 size 会影响全局？
		// font_face_handle 只是 wrapper，底层的 FT_Face 是共享的。
		// HarfBuzz font 是从 FT_Face 创建的。
		// 只要我们在 hb_shape 之前 set_size，并且 hb_font 关联正确即可。
		// 但为了线程安全和正确性，font_manager 应该保证 face 在使用时是独占或无所谓状态的。
		// 这里假设 set_size 是安全的 (font_face_view 可能持有的是 thread-local handle).
		face->set_size(snapped_size);

		// 3. 计算缩放因子 (Requested / Snapped)
		math::vec2 run_scale_factor = req_size_vec.as<float>() / snapped_size.as<float>();

		auto* raw_hb_font = get_hb_font_(face);
		hb_shape(raw_hb_font, hb_buffer_.get(), nullptr, 0);

		unsigned int len;
		hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		for(unsigned int i = 0; i < len; ++i){
			const glyph_index_t gid = infos[i].codepoint;
			const auto ch = full_text->get_text()[infos[i].cluster];

			// 遇到分隔符先结算 Block
			if(bool is_delimiter = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')){
				if(!flush_block()){
					return false;
				}
			}

			// --- 动态行距更新 ---
			// 获取字形的原始 metrics 并缩放，更新当前行的最大高度/厚度
			// 我们需要获取字形的 metrics。HarfBuzz 并没有直接提供 metrics (除了 advance)。
			// 我们需要通过 manager 获取 glyph 对象 (其中包含 metrics)。
			// 优化：对于空格等不可见字符，可能不需要加载 glyph 图像，但需要 metrics。
			// 这里为了简单，统一获取。
			glyph_identity id{gid, snapped_size};

			// 注意：这里可能会比较慢，因为每次都查。但在 rich text 场景下必须如此。
			// 可以通过缓存优化。
			const auto& glyph_metrics_ref = [&]() -> const glyph_metrics&{
				// Hack: 为了不在此处产生纹理加载开销，可以先只读 metrics?
				// 目前 manager.get_glyph_exact 会加载。
				// 暂时假设必须加载。
				// 实际项目中应有 get_glyph_metrics_only
				return manager.get_glyph_exact(*face, id).metrics();
			}();

			float asc, desc;
			if(is_vertical_()){
				// 垂直：宽度的贡献
				float width = glyph_metrics_ref.size.x * run_scale_factor.x;
				asc = width / 2.f;
				desc = width / 2.f;
			} else{
				// 水平：高度的贡献
				asc = glyph_metrics_ref.ascender() * run_scale_factor.y;
				desc = glyph_metrics_ref.descender() * run_scale_factor.y;
			}

			// [修改] 更新 Block 的 Metrics，而不是全局行的 Metrics
			// 只有非控制字符才贡献高度
			if(ch != '\n' && ch != '\r'){
				current_block.block_ascender = std::max(current_block.block_ascender, asc);
				current_block.block_descender = std::max(current_block.block_descender, desc);
			}

			// --- 控制字符处理 ---
			switch(ch){
			case '\r' : if(config.line_feed_type == linefeed::CRLF){
					commit_line_state();
					pen.*major_p = 0;
				}
				continue;
			case '\n' : if(config.line_feed_type == linefeed::CRLF) pen.*minor_p += 0;
				// LF 已经在 CRLF 中处理了位移? 不，通常 CRLF 是 CR 回首，LF 下移。
				// 修正：CRLF 模式下，\r 回首，\n 换行。
				// 上面 \r 已经回首。这里 \n 负责下移。
				advance_line();
				continue;
			case '\t' :{
				float tab_step = config.tab_scale * default_space_width; // Tab 使用默认宽度
				if(tab_step > std::numeric_limits<float>::epsilon()){
					pen.*major_p = (std::floor(pen.*major_p / tab_step) + 1) * tab_step;
				}
				continue;
			}
			case ' ' :{
				const auto sp = math::vec2{
						normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)
					} * run_scale_factor;
				pen = move_pen(pen, sp);
			}
				continue;
			default : break;
			}

			// --- 字形放置 ---
			glyph g = manager.get_glyph_exact(*face, id);

			const auto advance = math::vec2{
					normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)
				} * run_scale_factor;
			const float x_offset = normalize_hb_pos(pos[i].x_offset) * run_scale_factor.x;
			const float y_offset = normalize_hb_pos(pos[i].y_offset) * run_scale_factor.y;

			// 应用 HarfBuzz 偏移 + Rich Text 自定义偏移
			math::vec2 glyph_local_draw_pos{x_offset, -y_offset};
			glyph_local_draw_pos += rich_offset;

			if(is_reversed_()){
				current_block.total_advance = move_pen(current_block.total_advance, advance);
			}

			// 计算 AABB
			const math::frect actual_aabb = g.metrics().place_to(current_block.total_advance + glyph_local_draw_pos,
				run_scale_factor);
			const math::frect draw_aabb = actual_aabb.copy().expand(font_draw_expand * run_scale_factor);

			current_block.push_back(actual_aabb, {draw_aabb, std::move(g)});

			if(!is_reversed_()){
				current_block.total_advance = move_pen(current_block.total_advance, advance);
			}
		}
		return true;
	}

	void process_layout(){
		std::size_t current_idx = 0;
		std::size_t run_start = 0;
		font_face_handle* current_face = nullptr;

		// 辅助 lambda：获取当前字符在当前 rich text 状态下应该使用的 Face
		auto resolve_face = [&](font_face_handle* current, char32_t codepoint) -> font_face_handle*{
			if(current){
				if(current->index_of(codepoint)) return current;
			}
			// 1. 从 context 获取 font family (如果 context 没设，会返回 default)
			const auto* family = &rich_text_context_.get_font(manager.get_default_family());

			// 2. 临时创建一个 view 来查找 glyph (manager.use_family 会处理缓存)
			auto face_view = manager.use_family(family);

			// 3. 查找该字符的 face
			auto [best_face, _] = face_view.find_glyph_of(codepoint);
			return best_face;
		};

		while(current_idx < full_text->get_text().size()){
			bool state_changed = false;

			// 1. 检查是否有 Rich Text Token
			auto tokens = full_text->get_token_group(current_idx, last_iterator);
			if(!tokens.empty()){
				// 如果有 Token，必须结束当前的 Run
				if(current_face && current_idx > run_start){
					process_text_run(run_start, current_idx - run_start, current_face);
				}

				// 更新 Context
				// 注意：这里传入的 default_font_size 应该是 config.font_size
				rich_text_context_.update(manager, config.font_size.as<float>(), tokens);
				last_iterator = tokens.end();

				// 重置 Run
				run_start = current_idx;
				current_face = nullptr; // 强制重新解析 Face
				state_changed = true;
			}

			const auto codepoint = full_text->get_text()[current_idx];

			// 2. 解析当前字符的 Face
			font_face_handle* best_face = resolve_face(current_face, codepoint);

			if(current_face == nullptr){
				current_face = best_face;
			} else if(best_face != current_face){
				// 字体改变 (可能是 fallback，也可能是 context 里的 font 变了导致 resolve 变了)
				process_text_run(run_start, current_idx - run_start, current_face);
				current_face = best_face;
				run_start = current_idx;
			}

			current_idx++;
		}

		// 处理最后一段
		if(current_face != nullptr){
			process_text_run(run_start, full_text->get_text().size() - run_start, current_face);
		}

		flush_block();
		commit_line_state();
	}

	void finalize(){
		if(results.elems.empty()){
			results.extent = {0, 0};
			return;
		}

		auto extent = max_bound - min_bound;
		results.extent = extent;

		const float container_size = results.extent.*major_p;
		if(config.align != content_alignment::start){
			for(const auto& line : lines_){
				float offset = 0.f;
				float spacing_step = 0.f;
				const float remaining_space = container_size - line.line_advance;

				if(remaining_space <= 0.001f) continue;

				switch(config.align){
				case content_alignment::center : offset = remaining_space / 2.0f;
					break;
				case content_alignment::end : offset = remaining_space;
					break;
				case content_alignment::justify : if(line.count > 1){
						spacing_step = remaining_space / static_cast<float>(line.count - 1);
					}
					break;
				default : break;
				}

				math::vec2 move_vec{};
				for(std::size_t i = 0; i < line.count; ++i){
					auto& elem = results.elems[line.start_index + i];
					float current_offset = offset;
					if(config.align == content_alignment::justify){
						current_offset += spacing_step * static_cast<float>(i);
					}
					move_vec.*major_p = current_offset;
					move_vec.*minor_p = 0;
					elem.aabb.move(move_vec);
				}
			}
		}

		for(auto& elem : results.elems){
			elem.aabb.move(-min_bound);
		}
	}

	[[nodiscard]] glyph_layout&& crop() &&{
		return std::move(results);
	}
};

export glyph_layout layout_text(
	font_manager& manager,
	const font_family& font_group,
	const type_setting::tokenized_text& text,
	const layout_config& config
){
	glyph_layout empty_result{};
	if(text.get_text().empty()) return empty_result;

	// 初始 View 仍需基于传入的 font_group，但后续会根据 token 变化
	auto view = font_face_view{manager.use_family(&font_group)};
	if(std::ranges::empty(view)) return empty_result;

	layout_context ctx(manager, view, config, text);
	ctx.process_layout();
	ctx.finalize();

	return std::move(ctx).crop();
}
} // namespace mo_yanxi::font::hb
