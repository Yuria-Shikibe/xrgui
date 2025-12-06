module;

#include <cassert>

module mo_yanxi.gui.assets.manager;

namespace mo_yanxi::gui::global{

assets::image_page* builtin_page_{};

U u;

assets::resource_manager& resource_manager() noexcept{
	assert(global::builtin_page_ != nullptr && "GUI Resource Manager Not Initialized Yet");
	return u.resource_manager;
}

void initialize_resource_manager(mr::arena_id_t arena_id){
	if(builtin_page_){
		throw assets::duplicated_error{"GUI Resource Manager Already Initialized"};
	}
	std::construct_at(&u.resource_manager, arena_id);
	builtin_page_ = std::addressof(u.resource_manager.create_page(assets::builtin::page_name));
}

bool terminate_resource_manager() noexcept{
	if(builtin_page_){
		std::destroy_at(&u.resource_manager);
		builtin_page_ = nullptr;
		return true;
	}
	return false;
}

}

namespace mo_yanxi::gui::assets::builtin{

image_page& get_page() noexcept{
	assert(global::builtin_page_ != nullptr);
	return *global::builtin_page_;
}

}
