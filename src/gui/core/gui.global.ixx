export module mo_yanxi.gui.global;

export import mo_yanxi.gui.infrastructure;
import std;
namespace mo_yanxi::gui::global{

export inline ui_manager manager{0};

export
void initialize(){
	std::destroy_at(&manager);
	std::construct_at(&manager);
}

export
void terminate() noexcept {
	std::destroy_at(&manager);
	std::construct_at(&manager, 0);
}

}