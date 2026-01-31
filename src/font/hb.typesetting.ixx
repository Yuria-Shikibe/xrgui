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


namespace mo_yanxi::font::hb {

export enum struct layout_direction {
    deduced, ltr, rtl, ttb, btt
};

export enum struct linefeed {
    LF, CRLF
};

export enum struct content_alignment{
	start,      // 左对齐(LTR) / 上对齐(TTB)
	center,     // 居中
	end,        // 右对齐(LTR) / 下对齐(TTB)
	justify     // 两端对齐 (均匀分布)
};

export struct layout_result {
    math::frect aabb;
    glyph_borrow texture;
};

export struct glyph_layout {
    std::vector<layout_result> elems;
    math::vec2 extent;
};

// 辅助函数
font_ptr create_harfbuzz_font(const font_face_handle& face) {
    hb_font_t* font = hb_ft_font_create_referenced(face);
    return font_ptr{font};
}

constexpr float normalize_hb_pos(hb_position_t pos) {
    return static_cast<float>(pos) / 64.0f;
}

export struct layout_config {
    layout_direction direction;
    math::vec2 max_extent = math::vectors::constant2<float>::inf_positive_vec2;
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

struct layout_block {
	std::vector<layout_result> glyphs;
	math::vec2 total_advance{};
	math::vec2 pos_min{ math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

	void clear() {
		glyphs.clear();
		total_advance = {};
		pos_min =  math::vectors::constant2<float>::inf_positive_vec2;
		pos_max = -math::vectors::constant2<float>::inf_positive_vec2;
	}

	void push_back(math::frect glyph_region, const layout_result& glyph){
		glyphs.push_back(glyph);
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
	}

	void push_front(math::frect glyph_region, const layout_result& glyph, math::vec2 glyph_advance){
		for (auto && layout_result : glyphs){
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
		if(glyphs.empty()){return {};}
		return {tags::unchecked, tags::from_vertex, pos_min, pos_max};
	}
};

// --- 内部：排版上下文封装 ---
// 将排版过程中的状态封装在此类中，避免 lambda 捕获过多变量
struct layout_context {

	struct line_record{
		std::size_t start_index; // 对应 results.elems 的起始下标
		std::size_t count;       // 本行包含的字形数量
		float line_advance;      // 本行在主轴上的实际长度
	};

	struct indicator_cache {
		glyph_borrow texture; // 纹理引用
		math::frect glyph_aabb;     // 位于原点(0,0)时的相对AABB
		math::vec2 advance;   // 绘制后笔触需要移动的距离
	};

private:

    // 引用外部资源
    font_manager& manager;
    font_face_view view;
    layout_config config;
    const type_setting::tokenized_text* full_text;

    // 缓存
    lru_cache<font_face_handle*, font_ptr, 4> hb_cache_;
    hb::buffer_ptr hb_buffer_;

    // 排版状态
    glyph_layout results;
    math::vec2 pen{};
    math::vec2 min_bound{ math::vectors::constant2<float>::inf_positive_vec2};
    math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};

    // 布局常量
    math::vec2 scale_factor{1.f, 1.f};
    float line_spacing = 0.f;
    float space_width = 0.f;

    hb_direction_t target_hb_dir = HB_DIRECTION_LTR;

    // 轴指针：用于抽象水平/垂直布局
    float math::vec2::* major_p = &math::vec2::x;
    float math::vec2::* minor_p = &math::vec2::y;


	std::vector<line_record> lines_;
	std::size_t current_line_start_idx_ = 0;

	layout_block current_block{};

	indicator_cache cached_indicator_;

	type_setting::rich_text_context rich_text_context_;
	type_setting::tokenized_text::token_iterator last_iterator{full_text->get_init_token()};
public:
	// 构造函数：初始化所有布局参数
	layout_context(
		font_manager& m, font_face_view& v,
		const layout_config& c, const type_setting::tokenized_text& t
	) : manager(m)
		, view(v)
		, config(c)
		, full_text(&t){
		// 计算 snapped size
    	const glyph_size_type snapped_size{get_snapped_font_size()};

        hb_buffer_ = hb::make_buffer();
        results.elems.reserve(full_text->get_text().size());

        // 计算缩放因子
        scale_factor = config.font_size.as<float>() / snapped_size.as<float>();

        // 初始化方向和间距
    	for (auto& face : view) {
    		(void)face.set_size(snapped_size);
    	}

    	const math::vec2 base_spacing = view.get_line_spacing_vec() * scale_factor;

    	if (config.direction != layout_direction::deduced) {
    		switch (config.direction) {
    		case layout_direction::ltr: target_hb_dir = HB_DIRECTION_LTR; break;
    		case layout_direction::rtl: target_hb_dir = HB_DIRECTION_RTL; break;
    		case layout_direction::ttb: target_hb_dir = HB_DIRECTION_TTB; break;
    		case layout_direction::btt: target_hb_dir = HB_DIRECTION_BTT; break;
    		default: break;
    		}
    	}else{
    		hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text->get_text().data()), static_cast<int>(full_text->get_text().size()), 0, -1);
    		hb_buffer_guess_segment_properties(hb_buffer_.get());
    		target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
    		if(target_hb_dir == HB_DIRECTION_INVALID){
    			target_hb_dir = HB_DIRECTION_LTR;
    		}
    	}

    	auto& primary_face = view.face();
    	// 加载空格以确定宽度
    	FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

    	if (is_vertical_()) {
    		major_p = &math::vec2::y;
    		minor_p = &math::vec2::x;
    		line_spacing = base_spacing.x;

    		space_width = line_spacing; // 垂直模式下通常不直接用 advance.x
    	} else {
    		major_p = &math::vec2::x;
    		minor_p = &math::vec2::y;
    		line_spacing = base_spacing.y;

    		space_width = normalize_hb_pos(primary_face->glyph->advance.x);
    	}

		line_spacing = line_spacing * config.line_spacing_scale + config.line_spacing_scale;

		if (config.has_wrap_indicator()) {
			auto [face, index] = view.find_glyph_of(config.wrap_indicator_char);
			// 只有当能找到对应的字形时才启用
			if (index) {
				glyph_identity id{index, snapped_size};
				// 获取字形 (注意：这里直接同步获取，假设 manager 内部处理好了)
				glyph g = manager.get_glyph_exact(*face, id);

				// 计算步进 (参照 process_text_run 中的逻辑)
				math::vec2 adv{};
				const auto& m = g.metrics();

				const auto advance = m.advance * scale_factor;
				adv.*major_p = advance.*major_p * (is_vertical_() ? -1 : 1);

				math::frect local_aabb = m.place_to({}, scale_factor);

				cached_indicator_ = indicator_cache{
					std::move(g), // glyph 继承自 glyph_borrow，可以切片保存或移动
					local_aabb,
					adv
				};
			}else{
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

	[[nodiscard]] glyph_size_type get_snapped_font_size() const noexcept{
		return {
				get_snapped_size(config.font_size.x),
				get_snapped_size(config.font_size.y)
			};
	}

	void commit_line_state() noexcept {
		const float current_line_width = std::abs(pen.*major_p);
		const std::size_t current_count = results.elems.size() - current_line_start_idx_;

		// 只有当行内有内容时才记录
		if (current_count > 0 || current_line_width > 0) {
			lines_.push_back({current_line_start_idx_, current_count, current_line_width});
		}

		// 更新下一行的起始索引
		current_line_start_idx_ = results.elems.size();
	}

    // 换行操作
    void advance_line() noexcept {
		commit_line_state();

        pen.*minor_p += line_spacing;
        pen.*major_p = 0;
    }

    // 将累积的 Block 刷入最终结果
    bool flush_block() {
        if (current_block.glyphs.empty()) return true;

		{
        	//Check bounding box, if exceeded, directly exit.

        	const auto get_max_extent = [&](math::frect region){
        		if(min_bound.is_Inf()){
        			return region.extent();
        		}else{
        			return region.copy().expand_by({tags::unchecked, tags::from_vertex, min_bound, max_bound}).extent();
        		}
        	};

	        const auto global_region = current_block.get_local_draw_bound().copy().move(pen);
	        if(const auto target_max_major = get_max_extent(global_region).*major_p;
	        target_max_major > config.max_extent.*major_p){
        		//try advance line, put it to line head
        		advance_line();

	        	if (config.has_wrap_indicator()) {
	        		const auto& ind = cached_indicator_;
					current_block.push_front(
						ind.glyph_aabb,
						{ind.glyph_aabb.copy().expand(font_draw_expand * scale_factor), cached_indicator_.texture},
						ind.advance);
	        	}

				const auto region_new = current_block.get_local_draw_bound().copy().move(pen);
				const auto extent_new = get_max_extent(region_new);
        		if(extent_new.*major_p > config.max_extent.*major_p || extent_new.*minor_p > config.max_extent.*minor_p
        			){
        			//the next line can't hold it, directly fail
        			return false;
        		}

		        min_bound.min(region_new.vert_00());
		        max_bound.max(region_new.vert_11());
	        }else{
        		if(get_max_extent(global_region).*minor_p > config.max_extent.*minor_p){
        			return false;
        		}

        		min_bound.min(global_region.vert_00());
        		max_bound.max(global_region.vert_11());
        	}
		}

        for (auto&& item : current_block.glyphs) {
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

    // 处理单个文本片段
    bool process_text_run(
    	std::size_t start,
    	std::size_t length,
    	font_face_handle* face) {
        hb_buffer_clear_contents(hb_buffer_.get());
        hb_buffer_add_utf32(
            hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text->get_text().data()),
            static_cast<int>(full_text->get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
        hb_buffer_set_direction(hb_buffer_.get(), target_hb_dir);
        hb_buffer_guess_segment_properties(hb_buffer_.get());

        // 获取 HB Font
        auto* raw_hb_font = get_hb_font_(face);

        hb_shape(raw_hb_font, hb_buffer_.get(), nullptr, 0);

        unsigned int len;
        hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		std::span sz{infos, len};


        for (unsigned int i = 0; i < len; ++i) {
            const glyph_index_t gid = infos[i].codepoint;

        	//TODO get char32_t and check

            const auto ch = full_text->get_text()[infos[i].cluster];

            // --- 关键修改：遇到分隔符先结算 Block ---
            if (bool is_delimiter = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
                if(!flush_block()){
	                return false;
                }
            }

            // 处理控制字符
            switch (ch) {
                case '\r':
                    if (config.line_feed_type == linefeed::CRLF){
                    	commit_line_state();
	                    pen.*major_p = 0;
                    }
                    continue;
                case '\n':
                    if (config.line_feed_type == linefeed::CRLF) pen.*minor_p += line_spacing;
                    else advance_line();
                    continue;
                case '\t': {
                    float tab_step = config.tab_scale * space_width;
                    if (tab_step > std::numeric_limits<float>::epsilon()) {
                        pen.*major_p = (std::floor(pen.*major_p / tab_step) + 1) * tab_step;
                    }
                    continue;
                }
                case ' ':
                    // 空格不进 Block，直接移动 Pen
                    // HarfBuzz 已经计算了空格的 advance，我们需要手动应用它，因为 flush_block 只处理了非空格字符
                    {
                		const auto sp = math::vec2{normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)} * scale_factor;
						pen = move_pen(pen, sp);
                    }
                    continue;
                default: break;
            }

            // 获取字形
            const glyph_identity id{gid, get_snapped_font_size()};
            glyph g = manager.get_glyph_exact(*face, id);

            // HarfBuzz 给出的数据
        	const auto advance = math::vec2{normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)} * scale_factor;

            const float x_offset = normalize_hb_pos(pos[i].x_offset) * scale_factor.x;
            const float y_offset = normalize_hb_pos(pos[i].y_offset) * scale_factor.y;

            // 计算局部 AABB (相对于当前笔触)
            math::vec2 glyph_local_draw_pos{x_offset, -y_offset};
            // 修正：在 RTL 模式下，offset 的方向可能需要根据 HarfBuzz 文档细调，通常 HarfBuzz 的 offset 是基于可视方向的


        	if(is_reversed_()){
        		current_block.total_advance = move_pen(current_block.total_advance, advance);
        	}

        	const math::frect actual_aabb = g.metrics().place_to(current_block.total_advance + glyph_local_draw_pos, scale_factor);
        	const math::frect draw_aabb = actual_aabb.copy().expand(font_draw_expand * scale_factor);

        	current_block.push_back(actual_aabb, {draw_aabb, std::move(g)});

            // 累加 Block 宽度
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

		while (current_idx < full_text->get_text().size()) {
			auto tokens = full_text->get_token_group(current_idx, last_iterator);
			rich_text_context_.update(manager, {32, 32}, tokens);
			last_iterator = tokens.end();

			const auto codepoint = full_text->get_text()[current_idx];

			auto [best_face, _] = view.find_glyph_of(codepoint);

			if (current_face == nullptr) {
				current_face = best_face;
			} else if (best_face != current_face) {
				// 字体改变，处理旧 Run
				process_text_run(run_start, current_idx - run_start, current_face);

				// 开始新 Run
				current_face = best_face;
				run_start = current_idx;
			}

			current_idx++;
		}

		// 处理最后一段
		if (current_face != nullptr) {
			process_text_run(run_start, full_text->get_text().size() - run_start, current_face);
		}

		// 确保最后的 Block 被输出
		flush_block();
		commit_line_state();
	}

	void finalize(){
		if(results.elems.empty()){
			results.extent = {0, 0};
			return;
		}

        // 1. 计算总包围盒 (Determine global bounding box)
        auto extent = max_bound - min_bound;
        results.extent = extent;

        // 2. 获取对齐所需的基准尺寸 (主轴方向)
        // 用户要求：基于 results.extent 进行对齐
        // 注意：如果是 justify，通常是基于 max_extent (如果在配置里设置了限制)，
        // 但这里我们严格按照 results.extent (即最宽的那一行) 作为对齐容器的宽度。
        const float container_size = results.extent.*major_p;

        // 3. 应用对齐偏移
        if (config.align != content_alignment::start) {
            for (const auto& line : lines_) {
                // 如果是最后一行且要求 Justify，通常排版惯例是最后一行左对齐(Start)，
                // 除非显式要求强制对齐。这里简单处理：单行不满不Justify，或者全部Justify。
                // 常见的处理是：if (config.align == justify && is_last_line) continue;
                // 这里暂且对所有行一视同仁。

                float offset = 0.f;
                float spacing_step = 0.f; // 仅用于 Justify

                const float remaining_space = container_size - line.line_advance;

                // 容差，避免浮点误差导致的微小偏移
                if (remaining_space <= 0.001f) continue;

                switch (config.align) {
                    case content_alignment::center:
                        offset = remaining_space / 2.0f;
                        break;
                    case content_alignment::end:
                        offset = remaining_space;
                        break;
                    case content_alignment::justify:
                        if (line.count > 1) {
                            spacing_step = remaining_space / static_cast<float>(line.count - 1);
                        }
                        break;
                    default: break;
                }

                // 对该行的每个字形应用偏移
                // 注意：偏移是沿着 major 轴进行的
                // 对于 Justify，每个字形的偏移量是累加的

                math::vec2 move_vec{};

                for (std::size_t i = 0; i < line.count; ++i) {
                    auto& elem = results.elems[line.start_index + i];

                    float current_offset = offset;
                    if (config.align == content_alignment::justify) {
                        current_offset += spacing_step * static_cast<float>(i);
                    }

                    // 设置偏移向量
                    move_vec.*major_p = current_offset;
                    // minor 轴不需要由于对齐而移动
                    move_vec.*minor_p = 0;

                    elem.aabb.move(move_vec);
                }
            }
        }

        // 4. 标准化坐标 (将左上角/起始点对齐到 0,0)
        // 注意：min_bound 是在对齐操作*之前*计算的原始边界。
        // 对齐操作（特别是 Center/End）会改变内容的实际分布，但不会改变 Container 的大小。
        // 我们需要保持所有内容相对于 Container 的相对位置。
        // 原有逻辑是将 min_bound 移回 0,0。
        // 在对齐模式下，min_bound.*major_p 可能不再是内容的左边界（例如 Right Align 时左侧是空的）。
        // 这里的逻辑：results.extent 已经是正确的容器大小。
        // 我们应该根据 min_bound 将所有内容平移，使得原点 (0,0) 对应 min_bound 的位置。

        for(auto& elem : results.elems){
            elem.aabb.move(-min_bound);
        }
	}

	[[nodiscard]] glyph_layout&& crop() && {
		return std::move(results);
	}
};

export glyph_layout layout_text(
    font_manager& manager,
    const font_family& font_group,
    const type_setting::tokenized_text& text,
    const layout_config& config
) {
    glyph_layout empty_result{};
    if (text.get_text().empty()) return empty_result;

    auto view = font_face_view{manager.use_family(&font_group)};
    if (std::ranges::empty(view)) return empty_result;


    // 初始化上下文
    layout_context ctx(manager, view, config, text);
	ctx.process_layout();
	ctx.finalize();

    return std::move(ctx).crop();
}

} // namespace mo_yanxi::font::hb