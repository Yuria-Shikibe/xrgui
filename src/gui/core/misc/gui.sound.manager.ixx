export module mo_yanxi.gui.sound.manager;

import std;
import mo_yanxi.heterogeneous;
export import mo_yanxi.audio.resources;

namespace mo_yanxi::gui::sound{

export enum class play_event : std::size_t{
	on_press,
	on_release,
	on_double_press,
	on_drag,
	on_scroll,
	on_key,
	on_text_input,
	on_ime_composition,
};

export class asset_group;

export struct asset_group_deleter{
	void operator()(asset_group* group) const noexcept;
};

export using asset_group_handle = mo_yanxi::referenced_ptr<asset_group, asset_group_deleter>;

export class asset_group final : public mo_yanxi::referenced_object_atomic{
	std::vector<audio::audio_asset_handle> assets_{};

public:
	using event_type = play_event;
	using value_type = audio::audio_asset_handle;

	[[nodiscard]] asset_group() = default;

	[[nodiscard]] explicit asset_group(std::vector<audio::audio_asset_handle> assets)
		: assets_(std::move(assets)){
	}

	[[nodiscard]] asset_group(std::initializer_list<audio::audio_asset_handle> assets)
		: assets_(assets){
	}

	asset_group(const asset_group&) = delete;
	asset_group(asset_group&&) = delete;
	asset_group& operator=(const asset_group&) = delete;
	asset_group& operator=(asset_group&&) = delete;

	[[nodiscard]] audio::audio_asset_handle get(std::size_t index) const noexcept{
		if(index < assets_.size()){
			return assets_[index];
		}
		return {};
	}

	[[nodiscard]] audio::audio_asset_handle get(play_event event) const noexcept{
		return get(std::to_underlying(event));
	}

	void set(std::size_t index, audio::audio_asset_handle asset){
		if(index >= assets_.size()){
			assets_.resize(index + 1u);
		}
		assets_[index] = std::move(asset);
	}

	void set(play_event event, audio::audio_asset_handle asset){
		set(std::to_underlying(event), std::move(asset));
	}

	void clear(std::size_t index) noexcept{
		if(index < assets_.size()){
			assets_[index].reset();
		}
	}

	void clear(play_event event) noexcept{
		clear(std::to_underlying(event));
	}

	void clear() noexcept{
		assets_.clear();
	}

	void reserve(std::size_t size){
		assets_.reserve(size);
	}

	[[nodiscard]] bool empty() const noexcept{
		return assets_.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept{
		return assets_.size();
	}
};

export inline void asset_group_deleter::operator()(asset_group* group) const noexcept{
	delete group;
}

export [[nodiscard]] inline asset_group_handle make_asset_group(){
	return asset_group_handle{new asset_group{}};
}

export [[nodiscard]] inline asset_group_handle make_asset_group(std::vector<audio::audio_asset_handle> assets){
	return asset_group_handle{new asset_group{std::move(assets)}};
}

export [[nodiscard]] inline asset_group_handle make_asset_group(
	std::initializer_list<audio::audio_asset_handle> assets){
	return asset_group_handle{new asset_group{assets}};
}

export struct manager{
private:
	string_hash_map<asset_group_handle> groups_{};

public:
	manager() = default;

	void reserve(std::size_t size){
		groups_.reserve(size);
	}

	[[nodiscard]] asset_group_handle resolve(std::string_view key) const noexcept{
		if(const auto itr = groups_.find(key); itr != groups_.end()){
			return itr->second;
		}
		return {};
	}

	[[nodiscard]] asset_group_handle at(std::string_view key) const{
		if(const auto itr = groups_.find(key); itr != groups_.end()){
			return itr->second;
		}
		throw std::out_of_range{"sound group key not found"};
	}

	template <typename Key>
		requires std::convertible_to<Key, std::string_view>
	auto insert_or_assign(Key&& key, asset_group_handle group){
		return groups_.insert_or_assign(std::forward<Key>(key), std::move(group));
	}

	[[nodiscard]] bool contains(std::string_view key) const noexcept{
		return groups_.contains(key);
	}

	bool erase(std::string_view key) noexcept{
		return groups_.erase(key) > 0u;
	}

	void clear() noexcept{
		groups_.clear();
	}

	[[nodiscard]] bool empty() const noexcept{
		return groups_.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept{
		return groups_.size();
	}
};

}
