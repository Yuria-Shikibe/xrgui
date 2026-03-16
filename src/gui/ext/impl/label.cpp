module mo_yanxi.gui.elem.label;

import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.draw.instruction.general;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.vector2;
import mo_yanxi.math;

namespace mo_yanxi::gui{

void label_text_prov::on_update(react_flow::data_carrier<std::string>& data) {
	label->set_text(data.get());
}

void direct_label_text_prov::on_update(react_flow::data_carrier<typesetting::tokenized_text>& data){
	terminal<typesetting::tokenized_text>::on_update(data);
	label->set_tokenized_text(data.get());
}

// 修改为 u32_label
void direct_label::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const {
    elem::draw_layer(clipSpace, param);
    draw_style(param);
    if (param == 0) draw_text();
}

// 修改为 u32_label
void direct_label::draw_text() const {
    if (!render_cache_.has_drawable_text()) return;

    math::vec2 raw_ext = glyph_layout_.extent;
    math::vec2 trans_ext = raw_ext;
    if (transform_config_.rotation == text_rotation::deg_90 || transform_config_.rotation == text_rotation::deg_270) {
        trans_ext = {raw_ext.y, raw_ext.x};
    }

    math::vec2 reg_ext = get_glyph_draw_extent();
    math::vec2 src_local = get_glyph_src_local();
    math::vec2 src_abs = src_local + content_src_pos_abs();

    math::mat3 mat_abs = math::mat3_idt;

    // 因为底层矩阵使用左乘，变换叠加过程必须按照“由局部到全局（由内向外）”的顺序执行：
    // 1. 将原始包围盒的中心重置到原点
    mat_abs.translate(-raw_ext * 0.5f);

    float flip_x_v = transform_config_.flip_x ? -1.f : 1.f;
    float flip_y_v = transform_config_.flip_y ? -1.f : 1.f;
    if (transform_config_.flip_x || transform_config_.flip_y) {
        mat_abs.scale(flip_x_v, flip_y_v);
    }

    switch (transform_config_.rotation) {
        case text_rotation::deg_90:  mat_abs.rotate_90(); break;
        case text_rotation::deg_180: mat_abs.rotate_180(); break;
        case text_rotation::deg_270: mat_abs.rotate_270(); break;
        case text_rotation::deg_0:   break;
    }

    // 4. 对齐/缩放（针对 fit_ 模式）
    if (fit_ && trans_ext.x > 0.f && trans_ext.y > 0.f) {
        mat_abs.scale(reg_ext / trans_ext);
    }

    // 5. 平移到目标包围盒的实际屏幕中心点
    mat_abs.translate(src_abs + reg_ext * 0.5f);

    // 计算用于 Hit-Test 逆向映射的局部矩阵
    math::mat3 mat_local = mat_abs;
    mat_local.c3.x += (src_local.x - src_abs.x);
    mat_local.c3.y += (src_local.y - src_abs.y);

    // math::vec2 cursor_local = util::transform_scene2local(*this, get_scene().get_cursor_pos());
    // math::mat3 inv_local = mat_local;
    // inv_local.inv(); // 反转矩阵将当前鼠标映射至未变换的原始字形坐标系中
    // math::vec2 raw_hit_pos = inv_local * cursor_local;

    // auto hit = glyph_layout_.hit_test(raw_hit_pos, render_cache_.get_line_align());

    state_guard guard{renderer(), fx::batch_draw_mode::msdf};

    // 渲染文本和 Debug Hit
    {
        transform_guard _t{renderer(), mat_abs};
        render_cache_.push_to_renderer(renderer());

        // if (hit) {
        //     auto src = hit.source_line->calculate_alignment(glyph_layout_.extent, render_cache_.get_line_align(), glyph_layout_.direction);
        //     renderer() << graphic::draw::instruction::rect_aabb{
        //         .v00 = src.start_pos + hit.source->logical_rect.vert_00(),
        //         .v11 = src.start_pos + hit.source->logical_rect.vert_11(),
        //         .vert_color = {graphic::colors::ACID.copy_set_a(.6f)}
        //     };
        // }
    }

    // 渲染未参与变换的组件外侧边框
    // {
    //     math::mat3 mat_bb = math::mat3_idt;
    //     mat_bb.set_translation(src_abs);
    //     transform_guard _t{renderer(), mat_bb};
    //     renderer() << graphic::draw::instruction::rect_aabb_outline{
    //         .v00 = {},
    //         .v11 = reg_ext,
    //         .stroke = {2},
    //         .vert_color = {graphic::colors::CRIMSON.copy_set_a(.6f)}
    //     };
    // }
}

}
