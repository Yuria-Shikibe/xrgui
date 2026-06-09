export module mo_yanxi.gui.audio_resources;

import std;
export import mo_yanxi.audio;

namespace mo_yanxi::gui{

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
export using audio_resource_ptr = audio_asset_ptr;

export class audio_asset final : public mo_yanxi::referenced_object_atomic{
	friend class audio_resource_manager;

	std::string id_{};
	audio::load_desc desc_{};
	audio::resource_handle handle_{};
	audio::backend_resource_token backend_handle_{};
	audio::audio_resource_metadata backend_metadata_{};
	audio::audio_controller controller_{};
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
		audio::load_desc desc,
		audio::audio_controller controller,
		audio_resource_options options = {});

	~audio_asset();

	audio_asset(const audio_asset&) = delete;
	audio_asset(audio_asset&&) = delete;
	audio_asset& operator=(const audio_asset&) = delete;
	audio_asset& operator=(audio_asset&&) = delete;

	[[nodiscard]] std::string_view id() const noexcept{
		return id_;
	}

	[[nodiscard]] const audio::load_desc& load_description() const noexcept{
		return desc_;
	}

	[[nodiscard]] audio::resource_handle handle() const noexcept{
		return handle_;
	}

	[[nodiscard]] audio::backend_resource_token backend_handle() const noexcept{
		return backend_handle_;
	}

	[[nodiscard]] const audio::audio_resource_metadata& backend_metadata() const noexcept{
		return backend_metadata_;
	}

	[[nodiscard]] audio_resource_state state() const noexcept{
		return state_;
	}

	[[nodiscard]] bool loaded() const noexcept{
		return state_ == audio_resource_state::loaded;
	}

	[[nodiscard]] bool resident() const noexcept{
		return handle_ && (state_ == audio_resource_state::loading || state_ == audio_resource_state::loaded);
	}

	[[nodiscard]] audio_load_policy load_policy() const noexcept{
		return load_policy_;
	}

	[[nodiscard]] bool is_protected() const noexcept{
		return protected_resource_;
	}

	void set_protected(const bool value) noexcept{
		protected_resource_ = value;
	}

	[[nodiscard]] bool registered() const noexcept{
		return registered_;
	}

	[[nodiscard]] std::uint64_t reserved_bytes() const noexcept{
		return reserved_bytes_;
	}

	[[nodiscard]] std::uint64_t last_used() const noexcept{
		return last_used_;
	}

	[[nodiscard]] std::size_t reference_count() const noexcept{
		return referenced_object_atomic::ref_count();
	}

	[[nodiscard]] std::size_t external_ref_count() const noexcept{
		const auto manager_refs = owner_ == nullptr ? std::size_t{} : std::size_t{1};
		const auto total = reference_count();
		return total > manager_refs ? total - manager_refs : std::size_t{};
	}

	[[nodiscard]] std::string_view last_error() const noexcept{
		return last_error_;
	}

	[[nodiscard]] audio::resource_handle load_now();
	[[nodiscard]] audio::voice_handle play(audio::play_desc desc = {});
	void unload() noexcept;
	void reload();

	void apply_event(const audio::audio_event& event);

private:
	[[nodiscard]] static std::uint64_t estimate_reserved_bytes_(const audio::load_desc& desc) noexcept;
	[[nodiscard]] audio::resource_handle request_load_(bool retry_failed);
	bool release_backend_(bool force) noexcept;
	void touch_() noexcept;
};

export class audio_resource_manager{
	audio::audio_controller controller_{};
	audio_resource_limits limits_{};
	std::uint64_t access_clock_{};

	std::unordered_map<std::string, audio_asset*> aliases_{};
	std::unordered_map<audio::resource_handle, audio_asset*, audio::resource_handle_hash> resources_{};
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

	void set_controller(audio::audio_controller controller) noexcept{
		controller_ = std::move(controller);
	}

	[[nodiscard]] audio::audio_controller controller() const noexcept{
		return controller_;
	}

	void set_limits(audio_resource_limits limits){
		limits_ = limits;
		enforce_limits();
	}

	[[nodiscard]] audio_resource_limits limits() const noexcept{
		return limits_;
	}

	void set_reserved_space_limit(const std::uint64_t bytes){
		limits_.max_reserved_bytes = bytes;
		enforce_limits();
	}

	[[nodiscard]] audio_asset_ptr register_audio(
		std::string id,
		audio::load_desc desc,
		audio_resource_options options = {}){
		if(id.empty()){
			throw std::invalid_argument{"audio resource id must not be empty"};
		}
		if(aliases_.contains(id)){
			throw std::invalid_argument{"audio resource id already exists"};
		}

		audio_asset_ptr resource{new audio_asset{
			id,
			std::move(desc),
			controller_,
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
		audio::load_desc desc,
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
			audio::load_desc::from_file(std::move(path), stream, preload),
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
			audio::load_desc::from_memory(std::move(bytes), std::move(debug_name), stream),
			options);
	}

	[[nodiscard]] audio_asset_ptr find_audio(std::string_view id) const{
		if(const auto iter = aliases_.find(std::string{id}); iter != aliases_.end()){
			return audio_asset_ptr{iter->second};
		}
		return {};
	}

	bool unregister_audio(std::string_view id) noexcept{
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
		aliases_.clear();
		resources_.clear();
		for(auto& resource : retained_){
			if(resource){
				resource->registered_ = false;
				resource->release_backend_(true);
				resource->owner_ = nullptr;
			}
		}
		retained_.clear();
	}

	void consume_audio_event(const audio::audio_event& event){
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
			if(event.type == audio::audio_event_type::resource_failed ||
				event.type == audio::audio_event_type::resource_unloaded){
				resources_.erase(iter);
			}
		}else{
			resources_.erase(iter);
		}
		enforce_limits();
	}

	void maintain(){
		release_unused();
		enforce_limits();
		collect_retired_entries_();
	}

	void release_unused(){
		for(auto& resource : retained_){
			if(resource && resource->resident() && !resource->is_protected() && resource->external_ref_count() == 0){
				resource->release_backend_(false);
			}
		}
	}

	void enforce_limits(){
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
		if(resource.handle_){
			resources_[resource.handle_] = std::addressof(resource);
		}
	}

	void untrack_handle_(const audio::resource_handle handle) noexcept{
		if(handle){
			resources_.erase(handle);
		}
	}

	void touch_(audio_asset& resource) noexcept{
		resource.last_used_ = ++access_clock_;
	}

	[[nodiscard]] resident_summary resident_summary_() const noexcept{
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
	audio::load_desc desc,
	audio::audio_controller controller,
	audio_resource_options options)
	: id_(std::move(id)),
	  desc_(std::move(desc)),
	  controller_(std::move(controller)),
	  load_policy_(options.load_policy),
	  protected_resource_(options.protected_resource),
	  reserved_bytes_(options.reserved_bytes != 0 ? options.reserved_bytes : estimate_reserved_bytes_(desc_)){
}

audio_asset::~audio_asset(){
	release_backend_(true);
}

std::uint64_t audio_asset::estimate_reserved_bytes_(const audio::load_desc& desc) noexcept{
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

audio::resource_handle audio_asset::load_now(){
	return request_load_(true);
}

audio::voice_handle audio_asset::play(audio::play_desc desc){
	touch_();
	if(state_ == audio_resource_state::failed){
		return {};
	}

	const auto resource = request_load_(false);
	if(!resource){
		return {};
	}
	return controller_.play(resource, std::move(desc));
}

void audio_asset::unload() noexcept{
	static_cast<void>(release_backend_(false));
}

void audio_asset::reload(){
	if(protected_resource_){
		return;
	}
	static_cast<void>(release_backend_(false));
	state_ = audio_resource_state::unloaded;
	last_error_.clear();
	static_cast<void>(request_load_(true));
}

void audio_asset::apply_event(const audio::audio_event& event){
	if(event.resource != handle_){
		return;
	}

	switch(event.type){
	case audio::audio_event_type::resource_loaded:
		state_ = audio_resource_state::loaded;
		backend_handle_ = event.backend_resource ? event.backend_resource->token() : audio::backend_resource_token{};
		backend_metadata_ = event.backend_resource ? event.backend_resource->metadata() : audio::audio_resource_metadata{};
		last_error_.clear();
		break;
	case audio::audio_event_type::resource_failed:
		state_ = audio_resource_state::failed;
		handle_ = {};
		backend_handle_ = {};
		backend_metadata_ = {};
		last_error_ = event.message;
		break;
	case audio::audio_event_type::resource_unloaded:
		state_ = audio_resource_state::unloaded;
		handle_ = {};
		backend_handle_ = {};
		backend_metadata_ = {};
		break;
	default:
		break;
	}
}

audio::resource_handle audio_asset::request_load_(const bool retry_failed){
	if(handle_ && (state_ == audio_resource_state::loading || state_ == audio_resource_state::loaded)){
		return handle_;
	}
	if(state_ == audio_resource_state::failed && !retry_failed){
		return {};
	}
	if(!controller_.valid()){
		state_ = audio_resource_state::failed;
		last_error_ = "audio controller is unavailable";
		return {};
	}

	handle_ = controller_.load(desc_);
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
	controller_.unload(old_handle);
	state_ = audio_resource_state::unloaded;
	return true;
}

void audio_asset::touch_() noexcept{
	if(owner_ != nullptr){
		owner_->touch_(*this);
	}
}

void audio_asset_deleter::operator()(audio_asset* asset) const noexcept{
	delete asset;
}

}
