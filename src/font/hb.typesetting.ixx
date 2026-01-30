module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <vulkan/vulkan.h>

export module mo_yanxi.hb.typesetting;

import mo_yanxi.math;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.hb.wrap;
import mo_yanxi.graphic.image_region;
import mo_yanxi.encode;
import std;
import mo_yanxi.cache;

namespace mo_yanxi::font::hb {

export enum struct layout_direction {
    deduced, ltr, rtl, ttb, btt
};

export enum struct linefeed {
    LF, CRLF
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
    float tab_scale = 4.f;

	float line_spacing_scale = 1.25f;
	float line_spacing_fixed_distance = 0.f;

	char32_t wrap_indicator_char = U'\u2925';

	constexpr bool has_wrap_indicator() const noexcept{
		return wrap_indicator_char;
	}
};

// --- 内部：排版上下文封装 ---
// 将排版过程中的状态封装在此类中，避免 lambda 捕获过多变量
struct layout_context {

private:
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

    // 引用外部资源
    font_manager& manager;
    font_face_group_meta& font_group;
    font_face_view view;
    layout_config config;
    std::string_view full_text;

    // 缓存
    lru_cache<font_face_handle*, font_ptr, 4> hb_cache_;
    hb::buffer_ptr hb_buffer_;

    // 排版状态
    glyph_layout results;
    math::vec2 pen{0, 0};
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

	layout_block current_block{};

	struct indicator_cache {
		glyph_borrow texture; // 纹理引用
		math::frect aabb;     // 位于原点(0,0)时的相对AABB
		math::vec2 advance;   // 绘制后笔触需要移动的距离
	};
	indicator_cache cached_indicator_;
public:
	// 构造函数：初始化所有布局参数
	layout_context(
		font_manager& m, font_face_group_meta& fg, font_face_view& v,
		const layout_config& c, std::string_view t
	) : manager(m)
		, font_group(fg)
		, view(v)
		, config(c)
		, full_text(t){
		// 计算 snapped size
    	const glyph_size_type snapped_size{get_snapped_font_size()};

        hb_buffer_ = hb::make_buffer();
        results.elems.reserve(t.size());

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
    		hb_buffer_add_utf8(hb_buffer_.get(), full_text.data(), static_cast<int>(full_text.size()), 0, -1);
    		hb_buffer_guess_segment_properties(hb_buffer_.get());
    		target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
    		if(target_hb_dir == HB_DIRECTION_INVALID){
    			target_hb_dir = HB_DIRECTION_LTR;
    		}
    		hb_buffer_clear_contents(hb_buffer_.get());
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
				glyph g = manager.get_glyph_exact(font_group, view, *face, id);

				// 计算步进 (参照 process_text_run 中的逻辑)
				math::vec2 adv{};
				const auto& m = g.metrics();
				// 简单的根据方向计算 advance，这里主要处理 LTR/RTL/TTB 的简单情况
				// 复杂的 HarfBuzz position 在这里可能无法完全复用，但对于单个符号通常足够
				const float x_adv = m.advance.x * scale_factor.x;
				const float y_adv = m.advance.y * scale_factor.y;


				if(target_hb_dir == HB_DIRECTION_LTR){
					adv.x += x_adv;
				} // 通常 y_adv 为 0
				else if(target_hb_dir == HB_DIRECTION_RTL){
					adv.x -= x_adv;
				} else if(target_hb_dir == HB_DIRECTION_TTB){
					adv.y += y_adv;
				} // 垂直布局通常主要移动 Y
				else if(target_hb_dir == HB_DIRECTION_BTT){
					adv.y -= y_adv;
				}

				// 计算相对 AABB (位于 0,0)
				// metrics().place_to 需要具体的 pen 位置，这里给 0,0，之后在 flush_block 中再平移
				math::frect local_aabb = m.place_to({}, scale_factor);
				// 扩展绘制边界
				math::frect draw_aabb = local_aabb.copy().expand(font_draw_expand * scale_factor);

				cached_indicator_ = indicator_cache{
					std::move(g), // glyph 继承自 glyph_borrow，可以切片保存或移动
					draw_aabb,
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


    // 换行操作
    void advance_line() noexcept {
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
						ind.aabb,
						{ind.aabb.copy().expand(font_draw_expand * scale_factor), cached_indicator_.texture},
						ind.advance);
	        	}

	        	//TODO add auto feed line symbol

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

        // 2. 将 Block 内的字形提交到 results
        for (auto&& item : current_block.glyphs) {
        	item.aabb.move(pen);
        }

		results.elems.append_range(current_block.glyphs | std::views::as_rvalue);
		pen += current_block.total_advance;

        // 4. 清空 Block
        current_block.clear();

		return true;
    }

    // 处理单个文本片段
    bool process_text_run(
    	std::size_t start,
    	std::size_t length,
    	font_face_handle* face) {
        hb_buffer_clear_contents(hb_buffer_.get());
        hb_buffer_add_utf8(
            hb_buffer_.get(), full_text.data(),
            static_cast<int>(full_text.size()), static_cast<unsigned int>(start), static_cast<int>(length));
        hb_buffer_set_direction(hb_buffer_.get(), target_hb_dir);
        hb_buffer_guess_segment_properties(hb_buffer_.get());

        // 获取 HB Font
        hb_font_t* raw_hb_font = nullptr;
        if(auto ptr = hb_cache_.get(face)){
            raw_hb_font = ptr->get();
        }
        if (!raw_hb_font) {
            auto new_hb_font = create_harfbuzz_font(*face);
            hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
            raw_hb_font = new_hb_font.get();
            hb_cache_.put(face, std::move(new_hb_font));
        }

        hb_shape(raw_hb_font, hb_buffer_.get(), nullptr, 0);

        unsigned int len;
        hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

        for (unsigned int i = 0; i < len; ++i) {
            const glyph_index_t gid = infos[i].codepoint;
            char ch = full_text[infos[i].cluster];

            // --- 关键修改：遇到分隔符先结算 Block ---
            bool is_delimiter = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
            if (is_delimiter) {
                if(!flush_block()){
	                return false;
                }
            }

            // 处理控制字符
            switch (ch) {
                case '\r':
                    if (config.line_feed_type == linefeed::CRLF) pen.*major_p = 0;
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
                        const float sp_x = normalize_hb_pos(pos[i].x_advance) * scale_factor.x;
                        const float sp_y = normalize_hb_pos(pos[i].y_advance) * scale_factor.y;

                        if (target_hb_dir == HB_DIRECTION_LTR) { pen.x += sp_x; pen.y -= sp_y; }
                        else if (target_hb_dir == HB_DIRECTION_RTL) { pen.x -= sp_x; pen.y -= sp_y; }
                        else if (target_hb_dir == HB_DIRECTION_TTB) { pen.x += sp_x; pen.y -= sp_y; }
                        else if (target_hb_dir == HB_DIRECTION_BTT) { pen.x += sp_x; pen.y += sp_y; }
                    }
                    continue;
                default: break;
            }

            // 获取字形
            glyph_identity id{gid, get_snapped_font_size()};
            glyph g = manager.get_glyph_exact(font_group, view, *face, id);

            // HarfBuzz 给出的数据
            const float x_advance = normalize_hb_pos(pos[i].x_advance) * scale_factor.x;
            const float y_advance = normalize_hb_pos(pos[i].y_advance) * scale_factor.y;
            const float x_offset = normalize_hb_pos(pos[i].x_offset) * scale_factor.x;
            const float y_offset = normalize_hb_pos(pos[i].y_offset) * scale_factor.y;

            // 计算局部 AABB (相对于当前笔触)
            math::vec2 glyph_local_draw_pos{x_offset, -y_offset};
            // 修正：在 RTL 模式下，offset 的方向可能需要根据 HarfBuzz 文档细调，通常 HarfBuzz 的 offset 是基于可视方向的

        	const auto mov = [&] {
        		     if (target_hb_dir == HB_DIRECTION_LTR) { current_block.total_advance.x += x_advance; current_block.total_advance.y -= y_advance; }
        		else if (target_hb_dir == HB_DIRECTION_RTL) { current_block.total_advance.x -= x_advance; current_block.total_advance.y -= y_advance; }
        		else if (target_hb_dir == HB_DIRECTION_TTB) { current_block.total_advance.x += x_advance; current_block.total_advance.y -= y_advance; }
        		else if (target_hb_dir == HB_DIRECTION_BTT) { current_block.total_advance.x += x_advance; current_block.total_advance.y += y_advance; }
        	};

        	if(is_reversed_()){
        		mov();
        	}

        	const math::frect actual_aabb = g.metrics().place_to(current_block.total_advance + glyph_local_draw_pos, scale_factor);
        	const math::frect draw_aabb = actual_aabb.copy().expand(font_draw_expand * scale_factor);

        	current_block.push_back(actual_aabb, {draw_aabb, std::move(g)});

            // 累加 Block 宽度
        	if(!is_reversed_()){
        		mov();
        	}
        }
        return true;
    }

	void process_layout(){
		std::size_t current_idx = 0;
		std::size_t run_start = 0;
		font_face_handle* current_face = nullptr;

		while (current_idx < full_text.size()) {
			const auto len = encode::getUnicodeLength(full_text[current_idx]);
			const auto codepoint = encode::utf_8_to_32(full_text.data() + current_idx, len);

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
			current_idx += len;
		}

		// 处理最后一段
		if (current_face != nullptr) {
			process_text_run(run_start, full_text.size() - run_start, current_face);
		}

		// 确保最后的 Block 被输出
		flush_block();
	}

	void finalize(){
		if (results.elems.empty()) {
			results.extent = {0, 0};
		} else {
			auto extent = max_bound - min_bound;
			results.extent = extent;
			for (auto& elem : results.elems) {
				elem.aabb.move(-min_bound);
			}
		}
	}

	[[nodiscard]] glyph_layout&& crop() && {
		return std::move(results);
	}
};

export glyph_layout layout_text(
    font_manager& manager,
    font_face_group_meta& font_group,
    std::string_view text,
    const layout_config& config
) {
    glyph_layout empty_result{};
    if (text.empty()) return empty_result;

    auto view = font_face_view{font_group.get_thread_local()};
    if (std::ranges::empty(view)) return empty_result;


    // 初始化上下文
    layout_context ctx(manager, font_group, view, config, text);
	ctx.process_layout();
	ctx.finalize();

    return std::move(ctx).crop();
}

} // namespace mo_yanxi::font::hb