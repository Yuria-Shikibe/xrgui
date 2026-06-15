module;

#include <cassert>
export module mo_yanxi.audio.resources;

import std;
export import mo_yanxi.audio;

namespace mo_yanxi::audio{

export enum class audio_resource_state : std::uint8_t{
	unloaded,
	queued,
	loading,
	loaded,
	failed,
};

export enum class audio_resource_error : std::uint8_t{
	none,
	system_unavailable,
	load_not_accepted,
	resource_failed,
};

export struct audio_asset_id{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const audio_asset_id&) const noexcept = default;
};

export struct audio_asset_id_hash{
	[[nodiscard]] std::size_t operator()(const audio_asset_id id) const noexcept{
		return std::hash<std::uint64_t>{}(id.value);
	}
};

export struct audio_resource_options{
	audio_load_priority load_priority{default_audio_load_priority};
	bool protected_resource{};
};

export class audio_resource_manager;
export class audio_asset_record;
export struct audio_asset_handle;

export class audio_asset_record final : public mo_yanxi::referenced_object_atomic_lazy{
	friend class audio_resource_manager;
	friend struct audio_asset_handle;

	audio_system* system_;
	audio_resource_manager& owner_;
	audio_asset_id asset_id_{};
	load_desc desc_{};
	audio_resource_metadata metadata_{};
	audio_load_priority load_priority_{default_audio_load_priority};

	resource_handle handle_{};
	backend_resource_token backend_handle_{};
	std::atomic<audio_resource_state> state_{audio_resource_state::unloaded};
	bool protected_resource_{};
	audio_resource_error last_error_{audio_resource_error::none};

public:
	[[nodiscard]] audio_asset_record(
		audio_system& system,
		audio_resource_manager& owner,
		audio_asset_id id,
		load_desc desc,
		audio_resource_options options = {});

	~audio_asset_record() = default;

	audio_asset_record(const audio_asset_record&) = delete;
	audio_asset_record(audio_asset_record&&) = delete;
	audio_asset_record& operator=(const audio_asset_record&) = delete;
	audio_asset_record& operator=(audio_asset_record&&) = delete;

	[[nodiscard]] audio_asset_id asset_id() const noexcept{
		return asset_id_;
	}

	[[nodiscard]] const load_desc& load_description() const noexcept{
		return desc_;
	}

	[[nodiscard]] const audio_resource_metadata& backend_metadata() const noexcept{
		return metadata_;
	}

	[[nodiscard]] audio_system& system() const noexcept;

	[[nodiscard]] audio_resource_state state() const noexcept{
		return state_.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool loaded() const noexcept{
		return state() == audio_resource_state::loaded;
	}

	[[nodiscard]] bool resident() const noexcept{
		const auto current = state();
		return current == audio_resource_state::queued ||
			current == audio_resource_state::loading ||
			current == audio_resource_state::loaded;
	}

	[[nodiscard]] audio_load_priority load_priority() const noexcept
	{
		return load_priority_;
	}

	[[nodiscard]] bool is_protected() const noexcept;
	void set_protected(bool value) noexcept;

	[[nodiscard]] std::size_t external_ref_count() const noexcept{
		return ref_count();
	}

	[[nodiscard]] std::string last_error() const;

	[[nodiscard]] resource_handle load_now(audio_load_priority priority = default_audio_load_priority);
	void unload() noexcept;
	void reload(audio_load_priority priority = default_audio_load_priority);

private:
	[[nodiscard]] static std::string error_message_(audio_resource_error error);
};

struct audio_asset_handle : mo_yanxi::referenced_ptr<audio_asset_record, mo_yanxi::no_deletion_on_ref_count_to_zero>{
	using base_type = referenced_ptr;

	[[nodiscard]] audio_asset_handle() = default;

private:
	friend class audio_resource_manager;

	[[nodiscard]] explicit(false) audio_asset_handle(audio_asset_record& record)
		: base_type(std::addressof(record)){
	}

	[[nodiscard]] explicit(false) audio_asset_handle(audio_asset_record* record)
		: base_type(record){
	}
};

class audio_resource_manager{
	using asset_map = std::unordered_map<audio_asset_id, std::unique_ptr<audio_asset_record>, audio_asset_id_hash>;
	using resource_map = std::unordered_map<resource_handle, audio_asset_id, resource_handle_hash>;

	mutable std::shared_mutex catalog_mutex_{};

	audio_system* system_;
	std::uint64_t next_asset_id_{1};

	asset_map assets_{};
	resource_map resources_{};

public:
	[[nodiscard]] explicit audio_resource_manager(audio_system& system) noexcept
		: system_(&system){
	}

	~audio_resource_manager(){
		clear_audio();
	}

	audio_resource_manager(const audio_resource_manager&) = delete;
	audio_resource_manager(audio_resource_manager&&) = delete;
	audio_resource_manager& operator=(const audio_resource_manager&) = delete;
	audio_resource_manager& operator=(audio_resource_manager&&) = delete;

	[[nodiscard]] audio_system& system() const noexcept{
		return *system_;
	}

	[[nodiscard]] audio_asset_handle register_audio(
		load_desc desc,
		audio_resource_options options = {}){
		std::unique_lock lock{catalog_mutex_};

		const auto id = allocate_asset_id_();
		auto record = std::make_unique<audio_asset_record>(
			system(),
			*this,
			id,
			std::move(desc),
			options);
		auto* record_ptr = record.get();
		assets_.emplace(id, std::move(record));

		audio_asset_handle handle{*record_ptr};
		if(options.load_priority != lazy_audio_load_priority){
			(void)request_load_(*record_ptr, true, options.load_priority);
		}
		return handle;
	}

	[[nodiscard]] audio_asset_handle find_audio(const audio_asset_id id) const{
		std::shared_lock lock{catalog_mutex_};
		return audio_asset_handle{find_asset_unlocked_(id)};
	}

	bool erase_audio(const audio_asset_id id) noexcept{
		std::unique_lock lock{catalog_mutex_};
		auto* record = find_asset_unlocked_(id);
		if(record == nullptr){
			return false;
		}
		record->protected_resource_ = false;
		release_unused_();
		collect_retired_entries_();
		return true;
	}

	bool protect_audio(const audio_asset_id id, bool protected_resource = true) noexcept{
		std::unique_lock lock{catalog_mutex_};
		return protect_audio_unlocked_(id, protected_resource);
	}

	void clear_audio() noexcept{
		std::unique_lock lock{catalog_mutex_};
		resources_.clear();
		for(auto& entry : assets_){
			auto& record = entry.second;
			record->protected_resource_ = false;
			(void)(release_backend_(*record, true));
		}
		collect_retired_entries_();
	}

	void consume_audio_event(const audio_event& event){
		consume_audio_events(std::span{std::addressof(event), 1u});
	}

	void consume_audio_events(std::span<const audio_event> events){
		std::unique_lock lock{catalog_mutex_};
		for(const auto& event : events){
			consume_audio_event_unlocked_(event);
		}
	}

	void maintain(){
		std::unique_lock lock{catalog_mutex_};
		release_unused_();
		collect_retired_entries_();
	}

	void release_unused(){
		std::unique_lock lock{catalog_mutex_};
		release_unused_();
	}

private:
	[[nodiscard]] audio_asset_id allocate_asset_id_() noexcept{
		return audio_asset_id{next_asset_id_++};
	}

	[[nodiscard]] audio_asset_record* find_asset_unlocked_(const audio_asset_id id) noexcept{
		const auto iter = assets_.find(id);
		return iter == assets_.end() ? nullptr : iter->second.get();
	}

	[[nodiscard]] audio_asset_record* find_asset_unlocked_(const audio_asset_id id) const noexcept{
		const auto iter = assets_.find(id);
		return iter == assets_.end() ? nullptr : iter->second.get();
	}

	bool protect_audio_unlocked_(const audio_asset_id id, const bool protected_resource) noexcept{
		auto* record = find_asset_unlocked_(id);
		if(record == nullptr){
			return false;
		}
		record->protected_resource_ = protected_resource;
		return true;
	}

	[[nodiscard]] resource_handle request_load_(
		audio_asset_record& record,
		bool retry_failed,
		audio_load_priority priority){
		if(priority == lazy_audio_load_priority){
			return {};
		}
		const auto current_state = record.state_.load(std::memory_order_acquire);
		if(record.handle_ &&
			(current_state == audio_resource_state::queued ||
				current_state == audio_resource_state::loading ||
				current_state == audio_resource_state::loaded)){
			return record.handle_;
		}
		if(current_state == audio_resource_state::queued){
			return {};
		}
		if(current_state == audio_resource_state::failed && !retry_failed){
			return {};
		}
		if(!system().valid()){
			record.last_error_ = audio_resource_error::system_unavailable;
			record.state_.store(audio_resource_state::failed, std::memory_order_release);
			return {};
		}

		record.state_.store(audio_resource_state::queued, std::memory_order_release);
		const auto new_handle = system().load(record.desc_, priority);
		if(!new_handle){
			record.last_error_ = audio_resource_error::load_not_accepted;
			record.state_.store(audio_resource_state::failed, std::memory_order_release);
			return {};
		}

		record.handle_ = new_handle;
		record.backend_handle_ = {};
		record.last_error_ = audio_resource_error::none;
		resources_[new_handle] = record.asset_id_;
		record.state_.store(audio_resource_state::loading, std::memory_order_release);
		return new_handle;
	}

	bool release_backend_(audio_asset_record& record, bool force) noexcept{
		if(!record.handle_){
			if(record.state_.load(std::memory_order_acquire) != audio_resource_state::failed){
				record.state_.store(audio_resource_state::unloaded, std::memory_order_release);
			}
			return false;
		}
		if(record.protected_resource_ && !force){
			return false;
		}

		record.last_error_ = audio_resource_error::none;
		record.state_.store(audio_resource_state::unloaded, std::memory_order_release);
		const auto old_handle = std::exchange(record.handle_, {});
		record.backend_handle_ = {};
		resources_.erase(old_handle);
		system().unload(old_handle);
		return true;
	}

	void consume_audio_event_unlocked_(const audio_event& event){
		if(!event.resource){
			return;
		}

		const auto iter = resources_.find(event.resource);
		if(iter == resources_.end()){
			return;
		}

		auto* record = find_asset_unlocked_(iter->second);
		if(record == nullptr || event.resource != record->handle_){
			return;
		}

		switch(event.type){
		case audio_event_type::resource_loaded:
			record->backend_handle_ = event.backend_handle;
			record->last_error_ = audio_resource_error::none;
			record->state_.store(audio_resource_state::loaded, std::memory_order_release);
			break;
		case audio_event_type::resource_failed:
			record->last_error_ = audio_resource_error::resource_failed;
			record->state_.store(audio_resource_state::failed, std::memory_order_release);
			record->handle_ = {};
			record->backend_handle_ = {};
			resources_.erase(event.resource);
			break;
		case audio_event_type::resource_unloaded:
			record->last_error_ = audio_resource_error::none;
			record->state_.store(audio_resource_state::unloaded, std::memory_order_release);
			record->handle_ = {};
			record->backend_handle_ = {};
			resources_.erase(event.resource);
			break;
		default:
			break;
		}
	}

	[[nodiscard]] bool protected_of_(const audio_asset_record& record) const noexcept{
		std::shared_lock lock{catalog_mutex_};
		return record.protected_resource_;
	}

	void set_protected_(audio_asset_record& record, bool value) noexcept{
		std::unique_lock lock{catalog_mutex_};
		record.protected_resource_ = value;
	}

	[[nodiscard]] audio_resource_error last_error_of_(const audio_asset_record& record) const noexcept{
		std::shared_lock lock{catalog_mutex_};
		return record.last_error_;
	}

	[[nodiscard]] bool is_resident_unlocked_(const audio_asset_record& record) const noexcept{
		const auto current_state = record.state_.load(std::memory_order_acquire);
		return record.handle_ &&
			(current_state == audio_resource_state::queued ||
				current_state == audio_resource_state::loading ||
				current_state == audio_resource_state::loaded);
	}

	void release_unused_(){
		for(auto& entry : assets_){
			auto& record = entry.second;
			if(is_resident_unlocked_(*record) && !record->protected_resource_ && record->ref_count() == 0){
				(void)(release_backend_(*record, false));
			}
		}
	}

	void collect_retired_entries_() noexcept{
		std::vector<audio_asset_id> candidates{};
		for(const auto& entry : assets_){
			const auto& record = *entry.second;
			if(is_resident_unlocked_(record) || !record.droppable()){
				continue;
			}
			candidates.push_back(entry.first);
		}
		for(const auto id : candidates){
			const auto iter = assets_.find(id);
			if(iter == assets_.end()){
				continue;
			}
			auto& record = *iter->second;
			if(!is_resident_unlocked_(record) &&
					record.check_droppable_and_retire()){
				assets_.erase(iter);
			}
		}
	}

	friend class audio_asset_record;
};

audio_system& audio_asset_record::system() const noexcept{
	return *system_;
}

bool audio_asset_record::is_protected() const noexcept{
	return owner_.protected_of_(*this);
}

void audio_asset_record::set_protected(const bool value) noexcept{
	owner_.set_protected_(*this, value);
}

std::string audio_asset_record::last_error() const{
	return error_message_(owner_.last_error_of_(*this));
}

audio_asset_record::audio_asset_record(
	audio_system& system,
	audio_resource_manager& owner,
	audio_asset_id id,
	load_desc desc,
	audio_resource_options options)
	: system_(&system),
	  owner_(owner),
	  asset_id_(id),
	  desc_(std::move(desc)),
	  metadata_(make_audio_resource_metadata(desc_)),
	  load_priority_(options.load_priority),
	  protected_resource_(options.protected_resource){
}

std::string audio_asset_record::error_message_(const audio_resource_error error){
	switch(error){
	case audio_resource_error::none:
		return {};
	case audio_resource_error::system_unavailable:
		return "audio system is unavailable";
	case audio_resource_error::load_not_accepted:
		return "audio load command was not accepted";
	case audio_resource_error::resource_failed:
		return "audio resource failed";
	}
	return "audio resource failed";
}

resource_handle audio_asset_record::load_now(const audio_load_priority priority){
	auto& owner = owner_;
	std::unique_lock lock{owner.catalog_mutex_};
	return owner.request_load_(*this, true, priority);
}

void audio_asset_record::unload() noexcept{
	auto& owner = owner_;
	std::unique_lock lock{owner.catalog_mutex_};
	(void)(owner.release_backend_(*this, false));
}

void audio_asset_record::reload(const audio_load_priority priority){
	auto& owner = owner_;
	std::unique_lock lock{owner.catalog_mutex_};
	if(protected_resource_){
		return;
	}
	(void)(owner.release_backend_(*this, false));
	last_error_ = audio_resource_error::none;
	state_.store(audio_resource_state::unloaded, std::memory_order_release);
	(void)(owner.request_load_(*this, true, priority));
}

}
