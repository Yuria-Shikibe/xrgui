//
// Created by Matrix on 2025/10/30.
//

export module mo_yanxi.input_handle:key_mapping_manager;

import std;

import mo_yanxi.math.vector2;
import mo_yanxi.concepts;
import mo_yanxi.heterogeneous;
import :constants;
import :key_binding;

//TODO using mimalloc instead?
namespace mo_yanxi::input_handle{

export
template <typename ...Args>
struct input_manager{
private:
	std::unique_ptr<std::pmr::memory_resource> memory_resource_{
			std::make_unique<std::pmr::unsynchronized_pool_resource>(std::pmr::new_delete_resource())
		};

public:
	key_mapping<Args...> main_binds{memory_resource_.get()};

private:
	bool isInbound{false};

	math::vec2 cursor_pos_{};
	math::vec2 last_cursor_pos_{};
	math::vec2 mouse_velocity_{};
	math::vec2 scroll_offset_{};

	string_hash_map<
		std::unique_ptr<key_mapping_interface>,
		std::pmr::polymorphic_allocator<
			std::pair<const std::string, std::unique_ptr<key_mapping_interface>>>>
	subInputs{memory_resource_.get()};

public:
	key_mapping_interface* find_sub_input(std::string_view mappingName) const noexcept{
		if(auto* p = subInputs.try_find(mappingName)){
			return p->get();
		}
		return nullptr;
	}

	template <typename... CtxArgs>
		requires ((!std::derived_from<CtxArgs, key_mapping_interface>) && ...)
	key_mapping<CtxArgs...>& register_sub_input(const std::string_view mappingName){
		std::pair<decltype(subInputs)::iterator, bool> rst = subInputs.try_emplace(mappingName);
		if(rst.second){
			rst.first->second = std::make_unique<key_mapping<CtxArgs...>>(memory_resource_.get());
		}
		rst.first->second->ref_incr();
		return dynamic_cast<key_mapping<CtxArgs...>&>(*rst.first->second);
	}
	template <std::derived_from<key_mapping_interface> Mapping>
	Mapping& register_sub_input(const std::string_view mappingName){
		std::pair<decltype(subInputs)::iterator, bool> rst = subInputs.try_emplace(mappingName);
		if(rst.second){
			rst.first->second = std::make_unique<Mapping>(memory_resource_.get());
		}
		rst.first->second->ref_incr();
		return dynamic_cast<Mapping&>(*rst.first->second);
	}

	bool erase_sub_input(const std::string_view mappingName){
		if(const auto itr = subInputs.find(mappingName); itr != subInputs.end()){
			if(itr->second->ref_decr()){
				subInputs.erase(itr);
				return true;
			}
		}
		return false;
	}

	void inform(const key_set key){
		main_binds.inform_input(key);

		for(auto& subInput : subInputs | std::views::values){
			subInput->inform_input(key);
		}
	}

	void inform_mouse_action(const key_set key){
		inform(key);
	}

	void cursor_move_inform(const math::vec2 pos){
		last_cursor_pos_ = cursor_pos_;
		cursor_pos_ = pos;

		main_binds.inform_input(pos_binding_target::cursor_absolute, cursor_pos_);
		for(auto& subInput : subInputs | std::views::values){
			subInput->inform_input(pos_binding_target::cursor_absolute, cursor_pos_);
		}
	}

	[[nodiscard]] math::vec2 cursor_pos() const noexcept{
		return cursor_pos_;
	}

	[[nodiscard]] math::vec2 cursor_delta() const noexcept{
		return cursor_pos_ - last_cursor_pos_;
	}

	[[nodiscard]] math::vec2 last_cursor_pos() const noexcept{
		return last_cursor_pos_;
	}

	[[nodiscard]] math::vec2 scroll() const noexcept{
		return scroll_offset_;
	}

	void set_scroll_offset(const float x, const float y){
		scroll_offset_.set({x, y});

		main_binds.inform_input(pos_binding_target::scroll, scroll_offset_);
		for(auto& subInput : subInputs | std::views::values){
			subInput->inform_input(pos_binding_target::scroll, scroll_offset_);
		}
	}

	[[nodiscard]] bool is_cursor_inbound() const noexcept{
		return isInbound;
	}

	void set_inbound(const bool b) noexcept{
		if(isInbound != b){
			main_binds.inform_inbound(b, cursor_pos_);
			for(auto& subInput : subInputs | std::views::values){
				subInput->inform_inbound(b, cursor_pos_);
			}
			isInbound = b;
		}

	}

	void update(const float delta_in_tick){
		main_binds.on_push(delta_in_tick);

		mouse_velocity_ = cursor_pos_;
		mouse_velocity_ -= last_cursor_pos_;


		main_binds.inform_input(pos_binding_target::cursor_delta, mouse_velocity_);
		for(auto& subInput : subInputs | std::views::values){
			subInput->inform_input(pos_binding_target::cursor_delta, mouse_velocity_);
		}
		mouse_velocity_ /= delta_in_tick;

		main_binds.inform_input(pos_binding_target::cursor_velocity, mouse_velocity_);
		for(auto& subInput : subInputs | std::views::values){
			subInput->inform_input(pos_binding_target::cursor_velocity, mouse_velocity_);
		}

		for(auto& subInput : subInputs | std::views::values){
			subInput->update(delta_in_tick);
		}
	}
};
}
