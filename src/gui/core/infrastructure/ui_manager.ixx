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

	mutable std::mutex resources_mutex_{};
	string_hash_map<scene_resources> resources{};

	mutable std::mutex scenes_mutex_{};
	string_hash_map<std::unique_ptr<scene>> scenes{};

	std::atomic<scene*> focus{nullptr};

public:
	scene_resources& add_scene_resources(std::string_view name){
		std::lock_guard lock(resources_mutex_);
		return resources.try_emplace(name, get_arena_id()).first->second;
	}

	scene* switch_scene_to(std::string_view name) noexcept{
		std::lock_guard lock(scenes_mutex_);
		if(auto scene_itr = scenes.try_find(name)){
			// 使用 acq_rel 序交换原子变量
			return focus.exchange(scene_itr->get(), std::memory_order_acq_rel);
		}
		return nullptr;
	}

	[[nodiscard]] scene& get_current_focus() const noexcept{
		scene* current_focus = focus.load(std::memory_order_acquire);
		assert(current_focus);
		return *current_focus;
	}

private:
	template <typename S, typename ...Args>
	S& add_scene(std::string_view name, bool focusIt, Args&& ...args){
		std::lock_guard lock(scenes_mutex_);
		std::pair<decltype(scenes)::iterator, bool> itr = scenes.try_emplace(name, std::make_unique<S>(std::forward<Args>(args)...));

		if(focusIt){
			focus.store(itr.first->second.get(), std::memory_order_release);
			assert(focus != nullptr);
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
		scene_resources& resources_ref,
		bool focus_it,
		renderer_frontend&& renderer_ui,
		Args&&... args){
		auto& scene_ = this->add_scene<S>(
			name,
			focus_it,
			resources_ref,
			std::move(renderer_ui), std::in_place_type<T>,
			std::forward<Args>(args)...);

		auto& root = this->root_of<T>(name);
		return {scene_, root};
	}

	bool erase_scene(std::string_view name) /*noexcept*/{
		std::lock_guard lock(scenes_mutex_);
		if(auto itr = scenes.find(name); itr != scenes.end()){
			scene* expected = itr->second.get();
			// 使用 CAS 来确保只有当 focus 仍然指向当前将被删除的 scene 时才清空它
			focus.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);

			scenes.erase(itr);

			return true;
		}
		return false;
	}

	void erase_resource(std::string_view name) noexcept {
		std::lock_guard lock(resources_mutex_);
		resources.erase(name);
	}

	scene* get_scene(const std::string_view sceneName){
		std::lock_guard lock(scenes_mutex_);
		auto ptr = scenes.try_find(sceneName);
		return ptr ? ptr->get() : nullptr;
	}

	template <typename T>
	[[nodiscard]] T& root_of(const std::string_view sceneName){
		std::lock_guard lock(scenes_mutex_);
		if(const auto rst = scenes.try_find(sceneName)){
			return (*rst)->root<T>();
		}

		std::println(std::cerr, "Scene {} Not Found", sceneName);
		throw std::invalid_argument{"In-exist Scene Name"};
	}

	[[nodiscard]] bool is_scroll_idle() const noexcept{
		scene* current_focus = focus.load(std::memory_order_acquire);
		return !current_focus || current_focus->input_handler_.focus_scroll == nullptr;
	}

	[[nodiscard]] bool is_focus_idle() const noexcept{
		scene* current_focus = focus.load(std::memory_order_acquire);
		return !current_focus || current_focus->input_handler_.focus_cursor == nullptr;
	}

	[[nodiscard]] mr::arena_id_t get_arena_id() const{
		return pool_.get_arena_id();
	}

};

}