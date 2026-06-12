module;

#include <gtl/phmap.hpp>

export module mo_yanxi.audio.resources;

import std;
export import mo_yanxi.audio;

namespace mo_yanxi::audio{

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
	std::uint64_t reserved_bytes{};
};

export struct audio_prepare_options{
	audio_load_priority priority{default_audio_load_priority};
	bool retry_failed{};
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
	audio_asset_id asset_id_{};
	load_desc desc_{};
	audio_resource_metadata metadata_{};
	audio_load_priority default_load_priority_{default_audio_load_priority};
	std::uint64_t reserved_bytes_{};

	audio_raw_resource_handle raw_handle_{};
	backend_resource_token backend_handle_{};
	audio_resource_state state_{audio_resource_state::unloaded};
	bool protected_resource_{};
	std::uint32_t active_uses_{};
	audio_resource_error last_error_{audio_resource_error::none};
	std::vector<std::string> aliases_{};

public:
	[[nodiscard]] audio_asset_record(
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

	[[nodiscard]] audio_resource_state state() const noexcept{
		return state_;
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

	[[nodiscard]] audio_load_priority load_priority() const noexcept{
		return default_load_priority_;
	}

	[[nodiscard]] bool is_protected() const noexcept;
	void set_protected(bool value) noexcept;

	[[nodiscard]] std::uint64_t reserved_bytes() const noexcept{
		return reserved_bytes_;
	}

	[[nodiscard]] std::size_t external_ref_count() const noexcept{
		return ref_count();
	}

	[[nodiscard]] std::uint32_t active_uses() const noexcept{
		return active_uses_;
	}

	[[nodiscard]] std::string last_error() const;

	[[nodiscard]] audio_raw_resource_handle load_now(audio_load_priority priority = default_audio_load_priority);
	[[nodiscard]] audio_play_token prepare_play(audio_prepare_options options = {});

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

export using audio_clip_handle = audio_asset_handle;

export class audio_resource_manager{
	struct alias_hash{
		using is_transparent = void;

		[[nodiscard]] std::size_t operator()(const std::string_view value) const noexcept{
			return std::hash<std::string_view>{}(value);
		}
	};

	struct alias_equal{
		using is_transparent = void;

		[[nodiscard]] bool operator()(const std::string_view lhs, const std::string_view rhs) const noexcept{
			return lhs == rhs;
		}
	};

	using asset_map = std::unordered_map<audio_asset_id, std::unique_ptr<audio_asset_record>, audio_asset_id_hash>;
	using alias_map = std::unordered_map<std::string, audio_asset_id, alias_hash, alias_equal>;
	using resource_map = std::unordered_map<audio_raw_resource_handle, audio_asset_id, resource_handle_hash>;

	mutable std::mutex mutex_{};
	audio_loader* loader_{};
	audio_player* player_{};
	audio_resource_limits limits_{};
	std::uint64_t next_asset_id_{1};

	asset_map assets_{};
	alias_map aliases_{};
	resource_map resources_{};

public:
	[[nodiscard]] audio_resource_manager() noexcept = default;

	[[nodiscard]] audio_resource_manager(audio_loader& loader, audio_player& player) noexcept
		: loader_(std::addressof(loader)),
		  player_(std::addressof(player)){
	}

	[[nodiscard]] explicit audio_resource_manager(audio_system& system) noexcept
		: audio_resource_manager(system.loader(), system.player()){
	}

	~audio_resource_manager(){
		clear_audio();
	}

	audio_resource_manager(const audio_resource_manager&) = delete;
	audio_resource_manager(audio_resource_manager&&) = delete;
	audio_resource_manager& operator=(const audio_resource_manager&) = delete;
	audio_resource_manager& operator=(audio_resource_manager&&) = delete;

	void bind(audio_loader& loader, audio_player& player) noexcept{
		std::scoped_lock lock{mutex_};
		if(loader_ == std::addressof(loader) && player_ == std::addressof(player)){
			return;
		}
		clear_audio_unlocked_();
		loader_ = std::addressof(loader);
		player_ = std::addressof(player);
	}

	void attach_system(audio_system& system) noexcept{
		bind(system.loader(), system.player());
	}

	[[nodiscard]] audio_loader& loader() const noexcept{
		return *loader_;
	}

	[[nodiscard]] audio_loader* try_loader() const noexcept{
		return loader_;
	}

	[[nodiscard]] audio_player& player() const noexcept{
		return *player_;
	}

	[[nodiscard]] audio_player* try_player() const noexcept{
		return player_;
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
		std::string alias,
		load_desc desc,
		audio_resource_options options = {}){
		std::scoped_lock lock{mutex_};
		if(loader_ == nullptr){
			throw std::runtime_error{"audio resource manager requires an audio loader before registering audio"};
		}
		if(alias.empty()){
			throw std::invalid_argument{"audio resource alias must not be empty"};
		}
		if(aliases_.contains(alias)){
			throw std::invalid_argument{"audio resource alias already exists"};
		}

		const auto id = allocate_asset_id_();
		auto record = std::make_unique<audio_asset_record>(
			*this,
			id,
			std::move(desc),
			options);
		auto* record_ptr = record.get();
		assets_.emplace(id, std::move(record));
		add_alias_unlocked_(*record_ptr, std::move(alias));

		audio_asset_handle handle{*record_ptr};
		if(options.load_priority != lazy_audio_load_priority){
			(void)request_load_(*record_ptr, true, options.load_priority);
		}
		enforce_limits_();
		return handle;
	}

	[[nodiscard]] audio_asset_handle find_audio(const audio_asset_id id) const{
		std::scoped_lock lock{mutex_};
		return audio_asset_handle{find_asset_unlocked_(id)};
	}

	[[nodiscard]] audio_asset_handle find_audio(std::string_view alias) const{
		std::scoped_lock lock{mutex_};
		if(const auto iter = aliases_.find(alias); iter != aliases_.end()){
			return audio_asset_handle{find_asset_unlocked_(iter->second)};
		}
		return {};
	}

	[[nodiscard]] std::optional<audio_asset_id> get_relevant_id(std::string_view alias) const{
		std::scoped_lock lock{mutex_};
		if(const auto iter = aliases_.find(alias); iter != aliases_.end()){
			return iter->second;
		}
		return std::nullopt;
	}

	void add_alias(const audio_asset_id id, std::string alias){
		std::scoped_lock lock{mutex_};
		auto* record = find_asset_unlocked_(id);
		if(record == nullptr){
			throw std::invalid_argument{"audio asset id does not exist"};
		}
		add_alias_unlocked_(*record, std::move(alias));
	}

	bool erase_alias(std::string_view alias) noexcept{
		std::scoped_lock lock{mutex_};
		const bool erased = erase_alias_unlocked_(alias);
		release_unused_();
		collect_retired_entries_();
		return erased;
	}

	bool erase_audio(const audio_asset_id id) noexcept{
		std::scoped_lock lock{mutex_};
		auto* record = find_asset_unlocked_(id);
		if(record == nullptr){
			return false;
		}
		for(const auto& alias : record->aliases_){
			aliases_.erase(alias);
		}
		record->aliases_.clear();
		record->protected_resource_ = false;
		release_unused_();
		collect_retired_entries_();
		return true;
	}

	bool unregister_audio(std::string_view alias) noexcept{
		std::scoped_lock lock{mutex_};
		const auto iter = aliases_.find(alias);
		if(iter == aliases_.end()){
			return false;
		}
		auto* record = find_asset_unlocked_(iter->second);
		if(record == nullptr){
			aliases_.erase(iter);
			return false;
		}
		for(const auto& record_alias : record->aliases_){
			aliases_.erase(record_alias);
		}
		record->aliases_.clear();
		record->protected_resource_ = false;
		release_unused_();
		collect_retired_entries_();
		return true;
	}

	bool protect_audio(std::string_view alias, bool protected_resource = true) noexcept{
		std::scoped_lock lock{mutex_};
		const auto iter = aliases_.find(alias);
		if(iter == aliases_.end()){
			return false;
		}
		return protect_audio_unlocked_(iter->second, protected_resource);
	}

	bool protect_audio(const audio_asset_id id, bool protected_resource = true) noexcept{
		std::scoped_lock lock{mutex_};
		return protect_audio_unlocked_(id, protected_resource);
	}

	[[nodiscard]] audio_raw_resource_handle load(audio_asset_handle resource, audio_load_priority priority = default_audio_load_priority){
		if(!resource){
			return {};
		}
		std::scoped_lock lock{mutex_};
		return request_load_(*resource, true, priority);
	}

	[[nodiscard]] audio_play_token prepare_play(
		audio_asset_handle resource,
		audio_prepare_options options = {}){
		if(!resource){
			return {};
		}
		std::scoped_lock lock{mutex_};
		return prepare_play_unlocked_(*resource, options);
	}

	void unload(audio_asset_handle resource) noexcept{
		if(!resource){
			return;
		}
		std::scoped_lock lock{mutex_};
		(void)release_backend_(*resource, false);
	}

	void clear_audio() noexcept{
		std::scoped_lock lock{mutex_};
		clear_audio_unlocked_();
	}

	void consume_audio_event(const audio_event& event){
		std::scoped_lock lock{mutex_};
		consume_audio_event_unlocked_(event);
		enforce_limits_();
	}

	void maintain(){
		std::scoped_lock lock{mutex_};
		if(loader_ != nullptr){
			std::vector<audio_event> pending{};
			loader_->pop_events(pending);
			for(const auto& event : pending){
				consume_audio_event_unlocked_(event);
			}
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

	void add_alias_unlocked_(audio_asset_record& record, std::string alias){
		if(alias.empty()){
			throw std::invalid_argument{"audio resource alias must not be empty"};
		}
		const auto alias_view = std::string_view{alias};
		auto [iter, inserted] = aliases_.try_emplace(std::move(alias), record.asset_id_);
		if(!inserted){
			throw std::invalid_argument{"audio resource alias already exists"};
		}
		try{
			record.aliases_.push_back(iter->first);
		}catch(...){
			aliases_.erase(alias_view);
			throw;
		}
	}

	bool erase_alias_unlocked_(std::string_view alias) noexcept{
		const auto iter = aliases_.find(alias);
		if(iter == aliases_.end()){
			return false;
		}
		const auto id = iter->second;
		aliases_.erase(iter);
		if(auto* record = find_asset_unlocked_(id)){
			std::erase_if(record->aliases_, [alias](const std::string& candidate){
				return candidate == alias;
			});
		}
		return true;
	}

	bool protect_audio_unlocked_(const audio_asset_id id, const bool protected_resource) noexcept{
		auto* record = find_asset_unlocked_(id);
		if(record == nullptr){
			return false;
		}
		record->protected_resource_ = protected_resource;
		if(!protected_resource){
			enforce_limits_();
		}
		return true;
	}

	[[nodiscard]] audio_raw_resource_handle request_load_(
		audio_asset_record& record,
		bool retry_failed,
		audio_load_priority priority){
		if(priority == lazy_audio_load_priority){
			return {};
		}
		if(record.raw_handle_ &&
			(record.state_ == audio_resource_state::queued ||
				record.state_ == audio_resource_state::loading ||
				record.state_ == audio_resource_state::loaded)){
			return record.raw_handle_;
		}
		if(record.state_ == audio_resource_state::failed && !retry_failed){
			return {};
		}
		if(loader_ == nullptr){
			record.last_error_ = audio_resource_error::runtime_unavailable;
			record.state_ = audio_resource_state::failed;
			return {};
		}

		if(!record.raw_handle_){
			record.raw_handle_ = loader_->reserve();
			if(!record.raw_handle_){
				record.last_error_ = audio_resource_error::load_not_accepted;
				record.state_ = audio_resource_state::failed;
				return {};
			}
			resources_[record.raw_handle_] = record.asset_id_;
		}

		if(!loader_->load(record.raw_handle_, record.desc_, priority)){
			record.last_error_ = audio_resource_error::load_not_accepted;
			record.state_ = audio_resource_state::failed;
			return {};
		}

		record.backend_handle_ = {};
		record.last_error_ = audio_resource_error::none;
		record.state_ = audio_resource_state::queued;
		return record.raw_handle_;
	}

	[[nodiscard]] audio_play_token prepare_play_unlocked_(
		audio_asset_record& record,
		audio_prepare_options options){
		if(options.priority == lazy_audio_load_priority){
			options.priority = default_audio_load_priority;
		}
		if(record.state_ == audio_resource_state::failed && !options.retry_failed){
			return {};
		}
		const auto resource = request_load_(record, options.retry_failed, options.priority);
		if(!resource || loader_ == nullptr){
			return {};
		}
		return loader_->acquire_play_token(resource);
	}

	bool release_backend_(audio_asset_record& record, bool force) noexcept{
		if(!record.raw_handle_){
			if(record.state_ != audio_resource_state::failed){
				record.state_ = audio_resource_state::unloaded;
			}
			return false;
		}
		if((record.protected_resource_ || record.active_uses_ != 0) && !force){
			return false;
		}

		record.last_error_ = audio_resource_error::none;
		record.state_ = audio_resource_state::unloaded;
		record.backend_handle_ = {};
		const auto old_handle = std::exchange(record.raw_handle_, {});
		resources_.erase(old_handle);
		if(loader_ != nullptr){
			loader_->unload(old_handle);
		}
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
		if(record == nullptr || event.resource != record->raw_handle_){
			return;
		}

		switch(event.type){
		case audio_event_type::resource_loaded:
			record->backend_handle_ = event.backend_handle;
			record->metadata_ = event.backend_metadata;
			record->last_error_ = audio_resource_error::none;
			record->state_ = audio_resource_state::loaded;
			break;
		case audio_event_type::resource_failed:
			record->last_error_ = event.resource_error == audio_resource_error::none ?
				audio_resource_error::resource_failed :
				event.resource_error;
			record->state_ = audio_resource_state::failed;
			resources_.erase(event.resource);
			record->raw_handle_ = {};
			record->backend_handle_ = {};
			record->active_uses_ = 0;
			break;
		case audio_event_type::resource_unloaded:
			record->last_error_ = audio_resource_error::none;
			record->state_ = audio_resource_state::unloaded;
			resources_.erase(event.resource);
			record->raw_handle_ = {};
			record->backend_handle_ = {};
			record->active_uses_ = 0;
			break;
		case audio_event_type::playback_failed:
			if(record->active_uses_ != 0){
				--record->active_uses_;
			}
			break;
		case audio_event_type::playback_started:
			++record->active_uses_;
			break;
		case audio_event_type::playback_stopped:
			if(record->active_uses_ != 0){
				--record->active_uses_;
			}
			break;
		default:
			break;
		}
	}

	[[nodiscard]] bool is_resident_unlocked_(const audio_asset_record& record) const noexcept{
		return record.raw_handle_ &&
			(record.state_ == audio_resource_state::queued ||
				record.state_ == audio_resource_state::loading ||
				record.state_ == audio_resource_state::loaded);
	}

	[[nodiscard]] resident_summary resident_summary_() const noexcept{
		resident_summary summary{};
		for(const auto& record : assets_ | std::views::values){
			if(is_resident_unlocked_(*record)){
				++summary.count;
				if(std::numeric_limits<std::uint64_t>::max() - summary.bytes < record->reserved_bytes()){
					summary.bytes = std::numeric_limits<std::uint64_t>::max();
				}else{
					summary.bytes += record->reserved_bytes();
				}
			}
		}
		return summary;
	}

	[[nodiscard]] audio_asset_record* find_capacity_candidate_() noexcept{
		for(auto& record : assets_ | std::views::values){
			if(is_resident_unlocked_(*record) &&
				!record->protected_resource_ &&
				record->active_uses_ == 0 &&
				record->ref_count() == 0){
				return record.get();
			}
		}
		for(auto& record : assets_ | std::views::values){
			if(is_resident_unlocked_(*record) &&
				!record->protected_resource_ &&
				record->active_uses_ == 0){
				return record.get();
			}
		}
		return nullptr;
	}

	void release_unused_(){
		for(auto& record : assets_ | std::views::values){
			if(is_resident_unlocked_(*record) &&
				!record->protected_resource_ &&
				record->active_uses_ == 0 &&
				record->ref_count() == 0){
				(void)release_backend_(*record, false);
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
			(void)release_backend_(*candidate, false);
			summary = resident_summary_();
		}
	}

	void collect_retired_entries_() noexcept{
		std::vector<audio_asset_id> candidates{};
		for(const auto& [id, record] : assets_){
			if(!record->aliases_.empty() || is_resident_unlocked_(*record) || !record->droppable()){
				continue;
			}
			candidates.push_back(id);
		}
		for(const auto id : candidates){
			const auto iter = assets_.find(id);
			if(iter != assets_.end() &&
				iter->second->aliases_.empty() &&
				!is_resident_unlocked_(*iter->second) &&
				iter->second->check_droppable_and_retire()){
				assets_.erase(iter);
			}
		}
	}

	void clear_audio_unlocked_() noexcept{
		aliases_.clear();
		resources_.clear();
		for(auto& record : assets_ | std::views::values){
			record->aliases_.clear();
			record->protected_resource_ = false;
			record->active_uses_ = 0;
			(void)release_backend_(*record, true);
		}
		collect_retired_entries_();
	}

	friend class audio_asset_record;
};

export using audio_resource_index = audio_resource_manager;

export class audio_engine{
	audio_system system_;
	audio_resource_manager resources_;

public:
	explicit audio_engine(std::unique_ptr<audio_driver_backend> driver = make_null_audio_driver())
		: system_(std::move(driver)),
		  resources_(system_.loader(), system_.player()){
	}

	[[nodiscard]] audio_loader& loader() noexcept{
		return system_.loader();
	}

	[[nodiscard]] audio_player& player() noexcept{
		return system_.player();
	}

	[[nodiscard]] audio_resource_manager& resources() noexcept{
		return resources_;
	}

	[[nodiscard]] audio_system& system() noexcept{
		return system_;
	}

	void update(){
		resources_.maintain();
	}

	void shutdown() noexcept{
		resources_.clear_audio();
		system_.shutdown();
	}
};

audio_resource_manager& audio_asset_record::owner_checked_() const noexcept{
	return *owner_;
}

bool audio_asset_record::is_protected() const noexcept{
	std::scoped_lock lock{owner_checked_().mutex_};
	return protected_resource_;
}

void audio_asset_record::set_protected(const bool value) noexcept{
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	protected_resource_ = value;
	if(!value){
		owner.enforce_limits_();
	}
}

std::string audio_asset_record::last_error() const{
	std::scoped_lock lock{owner_checked_().mutex_};
	return error_message_(last_error_);
}

audio_asset_record::audio_asset_record(
	audio_resource_manager& owner,
	audio_asset_id id,
	load_desc desc,
	audio_resource_options options)
	: owner_(std::addressof(owner)),
	  asset_id_(id),
	  desc_(std::move(desc)),
	  metadata_(make_audio_resource_metadata(desc_)),
	  default_load_priority_(options.load_priority),
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
	case audio_resource_error::runtime_unavailable:
		return "audio runtime is unavailable";
	case audio_resource_error::load_not_accepted:
		return "audio load command was not accepted";
	case audio_resource_error::resource_failed:
		return "audio resource failed";
	case audio_resource_error::unloaded:
		return "audio resource unloaded";
	}
	return "audio resource failed";
}

audio_raw_resource_handle audio_asset_record::load_now(const audio_load_priority priority){
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	return owner.request_load_(*this, true, priority);
}

audio_play_token audio_asset_record::prepare_play(audio_prepare_options options){
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	return owner.prepare_play_unlocked_(*this, options);
}

bool audio_asset_record::play_detached(play_settings settings, const audio_load_priority priority){
	auto& owner = owner_checked_();
	auto* player = owner.try_player();
	if(player == nullptr){
		return false;
	}
	return player->play_detached(prepare_play({.priority = priority}), std::move(settings));
}

bool audio_asset_record::play_detached(
	const audio_channel& channel,
	play_settings settings,
	const audio_load_priority priority){
	auto& owner = owner_checked_();
	auto* player = owner.try_player();
	if(player == nullptr){
		return false;
	}
	return player->play_detached(channel, prepare_play({.priority = priority}), std::move(settings));
}

playback_control_handle audio_asset_record::play_controlled(
	play_settings settings,
	playback_control_options options,
	const audio_load_priority priority){
	auto& owner = owner_checked_();
	auto* player = owner.try_player();
	if(player == nullptr){
		return {};
	}
	return player->play_controlled(prepare_play({.priority = priority}), std::move(settings), options);
}

playback_control_handle audio_asset_record::play_controlled(
	const audio_channel& channel,
	play_settings settings,
	playback_control_options options,
	const audio_load_priority priority){
	auto& owner = owner_checked_();
	auto* player = owner.try_player();
	if(player == nullptr){
		return {};
	}
	return player->play_controlled(channel, prepare_play({.priority = priority}), std::move(settings), options);
}

void audio_asset_record::unload() noexcept{
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	(void)owner.release_backend_(*this, false);
}

void audio_asset_record::reload(const audio_load_priority priority){
	auto& owner = owner_checked_();
	std::scoped_lock lock{owner.mutex_};
	if(protected_resource_){
		return;
	}
	(void)owner.release_backend_(*this, false);
	last_error_ = audio_resource_error::none;
	state_ = audio_resource_state::unloaded;
	(void)owner.request_load_(*this, true, priority);
}

}
