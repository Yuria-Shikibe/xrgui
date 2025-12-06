//
// Created by Matrix on 2025/11/27.
//

export module mo_yanxi.gui.infrastructure:cursor;

import :elem_ptr;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.alloc;
export import mo_yanxi.allocator_aware_unique_ptr;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.vector2;

namespace mo_yanxi::gui{
namespace style{
export
struct cursor{
	virtual ~cursor() = default;
	virtual void draw(gui::renderer_frontend& renderer, math::raw_frect region, std::span<const elem* const> inbound_stack) const = 0;
};



export
enum struct cursor_type : std::uint16_t{
	regular,
	drag,
	clickable,
	text_editable,
	scroll_hori,
	scroll_vert,
	scroll,

	RESERVED_COUNT,
};

export
enum struct cursor_decoration_type : std::uint16_t{
	none,
	tooltip,

	RESERVED_COUNT,
};

export
struct cursor_style{
	cursor_type main;
	cursor_decoration_type dcor;
};


}

namespace assets::builtin{
export
struct default_cursor_regular : public style::cursor{
	void draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const override;
};

export constexpr inline default_cursor_regular default_cursor;
}

export
struct cursor_drawer{
	const style::cursor* main;
	const style::cursor* dcor;
};

struct cursor_collection{
private:
	using cursor_alloc = mr::unvs_allocator<style::cursor>;
	using ptr_type = allocator_aware_poly_unique_ptr<style::cursor, cursor_alloc>;
	using container = mr::vector<ptr_type>;
	container cursors_{std::to_underlying(style::cursor_type::RESERVED_COUNT)};
	container decorations_{std::to_underlying(style::cursor_decoration_type::RESERVED_COUNT)};

	cursor_drawer current_drawers_{};
	math::vec2 cursor_size_{32, 32};

	void add(container cursor_collection::* which, std::size_t where, ptr_type&& ptr){
		auto& cont = this->*which;
		cont.resize(std::max(cursors_.size(), where + 1));
		cont[where] = std::move(ptr);
	}
public:
	[[nodiscard]] math::vec2 get_cursor_size() const noexcept{
		return cursor_size_;
	}

	void set_cursor_size(const math::vec2 cursor_size) noexcept{
		cursor_size_ = cursor_size;
	}

	template <std::derived_from<style::cursor> T, typename ...Args>
	void add_cursor(const style::cursor_type cursor, Args&& ...args){
		const std::size_t sz = std::to_underlying(cursor);
		this->add(&cursor_collection::cursors_, sz, mo_yanxi::make_allocate_aware_poly_unique<T, cursor>(cursor_alloc{cursors_.get_allocator()}, std::forward<Args>(args) ...));
	}

	template <std::derived_from<style::cursor> T, typename ...Args>
	void add_cursor(const style::cursor_decoration_type cursor, Args&& ...args){
		const std::size_t sz = std::to_underlying(cursor);
		this->add(&cursor_collection::decorations_, sz, mo_yanxi::make_allocate_aware_poly_unique<T, cursor>(cursor_alloc{cursors_.get_allocator()}, std::forward<Args>(args) ...));
	}

	[[nodiscard]] cursor_drawer get_drawers(style::cursor_style style_pair) const noexcept{
		const auto sz_main = std::to_underlying(style_pair.main);
		const auto sz_dcor = std::to_underlying(style_pair.dcor);
		return {
			sz_main < cursors_.size() ? cursors_[sz_main].get() : nullptr,
			sz_dcor < decorations_.size() ? decorations_[sz_dcor].get() : nullptr,
		};
	}

	void set_drawers(style::cursor_style style_pair) noexcept{
		current_drawers_ = get_drawers(style_pair);
	}

	void draw(scene& scene) const;
};

}