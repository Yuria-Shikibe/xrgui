export module mo_yanxi.audio.resources;

import std;
export import mo_yanxi.audio;

namespace mo_yanxi::audio{

export enum class audio_resource_state : std::uint8_t{
	unloaded,
	loading,
	loaded,
	failed,
};

export enum class audio_load_policy : std::uint8_t{
	active,
	lazy,
};

export struct audio_resource_options{
	audio_load_policy load_policy{audio_load_policy::active};
	bool protected_resource{};
	std::uint64_t reserved_bytes{};
};

export struct audio_resource_limits{
	std::size_t max_loaded_resources{std::numeric_limits<std::size_t>::max()};
	std::uint64_t max_reserved_bytes{std::numeric_limits<std::uint64_t>::max()};
};

export class audio_resource_manager;
export class audio_asset;

export struct audio_asset_deleter{
	void operator()(audio_asset* asset) const noexcept;
};

export using audio_asset_ptr = mo_yanxi::referenced_ptr<audio_asset, audio_asset_deleter>;

export class audio_asset final : public mo_yanxi::referenced_object_atomic{
	friend class audio_resource_manager;

	mutable std::recursive_mutex mutex_{};
	std::string id_{};
	load_desc desc_{};
	resource_handle handle_{};
	backend_resource_token backend_handle_{};
	audio_resource_metadata backend_metadata_{};
	audio_system* system_{};
	audio_resource_state state_{audio_resource_state::unloaded};
	audio_load_policy load_policy_{audio_load_policy::active};
	bool protected_resource_{};
	bool registered_{true};
	std::uint64_t reserved_bytes_{};
	std::uint64_t last_used_{};
	std::string last_error_{};
	audio_resource_manager* owner_{};

public:
	[[nodiscard]] audio_asset(
		std::string id,
		load_desc desc,
		audio_system* system,
		audio_resource_options options = {});

	~audio_asset();

	audio_asset(const audio_asset&) = delete;
	audio_asset(audio_asset&&) = delete;
	audio_asset& operator=(const audio_asset&) = delete;
	audio_asset& operator=(audio_asset&&) = delete;

	[[nodiscard]] std::string_view id() const noexcept{
		return id_;
	}

	[[nodiscard]] const load_desc& load_description() const noexcept{
		return desc_;
	}

	[[nodiscard]] resource_handle handle() const noexcept{
		std::scoped_lock lock{mutex_};
		return handle_;
	}

	[[nodiscard]] backend_resource_token backend_handle() const noexcept{
		std::scoped_lock lock{mutex_};
		return backend_handle_;
	}

	[[nodiscard]] audio_resource_metadata backend_metadata() const noexcept{
		std::scoped_lock lock{mutex_};
		return backend_metadata_;
	}

	[[nodiscard]] audio_resource_state state() const noexcept{
		std::scoped_lock lock{mutex_};
		return state_;
	}

	[[nodiscard]] bool loaded() const noexcept{
		std::scoped_lock lock{mutex_};
		return state_ == audio_resource_state::loaded;
	}

	[[nodiscard]] bool resident() const noexcept{
		std::scoped_lock lock{mutex_};
		return handle_ && (state_ == audio_resource_state::loading || state_ == audio_resource_state::loaded);
	}

	[[nodiscard]] audio_load_policy load_policy() const noexcept{
		return load_policy_;
	}

	[[nodiscard]] bool is_protected() const noexcept{
		std::scoped_lock lock{mutex_};
		return protected_resource_;
	}

	void set_protected(const bool value) noexcept{
		std::scoped_lock lock{mutex_};
		protected_resource_ = value;
	}

	[[nodiscard]] bool registered() const noexcept{
		std::scoped_lock lock{mutex_};
		return registered_;
	}

	[[nodiscard]] std::uint64_t reserved_bytes() const noexcept{
		return reserved_bytes_;
	}

	[[nodiscard]] std::uint64_t last_used() const noexcept{
		std::scoped_lock lock{mutex_};
		return last_used_;
	}


	[[nodiscard]] std::size_t external_ref_count() const noexcept{
		const auto manager_refs = owner_ == nullptr ? std::size_t{} : std::size_t{1};
		const auto total = ref_count();
		return total > manager_refs ? total - manager_refs : std::size_t{};
	}

	[[nodiscard]] std::string last_error() const{
		std::scoped_lock lock{mutex_};
		return last_error_;
	}

	[[nodiscard]] resource_handle load_now();
	[[nodiscard]] bool play_detached(play_settings settings = {});
	[[nodiscard]] bool play_detached(const audio_channel& channel, play_settings settings = {});
	[[nodiscard]] playback_control_handle play_controlled(
		play_settings settings = {},
		playback_control_options options = {});
	[[nodiscard]] playback_control_handle play_controlled(
		const audio_channel& channel,
		play_settings settings = {},
		playback_control_options options = {});
	void unload() noexcept;
	void reload();

	void apply_event(const audio_event& event);

private:
	[[nodiscard]] static std::uint64_t estimate_reserved_bytes_(const load_desc& desc) noexcept;
	[[nodiscard]] resource_handle request_load_(bool retry_failed);
	bool release_backend_(bool force) noexcept;
	void touch_() noexcept;
};


class audio_resource_manager{
	mutable std::recursive_mutex mutex_{};
	audio_system* system_{};
	audio_resource_limits limits_{};
	std::uint64_t access_clock_{};

	std::unordered_map<std::string, audio_asset*> aliases_{};
	std::unordered_map<resource_handle, audio_asset*, resource_handle_hash> resources_{};
	std::vector<audio_asset_ptr> retained_{};

public:
	[[nodiscard]] audio_resource_manager() = default;

	~audio_resource_manager(){
		clear_audio();
	}

	audio_resource_manager(const audio_resource_manager&) = delete;
	audio_resource_manager(audio_resource_manager&&) = delete;
	audio_resource_manager& operator=(const audio_resource_manager&) = delete;
	audio_resource_manager& operator=(audio_resource_manager&&) = delete;

	void set_system(audio_system* system) noexcept{
		std::scoped_lock lock{mutex_};
		if(system_ == system){
			return;
		}
		for(auto& resource : retained_){
			if(resource){
				static_cast<void>(resource->release_backend_(true));
			}
		}
		system_ = system;
		for(auto& resource : retained_){
			if(resource){
				resource->system_ = system_;
			}
		}
	}

	void set_system(audio_system& system) noexcept{
		set_system(std::addressof(system));
	}

	void clear_system() noexcept{
		set_system(nullptr);
	}

	[[nodiscard]] audio_system* system() const noexcept{
		std::scoped_lock lock{mutex_};
		return system_;
	}

	void set_limits(audio_resource_limits limits){
		std::scoped_lock lock{mutex_};
		limits_ = limits;
		enforce_limits();
	}

	[[nodiscard]] audio_resource_limits limits() const noexcept{
		std::scoped_lock lock{mutex_};
		return limits_;
	}

	void set_reserved_space_limit(const std::uint64_t bytes){
		std::scoped_lock lock{mutex_};
		limits_.max_reserved_bytes = bytes;
		enforce_limits();
	}

	[[nodiscard]] audio_asset_ptr register_audio(
		std::string id,
		load_desc desc,
		audio_resource_options options = {}){
		std::scoped_lock lock{mutex_};
		if(id.empty()){
			throw std::invalid_argument{"audio resource id must not be empty"};
		}
		if(aliases_.contains(id)){
			throw std::invalid_argument{"audio resource id already exists"};
		}

		audio_asset_ptr resource{new audio_asset{
			id,
			std::move(desc),
			system_,
			options
		}};
		resource->owner_ = this;
		retained_.push_back(resource);
		aliases_.emplace(std::string{resource->id()}, resource.get());
		touch_(*resource);

		if(options.load_policy == audio_load_policy::active){
			static_cast<void>(resource->load_now());
		}
		enforce_limits();
		return resource;
	}

	[[nodiscard]] audio_asset_ptr register_audio_lazy(
		std::string id,
		load_desc desc,
		audio_resource_options options = {}){
		options.load_policy = audio_load_policy::lazy;
		return register_audio(std::move(id), std::move(desc), options);
	}

	[[nodiscard]] audio_asset_ptr register_audio_from_file(
		std::string id,
		std::filesystem::path path,
		audio_resource_options options = {},
		bool stream = false,
		bool preload = true){
		return register_audio(
			std::move(id),
			load_desc::from_file(std::move(path), stream, preload),
			options);
	}

	[[nodiscard]] audio_asset_ptr register_audio_from_memory(
		std::string id,
		std::vector<std::byte> bytes,
		std::string debug_name = {},
		audio_resource_options options = {},
		bool stream = false){
		return register_audio(
			std::move(id),
			load_desc::from_memory(std::move(bytes), std::move(debug_name), stream),
			options);
	}

	[[nodiscard]] audio_asset_ptr find_audio(std::string_view id) const{
		std::scoped_lock lock{mutex_};
		if(const auto iter = aliases_.find(std::string{id}); iter != aliases_.end()){
			return audio_asset_ptr{iter->second};
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

	bool protect_audio(std::string_view id, const bool protected_resource = true) noexcept{
		std::scoped_lock lock{mutex_};
		if(auto resource = find_audio(id)){
			resource->set_protected(protected_resource);
			if(!protected_resource){
				enforce_limits();
			}
			return true;
		}
		return false;
	}

	void clear_audio() noexcept{
		std::scoped_lock lock{mutex_};
		aliases_.clear();
		resources_.clear();
		for(auto& resource : retained_){
			if(resource){
				resource->registered_ = false;
				resource->release_backend_(true);
				resource->system_ = nullptr;
				resource->owner_ = nullptr;
			}
		}
		retained_.clear();
	}

	void consume_audio_event(const audio_event& event){
		std::scoped_lock lock{mutex_};
		if(!event.resource){
			return;
		}

		const auto iter = resources_.find(event.resource);
		if(iter == resources_.end()){
			return;
		}

		audio_asset* resource = iter->second;
		if(resource != nullptr){
			resource->apply_event(event);
			if(event.type == audio_event_type::resource_failed ||
				event.type == audio_event_type::resource_unloaded){
				resources_.erase(iter);
			}
		}else{
			resources_.erase(iter);
		}
		enforce_limits();
	}

	void maintain(){
		std::scoped_lock lock{mutex_};
		release_unused();
		enforce_limits();
		collect_retired_entries_();
	}

	void release_unused(){
		std::scoped_lock lock{mutex_};
		for(auto& resource : retained_){
			if(resource && resource->resident() && !resource->is_protected() && resource->external_ref_count() == 0){
				resource->release_backend_(false);
			}
		}
	}

	void enforce_limits(){
		std::scoped_lock lock{mutex_};
		auto summary = resident_summary_();
		while(summary.count > limits_.max_loaded_resources ||
			summary.bytes > limits_.max_reserved_bytes){
			auto* candidate = find_capacity_candidate_();
			if(candidate == nullptr){
				break;
			}
			candidate->release_backend_(false);
			summary = resident_summary_();
		}
	}

private:
	struct resident_summary{
		std::size_t count{};
		std::uint64_t bytes{};
	};

	void track_handle_(audio_asset& resource){
		std::scoped_lock lock{mutex_};
		if(resource.handle_){
			resources_[resource.handle_] = std::addressof(resource);
		}
	}

	void untrack_handle_(const resource_handle handle) noexcept{
		std::scoped_lock lock{mutex_};
		if(handle){
			resources_.erase(handle);
		}
	}

	void touch_(audio_asset& resource) noexcept{
		std::scoped_lock lock{mutex_};
		resource.last_used_ = ++access_clock_;
	}

	[[nodiscard]] resident_summary resident_summary_() const noexcept{
		std::scoped_lock lock{mutex_};
		resident_summary summary{};
		for(const auto& resource : retained_){
			if(resource && resource->resident()){
				++summary.count;
				if(std::numeric_limits<std::uint64_t>::max() - summary.bytes < resource->reserved_bytes()){
					summary.bytes = std::numeric_limits<std::uint64_t>::max();
				}else{
					summary.bytes += resource->reserved_bytes();
				}
			}
		}
		return summary;
	}

	[[nodiscard]] audio_asset* find_capacity_candidate_() const noexcept{
		std::scoped_lock lock{mutex_};
		audio_asset* best{};
		for(const auto& resource : retained_){
			if(!resource || !resource->resident() || resource->is_protected()){
				continue;
			}
			if(best == nullptr){
				best = resource.get();
				continue;
			}

			const auto lhs_refs = resource->external_ref_count();
			const auto rhs_refs = best->external_ref_count();
			if(lhs_refs == 0 && rhs_refs != 0){
				best = resource.get();
			}else if(lhs_refs == rhs_refs && resource->last_used() < best->last_used()){
				best = resource.get();
			}
		}
		return best;
	}

	void collect_retired_entries_() noexcept{
		std::scoped_lock lock{mutex_};
		std::erase_if(retained_, [](const audio_asset_ptr& resource){
			return !resource ||
				(!resource->registered() && !resource->resident() && resource->external_ref_count() == 0);
		});
	}

	friend class audio_asset;
};

export using audio_resource_index = audio_resource_manager;

audio_asset::audio_asset(
	std::string id,
	load_desc desc,
	audio_system* system,
	audio_resource_options options)
	: id_(std::move(id)),
	  desc_(std::move(desc)),
	  system_(system),
	  load_policy_(options.load_policy),
	  protected_resource_(options.protected_resource),
	  reserved_bytes_(options.reserved_bytes != 0 ? options.reserved_bytes : estimate_reserved_bytes_(desc_)){
}

audio_asset::~audio_asset(){
	release_backend_(true);
}

std::uint64_t audio_asset::estimate_reserved_bytes_(const load_desc& desc) noexcept{
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

resource_handle audio_asset::load_now(){
	std::scoped_lock lock{mutex_};
	return request_load_(true);
}

bool audio_asset::play_detached(play_settings settings){
	std::scoped_lock lock{mutex_};
	if(system_ == nullptr){
		return false;
	}
	return play_detached(system_->default_channel(), std::move(settings));
}

bool audio_asset::play_detached(const audio_channel& channel, play_settings settings){
	std::scoped_lock lock{mutex_};
	touch_();
	if(state_ == audio_resource_state::failed){
		return false;
	}

	const auto resource = request_load_(false);
	if(!resource){
		return false;
	}
	return channel.play_detached(resource, std::move(settings));
}

playback_control_handle audio_asset::play_controlled(
	play_settings settings,
	playback_control_options options){
	std::scoped_lock lock{mutex_};
	if(system_ == nullptr){
		return {};
	}
	return play_controlled(system_->default_channel(), std::move(settings), options);
}

playback_control_handle audio_asset::play_controlled(
	const audio_channel& channel,
	play_settings settings,
	playback_control_options options){
	std::scoped_lock lock{mutex_};
	touch_();
	if(state_ == audio_resource_state::failed){
		return {};
	}

	const auto resource = request_load_(false);
	if(!resource){
		return {};
	}
	return channel.play_controlled(resource, std::move(settings), options);
}

void audio_asset::unload() noexcept{
	std::scoped_lock lock{mutex_};
	static_cast<void>(release_backend_(false));
}

void audio_asset::reload(){
	std::scoped_lock lock{mutex_};
	if(protected_resource_){
		return;
	}
	static_cast<void>(release_backend_(false));
	state_ = audio_resource_state::unloaded;
	last_error_.clear();
	static_cast<void>(request_load_(true));
}

void audio_asset::apply_event(const audio_event& event){
	std::scoped_lock lock{mutex_};
	if(event.resource != handle_){
		return;
	}

	switch(event.type){
	case audio_event_type::resource_loaded:
		state_ = audio_resource_state::loaded;
		backend_handle_ = event.backend_handle;
		backend_metadata_ = event.backend_metadata;
		last_error_.clear();
		break;
	case audio_event_type::resource_failed:
		state_ = audio_resource_state::failed;
		handle_ = {};
		backend_handle_ = {};
		backend_metadata_ = {};
		last_error_ = "audio resource failed";
		break;
	case audio_event_type::resource_unloaded:
		state_ = audio_resource_state::unloaded;
		handle_ = {};
		backend_handle_ = {};
		backend_metadata_ = {};
		break;
	default:
		break;
	}
}

resource_handle audio_asset::request_load_(const bool retry_failed){
	std::scoped_lock lock{mutex_};
	if(handle_ && (state_ == audio_resource_state::loading || state_ == audio_resource_state::loaded)){
		return handle_;
	}
	if(state_ == audio_resource_state::failed && !retry_failed){
		return {};
	}
	if(system_ == nullptr || !system_->valid()){
		state_ = audio_resource_state::failed;
		last_error_ = "audio system is unavailable";
		return {};
	}

	handle_ = system_->load(desc_);
	if(!handle_){
		state_ = audio_resource_state::failed;
		last_error_ = "audio load command was not accepted";
		return {};
	}

	backend_handle_ = {};
	backend_metadata_ = {};
	state_ = audio_resource_state::loading;
	last_error_.clear();
	if(owner_ != nullptr){
		owner_->track_handle_(*this);
	}
	return handle_;
}

bool audio_asset::release_backend_(const bool force) noexcept{
	std::scoped_lock lock{mutex_};
	if(!handle_){
		if(state_ != audio_resource_state::failed){
			state_ = audio_resource_state::unloaded;
		}
		return false;
	}
	if(protected_resource_ && !force){
		return false;
	}

	const auto old_handle = std::exchange(handle_, {});
	backend_handle_ = {};
	backend_metadata_ = {};
	if(owner_ != nullptr){
		owner_->untrack_handle_(old_handle);
	}
	if(system_ != nullptr){
		system_->unload(old_handle);
	}
	state_ = audio_resource_state::unloaded;
	return true;
}

void audio_asset::touch_() noexcept{
	std::scoped_lock lock{mutex_};
	if(owner_ != nullptr){
		owner_->touch_(*this);
	}
}

void audio_asset_deleter::operator()(audio_asset* asset) const noexcept{
	delete asset;
}

}
