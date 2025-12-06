export module mo_yanxi.gui.action.generic;

export import mo_yanxi.gui.action;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;

namespace mo_yanxi::gui::action{
	/*
	 *export
	struct color_action final : action<elem>{
	private:
		graphic::color initialColor{};

	public:
		graphic::color dst_color{};

		[[nodiscard]] color_action(
			const mr::heap_allocator<>& allocator, float lifetime, interp_type<elem> interpFunc,
			const graphic::color& dst_color)
		: action<elem>(allocator, lifetime, interpFunc),
		dst_color(dst_color){
		}

		[[nodiscard]] color_action(const mr::heap_allocator<>& allocator, float lifetime,
			const graphic::color& dst_color)
		: action<elem>(allocator, lifetime),
		dst_color(dst_color){
		}

	protected:
		void apply(elem& elem, const float progress) override{
			elem.prop().graphic_data.style_color_scl = initialColor.create_lerp(dst_color, progress);
		}

		void begin(elem& elem) override{
			initialColor = elem.prop().graphic_data.style_color_scl;
		}

		void end(elem& elem) override{
			elem.prop().graphic_data.style_color_scl = dst_color;
		}
	};
	*/

	export
	struct alpha_action final : action<elem>{
	private:
		float initialAlpha{};

	public:
		float dst_alpha{};

		[[nodiscard]] alpha_action(const mr::heap_allocator<>& allocator, float lifetime, interp_func_t interpFunc,
			float dst_alpha)
		: action<elem>(allocator, lifetime, interpFunc),
		dst_alpha(dst_alpha){
		}

		[[nodiscard]] alpha_action(const mr::heap_allocator<>& allocator, float lifetime, float dst_alpha)
		: action<elem>(allocator, lifetime),
		dst_alpha(dst_alpha){
		}

		void apply(elem& elem, const float progress) override{
			elem.update_context_opacity(math::lerp(initialAlpha, dst_alpha, progress));
		}

		void begin(elem& elem) override{
			initialAlpha = elem.get_draw_opacity();
		}

		void end(elem& elem) override{
			elem.update_context_opacity(dst_alpha);
		}
	};

}

