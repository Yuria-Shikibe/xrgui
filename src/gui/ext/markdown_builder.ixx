export module mo_yanxi.gui.markdown_builder;

import std;

export import mo_yanxi.gui.markdown;
export import mo_yanxi.gui.elem.sequence;
export import mo_yanxi.gui.elem.table;
export import mo_yanxi.gui.elem.label;
export import mo_yanxi.font.manager;
export import mo_yanxi.graphic.color;
import mo_yanxi.unicode;
import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui::md {

export struct markdown_config {
	std::array<float, 6> heading_sizes{32.f, 26.f, 22.f, 18.f, 16.f, 14.f};
	graphic::color link_color{0.40f, 0.68f, 1.0f, 1.0f};
	graphic::color code_bg_color{0.16f, 0.18f, 0.22f, 1.0f};
	graphic::color quote_bar_color{0.40f, 0.68f, 1.0f, 1.0f};
	graphic::color quote_bg_color{0.08f, 0.13f, 0.18f, 0.80f};
	graphic::color quote_text_color{0.78f, 0.84f, 0.92f, 1.0f};
	graphic::color bullet_color{0.72f, 0.74f, 0.78f, 1.0f};
	float block_pad{8.f};
	float heading_pad{10.f};
	float table_pad{4.f};
	float code_block_font_size{14.f};
	float body_font_size{16.f};
	float quote_bar_width{4.f};
	float quote_indent{12.f};
	float list_marker_width{28.f};
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
	auto to_u8 = [](float v) {
		return static_cast<unsigned>(std::clamp(std::lround(v * 255.f), 0l, 255l));
	};

	std::u32string out;
	append_utf8(out, std::format("{{c:#{:02X}{:02X}{:02X}{:02X}}}",
		to_u8(color.r), to_u8(color.g), to_u8(color.b), to_u8(color.a)));
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

struct markdown_block_label : direct_label {
	graphic::color block_color{};
	graphic::color strip_color{};
	float strip_width{};
	bool draw_block_background{};

	using direct_label::direct_label;

	void record_draw_layer(draw_recorder& call_stack_builder) const override {
		direct_label::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_noop(*this, [](const markdown_block_label& s, const draw_call_param& p) static {
			if(!s.draw_block_background) return;
			if(p.layer_param.is_top()) return;
			if(!util::is_draw_param_valid(s, p)) return;

			auto region = s.bound_abs().intersection_with(p.draw_bound);
			auto v00 = region.vert_00();
			auto v11 = region.vert_11();
			auto fill = s.block_color.copy().mul_a(util::get_final_draw_opacity(s, p));
			s.renderer().push(graphic::draw::instruction::rect_aabb{
				.v00 = v00,
				.v11 = v11,
				.vert_color = {fill}
			});

			if(s.strip_width > 0.f) {
				auto strip = s.strip_color.copy().mul_a(util::get_final_draw_opacity(s, p));
				s.renderer().push(graphic::draw::instruction::rect_aabb{
					.v00 = v00,
					.v11 = {std::min(v00.x + s.strip_width, v11.x), v11.y},
					.vert_color = {strip}
				});
			}
		});
	}
};

struct markdown_block_sequence : sequence {
	graphic::color block_color{};
	graphic::color strip_color{};
	float strip_width{};
	bool draw_block_background{};

	using sequence::sequence;

	void record_draw_layer(draw_recorder& call_stack_builder) const override {
		sequence::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_noop(*this, [](const markdown_block_sequence& s, const draw_call_param& p) static {
			if(!s.draw_block_background) return;
			if(p.layer_param.is_top()) return;
			if(!util::is_draw_param_valid(s, p)) return;

			auto region = s.bound_abs().intersection_with(p.draw_bound);
			auto v00 = region.vert_00();
			auto v11 = region.vert_11();
			auto fill = s.block_color.copy().mul_a(util::get_final_draw_opacity(s, p));
			s.renderer().push(graphic::draw::instruction::rect_aabb{
				.v00 = v00,
				.v11 = v11,
				.vert_color = {fill}
			});

			if(s.strip_width > 0.f) {
				auto strip = s.strip_color.copy().mul_a(util::get_final_draw_opacity(s, p));
				s.renderer().push(graphic::draw::instruction::rect_aabb{
					.v00 = v00,
					.v11 = {std::min(v00.x + s.strip_width, v11.x), v11.y},
					.vert_color = {strip}
				});
			}
		});
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
		out += U"[image: ";
		append_u32(out, (*this)(node.alt));
		if(!node.url.empty()) {
			out += U" -> ";
			append_u32(out, node.url);
		}
		out += U"]";
	}

	template <typename T>
	void append(std::u32string&, const T&) const {
	}
};

direct_label& setup_label_base(direct_label& label, const markdown_config& config) {
	label.set_style();
	label.set_fit(false);
	label.set_expand_policy(layout::expand_policy::prefer);
	label.text_entire_align = align::pos::top_left;

	typesetting::layout_config typeset = {};
	typeset.default_font_size = math::vec2{config.body_font_size, config.body_font_size};
	label.set_typesetting_config(typeset);
	return label;
}

void apply_code_font(direct_label& label, const markdown_config& config) {
	typesetting::layout_config typeset = {};
	typeset.default_font_size = math::vec2{config.code_block_font_size, config.code_block_font_size};
	if(font::default_font_manager) {
		typeset.rich_text_fallback_style.family = font::default_font_manager->find_family(std::string_view{"mono"});
	}
	label.set_typesetting_config(typeset);
}

create_handle<direct_label, sequence::cell_type> append_rich_label(
	sequence& parent,
	const std::u32string_view text,
	const markdown_config& config
) {
	auto hdl = parent.create_back([&](direct_label& label) {
		setup_label_base(label, config);
		label.set_tokenized_text(typesetting::tokenized_text{text, typesetting::tokenize_tag::def});
	});
	hdl.cell().set_pending();
	hdl.cell().set_pad({config.block_pad, config.block_pad});
	return hdl;
}

create_handle<markdown_block_label, sequence::cell_type> append_raw_label(
	sequence& parent,
	std::u32string text,
	const markdown_config& config,
	bool code_block_style = false,
	bool quote_style = false,
	typesetting::tokenize_tag tokenize_tag = typesetting::tokenize_tag::raw
) {
	auto hdl = parent.create_back([&](markdown_block_label& label) {
		setup_label_base(label, config);
		if(code_block_style) {
			apply_code_font(label, config);
			label.draw_block_background = true;
			label.block_color = config.code_bg_color;
			label.set_self_boarder({6.f, 6.f, 6.f, 6.f});
		}
		if(quote_style) {
			label.draw_block_background = true;
			label.block_color = config.quote_bg_color;
			label.strip_width = config.quote_bar_width;
			label.strip_color = config.quote_bar_color;
			label.set_self_boarder({8.f, 8.f, 8.f + config.quote_indent, 8.f});
			label.text_color_scl = config.quote_text_color;
		}
		label.set_tokenized_text(typesetting::tokenized_text{std::move(text), tokenize_tag});
	});
	hdl.cell().set_pending();
	hdl.cell().set_pad({config.block_pad, config.block_pad});
	return hdl;
}

sequence& append_block_container(sequence& parent, const markdown_config& config, float left_indent = 0.f, bool quote_style = false) {
	auto hdl = parent.create_back([&](markdown_block_sequence& seq) {
		seq.set_style();
		seq.set_layout_spec(layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));
		seq.set_expand_policy(layout::expand_policy::prefer);
		seq.template_cell.set_pending();
		seq.template_cell.set_pad({config.block_pad, config.block_pad});
		if(quote_style) {
			seq.draw_block_background = true;
			seq.block_color = config.quote_bg_color;
			seq.strip_width = config.quote_bar_width;
			seq.strip_color = config.quote_bar_color;
		}
	});
	hdl.cell().set_pending();
	hdl.cell().set_pad({config.block_pad, config.block_pad});
	if(left_indent > 0.f) {
		hdl.elem().set_self_boarder({0.f, 0.f, left_indent, 0.f});
	}
	return hdl.elem();
}

void build_blocks(sequence& parent, const md::node_list& nodes, const markdown_config& config);

void build_list(sequence& parent, const md::list& node, const markdown_config& config) {
	auto table_hdl = parent.create_back([&](gui::table& tbl) {
		tbl.set_style();
		tbl.set_expand_policy(layout::expand_policy::prefer);
		tbl.set_entire_align(align::pos::top_left);
		tbl.template_cell.set_pad(config.table_pad);
	});
	table_hdl.cell().set_pending();
	table_hdl.cell().set_pad({config.block_pad, config.block_pad});

	auto& tbl = table_hdl.elem();
	inline_renderer render{config};
	std::uint32_t number = node.start_number;

	for(const auto& item : node.items) {
		auto marker_hdl = tbl.create_back([&](direct_label& label) {
			setup_label_base(label, config);
			label.text_entire_align = align::pos::top_right;
			label.text_color_scl = config.bullet_color;
			std::u32string marker = node.ordered ? std::u32string{} : std::u32string{U"•"};
			if(node.ordered) {
				append_utf8(marker, std::format("{}.", number));
			}
			label.set_tokenized_text(typesetting::tokenized_text{std::move(marker), typesetting::tokenize_tag::raw});
		});
		marker_hdl.cell().set_width(config.list_marker_width).set_pending({false, true});

		auto content_hdl = tbl.create_back([&](sequence& seq) {
			seq.set_style();
			seq.set_layout_spec(layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));
			seq.set_expand_policy(layout::expand_policy::prefer);
			seq.template_cell.set_pending();
			seq.template_cell.set_pad({2.f, 2.f});
			build_blocks(seq, item.blocks, config);
		});
		content_hdl.cell().set_pending({true, true}).set_width_passive().set_height_passive();
		tbl.end_line();
		++number;
	}
}

void build_table(sequence& parent, const md::table& node, const markdown_config& config) {
	auto table_hdl = parent.create_back([&](gui::table& tbl) {
		tbl.set_style();
		tbl.set_expand_policy(layout::expand_policy::prefer);
		tbl.set_entire_align(align::pos::top_left);
		tbl.template_cell.set_pad(config.table_pad);
	});
	table_hdl.cell().set_pending();
	table_hdl.cell().set_pad({config.block_pad, config.block_pad});

	auto& tbl = table_hdl.elem();
	inline_renderer render{config};

	for(std::uint32_t r = 0; r < node.rows; ++r) {
		auto row = node.get_row(r);
		for(std::uint32_t c = 0; c < row.size(); ++c) {
			auto cell_hdl = tbl.create_back([&](direct_label& label) {
				setup_label_base(label, config);
				auto text = render(row[c].children);
				if(r == 0) {
					text = rich_size_wrap(std::move(text), config.body_font_size, true);
				}
				label.set_tokenized_text(typesetting::tokenized_text{text, typesetting::tokenize_tag::def});
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
			cell_hdl.cell().set_pending({true, false}).set_width_passive().set_height_passive();
		}
		tbl.end_line();
	}
}

void build_blockquote(sequence& parent, const md::blockquote& node, const markdown_config& config) {
	std::u32string combined;
	inline_renderer render{config};
	bool all_paragraph_like = true;
	for(const auto& child : node.children) {
		if(const auto* para = std::get_if<md::paragraph>(&child.data)) {
			if(!combined.empty()) combined += U"\n\n";
			combined += render(para->children);
			continue;
		}
		if(const auto* heading = std::get_if<md::heading>(&child.data)) {
			if(!combined.empty()) combined += U"\n\n";
			combined += render(heading->children);
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
	build_blocks(container, node.children, config);
}

void build_blocks(sequence& parent, const md::node_list& nodes, const markdown_config& config) {
	inline_renderer render{config};

	for(const md::ast_node& node : nodes) {
		std::visit([&]<typename T0>(const T0& val) {
			using node_type = std::remove_cvref_t<T0>;

			if constexpr(std::is_same_v<node_type, md::paragraph>) {
				auto text = render(val.children);
				if(!text.empty()) {
					md::append_rich_label(parent, text, config);
				}
			} else if constexpr(std::is_same_v<node_type, md::heading>) {
				auto text = md::rich_size_wrap(render(val.children), config.heading_sizes[std::min<std::size_t>(val.level - 1, config.heading_sizes.size() - 1)], true);
				auto hdl = md::append_rich_label(parent, text, config);
				hdl.cell().set_pad({config.heading_pad, config.heading_pad});
			} else if constexpr(std::is_same_v<node_type, md::code_block>) {
				std::u32string text = val.content.empty() ? U" " : std::u32string{val.content};
				append_raw_label(parent, std::move(text), config, true, false);
			} else if constexpr(std::is_same_v<node_type, md::table>) {
				md::build_table(parent, val, config);
			} else if constexpr(std::is_same_v<node_type, md::list>) {
				md::build_list(parent, val, config);
			} else if constexpr(std::is_same_v<node_type, md::blockquote>) {
				md::build_blockquote(parent, val, config);
			} else if constexpr(std::is_same_v<node_type, md::thematic_break>) {
				// TODO use separator instead.
				auto hdl = append_rich_label(parent, U"{c:#7F8A99FF}--------------------------------{/c}", config);
				hdl.cell().set_pad({config.block_pad * 0.5f, config.block_pad * 0.5f});
			}
		}, node.data);
	}
}

export inline void append_markdown(
	sequence& parent,
	std::u32string_view markdown_text,
	const markdown_config& config = {}
) {
	markdown_parser parser{markdown_text};
	auto ast = parser.parse();
	build_blocks(parent, ast, config);
}

export inline elem_ptr build_markdown(
	scene& scene,
	elem* parent,
	std::u32string_view markdown_text,
	const markdown_config& config = {}
) {
	elem_ptr root{scene, parent, [text = std::u32string{markdown_text}, config](sequence& seq) {
		seq.set_style();
		seq.set_layout_spec(layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));
		seq.set_expand_policy(layout::expand_policy::prefer);
		seq.template_cell.set_pending();
		seq.template_cell.set_pad({config.block_pad, config.block_pad});
		append_markdown(seq, text, config);
	}};

	return root;
}

}
