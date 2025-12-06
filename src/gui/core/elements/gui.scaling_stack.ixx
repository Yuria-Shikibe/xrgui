//
// Created by Matrix on 2025/3/13.
//

export module mo_yanxi.gui.elem.manual_table;

export import mo_yanxi.gui.celled_group;
import std;
namespace mo_yanxi::gui{
	using passive_cell_adaptor = cell_adaptor<layout::scaled_cell>;

	export struct scaling_stack : celled_group<passive_cell_adaptor>{
		[[nodiscard]] scaling_stack(scene& scene, elem* parent)
			: universal_group<layout::scaled_cell>(scene, parent){
			layout_state.ignore_children();
			layout_state.intercept_lower_to_isolated = true;
		}

		void layout_elem() override{
			for (adaptor_type& cell : cells_){
				layout_cell(cell);
			}

			elem::layout_elem();
		}

	protected:
		void on_element_add(adaptor_type& adaptor) override{
			universal_group::on_element_add(adaptor);
			layout_cell(adaptor);
		}

		void layout_cell(adaptor_type& adaptor){
			const auto bound = content_extent();
			const auto src = adaptor.cell.region_scale.get_src() * bound;

			auto size = adaptor.cell.clamp_size(adaptor.cell.region_scale.extent() * bound) * get_scaling();

			auto region = math::frect{tags::from_extent, src, size};

			region.src = align::transform_offset(adaptor.cell.align, bound, region);
			adaptor.cell.allocated_region = region;

			adaptor.apply(*this, {region.width(), region.height()});
			if(!is_pos_smooth())adaptor.cell.update_relative_src(*adaptor.element, content_src_pos_abs());
		}
	};
}
