module;

export module mo_yanxi.gui.md_builder;

import std;

export import mo_yanxi.gui.markdown;
export import mo_yanxi.gui.elem.sequence;
export import mo_yanxi.gui.elem.table;
export import mo_yanxi.gui.elem.grid;
export import mo_yanxi.gui.elem.label;
export import mo_yanxi.gui.elem.scroll_pane;
export import mo_yanxi.gui.elem.image_frame;
export import mo_yanxi.font.manager;
export import mo_yanxi.graphic.color;
export import mo_yanxi.graphic.image_atlas;
import mo_yanxi.unicode;
import mo_yanxi.graphic.g2d;
import mo_yanxi.utility;
import mo_yanxi.typesetting.util;

namespace mo_yanxi::gui::md {

namespace styles{
export
enum variants{
	none,
	code_block,
	quote,
};
}

export struct markdown_image_config {
	graphic::image_page* page{};
	graphic::image_atlas* atlas{};
	std::filesystem::path base_path{};
	math::vec2 max_extent{1024.f, 640.f};
	align::scale scaling{align::scale::fit};
	align::pos align{align::pos::center_left};
	bool wait_for_load{};
};

export struct markdown_config {
	std::array<float, 6> heading_sizes{56.f, 44.f, 32.f, 24.f, 20.f, 16.f};
	graphic::color link_color{0.40f, 0.68f, 1.0f, 1.0f};
	graphic::color code_bg_color{0.16f, 0.18f, 0.22f, 1.0f};
	graphic::color quote_bar_color{0.40f, 0.68f, 1.0f, 1.0f};
	graphic::color quote_bg_color{0.08f, 0.13f, 0.18f, 0.80f};
	graphic::color quote_text_color{0.78f, 0.84f, 0.92f, 1.0f};
	graphic::color bullet_color{0.72f, 0.74f, 0.78f, 1.0f};
	float block_pad{12.f};
	float heading_pad{10.f};
	float table_pad{4.f};

	graphic::color table_bg_color{0.12f, 0.14f, 0.18f, 0.50f};
	graphic::color table_grid_color{0.35f, 0.40f, 0.48f, 0.80f};
	graphic::color table_header_bg_color{0.16f, 0.20f, 0.28f, 0.70f};
	graphic::color table_even_row_bg_color{0.14f, 0.16f, 0.22f, 0.35f};
	float table_grid_width{1.f};
	float code_block_font_size{typesetting::glyph_size::pt_14};
	float body_font_size{typesetting::glyph_size::standard_size};
	float quote_bar_width{4.f};
	float quote_indent{12.f};
	math::vec2 ppi{typesetting::glyph_size::screen_ppi};

	std::string_view style_family_name{"markdown"};
	std::optional<markdown_image_config> image{};

	float to_px(float pt) const noexcept {
		return typesetting::glyph_size::get_glyph_std_size_at(pt, ppi).x;
	}

	float body_font_size_px() const noexcept {
		return to_px(body_font_size);
	}

	float code_block_font_size_px() const noexcept {
		return to_px(code_block_font_size);
	}

	float heading_size_px(std::uint32_t level) const noexcept {
		auto idx = std::min<std::size_t>(level - 1, heading_sizes.size() - 1);
		return to_px(heading_sizes[idx]);
	}
};

export inline std::optional<std::u32string> try_read_markdown_utf8_file(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if(!file.is_open()) {
		return std::nullopt;
	}

	const auto end_pos = file.tellg();
	if(end_pos < 0) {
		return std::nullopt;
	}

	std::string text(static_cast<std::size_t>(end_pos), '\0');
	file.seekg(0, std::ios::beg);
	if(!text.empty()) {
		file.read(text.data(), static_cast<std::streamsize>(text.size()));
	}

	if(text.size() >= 3 &&
		static_cast<unsigned char>(text[0]) == 0xEF &&
		static_cast<unsigned char>(text[1]) == 0xBB &&
		static_cast<unsigned char>(text[2]) == 0xBF) {
		text.erase(0, 3);
	}

	return unicode::utf8_to_utf32(text);
}

void append_utf8(std::u32string& out, std::string_view text) {
	unicode::append_utf8_to_utf32(text, out);
}

void append_u32(std::u32string& out, std::u32string_view text) {
	out.append(text.begin(), text.end());
}

std::u32string escape_rich_text_literal(std::u32string_view text) {
	std::u32string out;
	out.reserve(text.size());
	for(const char32_t ch : text) {
		switch(ch) {
		case U'{':
			out += U"{{";
			break;
		case U'}':
			out += U"}}";
			break;
		default:
			out.push_back(ch);
			break;
		}
	}
	return out;
}

std::u32string color_tag(const graphic::color& color) {
	auto hex = std::format("{:#a}", color);
	std::u32string out{U"{c:"};
	append_utf8(out, hex);
	out += U'}';
	return out;
}

std::u32string rich_size_wrap(std::u32string body, float size, bool bold = false, bool italic = false) {
	std::u32string out;
	append_utf8(out, std::format("{{size:{}}}", size));
	if(bold) out += U"{b}";
	if(italic) out += U"{i}";
	out += std::move(body);
	if(italic) out += U"{/i}";
	if(bold) out += U"{/b}";
	out += U"{/size}";
	return out;
}

struct markdown_separator : elem {
	graphic::color line_color{0.50f, 0.54f, 0.60f, 1.0f};
	float stroke_width{2.f};

	using elem::elem;

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		extent.apply({0.f, std::max(stroke_width, 0.f)});
		return extent.potential_extent();
	}

	void record_draw_layer(draw_recorder& call_stack_builder) const override {
		call_stack_builder.push_call_noop(*this, [](const markdown_separator& s, const draw_call_param& p,
		                                            const draw_immut_args& args) static {
			if(!args.layer.is_top()) return;
			if(!util::is_draw_param_valid(s, p)) return;

			auto bounds = s.content_bound_abs();
			auto mid_y = (bounds.vert_00().y + bounds.vert_11().y) * 0.5f;
			auto color = s.line_color.copy().mul_a(util::get_final_draw_opacity(s, p));

			s.renderer().push(graphic::g2d::line{
				.src = {bounds.vert_00().x, mid_y},
				.dst = {bounds.vert_11().x, mid_y},
				.color = {color, color},
				.stroke = s.stroke_width,
			});
		});
	}
};

struct md_table : gui::grid {
	markdown_config config_{};

	using grid::grid;

	void record_draw_layer(draw_recorder& call_stack_builder) const override {
		call_stack_builder.push_call_noop(*this, [](const md_table& t, const draw_call_param& p,
		                                            const draw_immut_args& args) static {
			if(!args.layer.is_top()) return;
			if(!util::is_draw_param_valid(t, p)) return;

			auto children = t.exposed_children();
			auto [num_cols, num_rows] = t.grid_extent();
			if(children.empty() || num_cols == 0 || num_rows == 0) return;

			auto content_origin = t.content_src_pos_abs();
			auto content_extent = t.content_bound_abs().extent();
			auto content_left = content_origin.x;
			auto content_top = content_origin.y;
			auto content_right = content_left + content_extent.x;
			auto content_bottom = content_top + content_extent.y;

			auto opacity = util::get_final_draw_opacity(t, p);
			auto& renderer = t.renderer();
			auto& cfg = t.config_;

			renderer.update_state(fx::batch_draw_mode::def);

			auto bg_color = cfg.table_bg_color.copy().mul_a(opacity);
			renderer << graphic::g2d::rect_aabb{
				.v00 = content_origin,
				.v11 = {content_right, content_bottom},
				.vert_color = {bg_color}
			};

			state_guard g{renderer, fx::make_blend_write_mask(false)};

			auto grid_color = cfg.table_grid_color.copy().mul_a(opacity);
			float grid_width = cfg.table_grid_width;
			auto header_color = cfg.table_header_bg_color.copy().mul_a(opacity);
			auto even_color = cfg.table_even_row_bg_color.copy().mul_a(opacity);

			std::vector<float> col_lefts;
			std::vector<float> col_rights;
			for(std::uint32_t c = 0; c < num_cols; ++c) {
				auto* child = children[c];
				col_lefts.push_back(child->pos_abs().x);
				col_rights.push_back(child->pos_abs().x + child->extent().x);
			}

			std::vector<float> col_lines;
			col_lines.push_back(content_left);
			for(std::uint32_t c = 0; c + 1 < num_cols; ++c)
				col_lines.push_back((col_rights[c] + col_lefts[c + 1]) * 0.5f);
			col_lines.push_back(content_right);

			std::vector<float> row_tops;
			std::vector<float> row_bottoms;

			for(std::uint32_t r = 0; r < num_rows; ++r) {
				float row_top = std::numeric_limits<float>::max();
				float row_bottom = std::numeric_limits<float>::lowest();

				for(std::uint32_t c = 0; c < num_cols; ++c) {
					auto* child = children[r * num_cols + c];
					float top = child->pos_abs().y;
					float bottom = top + child->extent().y;
					if(top < row_top) row_top = top;
					if(bottom > row_bottom) row_bottom = bottom;
				}

				row_tops.push_back(row_top);
				row_bottoms.push_back(row_bottom);
			}

			std::vector<float> row_lines;
			row_lines.push_back(content_top);
			for(std::size_t r = 0; r + 1 < row_tops.size(); ++r)
				row_lines.push_back((row_bottoms[r] + row_tops[r + 1]) * 0.5f);
			row_lines.push_back(content_bottom);

			for(std::size_t r = 0; r < row_tops.size(); ++r) {
				if(r == 0) {
					renderer << graphic::g2d::rect_aabb{
						.v00 = {content_left, row_tops[r]},
						.v11 = {content_right, row_bottoms[r]},
						.vert_color = {header_color}
					};
				} else if((r & 1) == 0) {
					renderer << graphic::g2d::rect_aabb{
						.v00 = {content_left, row_tops[r]},
						.v11 = {content_right, row_bottoms[r]},
						.vert_color = {even_color}
					};
				}
			}

			for(float x : col_lines) {
				renderer << graphic::g2d::line{
					.src = {x, content_top},
					.dst = {x, content_bottom},
					.color = {grid_color, grid_color},
					.stroke = grid_width,
				};
			}

			for(float y : row_lines) {
				renderer << graphic::g2d::line{
					.src = {content_left, y},
					.dst = {content_right, y},
					.color = {grid_color, grid_color},
					.stroke = grid_width,
				};
			}
		});

		grid::record_draw_layer(call_stack_builder);
	}
};

struct markdown_bullet : elem {
	graphic::color bullet_color{0.72f, 0.74f, 0.78f, 1.0f};
	bool hollow = false;
	align::pos anchor = align::pos::center;
	float size_ratio = 0.75f;
	float stroke_ratio = 0.3f;

	using elem::elem;

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		return (extent.apply(math::vec2{marker_size, marker_size}), extent.potential_extent());
	}

	float marker_size{20.f};

	void record_draw_layer(draw_recorder& call_stack_builder) const override {
		elem::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_noop(*this, [](const markdown_bullet& s, const draw_call_param& p,
		                                            const draw_immut_args& args) static {
			if(!args.layer.is_top()) return;
			if(!util::is_draw_param_valid(s, p)) return;

			auto bounds = s.content_bound_abs();
			auto extent = bounds.extent();
			auto radius = std::min(extent.x, extent.y) * s.size_ratio * 0.5f;
			auto stroke = radius * s.stroke_ratio;

			auto center = align::get_vert(s.anchor, bounds);
			auto color = s.bullet_color.copy().mul_a(util::get_final_draw_opacity(s, p));

			math::range radius_range = s.hollow
				? math::range{radius - stroke, radius}
				: math::range{0.f, radius};

			s.renderer().push(graphic::g2d::poly{
				.pos = center,
				.segments = graphic::g2d::get_circle_vertices(radius),
				.radius = radius_range,
				.color = {color, color}
			});
		});
	}
};

struct markdown_image : image_frame_single<drawable_image<>> {
	math::vec2 max_extent{1024.f, 640.f};

	using image_frame_single<drawable_image<>>::image_frame_single;

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		const auto preferred_optional = this->drawable_.get_preferred_extent();
		if(!preferred_optional) return std::nullopt;

		const math::vec2 preferred = preferred_optional.value_or();
		if(preferred.x <= 0.f || preferred.y <= 0.f) return std::nullopt;

		math::vec2 limit = max_extent;
		const math::vec2 potential = extent.potential_extent();
		if(std::isfinite(potential.x) && potential.x > 0.f) {
			limit.x = std::min(limit.x, potential.x);
		}
		if(std::isfinite(potential.y) && potential.y > 0.f) {
			limit.y = std::min(limit.y, potential.y);
		}

		if(limit.x <= 0.f || limit.y <= 0.f) return std::nullopt;

		const float scale = std::min({1.f, limit.x / preferred.x, limit.y / preferred.y});
		return preferred * std::max(scale, 0.f);
	}
};

struct inline_renderer {
	const markdown_config& config;

	std::u32string operator()(const md::node_list& nodes) const {
		std::u32string out;
		for(const md::ast_node& node : nodes) {
			append_node(out, node);
		}
		return out;
	}

	void append_node(std::u32string& out, const md::ast_node& node) const {
		std::visit([&](const auto& val) { append(out, val); }, node.data);
	}

	void append_children_to(std::u32string& out, const md::node_list& nodes) const {
		for(const md::ast_node& node : nodes) {
			append_node(out, node);
		}
	}

	void append(std::u32string& out, const md::text& node) const {
		append_u32(out, node.content);
	}

	void append(std::u32string& out, const md::code_span& node) const {
		out += U"{f:mono}{w:r}";
		out += escape_rich_text_literal(node.content);
		out += U"{/w}{/f}";
	}

	void append(std::u32string& out, const md::emphasis& node) const {
		out += U"{i}";
		append_u32(out, (*this)(node.children));
		out += U"{/i}";
	}

	void append(std::u32string& out, const md::strong_emphasis& node) const {
		out += U"{b}";
		append_u32(out, (*this)(node.children));
		out += U"{/b}";
	}

	void append(std::u32string& out, const md::link& node) const {
		append_u32(out, color_tag(config.link_color));
		out += U"{u}";
		append_u32(out, (*this)(node.children));
		out += U"{/u}{/c}";
	}

	void append(std::u32string& out, const md::image& node) const {
		append_u32(out, color_tag(graphic::color{1.0f, 0.40f, 0.40f, 1.0f}));
		out += U"[unsupported inline image: ";
		append_u32(out, escape_rich_text_literal((*this)(node.alt)));
		if(!node.url.empty()) {
			out += U" -> ";
			append_u32(out, escape_rich_text_literal(node.url));
		}
		out += U"]{/c}";
	}

	template <typename T>
	void append(std::u32string&, const T&) const {
	}
};

direct_label& setup_label_base(direct_label& label, const markdown_config& config) {
	label.set_style();
	label.set_fit(false);
	label.set_expand_policy(layout::expand_policy::resize_to_fit);
	label.text_entire_align = align::pos::top_left;

	typesetting::layout_config typeset = {};
	auto body_px = config.body_font_size_px();
	typeset.default_font_size = math::vec2{body_px, body_px};
	label.set_typesetting_config(typeset);
	return label;
}

void apply_code_font(direct_label& label, const markdown_config& config) {
	typesetting::layout_config typeset = {};
	auto code_px = config.code_block_font_size_px();
	typeset.default_font_size = math::vec2{code_px, code_px};
	if(font::default_font_manager) {
		typeset.rich_text_fallback_style.family = font::default_font_manager->find_family(std::string_view{"mono"});
	}
	label.set_typesetting_config(typeset);
}

create_handle<direct_label, sequence::cell_type> append_rich_label(
	sequence& parent,
	std::u32string&& text,
	const markdown_config& config
) {
	auto hdl = parent.create_back([&](direct_label& label) {
		setup_label_base(label, config);
		label.set_tokenized_text(typesetting::tokenized_text{std::move(text), typesetting::tokenize_tag::def});
	});
	hdl.cell().set_pending();
	hdl.cell().set_pad({config.block_pad, config.block_pad});
	return hdl;
}

create_handle<direct_label, sequence::cell_type> append_raw_label(
	sequence& parent,
	std::u32string text,
	const markdown_config& config,
	bool code_block_style = false,
	bool quote_style = false,
	typesetting::tokenize_tag tokenize_tag = typesetting::tokenize_tag::raw
) {
	auto hdl = parent.create_back([&](direct_label& label) {
		setup_label_base(label, config);
		if(code_block_style) {
			apply_code_font(label, config);
			util::sync_set_elem_style(label, styles::code_block, "markdown");
			label.set_self_border({6.f, 6.f, 6.f, 6.f});
		}
		if(quote_style) {
			label.set_style();

			label.set_self_border({8.f, 8.f, 8.f + config.quote_indent, 8.f});
			label.text_color_scl = config.quote_text_color;
		}
		label.set_tokenized_text(typesetting::tokenized_text{std::move(text), tokenize_tag});
	});
	hdl.cell().set_pending();
	hdl.cell().set_pad({config.block_pad, config.block_pad});
	return hdl;
}

void append_markdown_error(sequence& parent, std::u32string_view message, const markdown_config& config) {
	std::u32string text = color_tag(graphic::color{1.0f, 0.35f, 0.35f, 1.0f});
	append_u32(text, escape_rich_text_literal(message));
	text += U"{/c}";
	append_raw_label(parent, std::move(text), config, false, false, typesetting::tokenize_tag::def);
}

void append_markdown_error(sequence& parent, std::string_view message, const markdown_config& config) {
	std::u32string u32_message;
	append_utf8(u32_message, message);
	append_markdown_error(parent, u32_message, config);
}

bool is_ascii_alpha(char ch) noexcept {
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_windows_drive_path(std::string_view value) noexcept {
	if(value.size() < 2 || !is_ascii_alpha(value[0]) || value[1] != ':') return false;
	return value.size() == 2 || value[2] == '\\' || value[2] == '/';
}

bool has_unsupported_image_scheme(std::string_view value) noexcept {
	const std::size_t colon = value.find(':');
	if(colon == std::string_view::npos) return false;
	if(is_windows_drive_path(value)) return false;

	const std::size_t first_separator = value.find_first_of("/\\");
	return first_separator == std::string_view::npos || colon < first_separator;
}

std::filesystem::path resolve_markdown_image_path(
	const markdown_image_config& image_config,
	std::u32string_view url
) {
	const std::string url_utf8 = unicode::utf32_to_utf8(url);
	if(url_utf8.empty()) {
		throw std::runtime_error{"markdown image URL is empty"};
	}
	if(has_unsupported_image_scheme(url_utf8)) {
		throw std::runtime_error{std::format("unsupported markdown image URL scheme: {}", url_utf8)};
	}

	std::filesystem::path raw_path{url_utf8};
	std::filesystem::path resolved = raw_path.is_absolute()
		? raw_path
		: image_config.base_path / raw_path;

	std::error_code ec;
	resolved = std::filesystem::absolute(resolved, ec);
	if(ec) {
		throw std::runtime_error{std::format("failed to resolve markdown image path '{}': {}", url_utf8, ec.message())};
	}

	if(!std::filesystem::exists(resolved, ec)) {
		if(ec) {
			throw std::runtime_error{std::format("failed to inspect markdown image path '{}': {}", resolved.string(), ec.message())};
		}
		throw std::runtime_error{std::format("markdown image path does not exist: {}", resolved.string())};
	}

	resolved = std::filesystem::weakly_canonical(resolved, ec);
	if(ec) {
		throw std::runtime_error{std::format("failed to canonicalize markdown image path '{}': {}", resolved.string(), ec.message())};
	}

	return resolved;
}

constant_image_region_borrow load_markdown_image_region(
	const markdown_image_config& image_config,
	const md::image& node
) {
	if(image_config.page == nullptr) {
		throw std::runtime_error{"markdown image page is not configured"};
	}

	const auto path = resolve_markdown_image_path(image_config, node.url);
	const std::string path_string = path.string();
	const std::string region_name = std::format("markdown:{}", path_string);
	auto result = image_config.page->register_named_region(
		region_name,
		graphic::image_load_description{graphic::bitmap_path_load{path_string}});
	return constant_image_region_borrow{result.region};
}

void append_markdown_image(sequence& parent, const md::image& node, const markdown_config& config) {
	if(!config.image) {
		append_markdown_error(parent, U"markdown image cannot render: markdown_config::image is not configured", config);
		return;
	}

	try {
		const auto region = load_markdown_image_region(*config.image, node);
		auto hdl = parent.create_back([&](markdown_image& image) {
			image.set_style();
			image.max_extent = config.image->max_extent;
			image.style.scaling = config.image->scaling;
			image.style.align = config.image->align;
			image.set_drawable(drawable_image<>{region});
		});
		hdl.cell().set_pending();
		hdl.cell().set_pad({config.block_pad, config.block_pad});
	} catch(const std::exception& e) {
		std::u32string message = U"markdown image failed: ";
		append_u32(message, node.url);
		message += U" (";
		append_utf8(message, e.what());
		message += U")";
		append_markdown_error(parent, message, config);
	}
}

void wait_markdown_images_if_requested(sequence& parent, const markdown_config& config) {
	if(!config.image || !config.image->wait_for_load) return;
	if(config.image->atlas == nullptr) {
		append_markdown_error(parent, U"markdown image wait_for_load requires markdown_image_config::atlas", config);
		return;
	}
	config.image->atlas->wait_load();
}

sequence& append_block_container(sequence& parent, const markdown_config& config, float left_indent = 0.f, bool quote_style = false) {
	auto hdl = parent.create_back([&](sequence& seq) {
		seq.set_style();
		seq.set_layout_spec(layout::layout_policy::hori_major);
		seq.template_cell.set_pending();
		seq.template_cell.set_pad({config.block_pad, config.block_pad});
		if(quote_style) {
			util::sync_set_elem_style(seq, styles::quote, config.style_family_name);
		}
	});
	hdl.cell().set_pending();
	hdl.cell().set_pad({config.block_pad, config.block_pad});
	// if(left_indent > 0.f) {
	// 	hdl.elem().set_self_border({});
	// }
	return hdl.elem();
}

void build_blocks(sequence& parent, const md::node_list& nodes, const markdown_config& config, std::uint32_t depth = 0);

void build_list(sequence& parent, const md::list& node, const markdown_config& config, std::uint32_t depth = 0) {
	if(depth > 128) return;
	const auto item_count = static_cast<std::uint32_t>(node.items.size());
	if(item_count == 0) return;

	// Column spec: [pending (marker), passive (content)]
	grid_mixed col_spec;
	col_spec.heads.push_back({
		layout::stated_size{layout::size_category::pending, 0},
		align::padding1d{config.table_pad, config.table_pad}
	});
	col_spec.heads.push_back({
		layout::stated_size{layout::size_category::passive, 1},
		align::padding1d{config.table_pad, config.table_pad}
	});

	// Row spec: [pending] * item_count
	grid_mixed row_spec;
	row_spec.heads.reserve(static_cast<std::size_t>(item_count) * 2);
	for(std::uint32_t i = 0; i < item_count; ++i){
		row_spec.heads.push_back({
			layout::stated_size{layout::size_category::mastering, 60},
			align::padding1d{2.f, 2.f}
		});
		row_spec.heads.push_back({
			layout::stated_size{layout::size_category::pending, 0},
			align::padding1d{2.f, 2.f}
		});
	}

	auto grid_hdl = parent.create_back([&](grid& g) {
		g.set_style();
		g.set_expand_policy(layout::expand_policy::resize_to_fit);
	}, math::vector2<grid_dim_spec>{
		grid_dim_spec{std::move(col_spec)},
		grid_dim_spec{std::move(row_spec)}
	});
	grid_hdl.cell().set_pending();
	grid_hdl.cell().set_pad({config.block_pad, config.block_pad});

	auto& g = grid_hdl.elem();
	inline_renderer render{config};
	std::uint32_t number = node.start_number;

	for(std::uint32_t row = 0; row < item_count; ++row){
		const auto& item = node.items[row];

		// Marker in column 0
		{
			if(!node.ordered){
				auto marker_hdl = g.create_back([&](markdown_bullet& bullet){
					bullet.set_style();
					bullet.bullet_color = config.bullet_color;
					bullet.hollow = (depth % 2 != 0);
					bullet.marker_size = 20.f;
				});
				marker_hdl.cell().extent = {
					{.type = grid_extent_type::src_extent, .desc = {0, 1}},
					{.type = grid_extent_type::src_extent, .desc = {static_cast<std::uint16_t>(row * 2), 1}}
				};
				marker_hdl.cell().unsaturate_cell_elem_align = align::pos::bottom_right;
			} else {
				auto marker_hdl = g.create_back([&](direct_label& label){
					setup_label_base(label, config);
					label.set_fit_type(label_fit_type::scl);
					label.text_entire_align = align::pos::top_right;
					label.text_color_scl = config.bullet_color;
					std::u32string marker;
					append_utf8(marker, std::format("{}.", number));
					label.set_tokenized_text(typesetting::tokenized_text{std::move(marker), typesetting::tokenize_tag::raw});
				});
				marker_hdl.cell().extent = {
					{.type = grid_extent_type::src_extent, .desc = {0, 1}},
					{.type = grid_extent_type::src_extent, .desc = {static_cast<std::uint16_t>(row * 2), 1}}
				};
				marker_hdl.cell().unsaturate_cell_elem_align = align::pos::bottom_right;
			}
		}

		// Content in column 1
		{
			auto content_hdl = g.create_back([&](sequence& seq){
				seq.set_style();
				seq.set_layout_spec(layout::layout_policy::hori_major);
				seq.template_cell.set_pending();
				seq.template_cell.set_pad({2.f, 2.f});
				build_blocks(seq, item.blocks, config, depth + 1);
			});
			content_hdl.cell().extent = {
				{.type = grid_extent_type::src_extent, .desc = {1, 1}},
				{.type = grid_extent_type::src_extent, .desc = {static_cast<std::uint16_t>(row * 2), 2}}
			};
			content_hdl.cell().unsaturate_cell_elem_align = align::pos::center_left;
		}
		++number;
	}
}

void build_table(sequence& parent, const md::table& node, const markdown_config& config) {
	const auto num_cols = node.cols;
	const auto num_rows = node.rows;
	if(num_cols == 0 || num_rows == 0) return;


	auto table_hdl = parent.create_back([&](scroll_adaptor<md_table>& s) {
		s.set_layout_spec(layout::layout_policy::vert_major);
		s.set_expand_policy(layout::expand_policy::resize_to_fit);
		s.set_overlay_bar(true);
		s.set_style();

		auto& tbl = s.get_elem();
		tbl.set_style();
		tbl.set_extent_spec(math::vector2<grid_dim_spec>{
		   grid_all_pending{num_cols, {config.table_pad, config.table_pad}},
		   grid_all_pending{num_rows, {config.table_pad, config.table_pad}}
	   });
		tbl.set_expand_policy(layout::expand_policy::resize_to_fit);
		tbl.config_ = config;
	}, layout::layout_specifier::fixed(layout::layout_policy::vert_major));

	table_hdl.cell().set_pending();
	table_hdl.cell().set_pad({config.block_pad, config.block_pad});

	auto& tbl = table_hdl.elem();
	inline_renderer render{config};

	for(std::uint32_t r = 0; r < num_rows; ++r) {
		auto row = node.get_row(r);
		for(std::uint32_t c = 0; c < num_cols; ++c) {
			auto text = render(row[c].children);
			if(r == 0)
				text = rich_size_wrap(std::move(text), config.body_font_size_px(), true);

			auto cell_hdl = tbl.get_elem().create_back([&](direct_label& label) {
				label.set_self_border(gui::border_t{}.set(4));
				setup_label_base(label, config);
				label.set_tokenized_text(typesetting::tokenized_text{std::move(text), typesetting::tokenize_tag::def});
				switch(c < node.alignments.size() ? node.alignments[c] : table_align::none) {
				case table_align::left:
					label.text_entire_align = align::pos::center_left;
					break;
				case table_align::center:
					label.text_entire_align = align::pos::center;
					break;
				case table_align::right:
					label.text_entire_align = align::pos::center_right;
					break;
				case table_align::none:
					label.text_entire_align = align::pos::center_left;
					break;
				}
			});
			cell_hdl.cell().extent = {
				{.type = grid_extent_type::src_extent, .desc = {static_cast<std::uint16_t>(c), 1}},
				{.type = grid_extent_type::src_extent, .desc = {static_cast<std::uint16_t>(r), 1}}
			};
		}
	}
}

bool contains_top_level_image(const md::node_list& nodes) noexcept {
	return std::ranges::any_of(nodes, [](const md::ast_node& node) {
		return std::holds_alternative<md::image>(node.data);
	});
}

void build_paragraph(sequence& parent, const md::paragraph& node, const markdown_config& config) {
	inline_renderer render{config};
	std::u32string pending_text;

	const auto flush_text = [&]() {
		if(pending_text.empty()) return;
		append_rich_label(parent, std::move(pending_text), config);
		pending_text.clear();
	};

	for(const md::ast_node& child : node.children) {
		if(const auto* image = std::get_if<md::image>(&child.data)) {
			flush_text();
			append_markdown_image(parent, *image, config);
			continue;
		}
		render.append_node(pending_text, child);
	}

	flush_text();
}

void build_blockquote(sequence& parent, const md::blockquote& node, const markdown_config& config, std::uint32_t depth = 0) {
	if(depth > 128) return;
	std::u32string combined;
	inline_renderer render{config};
	bool all_paragraph_like = true;
	for(const auto& child : node.children) {
		if(const auto* para = std::get_if<md::paragraph>(&child.data)) {
			if(contains_top_level_image(para->children)) {
				all_paragraph_like = false;
				break;
			}
			if(!combined.empty()) combined += U"\n\n";
			render.append_children_to(combined, para->children);
			continue;
		}
		if(const auto* heading = std::get_if<md::heading>(&child.data)) {
			if(!combined.empty()) combined += U"\n\n";
			render.append_children_to(combined, heading->children);
			continue;
		}
		all_paragraph_like = false;
		break;
	}

	if(all_paragraph_like && !combined.empty()) {
		append_raw_label(parent, std::move(combined), config, false, true, typesetting::tokenize_tag::def);
		return;
	}

	auto& container = append_block_container(parent, config, config.quote_indent, true);
	build_blocks(container, node.children, config, depth);
}

void build_blocks(sequence& parent, const node_list& nodes, const markdown_config& config, std::uint32_t depth) {
	if(depth > 128) return;
	inline_renderer render{config};

	for(const ast_node& node : nodes) {
		std::visit(overload_def_noop{
			std::in_place_type<void>,
			[&](const paragraph& val){
				build_paragraph(parent, val, config);
			},
		[&](const heading& val){
			auto text = rich_size_wrap(render(val.children), config.heading_size_px(val.level), true);
			auto hdl = append_rich_label(parent, std::move(text), config);
				hdl.cell().set_pad({config.heading_pad, config.heading_pad});
			},
			[&](const code_block& val){
				std::u32string text = val.content.empty() ? U" " : std::u32string{val.content};
				append_raw_label(parent, std::move(text), config, true, false);
			},
			[&](const table& val){
				build_table(parent, val, config);
			},
			[&](const list& val){
				build_list(parent, val, config, depth);
			},
			[&](const blockquote& val){
				build_blockquote(parent, val, config, depth);
			},
			[&](const thematic_break& val){
				auto hdl = parent.create_back([&](markdown_separator& sep) {
					sep.set_style();
				});
				hdl.cell().set_pending();
				hdl.cell().margin.set_hori(12);
				hdl.cell().set_pad({config.block_pad * 0.5f, config.block_pad * 0.5f});
			},
		}, node.data);

	}
}

export inline void append_markdown(
	sequence& parent,
	std::u32string_view markdown_text,
	const markdown_config& config = {},
	std::uint32_t depth = 0
) {
	markdown_parser parser{markdown_text};

	auto ast = parser.parse();

	build_blocks(parent, ast, config, depth);
	wait_markdown_images_if_requested(parent, config);
}

export inline void append_markdown_file(
	sequence& parent,
	const std::filesystem::path& path,
	markdown_config config = {},
	std::uint32_t depth = 0
) {
	if(config.image && config.image->base_path.empty()) {
		config.image->base_path = path.parent_path();
	}

	if(auto text = try_read_markdown_utf8_file(path)) {
		append_markdown(parent, *text, config, depth);
	} else {
		std::u32string message = U"Failed to load markdown file:\n";
		message.append(path.u32string());
		append_markdown_error(parent, message, config);
	}
}

export inline elem_ptr build_markdown(
	scene& scene,
	elem* parent,
	std::u32string_view markdown_text,
	const markdown_config& config = {},
	std::uint32_t depth = 0
) {
	elem_ptr root{scene, parent, [text = std::u32string{markdown_text}, config, depth](sequence& seq) {
		seq.set_style();
		seq.set_layout_spec(layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));
		seq.template_cell.set_pending();
		seq.template_cell.set_pad({config.block_pad, config.block_pad});
		append_markdown(seq, text, config, depth);
		seq.notify_isolated_layout_changed();
	}};

	return root;
}

}
