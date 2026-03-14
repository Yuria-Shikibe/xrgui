module;

#include <cassert>

export module mo_yanxi.gui.elem.text_edit_v2;

import std;
export import mo_yanxi.gui.elem.label_v2;
export import mo_yanxi.gui.text_edit_core;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;
import mo_yanxi.math;

namespace mo_yanxi::gui {

export struct text_edit_v2_key_binding;

export struct text_edit_v2 : label_v2 {
public:
    [[nodiscard]] text_edit_v2(scene& scene, elem* parent)
        : label_v2(scene, parent) {
    	get_scene().active_update_elems.insert(this);

        interactivity = interactivity_flag::enabled;
        extend_focus_until_mouse_drop = true;

        // 初始状态下直接显示 Hint 占位符
        label_v2::set_text(hint_text_when_idle_);
    }

    ~text_edit_v2() override {
        set_focus(false);
    }

    // --- 文本同步与状态管理 ---
    template <typename StrTy>
        requires std::constructible_from<std::string, StrTy&&>
    void set_hint_text(StrTy&& ty) {
        hint_text_when_idle_ = std::forward<StrTy>(ty);
        if (is_idle_) {
            label_v2::set_text(hint_text_when_idle_);
        }
    }

    void set_banned_characters(std::initializer_list<char32_t> chars) {
        prohibited_codes_ = chars;
    }

    void add_file_banned_characters() {
        prohibited_codes_.insert_range(std::initializer_list<char32_t>{U'<', U'>', U':', U'"', U'/', U'\\', U'|', U'*', U'?'});
    }

    [[nodiscard]] std::string_view get_text() const noexcept override {
        if (is_idle_) return {};
        return label_v2::get_text();
    }

    [[nodiscard]] bool is_idle() const noexcept { return is_idle_; }
    [[nodiscard]] bool is_failed() const noexcept { return failed_hint_timer_ > 0.f; }

    // --- 核心动作封装 ---
    void undo() { if (core_.undo()) sync_view_from_core(); }
    void redo() { if (core_.redo()) sync_view_from_core(); }

    void action_do_insert(std::u32string_view str) {
        if (core_.insert_text(str)) sync_view_from_core();
    }

    void action_do_delete() { if (core_.action_delete()) sync_view_from_core(); }
    void action_do_backspace() { if (core_.action_backspace()) sync_view_from_core(); }
    void action_enter() {
        if (!prohibited_codes_.contains(U'\n')) {
            core_.insert_text(U"\n");
            sync_view_from_core();
        }
    }
    void action_tab() {
        if (!prohibited_codes_.contains(U'\t')) {
            core_.insert_text(U"\t");
            sync_view_from_core();
        }
    }

    void action_move_left(bool select, bool jump) {
        if(jump) core_.action_jump_left(glyph_layout_, select);
        else core_.action_move_left(select);
        reset_blink();
    }

    void action_move_right(bool select, bool jump) {
        if(jump) core_.action_jump_right(glyph_layout_, select);
        else core_.action_move_right(select);
        reset_blink();
    }

    void action_move_up(bool select) {
        core_.action_move_up(glyph_layout_, text_line_align, select);
        reset_blink();
    }
    void action_move_down(bool select) {
        core_.action_move_down(glyph_layout_, text_line_align, select);
        reset_blink();
    }
    void action_move_line_begin(bool select) {
        core_.action_move_line_begin(glyph_layout_, select);
        reset_blink();
    }
    void action_move_line_end(bool select) {
        core_.action_move_line_end(glyph_layout_, select);
        reset_blink();
    }

    void action_select_all() {
        core_.action_select_all();
        reset_blink();
    }

    void action_copy() const;
    void action_paste();
    void action_cut();

protected:
    text_editor_core core_{};
    std::string hint_text_when_idle_{">_..."};
    bool is_idle_{true};
    float failed_hint_timer_{0.f};
    float caret_blink_timer_{0.f};
    std::size_t maximum_units_{std::numeric_limits<std::size_t>::max()};
    mr::heap_uset<char32_t> prohibited_codes_{mr::get_default_heap_allocator()};

    void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;
    graphic::color get_text_draw_color() const noexcept override;

public:
    bool update(float delta_in_ticks) override;

    events::op_afterwards on_drag(const events::drag event) override;
    events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override;
    events::op_afterwards on_key_input(const input_handle::key_set key) override;
    events::op_afterwards on_unicode_input(const char32_t val) override;
    events::op_afterwards on_esc() override;

	void update_ime_position() const {
		//TODO
		// if (const auto cmt = get_scene().get_communicator()) {
		// 	auto region = get_caret_local_aabb(content_height());
		// 	region.move(util::transform_local2scene(*this, get_glyph_src_local()));
		// 	cmt->set_ime_cursor_rect(region);
		// }
	}

private:
    void set_focus(bool keyFocused);
    void sync_view_from_core();
    void reset_blink() { caret_blink_timer_ = 0.f; }
    void set_input_invalid() { failed_hint_timer_ = 10.f; }
    void draw_selection_and_caret() const;


    [[nodiscard]] math::frect get_caret_local_aabb(float fallback_height) const{
    	auto& layout = glyph_layout_;
    	auto align = text_line_align;
	    auto& core = core_;

	    auto caret = core.get_caret();
	    auto ordered = caret.get_ordered();
	    bool has_sel = caret.has_region();
	    auto text_u32 = core.get_text();

	    // 1. 空布局的兜底处理
	    if(layout.lines.empty()){
		    return math::frect{
				    tags::from_extent,
				    {0.0f, -fallback_height * 0.8f},
				    {2.0f, fallback_height}
			    };
	    }

	    float min_x = std::numeric_limits<float>::max();
	    float min_y = std::numeric_limits<float>::max();
	    float max_x = std::numeric_limits<float>::lowest();
	    float max_y = std::numeric_limits<float>::lowest();

	    bool caret_found = false;
	    std::size_t next_line_start_idx = 0;

	    for(std::size_t line_idx = 0; line_idx < layout.lines.size(); ++line_idx){
		    const auto& line = layout.lines[line_idx];
		    auto align_res = line.calculate_alignment(layout.extent, align, layout.direction);

		    // 注意：这里去掉了 off (get_glyph_src_abs())，保留在 glyph_layout 坐标系下
		    math::vec2 line_src = align_res.start_pos;

		    float line_height = line.rect.ascender + line.rect.descender;
		    float line_top_y = -line.rect.ascender;

		    // 行高异常兜底
		    if(line_height < 0.1f){
			    line_height = fallback_height;
			    line_top_y = -fallback_height * 0.8f;
		    }

		    std::size_t line_start_idx = next_line_start_idx;
		    std::size_t line_end_idx = line_start_idx;

		    if(line.cluster_range.size > 0){
			    line_start_idx = layout.clusters[line.cluster_range.pos].cluster_index;
			    const auto& last_cluster = layout.clusters[line.cluster_range.pos + line.cluster_range.size - 1];
			    line_end_idx = last_cluster.cluster_index + last_cluster.cluster_span;
		    }
		    next_line_start_idx = line_end_idx;

		    if(has_sel){
			    // === 处理范围选中区域的 AABB ===
			    bool line_has_sel = false;
			    float sel_min_x = std::numeric_limits<float>::max();
			    float sel_max_x = std::numeric_limits<float>::lowest();
			    bool extend_to_edge = false;

			    if(line.cluster_range.size == 0){
				    if(ordered.src <= line_start_idx && ordered.dst > line_start_idx){
					    line_has_sel = true;
					    sel_min_x = 0.0f;
					    sel_max_x = 8.0f;
					    extend_to_edge = true;
				    }
			    } else{
				    for(std::size_t i = 0; i < line.cluster_range.size; ++i){
					    const auto& cluster = layout.clusters[line.cluster_range.pos + i];
					    if(cluster.cluster_index >= ordered.src && cluster.cluster_index < ordered.dst){
						    line_has_sel = true;
						    sel_min_x = std::min(sel_min_x, cluster.logical_rect.vert_00().x);
						    sel_max_x = std::max(sel_max_x, cluster.logical_rect.vert_11().x);

						    if(cluster.cluster_index < text_u32.size() &&
							    (text_u32[cluster.cluster_index] == U'\n' || text_u32[cluster.cluster_index] == U'\r')){
							    extend_to_edge = true;
						    }
					    }
				    }
				    if(ordered.dst >= next_line_start_idx && ordered.dst > line_start_idx){
					    extend_to_edge = true;
				    }
			    }

			    if(line_has_sel){
				    float abs_min_x = line_src.x + sel_min_x;
				    float abs_max_x = line_src.x + sel_max_x;
				    float abs_top_y = line_src.y + line_top_y;
				    float abs_bottom_y = line_src.y + line_top_y + line_height;

				    if(extend_to_edge){
					    float layout_right_edge = layout.extent.x;
					    abs_max_x = std::max({abs_max_x, layout_right_edge, abs_min_x + 8.0f});
				    }

				    min_x = std::min(min_x, abs_min_x);
				    min_y = std::min(min_y, abs_top_y);
				    max_x = std::max(max_x, abs_max_x);
				    max_y = std::max(max_y, abs_bottom_y);
				    caret_found = true;
			    }
		    } else if(!caret_found){
			    // === 处理单光标的 AABB ===
			    for(std::size_t i = 0; i < line.cluster_range.size; ++i){
				    const auto& cluster = layout.clusters[line.cluster_range.pos + i];

				    // 包含了组合字形的等分点计算逻辑
				    if(caret.dst >= cluster.cluster_index && caret.dst < cluster.cluster_index + cluster.cluster_span){
					    float span_ratio = 0.0f;
					    if(cluster.cluster_span > 1){
						    span_ratio = static_cast<float>(caret.dst - cluster.cluster_index) / static_cast<float>(
							    cluster.cluster_span);
					    }

					    float left_x = cluster.logical_rect.vert_00().x;
					    float right_x = cluster.logical_rect.vert_11().x;
					    float target_x = left_x + (right_x - left_x) * span_ratio;

					    math::vec2 final_caret_pos = line_src + math::vec2{target_x, line_top_y};

					    min_x = final_caret_pos.x;
					    min_y = final_caret_pos.y;
					    max_x = final_caret_pos.x + 2.0f; // 光标宽度 2px
					    max_y = final_caret_pos.y + line_height;
					    caret_found = true;
					    break;
				    }
			    }

			    // 处理行尾兜底（光标在这一行最后的情况）
			    if(!caret_found && caret.dst == line_end_idx){
				    bool is_last_line = (line_idx == layout.lines.size() - 1);
				    bool ends_with_newline = (caret.dst > 0 && caret.dst <= text_u32.size() && text_u32[caret.dst - 1]
					    == U'\n');

				    if(line.cluster_range.size == 0){
					    min_x = line_src.x;
					    min_y = line_src.y + line_top_y;
					    max_x = line_src.x + 2.0f;
					    max_y = line_src.y + line_top_y + line_height;
					    caret_found = true;
				    } else if(is_last_line || !ends_with_newline){
					    const auto& last_cluster = layout.clusters[line.cluster_range.pos + line.cluster_range.size -
						    1];
					    min_x = line_src.x + last_cluster.logical_rect.get_end_x();
					    min_y = line_src.y + line_top_y;
					    max_x = min_x + 2.0f;
					    max_y = min_y + line_height;
					    caret_found = true;
				    }
			    }
		    }
	    }

	    // 如果无论如何都没找到（异常情况），回退到原点
	    if(!caret_found){
		    return math::frect{
				    tags::from_extent,
				    {0.0f, -fallback_height * 0.8f},
				    {2.0f, fallback_height}
			    };
	    }

	    // 利用最大/最小坐标构建最终的矩形
	    return math::frect{
			    tags::unchecked,
			    tags::from_vertex,
			    {min_x, min_y},
			    {max_x, max_y}
		    };
    }

};

export struct text_edit_v2_key_binding : input_handle::key_mapping<text_edit_v2&> {
    using key_mapping::key_mapping;
};

export void load_default_text_edit_v2_key_binding(text_edit_v2_key_binding& bind);

} // namespace mo_yanxi::gui