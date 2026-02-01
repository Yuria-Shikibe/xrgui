module mo_yanxi.gui.elem.text_holder;

void mo_yanxi::gui::push(gui::renderer_frontend& r,
	const graphic::draw::instruction::draw_record_storage<mr::heap_allocator<>>& buf){
	r.push(buf.heads(), buf.data());
}
