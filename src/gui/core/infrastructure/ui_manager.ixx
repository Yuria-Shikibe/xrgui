module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:ui_manager;

import mo_yanxi.heterogeneous;
import mo_yanxi.gui.alloc;
import :scene;


namespace mo_yanxi::gui{

namespace scene_names{
constexpr std::string_view main{"main"};
}

export
template <typename T>
struct scene_add_result{
	scene& scene;
	T& root_group;
};

export
struct ui_manager{

	[[nodiscard]]  ui_manager() : ui_manager(128){

	}

	[[nodiscard]] explicit ui_manager(const std::size_t PoolSize_MB) : pool_(PoolSize_MB > 0 ? mr::make_memory_pool(PoolSize_MB) : mr::raw_memory_pool{}){

	}

	[[nodiscard]] explicit ui_manager(const std::size_t PoolSize_MB, std::string name, scene&& scene) : ui_manager(PoolSize_MB){
		focus = &scenes.insert_or_assign(std::move(name), std::move(scene)).first->second;
	}

private:
	mr::raw_memory_pool pool_{};
	string_hash_map<scene> scenes{};
	scene* focus{};

public:

	scene* switch_scene_to(std::string_view name) noexcept{
		if(auto scene = scenes.try_find(name)){
			return std::exchange(focus, scene);
		}
		return nullptr;
	}

	[[nodiscard]] scene& get_current_focus() const noexcept{
		assert(focus);
		return *focus;
	}

private:
	scene& add_scene(std::string_view name, scene&& scene, bool focusIt = false){
		auto itr = scenes.insert_or_assign(name, std::move(scene));
		if(focusIt){
			focus = std::addressof(itr.first->second);
		}

		return itr.first->second;
	}
public:
	[[nodiscard]] mr::raw_memory_pool& get_pool() noexcept{
		return pool_;
	}

	template <std::derived_from<elem> T, typename... Args>
		requires (std::constructible_from<T, scene&, elem*, Args&&...>)
	scene_add_result<T> add_scene(
		std::string_view name,
		bool focus_it,
		renderer_frontend&& renderer_ui,
		Args&&... args){
		auto& scene_ = this->add_scene(
			name,
			scene{
				this->pool_.get_arena_id(),
				std::move(renderer_ui), std::in_place_type<T>,
				std::forward<Args>(args)...
			}, focus_it);

		auto& root = this->root_of<T>(name);
		return {scene_, root};
	}

	bool erase_scene(std::string_view name) /*noexcept*/{
		if(auto itr = scenes.find(name); itr != scenes.end()){
			if(std::addressof(itr->second) == focus){
				if(name == scene_names::main){
					throw std::runtime_error{"erase main while focusing it"};
				}
				focus = &scenes.at(scene_names::main);
			}

			scenes.erase(itr);

			return true;
		}
		return false;
	}

	void draw() const{
		if(focus) focus->draw();
	}

	void update(const float delta_in_ticks) const{
		if(focus) focus->update(delta_in_ticks);
	}

	void layout() const{
		if(focus) focus->layout();
	}

	void input_key(const input_handle::key_set k) const{
		if(focus) focus->input_key(k);
	}

	void input_inbound(bool is_inbound) const{
		if(focus) focus->input_inbound(is_inbound);
	}

	void input_mouse(const input_handle::key_set k) const{
		if(focus) focus->input_mouse(k);
	}

	void input_scroll(const float x, const float y) const{
		if(focus) focus->on_scroll({x, y});
	}


	void input_unicode(const char32_t val) const{
		if(focus) focus->on_unicode_input(val);
	}

	void cursor_pos_update(const float x, const float y) const{
		if(focus) focus->inform_cursor_move({x, y});
	}

	void scroll_update(const float x, const float y) const{
		if(focus) focus->on_scroll({x, y});
	}

	scene* get_scene(const std::string_view sceneName){
		return scenes.try_find(sceneName);
	}

	template <typename T>
	[[nodiscard]] T& root_of(const std::string_view sceneName){
		if(const auto rst = scenes.try_find(sceneName)){
			return rst->root<T>();
		}

		std::println(std::cerr, "Scene {} Not Found", sceneName);
		throw std::invalid_argument{"In-exist Scene Name"};
	}

	void resize(const math::frect region, const std::string_view name = scene_names::main){
		if(const auto rst = scenes.try_find(name)){
			rst->resize(region);
		}
	}

	void resize_all(const math::frect region){
		for(auto& scene : scenes | std::views::values){
			scene.resize(region);
		}
	}

	[[nodiscard]] bool is_scroll_idle() const noexcept{
		return !focus || focus->focus_scroll_ == nullptr;
	}

	[[nodiscard]] bool is_focus_idle() const noexcept{
		return !focus || focus->focus_cursor_ == nullptr;
	}

	[[nodiscard]] mr::arena_id_t get_arena_id() const{
		return pool_.get_arena_id();
	}
};
}
