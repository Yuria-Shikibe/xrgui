module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:ui_manager;

import std;
import mo_yanxi.heterogeneous;
import mo_yanxi.gui.alloc;
import mo_yanxi.input_handle.input_event_queue;
import :scene;


namespace mo_yanxi::gui{

namespace scene_names{
constexpr std::string_view main{"main"};
}

export
template <typename S, typename T>
struct scene_add_result{
	S& scene;
	T& root_group;
};

export
struct ui_manager{

	[[nodiscard]]  ui_manager() : ui_manager(128){

	}

	[[nodiscard]] explicit ui_manager(const std::size_t PoolSize_MB) : pool_(PoolSize_MB > 0 ? mr::make_memory_pool(PoolSize_MB) : mr::raw_memory_pool{}){

	}

private:
	mr::raw_memory_pool pool_{};
	string_hash_map<scene_resources> resources{};
	string_hash_map<std::unique_ptr<scene>> scenes{};
	scene* focus{};

public:
	scene_resources& add_scene_resources(std::string_view name){
		return resources.try_emplace(name, get_arena_id()).first->second;
	}

	scene* switch_scene_to(std::string_view name) noexcept{
		if(auto scene = scenes.try_find(name)){
			return std::exchange(focus, scene->get());
		}
		return nullptr;
	}

	[[nodiscard]] scene& get_current_focus() const noexcept{
		assert(focus);
		return *focus;
	}

private:
	template <typename S, typename ...Args>
	S& add_scene(std::string_view name, bool focusIt, Args&& ...args){
		std::pair<decltype(scenes)::iterator, bool> itr = scenes.try_emplace(name, std::make_unique<S>(std::forward<Args>(args)...));
		if(focusIt){
			focus = itr.first->second.get();
		}

		return static_cast<S&>(*itr.first->second.get());
	}
public:
	[[nodiscard]] mr::raw_memory_pool& get_pool() noexcept{
		return pool_;
	}

	template <std::derived_from<scene> S = scene, std::derived_from<elem> T, typename... Args>
		requires (std::constructible_from<T, scene&, elem*, Args&&...>)
	scene_add_result<S, T> add_scene(
		std::string_view name,
		scene_resources& resources,
		bool focus_it,
		renderer_frontend&& renderer_ui,
		Args&&... args){
		auto& scene_ = this->add_scene<S>(
			name,
			focus_it,
			resources,
			std::move(renderer_ui), std::in_place_type<T>,
			std::forward<Args>(args)...);

		auto& root = this->root_of<T>(name);
		return {scene_, root};
	}

	bool erase_scene(std::string_view name) /*noexcept*/{
		if(auto itr = scenes.find(name); itr != scenes.end()){
			if(itr->second.get() == focus){
				if(name == scene_names::main){
					throw std::runtime_error{"erase main while focusing it"};
				}
				focus = scenes.at(scene_names::main).get();
			}

			scenes.erase(itr);

			return true;
		}
		return false;
	}

	void draw() const{
		if(focus) focus->draw();
	}

	void update(const double delta_in_ticks) const{
		if(focus) focus->update(delta_in_ticks);
	}

	void layout() const{
		if(focus) focus->layout();
	}

	void input_key(const input_handle::key_set k) const{
		if(focus) focus->on_key_input(k);
	}

	void input_inbound(bool is_inbound) const{
		if(focus) focus->input_handler_.input_inbound(is_inbound);
	}

	void input_mouse(const input_handle::key_set k) const{
		if(focus) focus->on_mouse_input(k);
	}

	void input_scroll(const float x, const float y) const{
		if(focus) focus->on_scroll({x, y});
	}


	void input_unicode(const char32_t val) const{
		if(focus) focus->on_unicode_input(val);
	}

	void cursor_pos_update(const float x, const float y) const{
		if(focus) focus->on_cursor_move({x, y});
	}

	void scroll_update(const float x, const float y) const{
		if(focus) focus->on_scroll({x, y});
	}

	scene* get_scene(const std::string_view sceneName){
		auto ptr = scenes.try_find(sceneName);
		return ptr ? ptr->get() : nullptr;
	}

	template <typename T>
	[[nodiscard]] T& root_of(const std::string_view sceneName){
		if(const auto rst = scenes.try_find(sceneName)){
			return (*rst)->root<T>();
		}

		std::println(std::cerr, "Scene {} Not Found", sceneName);
		throw std::invalid_argument{"In-exist Scene Name"};
	}

	void resize(const math::frect region, const std::string_view name = scene_names::main){
		if(const auto rst = scenes.try_find(name)){
			(*rst)->resize(region);
		}
	}

	void resize_all(const math::frect region){
		for(auto& rst : scenes | std::views::values){
			rst->resize(region);
		}
	}

	[[nodiscard]] bool is_scroll_idle() const noexcept{
		return !focus || focus->input_handler_.focus_scroll == nullptr;
	}

	[[nodiscard]] bool is_focus_idle() const noexcept{
		return !focus || focus->input_handler_.focus_cursor == nullptr;
	}

	[[nodiscard]] mr::arena_id_t get_arena_id() const{
		return pool_.get_arena_id();
	}

	void consume(std::span<const input_handle::input_event_variant> events) const {
		using namespace input_handle;
		auto& f = get_current_focus();
		for(const auto& ev : events){
			switch(ev.type){
			case input_event_type::input_key:
				f.on_key_input(ev.input_key);
				break;
			case input_event_type::input_mouse:
				f.on_mouse_input(ev.input_key);
				break;
			case input_event_type::input_scroll:
				f.on_scroll(ev.cursor);
				break;
			case input_event_type::input_u32:
				f.on_unicode_input(ev.input_char);
				break;
			case input_event_type::cursor_inbound:
				f.on_inbound_changed(ev.is_inbound);
				break;
			case input_event_type::cursor_move:
				f.on_cursor_move(ev.cursor);
				break;
			case input_event_type::frame_split:
				f.update(std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 60>>>(ev.frame_delta_time).count());
				break;
			}
		}
	}
};

}
