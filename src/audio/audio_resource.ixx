module;

#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include "plf_hive.h"
#endif

export module mo_yanxi.audio.resources;

import std;
export import mo_yanxi.audio;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
#endif

namespace mo_yanxi::audio{

export enum class audio_resource_state : std::uint8_t{
	unloaded,
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

export struct audio_resource_options{
	audio_load_priority load_priority{default_audio_load_priority};
	bool protected_resource{};
	std::uint64_t reserved_bytes{};
};

export struct audio_resource_limits{
	std::size_t max_loaded_resources{std::numeric_limits<std::size_t>::max()};
	std::uint64_t max_reserved_bytes{std::numeric_limits<std::uint64_t>::max()};
};

export class audio_resource_manager;
export class audio_asset_record;
export struct audio_asset_handle;

export class audio_asset_record final : public mo_yanxi::referenced_object_atomic_lazy{
	friend class audio_resource_manager;
	friend struct audio_asset_handle;

	audio_resource_manager* owner_{};
	std::string id_{};
	load_desc desc_{};
	audio_resource_metadata metadata_{};
	audio_system* system_{};
	audio_load_priority load_priority_{default_audio_load_priority};
	std::uint64_t reserved_bytes_{};

	resource_handle handle_{};
	backend_resource_token backend_handle_{};
	audio_resource_state state_{audio_resource_state::unloaded};
	bool protected_resource_{};
	bool registered_{true};
	std::uint64_t last_used_{};
	audio_resource_error last_error_{audio_resource_error::none};

public:
	[[nodiscard]] audio_asset_record(
		audio_resource_manager& owner,
		std::string id,
		load_desc desc,
		audio_system& system,
		audio_resource_options options = {});

	~audio_asset_record() = default;

	audio_asset_record(const audio_asset_record&) = delete;
	audio_asset_record(audio_asset_record&&) = delete;
	audio_asset_record& operator=(const audio_asset_record&) = delete;
	audio_asset_record& operator=(audio_asset_record&&) = delete;

	[[nodiscard]] std::string_view id() const noexcept{
		return id_;
	}

	[[nodiscard]] const load_desc& load_description() const noexcept{
		return desc_;
	}

	[[nodiscard]] const audio_resource_metadata& backend_metadata() const noexcept{
		return metadata_;
	}

	[[nodiscard]] audio_system& system() const noexcept{
		assert(system_ != nullptr);
		return *system_;
	}

	[[nodiscard]] audio_system* try_system() const noexcept{
		return system_;
	}

	[[nodiscard]] resource_handle handle() const noexcept;
	[[nodiscard]] backend_resource_token backend_handle() const noexcept;
	[[nodiscard]] audio_resource_state state() const noexcept;

	[[nodiscard]] bool loaded() const noexcept{
		return state() == audio_resource_state::loaded;
	}

	[[nodiscard]] bool resident() const noexcept;

	[[nodiscard]] audio_load_priority load_priority() const noexcept{
		return load_priority_;
	}

	[[nodiscard]] bool is_protected() const noexcept;
	void set_protected(bool value) noexcept;
	[[nodiscard]] bool registered() const noexcept;

	[[nodiscard]] std::uint64_t reserved_bytes() const noexcept{
		return reserved_bytes_;
	}

	[[nodiscard]] std::uint64_t last_used() const noexcept;
	[[nodiscard]] std::size_t external_ref_count() const noexcept;
	[[nodiscard]] std::string last_error() const;

	[[nodiscard]] resource_handle load_now(audio_load_priority priority = default_audio_load_priority);
	[[nodiscard]] bool play_detached(
		play_settings settings = {},
		audio_load_priority priority = default_audio_load_priority);
	[[nodiscard]] bool play_detached(
		const audio_channel& channel,
		play_settings settings = {},
		audio_load_priority priority = default_audio_load_priority);
	[[nodiscard]] playback_control_handle play_controlled(
		play_settings settings = {},
		playback_control_options options = {},
		audio_load_priority priority = default_audio_load_priority);
	[[nodiscard]] playback_control_handle play_controlled(
		const audio_channel& channel,
		play_settings settings = {},
		playback_control_options options = {},
		audio_load_priority priority = default_audio_load_priority);
	void unload() noexcept;
	void reload(audio_load_priority priority = default_audio_load_priority);

private:
	[[nodiscard]] static std::uint64_t estimate_reserved_bytes_(const load_desc& desc) noexcept;
	[[nodiscard]] static std::string error_message_(audio_resource_error error);
	[[nodiscard]] audio_resource_manager& owner_checked_() const noexcept;
};

struct audio_asset_handle : mo_yanxi::referenced_ptr<audio_asset_record, mo_yanxi::no_deletion_on_ref_count_to_zero>{
	using base_type = mo_yanxi::referenced_ptr<audio_asset_record, mo_yanxi::no_deletion_on_ref_count_to_zero>;

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
	mutable std::mutex mutex_{};
	audio_system* system_{};
	audio_resource_limits limits_{};
	std::uint64_t access_clock_{};

	//TODO directly store hive iterator?

	std::unordered_map<std::string, audio_asset_record*> aliases_{};
	std::unordered_map<resource_handle, audio_asset_record*, resource_handle_hash> resources_{};
	plf::hive<audio_asset_record> records_{};

public:
	[[nodiscard]] explicit audio_resource_manager(audio_system& system) noexcept
		: system_(std::addressof(system)){
	}

	~audio_resource_manager(){
		clear_audio();
	}

	audio_resource_manager(const audio_resource_manager&) = delete;
	audio_resource_manager(audio_resource_manager&&) = delete;
	audio_resource_manager& operator=(const audio_resource_manager&) = delete;
	audio_resource_manager& operator=(audio_resource_manager&&) = delete;

	[[nodiscard]] audio_system& system() const noexcept{
		std::scoped_lock lock{mutex_};
		assert(system_ != nullptr);
		return *system_;
	}

	[[nodiscard]] audio_system* try_system() const noexcept{
		std::scoped_lock lock{mutex_};
		return system_;
	}

	void set_limits(audio_resource_limits limits){
		std::scoped_lock lock{mutex_};
		limits_ = limits;
		enforce_limits_();
	}

	[[nodiscard]] audio_resource_limits limits() const noexcept{
		std::scoped_lock lock{mutex_};
		return limits_;
	}

	void set_reserved_space_limit(std::uint64_t bytes){
		std::scoped_lock lock{mutex_};
		limits_.max_reserved_bytes = bytes;
		enforce_limits_();
	}

	[[nodiscard]] audio_asset_handle register_audio(
		std::string id,
		load_desc desc,
		audio_resource_options options = {}){
		std::scoped_lock lock{mutex_};
		if(system_ == nullptr){
			throw std::runtime_error{"audio resource manager requires an audio system before registering audio"};
		}
		if(id.empty()){
			throw std::invalid_argument{"audio resource id must not be empty"};
		}
		if(aliases_.contains(id)){
			throw std::invalid_argument{"audio resource id already exists"};
		}

		auto record_iter = records_.emplace(
			*this,
			std::move(id),
			std::move(desc),
			*system_,
			options);
		auto& record = *record_iter;
		aliases_.emplace(std::string{record.id()}, std::addressof(record));
		touch_(record);

		audio_asset_handle handle{record};
		if(options.load_priority != lazy_audio_load_priority){
			static_cast<void>(request_load_(record, true, options.load_priority));
		}
		enforce_limits_();
		return handle;
	}

	[[nodiscard]] audio_asset_handle find_audio(std::string_view id) const{
		std::scoped_lock lock{mutex_};
		if(const auto iter = aliases_.find(std::string{id}); iter != aliases_.end()){
			return audio_asset_handle{iter->second};
		}
		return {};
	}

	bool unregister_audio(std::string_view id) noexcept{
		std::scoped_lock lock{mutex_};
		const auto iter = aliases_.find(std::string{id});
		if(iter == aliases_.end()){
			return false;
		}
		if(iter->second != nullptr){
			iter->second->registered_ = false;
		}
		aliases_.erase(iter);
		return true;
	}

	bool protect_audio(std::string_view id, bool protected_resource = true) noexcept{
		std::scoped_lock lock{mutex_};
		const auto iter = aliases_.find(std::string{id});
		if(iter == aliases_.end() || iter->second == nullptr){
			return false;
		}
		iter->second->protected_resource_ = protected_resource;
		if(!protected_resource){
			enforce_limits_();
		}
		return true;
	}

	void clear_audio() noexcept{
		std::scoped_lock lock{mutex_};
		aliases_.clear();
		resources_.clear();
		for(auto& record : records_){
			record.registered_ = false;
			static_cast<void>(release_backend_(record, true));
		}
		collect_retired_entries_();
	}

	void consume_audio_event(const audio_event& event){
		std::scoped_lock lock{mutex_};
		consume_audio_event_unlocked_(event);
		enforce_limits_();
	}

	void maintain(){
		std::scoped_lock lock{mutex_};
		if(system_ != nullptr){
			system_->poll_events([this](const audio_event& event){
				consume_audio_event_unlocked_(event);
			});
		}
		release_unused_();
		enforce_limits_();
		collect_retired_entries_();
	}

	void release_unused(){
		std::scoped_lock lock{mutex_};
		release_unused_();
	}

private:
	struct resident_summary{
		std::size_t count{};
		std::uint64_t bytes{};
	};

	[[nodiscard]] resource_handle request_load_(
		audio_asset_record& record,
		bool retry_failed,
		audio_load_priority priority){
		if(priority == lazy_audio_load_priority){
			return {};
		}
		const auto current_state = record.state_;
		if(record.handle_ &&
			(current_state == audio_resource_state::loading || current_state == audio_resource_state::loaded)){
			return record.handle_;
		}
		if(current_state == audio_resource_state::failed && !retry_failed){
			return {};
		}
		if(record.system_ == nullptr || !record.system_->valid()){
			record.state_ = audio_resource_state::failed;
			record.last_error_ = audio_resource_error::system_unavailable;
			return {};
		}

		const auto new_handle = record.system_->load(record.desc_, priority);
		if(!new_handle){
			record.state_ = audio_resource_state::failed;
			record.last_error_ = audio_resource_error::load_not_accepted;
			return {};
		}

		record.handle_ = new_handle;
		record.backend_handle_ = {};
		record.state_ = audio_resource_state::loading;
		record.last_error_ = audio_resource_error::none;
		resources_[new_handle] = std::addressof(record);
		return new_handle;
	}

	bool release_backend_(audio_asset_record& record, bool force) noexcept{
		if(!record.handle_){
			if(record.state_ != audio_resource_state::failed){
				record.state_ = audio_resource_state::unloaded;
			}
			return false;
		}
		if(record.protected_resource_ && !force){
			return false;
		}

		const auto old_handle = std::exchange(record.handle_, {});
		record.backend_handle_ = {};
		resources_.erase(old_handle);
		if(record.system_ != nullptr){
			record.system_->unload(old_handle);
		}
		record.state_ = audio_resource_state::unloaded;
		record.last_error_ = audio_resource_error::none;
		return true;
	}

	void consume_audio_event_unlocked_(const audio_event& event){
		if(!event.resource){
			return;
		}

		const auto iter = resources_.find(event.resource);
		if(iter == resources_.end() || iter->second == nullptr){
			return;
		}

		auto& record = *iter->second;
		if(event.resource != record.handle_){
			return;
		}

		switch(event.type){
		case audio_event_type::resource_loaded:
			record.backend_handle_ = event.backend_handle;
			record.metadata_ = event.backend_metadata;
			record.state_ = audio_resource_state::loaded;
			record.last_error_ = audio_resource_error::none;
			break;
		case audio_event_type::resource_failed:
			record.state_ = audio_resource_state::failed;
			record.handle_ = {};
			record.backend_handle_ = {};
			record.last_error_ = audio_resource_error::resource_failed;
			resources_.erase(iter);
			break;
		case audio_event_type::resource_unloaded:
			record.state_ = audio_resource_state::unloaded;
			record.handle_ = {};
			record.backend_handle_ = {};
			record.last_error_ = audio_resource_error::none;
			resources_.erase(iter);
			break;
		default:
			break;
		}
	}

	void touch_(audio_asset_record& record) noexcept{
		record.last_used_ = ++access_clock_;
	}

	[[nodiscard]] resource_handle handle_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.handle_;
	}

	[[nodiscard]] backend_resource_token backend_handle_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.backend_handle_;
	}

	[[nodiscard]] audio_resource_state state_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.state_;
	}

	[[nodiscard]] bool resident_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return is_resident_unlocked_(record);
	}

	[[nodiscard]] bool protected_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.protected_resource_;
	}

	void set_protected_(audio_asset_record& record, bool value) noexcept{
		std::scoped_lock lock{mutex_};
		record.protected_resource_ = value;
		if(!value){
			enforce_limits_();
		}
	}

	[[nodiscard]] bool registered_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.registered_;
	}

	[[nodiscard]] std::uint64_t last_used_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.last_used_;
	}

	[[nodiscard]] audio_resource_error last_error_of_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.last_error_;
	}

	[[nodiscard]] std::size_t external_ref_count_(const audio_asset_record& record) const noexcept{
		std::scoped_lock lock{mutex_};
		return record.ref_count();
	}

	[[nodiscard]] bool is_resident_unlocked_(const audio_asset_record& record) const noexcept{
		return record.handle_ &&
			(record.state_ == audio_resource_state::loading || record.state_ == audio_resource_state::loaded);
	}

	[[nodiscard]] resident_summary resident_summary_() const noexcept{
		resident_summary summary{};
		for(const auto& record : records_){
			if(is_resident_unlocked_(record)){
				++summary.count;
				if(std::numeric_limits<std::uint64_t>::max() - summary.bytes < record.reserved_bytes()){
					summary.bytes = std::numeric_limits<std::uint64_t>::max();
				}else{
					summary.bytes += record.reserved_bytes();
				}
			}
		}
		return summary;
	}

	[[nodiscard]] audio_asset_record* find_capacity_candidate_() noexcept{
		audio_asset_record* best{};
		for(auto& record : records_){
			if(!is_resident_unlocked_(record) || record.protected_resource_){
				continue;
			}
			if(best == nullptr){
				best = std::addressof(record);
				continue;
			}

			const auto lhs_refs = record.ref_count();
			const auto rhs_refs = best->ref_count();
			if(lhs_refs == 0 && rhs_refs != 0){
				best = std::addressof(record);
			}else if(lhs_refs == rhs_refs && record.last_used_ < best->last_used_){
				best = std::addressof(record);
			}
		}
		return best;
	}

	void release_unused_(){
		for(auto& record : records_){
			if(is_resident_unlocked_(record) && !record.protected_resource_ && record.ref_count() == 0){
				static_cast<void>(release_backend_(record, false));
			}
		}
	}

	void enforce_limits_(){
		auto summary = resident_summary_();
		while(summary.count > limits_.max_loaded_resources ||
			summary.bytes > limits_.max_reserved_bytes){
			auto* candidate = find_capacity_candidate_();
			if(candidate == nullptr){
				break;
			}
			static_cast<void>(release_backend_(*candidate, false));
			summary = resident_summary_();
		}
	}

	void collect_retired_entries_() noexcept{
		for(auto iter = records_.begin(); iter != records_.end();){
			auto& record = *iter;
			if(record.registered_ || is_resident_unlocked_(record) || !record.droppable()){
				++iter;
				continue;
			}
			if(record.check_droppable_and_retire()){
				iter = records_.erase(iter);
			}else{
				++iter;
			}
		}
	}

	friend class audio_asset_record;
};

export using audio_resource_index = audio_resource_manager;

audio_resource_manager& audio_asset_record::owner_checked_() const noexcept{
	assert(owner_ != nullptr);
	return *owner_;
}

resource_handle audio_asset_record::handle() const noexcept{
	return owner_checked_().handle_of_(*this);
}

backend_resource_token audio_asset_record::backend_handle() const noexcept{
	return owner_checked_().backend_handle_of_(*this);
}

audio_resource_state audio_asset_record::state() const noexcept{
	return owner_checked_().state_of_(*this);
}

bool audio_asset_record::resident() const noexcept{
	return owner_checked_().resident_(*this);
}

bool audio_asset_record::is_protected() const noexcept{
	return owner_checked_().protected_of_(*this);
}

void audio_asset_record::set_protected(const bool value) noexcept{
	owner_checked_().set_protected_(*this, value);
}

bool audio_asset_record::registered() const noexcept{
	return owner_checked_().registered_of_(*this);
}

std::uint64_t audio_asset_record::last_used() const noexcept{
	return owner_checked_().last_used_of_(*this);
}

std::size_t audio_asset_record::external_ref_count() const noexcept{
	return owner_checked_().external_ref_count_(*this);
}

std::string audio_asset_record::last_error() const{
	return error_message_(owner_checked_().last_error_of_(*this));
}

audio_asset_record::audio_asset_record(
	audio_resource_manager& owner,
	std::string id,
	load_desc desc,
	audio_system& system,
	audio_resource_options options)
	: owner_(std::addressof(owner)),
	  id_(std::move(id)),
	  desc_(std::move(desc)),
	  metadata_(make_audio_resource_metadata(desc_)),
	  system_(std::addressof(system)),
	  load_priority_(options.load_priority),
	  reserved_bytes_(options.reserved_bytes != 0 ? options.reserved_bytes : estimate_reserved_bytes_(desc_)),
	  protected_resource_(options.protected_resource){
}

std::uint64_t audio_asset_record::estimate_reserved_bytes_(const load_desc& desc) noexcept{
	if(const auto* memory = desc.memory()){
		return memory->size();
	}
	const auto* path = desc.path();
	if(path == nullptr || path->empty()){
		return 0;
	}
	std::error_code ec{};
	const auto size = std::filesystem::file_size(*path, ec);
	return ec ? 0 : size;
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
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	return owner.request_load_(*this, true, priority);
}

bool audio_asset_record::play_detached(play_settings settings, const audio_load_priority priority){
	auto* system = try_system();
	if(system == nullptr){
		return false;
	}
	return play_detached(system->default_channel(), std::move(settings), priority);
}

bool audio_asset_record::play_detached(
	const audio_channel& channel,
	play_settings settings,
	const audio_load_priority priority){
	auto& owner = owner_checked_();
	resource_handle resource{};
	{
		std::scoped_lock lock{owner.mutex_};
		owner.touch_(*this);
		if(state_ == audio_resource_state::failed){
			return false;
		}
		resource = owner.request_load_(*this, false, priority);
	}
	if(!resource){
		return false;
	}
	return channel.play_detached(resource, std::move(settings));
}

playback_control_handle audio_asset_record::play_controlled(
	play_settings settings,
	playback_control_options options,
	const audio_load_priority priority){
	auto* system = try_system();
	if(system == nullptr){
		return {};
	}
	return play_controlled(system->default_channel(), std::move(settings), options, priority);
}

playback_control_handle audio_asset_record::play_controlled(
	const audio_channel& channel,
	play_settings settings,
	playback_control_options options,
	const audio_load_priority priority){
	auto& owner = owner_checked_();
	resource_handle resource{};
	{
		std::scoped_lock lock{owner.mutex_};
		owner.touch_(*this);
		if(state_ == audio_resource_state::failed){
			return {};
		}
		resource = owner.request_load_(*this, false, priority);
	}
	if(!resource){
		return {};
	}
	return channel.play_controlled(resource, std::move(settings), options);
}

void audio_asset_record::unload() noexcept{
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	static_cast<void>(owner.release_backend_(*this, false));
}

void audio_asset_record::reload(const audio_load_priority priority){
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	if(protected_resource_){
		return;
	}
	static_cast<void>(owner.release_backend_(*this, false));
	state_ = audio_resource_state::unloaded;
	last_error_ = audio_resource_error::none;
	static_cast<void>(owner.request_load_(*this, true, priority));
}

}
