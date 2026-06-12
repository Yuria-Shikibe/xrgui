module;

#include <gtl/phmap.hpp>

export module mo_yanxi.audio;

import std;
export import mo_yanxi.referenced_ptr;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.concurrent.mpsc_double_buffer;

namespace mo_yanxi::audio{

export enum class bus : std::uint8_t{
	master,
	ui,
	music,
	sfx,
	app,
};

export struct channel_id{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const channel_id&) const noexcept = default;
};

export [[nodiscard]] constexpr channel_id channel_id_from_bus(const bus value) noexcept{
	return channel_id{static_cast<std::uint64_t>(std::to_underlying(value)) + 1u};
}

export struct audio_raw_resource_handle{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const audio_raw_resource_handle&) const noexcept = default;
};

export using resource_handle = audio_raw_resource_handle;

export struct playback_id{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const playback_id&) const noexcept = default;
};

export struct backend_resource_token{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const backend_resource_token&) const noexcept = default;
};

export using backend_resource_handle = void*;

export using audio_load_priority = std::uint32_t;
export constexpr audio_load_priority lazy_audio_load_priority{};
export constexpr audio_load_priority default_audio_load_priority{1};
export using audio_thread_id = std::uint64_t;

export [[nodiscard]] audio_thread_id current_audio_thread_id() noexcept;

export struct resource_handle_hash{
	[[nodiscard]] std::size_t operator()(const audio_raw_resource_handle handle) const noexcept{
		return std::hash<std::uint64_t>{}(handle.value);
	}
};

export struct channel_id_hash{
	[[nodiscard]] std::size_t operator()(const channel_id id) const noexcept{
		return std::hash<std::uint64_t>{}(id.value);
	}
};

export struct playback_id_hash{
	[[nodiscard]] std::size_t operator()(const playback_id handle) const noexcept{
		return std::hash<std::uint64_t>{}(handle.value);
	}
};

export enum class load_source_kind : std::uint8_t{
	file,
	memory,
};

export using load_source = std::variant<std::filesystem::path, std::vector<std::byte>>;

export struct load_desc{
	load_source source{std::filesystem::path{}};
	std::string debug_name{};
	bool stream{};
	bool preload{true};

	[[nodiscard]] load_source_kind source_kind() const noexcept{
		return std::holds_alternative<std::vector<std::byte>>(source) ?
			load_source_kind::memory :
			load_source_kind::file;
	}

	[[nodiscard]] std::size_t source_size_hint() const noexcept{
		if(const auto* bytes = memory()){
			return bytes->size();
		}
		return 0u;
	}

	[[nodiscard]] const std::filesystem::path* path() const noexcept{
		return std::get_if<std::filesystem::path>(&source);
	}

	[[nodiscard]] std::filesystem::path* path() noexcept{
		return std::get_if<std::filesystem::path>(&source);
	}

	[[nodiscard]] const std::vector<std::byte>* memory() const noexcept{
		return std::get_if<std::vector<std::byte>>(&source);
	}

	[[nodiscard]] std::vector<std::byte>* memory() noexcept{
		return std::get_if<std::vector<std::byte>>(&source);
	}

	[[nodiscard]] static load_desc from_file(
		std::filesystem::path path,
		bool stream = false,
		bool preload = true){
		return load_desc{
			.source = std::move(path),
			.stream = stream,
			.preload = preload
		};
	}

	[[nodiscard]] static load_desc from_memory(
		std::vector<std::byte> bytes,
		std::string debug_name = {},
		bool stream = false){
		return load_desc{
			.source = std::move(bytes),
			.debug_name = std::move(debug_name),
			.stream = stream,
			.preload = true
		};
	}

	[[nodiscard]] static load_desc from_memory(
		std::span<const std::byte> bytes,
		std::string debug_name = {},
		bool stream = false){
		return from_memory(
			std::vector<std::byte>{bytes.begin(), bytes.end()},
			std::move(debug_name),
			stream);
	}
};

export struct audio_resource_metadata{
	std::uint64_t reserved_bytes{};
	std::string debug_name{};
	load_source_kind source{load_source_kind::file};
	bool stream{};
	bool preload{true};
};

export [[nodiscard]] inline audio_resource_metadata make_audio_resource_metadata(const load_desc& desc){
	return audio_resource_metadata{
		.reserved_bytes = static_cast<std::uint64_t>(desc.source_size_hint()),
		.debug_name = desc.debug_name,
		.source = desc.source_kind(),
		.stream = desc.stream,
		.preload = desc.preload
	};
}

export enum class audio_resource_state : std::uint8_t{
	unloaded,
	queued,
	loading,
	loaded,
	failed,
};

export enum class audio_resource_error : std::uint8_t{
	none,
	runtime_unavailable,
	load_not_accepted,
	resource_failed,
	unloaded,
};

export struct backend_resource_result{
	backend_resource_handle handle{};
	backend_resource_token token{};
	audio_resource_metadata metadata{};

	[[nodiscard]] explicit operator bool() const noexcept{
		return handle != nullptr;
	}
};

export struct audio_backend_resource_view{
	backend_resource_handle handle{};
	backend_resource_token token{};
	audio_resource_metadata metadata{};

	[[nodiscard]] explicit operator bool() const noexcept{
		return handle != nullptr;
	}
};

export struct play_settings{
	float volume{1.f};
	float pan{};
	float pitch{1.f};
	bool loop{};
};

export struct voice_params{
	static constexpr std::uint8_t volume_field{1u << 0u};
	static constexpr std::uint8_t pan_field{1u << 1u};
	static constexpr std::uint8_t pitch_field{1u << 2u};
	static constexpr std::uint8_t loop_field{1u << 3u};
	static constexpr std::uint8_t all_fields{volume_field | pan_field | pitch_field | loop_field};

	float volume{1.f};
	float pan{};
	float pitch{1.f};
	bool loop{};
	std::uint8_t valid_fields{};

	[[nodiscard]] constexpr voice_params() noexcept = default;

	[[nodiscard]] constexpr voice_params(
		const float volume_,
		const float pan_,
		const float pitch_,
		const bool loop_) noexcept
		: volume(volume_),
		  pan(pan_),
		  pitch(pitch_),
		  loop(loop_),
		  valid_fields(all_fields){
	}

	[[nodiscard]] static constexpr voice_params all(
		const float volume = 1.f,
		const float pan = 0.f,
		const float pitch = 1.f,
		const bool loop = false) noexcept{
		return voice_params{volume, pan, pitch, loop};
	}

	[[nodiscard]] static constexpr voice_params with_volume(const float value) noexcept{
		auto params = voice_params{};
		params.set_volume(value);
		return params;
	}

	[[nodiscard]] static constexpr voice_params with_pan(const float value) noexcept{
		auto params = voice_params{};
		params.set_pan(value);
		return params;
	}

	[[nodiscard]] static constexpr voice_params with_pitch(const float value) noexcept{
		auto params = voice_params{};
		params.set_pitch(value);
		return params;
	}

	[[nodiscard]] static constexpr voice_params with_loop(const bool value) noexcept{
		auto params = voice_params{};
		params.set_loop(value);
		return params;
	}

	constexpr voice_params& set_volume(const float value) noexcept{
		volume = value;
		valid_fields = static_cast<std::uint8_t>(valid_fields | volume_field);
		return *this;
	}

	constexpr voice_params& set_pan(const float value) noexcept{
		pan = value;
		valid_fields = static_cast<std::uint8_t>(valid_fields | pan_field);
		return *this;
	}

	constexpr voice_params& set_pitch(const float value) noexcept{
		pitch = value;
		valid_fields = static_cast<std::uint8_t>(valid_fields | pitch_field);
		return *this;
	}

	constexpr voice_params& set_loop(const bool value) noexcept{
		loop = value;
		valid_fields = static_cast<std::uint8_t>(valid_fields | loop_field);
		return *this;
	}

	constexpr void clear_volume() noexcept{
		valid_fields = static_cast<std::uint8_t>(valid_fields & ~volume_field);
	}

	constexpr void clear_pan() noexcept{
		valid_fields = static_cast<std::uint8_t>(valid_fields & ~pan_field);
	}

	constexpr void clear_pitch() noexcept{
		valid_fields = static_cast<std::uint8_t>(valid_fields & ~pitch_field);
	}

	constexpr void clear_loop() noexcept{
		valid_fields = static_cast<std::uint8_t>(valid_fields & ~loop_field);
	}

	[[nodiscard]] constexpr bool has_volume() const noexcept{
		return (valid_fields & volume_field) != 0;
	}

	[[nodiscard]] constexpr bool has_pan() const noexcept{
		return (valid_fields & pan_field) != 0;
	}

	[[nodiscard]] constexpr bool has_pitch() const noexcept{
		return (valid_fields & pitch_field) != 0;
	}

	[[nodiscard]] constexpr bool has_loop() const noexcept{
		return (valid_fields & loop_field) != 0;
	}

	[[nodiscard]] constexpr bool empty() const noexcept{
		return valid_fields == 0;
	}
};

export enum class playback_release_policy : std::uint8_t{
	stop_on_release,
	detach_on_release,
};

export struct playback_control_options{
	playback_release_policy release_policy{playback_release_policy::stop_on_release};
};

export struct device_config{
	bool enabled{true};
	bool require_device{};
	std::uint32_t sample_rate{};
	std::uint32_t channels{};
};

export enum class audio_event_type : std::uint8_t{
	resource_loaded,
	resource_failed,
	resource_unloaded,
	playback_started,
	playback_failed,
	playback_stopped,
	backend_error,
	budget_exceeded,
};

export struct audio_event{
	audio_event_type type{};
	audio_raw_resource_handle resource{};
	backend_resource_token backend_handle{};
	audio_resource_metadata backend_metadata{};
	audio_resource_error resource_error{audio_resource_error::none};
	playback_id playback{};
};

export struct audio_backend_event{
	audio_event_type type{};
	playback_id playback{};
};

export class audio_driver_backend{
public:
	virtual ~audio_driver_backend() = default;

	virtual backend_resource_result load_resource(load_desc desc) = 0;
	virtual void release_resource(backend_resource_handle handle) noexcept = 0;
	virtual void register_channel(channel_id channel) = 0;
	virtual void start_voice(
		audio_backend_resource_view resource,
		channel_id channel,
		playback_id playback,
		const play_settings& settings,
		bool controlled) = 0;
	virtual void stop(playback_id playback) noexcept = 0;
	virtual void detach(playback_id playback) noexcept = 0;
	virtual void pause(playback_id playback) noexcept = 0;
	virtual void resume(playback_id playback) noexcept = 0;
	virtual void set_playback_params(playback_id playback, const voice_params& params) noexcept = 0;
	virtual void set_channel_volume(channel_id channel, float volume) noexcept = 0;
	virtual void update(std::vector<audio_backend_event>& out_events) = 0;
	virtual void shutdown() noexcept = 0;
};

export class null_audio_driver final : public audio_driver_backend{
	std::uint64_t next_token_{1};

public:
	backend_resource_result load_resource(load_desc desc) override{
		auto* token = new backend_resource_token{next_token_++};
		return backend_resource_result{
			.handle = token,
			.token = *token,
			.metadata = make_audio_resource_metadata(desc)
		};
	}

	void release_resource(backend_resource_handle handle) noexcept override{
		delete static_cast<backend_resource_token*>(handle);
	}

	void register_channel(channel_id) override{
	}

	void start_voice(audio_backend_resource_view, channel_id, playback_id, const play_settings&, bool) override{
	}

	void stop(playback_id) noexcept override{
	}

	void detach(playback_id) noexcept override{
	}

	void pause(playback_id) noexcept override{
	}

	void resume(playback_id) noexcept override{
	}

	void set_playback_params(playback_id, const voice_params&) noexcept override{
	}

	void set_channel_volume(channel_id, float) noexcept override{
	}

	void update(std::vector<audio_backend_event>&) override{
	}

	void shutdown() noexcept override{
	}
};

export [[nodiscard]] inline std::unique_ptr<audio_driver_backend> make_null_audio_driver(){
	return std::make_unique<null_audio_driver>();
}

export class audio_runtime;
export class audio_loader;
export class audio_player;
export class playback_control;

export struct audio_play_token{
	friend class audio_runtime;
	friend class audio_loader;
	friend class audio_player;

private:
	audio_runtime* runtime_{};
	audio_raw_resource_handle resource_{};
	std::uint64_t lease_{};

	[[nodiscard]] audio_play_token(
		audio_runtime& runtime,
		audio_raw_resource_handle resource,
		std::uint64_t lease) noexcept
		: runtime_(std::addressof(runtime)),
		  resource_(resource),
		  lease_(lease){
	}

public:
	[[nodiscard]] audio_play_token() = default;
	~audio_play_token();

	audio_play_token(const audio_play_token&) = delete;
	audio_play_token& operator=(const audio_play_token&) = delete;

	[[nodiscard]] audio_play_token(audio_play_token&& other) noexcept
		: runtime_(std::exchange(other.runtime_, nullptr)),
		  resource_(std::exchange(other.resource_, {})),
		  lease_(std::exchange(other.lease_, {})){
	}

	audio_play_token& operator=(audio_play_token&& other) noexcept{
		if(this != std::addressof(other)){
			reset();
			runtime_ = std::exchange(other.runtime_, nullptr);
			resource_ = std::exchange(other.resource_, {});
			lease_ = std::exchange(other.lease_, {});
		}
		return *this;
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return runtime_ != nullptr && static_cast<bool>(resource_) && lease_ != 0;
	}

	[[nodiscard]] audio_raw_resource_handle resource() const noexcept{
		return resource_;
	}

	void reset() noexcept;
};

struct thread_bound_referenced_object{
	audio_thread_id owner_thread_{current_audio_thread_id()};
	std::size_t reference_count_{};

	[[nodiscard]] bool is_owner_thread() const noexcept{
		return current_audio_thread_id() == owner_thread_;
	}

	void require_owner_thread(const char* operation) const{
		if(!is_owner_thread()){
			throw std::runtime_error{std::format("{} called from a non-owner audio thread", operation)};
		}
	}

	[[nodiscard]] std::size_t ref_count() const noexcept{
		return reference_count_;
	}

	void ref_incr() noexcept{
		if(is_owner_thread()){
			++reference_count_;
		}
	}

	bool ref_decr() noexcept{
		if(!is_owner_thread() || reference_count_ == 0){
			return false;
		}
		--reference_count_;
		return reference_count_ == 0;
	}
};

export struct playback_control_deleter{
	void operator()(playback_control* control) const noexcept;
};

export using playback_control_handle = mo_yanxi::referenced_ptr<playback_control, playback_control_deleter>;

export class playback_control final : public thread_bound_referenced_object{
	template <typename T, typename D>
	friend struct mo_yanxi::referenced_ptr;
	friend class audio_player;

	audio_player* player_{};
	playback_id playback_{};
	playback_release_policy release_policy_{playback_release_policy::stop_on_release};

	[[nodiscard]] playback_control(
		audio_player& player,
		playback_id playback,
		playback_control_options options) noexcept;

	void invalidate_() noexcept{
		playback_ = {};
	}

	void release_(playback_release_policy policy) noexcept;

public:
	~playback_control();

	playback_control(const playback_control&) = delete;
	playback_control(playback_control&&) = delete;
	playback_control& operator=(const playback_control&) = delete;
	playback_control& operator=(playback_control&&) = delete;

	[[nodiscard]] explicit operator bool() const noexcept{
		return valid();
	}

	[[nodiscard]] bool valid() const noexcept{
		return static_cast<bool>(playback_);
	}

	[[nodiscard]] playback_id id() const noexcept{
		return playback_;
	}

	[[nodiscard]] playback_release_policy release_policy() const noexcept{
		return release_policy_;
	}

	void set_release_policy(playback_release_policy policy);
	void stop();
	void detach();
	void pause() const;
	void resume() const;
	void set_params(voice_params params) const;
	void set_volume(float value){
		set_params(voice_params::with_volume(value));
	}

	void set_pan(float value){
		set_params(voice_params::with_pan(value));
	}

	void set_pitch(float value){
		set_params(voice_params::with_pitch(value));
	}

	void set_loop(bool value){
		set_params(voice_params::with_loop(value));
	}
};

namespace cmd{

struct load_resource{
	audio_raw_resource_handle resource{};
	load_desc desc{};
	audio_load_priority priority{default_audio_load_priority};
	std::uint64_t sequence{};
};

struct unload_resource{
	audio_raw_resource_handle resource{};
};

struct register_channel{
	channel_id channel{};
};

struct start_playback{
	audio_raw_resource_handle resource{};
	std::uint64_t lease{};
	channel_id channel{};
	playback_id playback{};
	play_settings settings{};
	bool controlled{};
};

struct stop_playback{
	playback_id playback{};
};

struct pause_playback{
	playback_id playback{};
};

struct resume_playback{
	playback_id playback{};
};

struct set_playback_params{
	playback_id playback{};
	voice_params params{};
};

struct release_playback{
	playback_id playback{};
	playback_release_policy policy{playback_release_policy::stop_on_release};
};

struct set_channel_volume{
	channel_id channel{};
	float volume{1.f};
};

struct release_lease{
	audio_raw_resource_handle resource{};
	std::uint64_t lease{};
};

struct shutdown_audio{};

using command = std::variant<
	load_resource,
	unload_resource,
	register_channel,
	start_playback,
	stop_playback,
	pause_playback,
	resume_playback,
	set_playback_params,
	release_playback,
	set_channel_volume,
	release_lease,
	shutdown_audio>;

}

export class audio_runtime{
	friend class audio_loader;
	friend class audio_player;
	friend struct audio_play_token;

	struct raw_resource_entry{
		audio_raw_resource_handle handle{};
		backend_resource_handle backend_handle{};
		backend_resource_token backend_token{};
		audio_resource_metadata metadata{};
		audio_resource_state state{audio_resource_state::unloaded};
		audio_resource_error last_error{audio_resource_error::none};
		std::uint32_t active_lease_count{};
	};

	struct active_playback_entry{
		audio_raw_resource_handle resource{};
		std::uint64_t lease{};
		bool controlled{};
	};

	struct channel_slot{
		std::atomic<std::uint32_t> channel_value{};
		audio_thread_id owner_thread{};
	};

	struct channel_registration{
		channel_slot* slot{};
		bool inserted{};
	};

	static constexpr std::uint32_t empty_channel_slot_value_{};
	static constexpr std::uint32_t pending_channel_slot_value_{std::numeric_limits<std::uint32_t>::max()};

	ccur::mpsc_queue<cmd::command> commands_{};
	ccur::mpsc_double_buffer<audio_event> events_{};

	std::unique_ptr<audio_driver_backend> driver_{};
	void* worker_handle_{};
	std::atomic<audio_thread_id> worker_thread_{};
	std::atomic_bool accepting_{true};
	std::atomic<std::uint64_t> next_resource_{1};
	std::atomic<std::uint64_t> next_playback_{1};
	std::atomic<std::uint64_t> next_lease_{1};

	std::array<channel_slot, 8> channels_{};

	std::recursive_mutex state_mutex_{};
	std::unordered_map<audio_raw_resource_handle, raw_resource_entry, resource_handle_hash> resources_{};
	std::unordered_set<audio_raw_resource_handle, resource_handle_hash> pending_load_resources_{};
	std::vector<cmd::load_resource> pending_loads_{};
	std::unordered_map<audio_raw_resource_handle, std::vector<cmd::start_playback>, resource_handle_hash> pending_playbacks_{};
	std::unordered_map<playback_id, active_playback_entry, playback_id_hash> active_playbacks_{};
	std::uint64_t next_load_sequence_{1};

public:
	static constexpr std::size_t default_channel_capacity{8};

	explicit audio_runtime(std::unique_ptr<audio_driver_backend> driver = make_null_audio_driver());
	~audio_runtime();

	audio_runtime(const audio_runtime&) = delete;
	audio_runtime(audio_runtime&&) = delete;
	audio_runtime& operator=(const audio_runtime&) = delete;
	audio_runtime& operator=(audio_runtime&&) = delete;

	[[nodiscard]] bool valid() const noexcept{
		return accepting_.load(std::memory_order_acquire);
	}

	void shutdown() noexcept;

	template <typename Fn>
		requires std::invocable<Fn&, const audio_event&>
	void poll_events(Fn&& fn);

private:
	struct pending_load_less{
		[[nodiscard]] bool operator()(const cmd::load_resource& lhs, const cmd::load_resource& rhs) const noexcept{
			if(lhs.priority != rhs.priority){
				return lhs.priority < rhs.priority;
			}
			return lhs.sequence > rhs.sequence;
		}
	};

	void start();
	static unsigned long __stdcall worker_entry_(void* context) noexcept;
	[[nodiscard]] audio_raw_resource_handle allocate_resource() noexcept{
		return audio_raw_resource_handle{next_resource_.fetch_add(1, std::memory_order_relaxed)};
	}

	[[nodiscard]] playback_id allocate_playback() noexcept{
		return playback_id{next_playback_.fetch_add(1, std::memory_order_relaxed)};
	}

	[[nodiscard]] std::uint64_t allocate_lease() noexcept{
		return next_lease_.fetch_add(1, std::memory_order_relaxed);
	}

	[[nodiscard]] const channel_slot* find_channel_slot_(channel_id channel) const noexcept;
	[[nodiscard]] channel_registration reserve_channel_slot_(channel_id channel, audio_thread_id owner_thread);
	void commit_channel_slot_(channel_slot& slot, channel_id channel) noexcept{
		slot.channel_value.store(static_cast<std::uint32_t>(channel.value), std::memory_order_release);
	}

	void rollback_channel_slot_(channel_slot& slot) noexcept{
		slot.owner_thread = {};
		slot.channel_value.store(empty_channel_slot_value_, std::memory_order_release);
	}

	[[nodiscard]] bool channel_registered_to_current_thread_(channel_id channel) const noexcept;
	[[nodiscard]] bool post(cmd::command command) noexcept;
	void push_event(audio_event event);
	void push_backend_events(std::vector<audio_backend_event>& new_events);
	void pop_events(std::vector<audio_event>& out);
	void run();
	void process(cmd::command command);
	void enqueue_load_(cmd::load_resource command);
	[[nodiscard]] bool process_pending_loads_();
	void process_load_(cmd::load_resource command);
	[[nodiscard]] bool is_pending_load_(audio_raw_resource_handle resource) const noexcept;
	void flush_pending_playbacks_(audio_raw_resource_handle resource);
	void fail_pending_playbacks_(audio_raw_resource_handle resource);
	void start_playback_(cmd::start_playback command);
	void release_resource_backend_(raw_resource_entry& entry) noexcept;
	void release_lease_(audio_raw_resource_handle resource, std::uint64_t lease) noexcept;
};

template <typename Fn>
	requires std::invocable<Fn&, const audio_event&>
void audio_runtime::poll_events(Fn&& fn){
	std::vector<audio_event> pending{};
	pop_events(pending);
	for(const auto& event : pending){
		std::invoke(fn, event);
	}
}

export class audio_loader{
	audio_runtime* runtime_{};

public:
	[[nodiscard]] audio_loader() = default;

	[[nodiscard]] explicit audio_loader(audio_runtime& runtime) noexcept
		: runtime_(std::addressof(runtime)){
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return runtime_ != nullptr;
	}

	[[nodiscard]] audio_raw_resource_handle create(
		load_desc desc,
		audio_load_priority priority = default_audio_load_priority);
	[[nodiscard]] audio_raw_resource_handle reserve() noexcept;
	[[nodiscard]] bool load(
		audio_raw_resource_handle resource,
		load_desc desc,
		audio_load_priority priority = default_audio_load_priority);
	void unload(audio_raw_resource_handle resource) noexcept;
	[[nodiscard]] audio_play_token acquire_play_token(audio_raw_resource_handle resource) noexcept;

	void pop_events(std::vector<audio_event>& out);
};

export class audio_channel{
	friend class audio_player;

	audio_player* player_{};
	channel_id id_{};
	audio_thread_id owner_thread_{};

	[[nodiscard]] audio_channel(
		audio_player& player,
		channel_id id,
		audio_thread_id owner_thread) noexcept
		: player_(std::addressof(player)),
		  id_(id),
		  owner_thread_(owner_thread){
	}

public:
	[[nodiscard]] audio_channel() = default;

	[[nodiscard]] explicit operator bool() const noexcept{
		return player_ != nullptr && static_cast<bool>(id_);
	}

	[[nodiscard]] channel_id id() const noexcept{
		return id_;
	}

	[[nodiscard]] bool is_owner_thread() const noexcept{
		return current_audio_thread_id() == owner_thread_;
	}

	[[nodiscard]] bool play_detached(audio_play_token token, play_settings settings = {}) const;
	[[nodiscard]] playback_control_handle play_controlled(
		audio_play_token token,
		play_settings settings = {},
		playback_control_options options = {}) const;
	void set_volume(float volume) const;
};

export class audio_player{
	friend class playback_control;
	friend class audio_channel;

	audio_runtime* runtime_{};

public:
	[[nodiscard]] audio_player() = default;

	[[nodiscard]] explicit audio_player(audio_runtime& runtime) noexcept
		: runtime_(std::addressof(runtime)){
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return runtime_ != nullptr;
	}

	[[nodiscard]] audio_channel register_channel(channel_id channel);
	[[nodiscard]] std::optional<audio_channel> find_channel(channel_id channel) noexcept;
	[[nodiscard]] audio_channel default_channel();
	[[nodiscard]] bool play_detached(
		audio_channel channel,
		audio_play_token token,
		play_settings settings = {});
	[[nodiscard]] playback_control_handle play_controlled(
		audio_channel channel,
		audio_play_token token,
		play_settings settings = {},
		playback_control_options options = {});
	[[nodiscard]] bool play_detached(audio_play_token token, play_settings settings = {});

	[[nodiscard]] playback_control_handle play_controlled(
		audio_play_token token,
		play_settings settings = {},
		playback_control_options options = {});

	bool set_bus_volume(bus target_bus, float volume) noexcept;

private:
	void post_stop_playback_(playback_id playback) noexcept;
	void post_detach_playback_(playback_id playback) noexcept;
	void post_pause_playback_(playback_id playback) noexcept;
	void post_resume_playback_(playback_id playback) noexcept;
	void post_set_playback_params_(playback_id playback, voice_params params) noexcept;
	void post_release_playback_(playback_id playback, playback_release_policy policy) noexcept;
	void post_set_channel_volume_(channel_id channel, float volume) noexcept;
};

export class audio_system{
	audio_runtime runtime_;
	audio_loader loader_;
	audio_player player_;

public:
	static constexpr std::size_t default_channel_capacity{audio_runtime::default_channel_capacity};

	explicit audio_system(std::unique_ptr<audio_driver_backend> driver = make_null_audio_driver())
		: runtime_(std::move(driver)),
		  loader_(runtime_),
		  player_(runtime_){
	}

	[[nodiscard]] bool valid() const noexcept{
		return runtime_.valid();
	}

	[[nodiscard]] audio_loader& loader() noexcept{
		return loader_;
	}

	[[nodiscard]] const audio_loader& loader() const noexcept{
		return loader_;
	}

	[[nodiscard]] audio_player& player() noexcept{
		return player_;
	}

	[[nodiscard]] const audio_player& player() const noexcept{
		return player_;
	}

	[[nodiscard]] audio_raw_resource_handle load(
		load_desc desc,
		audio_load_priority priority = default_audio_load_priority){
		return loader_.create(std::move(desc), priority);
	}

	void unload(audio_raw_resource_handle resource) noexcept{
		loader_.unload(resource);
	}

	[[nodiscard]] audio_channel register_channel(channel_id channel){
		return player_.register_channel(channel);
	}

	[[nodiscard]] std::optional<audio_channel> find_channel(channel_id channel) noexcept{
		return player_.find_channel(channel);
	}

	[[nodiscard]] audio_channel get_channel(channel_id channel){
		auto found = find_channel(channel);
		if(!found){
			throw std::runtime_error{"audio channel is not registered or is bound to another thread"};
		}
		return *found;
	}

	[[nodiscard]] audio_channel default_channel(){
		return player_.default_channel();
	}

	[[nodiscard]] bool play_detached(audio_raw_resource_handle resource, play_settings settings = {}){
		return player_.play_detached(loader_.acquire_play_token(resource), std::move(settings));
	}

	[[nodiscard]] playback_control_handle play_controlled(
		audio_raw_resource_handle resource,
		play_settings settings = {},
		playback_control_options options = {}){
		return player_.play_controlled(loader_.acquire_play_token(resource), std::move(settings), options);
	}

	void set_bus_volume(bus target_bus, float volume){
		if(!player_.set_bus_volume(target_bus, volume)){
			throw std::runtime_error{"audio bus channel is not registered"};
		}
	}

	void shutdown() noexcept{
		runtime_.shutdown();
	}

	template <typename Fn>
		requires std::invocable<Fn&, const audio_event&>
	void poll_events(Fn&& fn){
		runtime_.poll_events(std::forward<Fn>(fn));
	}
};

}

extern "C"{
__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId() noexcept;
__declspec(dllimport) void* __stdcall CreateThread(
	void* thread_attributes,
	unsigned long long stack_size,
	unsigned long(__stdcall* start_address)(void*),
	void* parameter,
	unsigned long creation_flags,
	unsigned long* thread_id) noexcept;
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(
	void* handle,
	unsigned long milliseconds) noexcept;
__declspec(dllimport) int __stdcall CloseHandle(void* handle) noexcept;
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds) noexcept;
__declspec(dllimport) int __stdcall SwitchToThread() noexcept;
}

namespace mo_yanxi::audio{

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

template <typename... Ts>
struct audio_overload : Ts...{
	using Ts::operator()...;
};

template <typename... Ts>
audio_overload(Ts...) -> audio_overload<Ts...>;

audio_thread_id current_audio_thread_id() noexcept{
	return static_cast<audio_thread_id>(GetCurrentThreadId());
}

audio_play_token::~audio_play_token(){
	reset();
}

void audio_play_token::reset() noexcept{
	const auto runtime = std::exchange(runtime_, nullptr);
	const auto resource = std::exchange(resource_, {});
	const auto lease = std::exchange(lease_, {});
	if(runtime != nullptr && resource && lease != 0){
		runtime->release_lease_(resource, lease);
	}
}

playback_control::playback_control(
	audio_player& player,
	const playback_id playback,
	const playback_control_options options) noexcept
	: player_(std::addressof(player)),
	  playback_(playback),
	  release_policy_(options.release_policy){
}

playback_control::~playback_control(){
	release_(release_policy());
}

void playback_control::set_release_policy(const playback_release_policy policy){
	require_owner_thread("playback_control::set_release_policy");
	release_policy_ = policy;
}

void playback_control::release_(const playback_release_policy policy) noexcept{
	if(!is_owner_thread()){
		return;
	}
	const playback_id playback = std::exchange(playback_, {});
	if(!playback){
		return;
	}
	if(player_ != nullptr){
		player_->post_release_playback_(playback, policy);
	}
}

void playback_control::stop(){
	require_owner_thread("playback_control::stop");
	release_(playback_release_policy::stop_on_release);
}

void playback_control::detach(){
	require_owner_thread("playback_control::detach");
	release_(playback_release_policy::detach_on_release);
}

void playback_control::pause() const{
	require_owner_thread("playback_control::pause");
	const auto playback = id();
	if(!playback){
		return;
	}
	if(player_ != nullptr){
		player_->post_pause_playback_(playback);
	}
}

void playback_control::resume() const{
	require_owner_thread("playback_control::resume");
	const auto playback = id();
	if(!playback){
		return;
	}
	if(player_ != nullptr){
		player_->post_resume_playback_(playback);
	}
}

void playback_control::set_params(voice_params params) const{
	require_owner_thread("playback_control::set_params");
	const auto playback = id();
	if(!playback || params.empty()){
		return;
	}
	if(player_ != nullptr){
		player_->post_set_playback_params_(playback, params);
	}
}

void playback_control_deleter::operator()(playback_control* control) const noexcept{
	delete control;
}

unsigned long __stdcall audio_runtime::worker_entry_(void* context) noexcept{
	auto* runtime = static_cast<audio_runtime*>(context);
	runtime->worker_thread_.store(current_audio_thread_id(), std::memory_order_release);
	try{
		runtime->run();
	}catch(...){
		runtime->push_event(audio_event{
			.type = audio_event_type::backend_error
		});
	}
	runtime->worker_thread_.store({}, std::memory_order_release);
	return 0;
}

void audio_runtime::start(){
	unsigned long thread_id{};
	worker_handle_ = CreateThread(
		nullptr,
		0,
		&audio_runtime::worker_entry_,
		this,
		0,
		&thread_id);
	if(worker_handle_ == nullptr){
		accepting_.store(false, std::memory_order_release);
		throw std::runtime_error{"audio worker thread could not be created"};
	}
	worker_thread_.store(static_cast<audio_thread_id>(thread_id), std::memory_order_release);
}

const audio_runtime::channel_slot* audio_runtime::find_channel_slot_(const channel_id channel) const noexcept{
	if(!channel || channel.value >= pending_channel_slot_value_){
		return nullptr;
	}
	const auto channel_value = static_cast<std::uint32_t>(channel.value);
	for(const auto& slot : channels_){
		if(slot.channel_value.load(std::memory_order_acquire) == channel_value){
			return std::addressof(slot);
		}
	}
	return nullptr;
}

audio_runtime::channel_registration audio_runtime::reserve_channel_slot_(
	const channel_id channel,
	const audio_thread_id owner_thread){
	if(!channel){
		throw std::invalid_argument{"audio channel id must not be empty"};
	}
	if(channel.value >= pending_channel_slot_value_){
		throw std::invalid_argument{"audio channel id is reserved for the channel registry"};
	}
	const auto channel_value = static_cast<std::uint32_t>(channel.value);

	for(;;){
		bool saw_pending_slot = false;
		for(auto& slot : channels_){
			const auto current = slot.channel_value.load(std::memory_order_acquire);
			if(current == channel_value){
				if(slot.owner_thread != owner_thread){
					throw std::runtime_error{"audio channel is already bound to another thread"};
				}
				return channel_registration{
					.slot = std::addressof(slot),
					.inserted = false
				};
			}

			if(current == pending_channel_slot_value_){
				saw_pending_slot = true;
				continue;
			}
			if(current != empty_channel_slot_value_){
				continue;
			}

			auto expected = empty_channel_slot_value_;
			if(slot.channel_value.compare_exchange_strong(
				   expected,
				   pending_channel_slot_value_,
				   std::memory_order_acq_rel,
				   std::memory_order_acquire)){
				slot.owner_thread = owner_thread;
				return channel_registration{
					.slot = std::addressof(slot),
					.inserted = true
				};
			}
			if(expected == pending_channel_slot_value_){
				saw_pending_slot = true;
			}
		}

		if(!saw_pending_slot){
			throw std::runtime_error{"audio channel registry capacity exceeded"};
		}
		(void)SwitchToThread();
	}
}

bool audio_runtime::channel_registered_to_current_thread_(const channel_id channel) const noexcept{
	const auto* slot = find_channel_slot_(channel);
	return slot != nullptr && slot->owner_thread == current_audio_thread_id();
}

bool audio_runtime::post(cmd::command command) noexcept{
	if(!accepting_.load(std::memory_order_acquire)){
		return false;
	}

	try{
		commands_.push(std::move(command));
		commands_.notify();
		return true;
	}catch(...){
		return false;
	}
}

void audio_runtime::push_event(audio_event event){
	events_.push(std::move(event));
}

void audio_runtime::push_backend_events(std::vector<audio_backend_event>& new_events){
	for(auto& backend_event : new_events){
		audio_event event{
			.type = backend_event.type,
			.playback = backend_event.playback
		};
		if(backend_event.playback){
			std::scoped_lock lock{state_mutex_};
			if(auto node = active_playbacks_.extract(backend_event.playback)){
				event.resource = node.mapped().resource;
				release_lease_(node.mapped().resource, node.mapped().lease);
			}
		}
		events_.push(std::move(event));
	}
	new_events.clear();
}

void audio_runtime::pop_events(std::vector<audio_event>& out){
	out.clear();
	if(auto fetched = events_.fetch()){
		out.reserve(fetched->size());
		for(auto& event : *fetched){
			out.push_back(std::move(event));
		}
	}
}

void audio_runtime::run(){
	for(;;){
		bool processed_command = false;
		while(auto command = commands_.try_consume()){
			processed_command = true;
			if(std::holds_alternative<cmd::shutdown_audio>(*command)){
				accepting_.store(false, std::memory_order_release);
				break;
			}
			process(std::move(*command));
		}
		if(accepting_.load(std::memory_order_acquire)){
			processed_command = process_pending_loads_() || processed_command;
		}

		std::vector<audio_backend_event> driver_events{};
		try{
			driver_->update(driver_events);
		}catch(...){
			push_event(audio_event{
				.type = audio_event_type::backend_error
			});
		}
		push_backend_events(driver_events);

		if(!accepting_.load(std::memory_order_acquire) && commands_.empty()){
			break;
		}
		if(!processed_command){
			Sleep(16);
		}
	}

	events_.clear();
	{
		std::scoped_lock lock{state_mutex_};
		active_playbacks_.clear();
		pending_playbacks_.clear();
		pending_loads_.clear();
		pending_load_resources_.clear();
		for(auto& entry : resources_ | std::views::values){
			release_resource_backend_(entry);
		}
		resources_.clear();
	}
	if(driver_){
		driver_->shutdown();
	}
}

void audio_runtime::process(cmd::command command){
	std::visit(audio_overload{
		[this](cmd::load_resource command){
			enqueue_load_(std::move(command));
		},
		[this](const cmd::unload_resource& command){
			if(!command.resource){
				return;
			}

			std::vector<playback_id> stopped_playbacks{};
			std::scoped_lock lock{state_mutex_};
			if(pending_load_resources_.erase(command.resource) != 0){
				fail_pending_playbacks_(command.resource);
			}

			if(auto iter = resources_.find(command.resource); iter != resources_.end()){
				for(auto playback_iter = active_playbacks_.begin(); playback_iter != active_playbacks_.end();){
					if(playback_iter->second.resource == command.resource){
						stopped_playbacks.push_back(playback_iter->first);
						release_lease_(playback_iter->second.resource, playback_iter->second.lease);
						playback_iter = active_playbacks_.erase(playback_iter);
					}else{
						++playback_iter;
					}
				}
				for(const auto playback : stopped_playbacks){
					driver_->stop(playback);
				}
				release_resource_backend_(iter->second);
				iter->second.state = audio_resource_state::unloaded;
				iter->second.last_error = audio_resource_error::unloaded;
				push_event(audio_event{
					.type = audio_event_type::resource_unloaded,
					.resource = command.resource
				});
				resources_.erase(iter);
			}else{
				push_event(audio_event{
					.type = audio_event_type::resource_unloaded,
					.resource = command.resource
				});
			}
		},
		[this](const cmd::register_channel& command){
			if(!command.channel){
				return;
			}
			try{
				driver_->register_channel(command.channel);
			}catch(...){
				push_event(audio_event{
					.type = audio_event_type::backend_error
				});
			}
		},
		[this](cmd::start_playback command){
			start_playback_(std::move(command));
		},
		[this](const cmd::stop_playback& command){
			if(command.playback){
				driver_->stop(command.playback);
			}
		},
		[this](const cmd::pause_playback& command){
			if(command.playback){
				driver_->pause(command.playback);
			}
		},
		[this](const cmd::resume_playback& command){
			if(command.playback){
				driver_->resume(command.playback);
			}
		},
		[this](const cmd::set_playback_params& command){
			if(command.playback && !command.params.empty()){
				driver_->set_playback_params(command.playback, command.params);
			}
		},
		[this](const cmd::release_playback& command){
			if(!command.playback){
				return;
			}
			switch(command.policy){
			case playback_release_policy::stop_on_release:
				driver_->stop(command.playback);
				break;
			case playback_release_policy::detach_on_release:
				driver_->detach(command.playback);
				break;
			}
		},
		[this](const cmd::set_channel_volume& command){
			if(command.channel){
				driver_->set_channel_volume(command.channel, command.volume);
			}
		},
		[this](const cmd::release_lease& command){
			release_lease_(command.resource, command.lease);
		},
		[](const cmd::shutdown_audio&) noexcept{
		}
	}, std::move(command));
}

void audio_runtime::enqueue_load_(cmd::load_resource command){
	if(!command.resource || command.priority == lazy_audio_load_priority){
		return;
	}
	command.sequence = next_load_sequence_++;
	std::scoped_lock lock{state_mutex_};
	auto& entry = resources_[command.resource];
	entry.handle = command.resource;
	entry.state = audio_resource_state::queued;
	entry.last_error = audio_resource_error::none;
	pending_load_resources_.insert(command.resource);
	pending_loads_.push_back(std::move(command));
	std::push_heap(pending_loads_.begin(), pending_loads_.end(), pending_load_less{});
}

bool audio_runtime::process_pending_loads_(){
	bool processed{};
	while(true){
		cmd::load_resource command{};
		{
			std::scoped_lock lock{state_mutex_};
			if(pending_loads_.empty()){
				break;
			}
			std::pop_heap(pending_loads_.begin(), pending_loads_.end(), pending_load_less{});
			command = std::move(pending_loads_.back());
			pending_loads_.pop_back();
			if(pending_load_resources_.erase(command.resource) == 0){
				fail_pending_playbacks_(command.resource);
				continue;
			}
		}
		process_load_(std::move(command));
		processed = true;
	}
	return processed;
}

void audio_runtime::process_load_(cmd::load_resource command){
	{
		std::scoped_lock lock{state_mutex_};
		auto& entry = resources_[command.resource];
		entry.handle = command.resource;
		entry.state = audio_resource_state::loading;
	}
	try{
		auto loaded_resource = driver_->load_resource(std::move(command.desc));
		if(!loaded_resource){
			std::scoped_lock lock{state_mutex_};
			auto& entry = resources_[command.resource];
			entry.state = audio_resource_state::failed;
			entry.last_error = audio_resource_error::resource_failed;
			push_event(audio_event{
				.type = audio_event_type::resource_failed,
				.resource = command.resource,
				.resource_error = audio_resource_error::resource_failed
			});
			fail_pending_playbacks_(command.resource);
			return;
		}

		std::vector<cmd::start_playback> playbacks{};
		{
			std::scoped_lock lock{state_mutex_};
			auto& entry = resources_[command.resource];
			release_resource_backend_(entry);
			entry.backend_handle = std::exchange(loaded_resource.handle, nullptr);
			entry.backend_token = loaded_resource.token;
			entry.metadata = std::move(loaded_resource.metadata);
			entry.state = audio_resource_state::loaded;
			entry.last_error = audio_resource_error::none;
			push_event(audio_event{
				.type = audio_event_type::resource_loaded,
				.resource = command.resource,
				.backend_handle = entry.backend_token,
				.backend_metadata = entry.metadata
			});
			if(auto node = pending_playbacks_.extract(command.resource)){
				playbacks = std::move(node.mapped());
			}
		}
		for(auto& playback : playbacks){
			start_playback_(std::move(playback));
		}
	}catch(...){
		std::scoped_lock lock{state_mutex_};
		auto& entry = resources_[command.resource];
		entry.state = audio_resource_state::failed;
		entry.last_error = audio_resource_error::resource_failed;
		push_event(audio_event{
			.type = audio_event_type::resource_failed,
			.resource = command.resource,
			.resource_error = audio_resource_error::resource_failed
		});
		fail_pending_playbacks_(command.resource);
	}
}

bool audio_runtime::is_pending_load_(const audio_raw_resource_handle resource) const noexcept{
	return pending_load_resources_.contains(resource);
}

void audio_runtime::flush_pending_playbacks_(const audio_raw_resource_handle resource){
	if(auto node = pending_playbacks_.extract(resource)){
		for(auto& playback : node.mapped()){
			start_playback_(std::move(playback));
		}
	}
}

void audio_runtime::fail_pending_playbacks_(const audio_raw_resource_handle resource){
	if(auto node = pending_playbacks_.extract(resource)){
		for(const auto& playback : node.mapped()){
			release_lease_(playback.resource, playback.lease);
			push_event(audio_event{
				.type = audio_event_type::playback_failed,
				.resource = playback.resource,
				.playback = playback.playback
			});
		}
	}
}

void audio_runtime::start_playback_(cmd::start_playback command){
	if(!command.resource || !command.channel || !command.playback){
		release_lease_(command.resource, command.lease);
		return;
	}

	audio_backend_resource_view view{};
	{
		std::scoped_lock lock{state_mutex_};
		const auto resource = resources_.find(command.resource);
		if(resource == resources_.end() || resource->second.state != audio_resource_state::loaded){
			if(is_pending_load_(command.resource)){
				pending_playbacks_[command.resource].push_back(std::move(command));
				return;
			}
			release_lease_(command.resource, command.lease);
			push_event(audio_event{
				.type = audio_event_type::playback_failed,
				.resource = command.resource,
				.playback = command.playback
			});
			return;
		}
		view = audio_backend_resource_view{
			.handle = resource->second.backend_handle,
			.token = resource->second.backend_token,
			.metadata = resource->second.metadata
		};
	}

	try{
		driver_->start_voice(
			view,
			command.channel,
			command.playback,
			command.settings,
			command.controlled);
		{
			std::scoped_lock lock{state_mutex_};
			active_playbacks_[command.playback] = active_playback_entry{
				.resource = command.resource,
				.lease = command.lease,
				.controlled = command.controlled
			};
		}
		push_event(audio_event{
			.type = audio_event_type::playback_started,
			.resource = command.resource,
			.playback = command.playback
		});
	}catch(...){
		release_lease_(command.resource, command.lease);
		push_event(audio_event{
			.type = audio_event_type::playback_failed,
			.resource = command.resource,
			.playback = command.playback
		});
	}
}

void audio_runtime::release_resource_backend_(raw_resource_entry& entry) noexcept{
	const auto handle = std::exchange(entry.backend_handle, nullptr);
	entry.backend_token = {};
	if(handle != nullptr && driver_ != nullptr){
		driver_->release_resource(handle);
	}
}

void audio_runtime::release_lease_(
	const audio_raw_resource_handle resource,
	const std::uint64_t lease) noexcept{
	if(!resource || lease == 0){
		return;
	}
	const auto worker_thread = worker_thread_.load(std::memory_order_acquire);
	if(worker_thread != 0 && worker_thread != current_audio_thread_id()){
		if(post(cmd::release_lease{
			   .resource = resource,
			   .lease = lease
		   })){
			return;
		}
	}
	if(worker_thread != 0 && worker_thread == current_audio_thread_id()){
		std::scoped_lock lock{state_mutex_};
		if(auto iter = resources_.find(resource); iter != resources_.end() && iter->second.active_lease_count != 0){
			--iter->second.active_lease_count;
		}
	}
}

audio_runtime::audio_runtime(std::unique_ptr<audio_driver_backend> driver)
	: driver_(std::move(driver)){
	if(driver_ == nullptr){
		driver_ = make_null_audio_driver();
	}
	start();
}

audio_runtime::~audio_runtime(){
	shutdown();
}

void audio_runtime::shutdown() noexcept{
	const bool was_accepting = accepting_.exchange(false, std::memory_order_acq_rel);
	if(was_accepting){
		try{
			commands_.push(cmd::shutdown_audio{});
			commands_.notify();
		}catch(...){
		}
	}

	if(worker_handle_ != nullptr){
		commands_.notify();
		if(worker_thread_.load(std::memory_order_acquire) != current_audio_thread_id()){
			(void)WaitForSingleObject(worker_handle_, 0xFFFFFFFFul);
			commands_.clear();
			(void)CloseHandle(worker_handle_);
			worker_handle_ = nullptr;
		}
	}
	events_.clear();
}

audio_raw_resource_handle audio_loader::reserve() noexcept{
	if(runtime_ == nullptr || !runtime_->valid()){
		return {};
	}
	return runtime_->allocate_resource();
}

audio_raw_resource_handle audio_loader::create(load_desc desc, const audio_load_priority priority){
	const auto resource = reserve();
	if(!resource){
		return {};
	}
	if(priority != lazy_audio_load_priority){
		if(!load(resource, std::move(desc), priority)){
			return {};
		}
	}else{
		if(runtime_ != nullptr){
			std::scoped_lock lock{runtime_->state_mutex_};
			auto& entry = runtime_->resources_[resource];
			entry.handle = resource;
			entry.metadata = make_audio_resource_metadata(desc);
			entry.state = audio_resource_state::unloaded;
		}
	}
	return resource;
}

bool audio_loader::load(
	const audio_raw_resource_handle resource,
	load_desc desc,
	const audio_load_priority priority){
	if(runtime_ == nullptr || !runtime_->valid() || !resource || priority == lazy_audio_load_priority){
		return false;
	}
	return runtime_->post(cmd::load_resource{
		.resource = resource,
		.desc = std::move(desc),
		.priority = priority
	});
}

void audio_loader::unload(const audio_raw_resource_handle resource) noexcept{
	if(runtime_ == nullptr || !resource){
		return;
	}
	(void)runtime_->post(cmd::unload_resource{.resource = resource});
}

void audio_loader::pop_events(std::vector<audio_event>& out){
	if(runtime_ == nullptr){
		out.clear();
		return;
	}
	runtime_->pop_events(out);
}

audio_play_token audio_loader::acquire_play_token(const audio_raw_resource_handle resource) noexcept{
	if(runtime_ == nullptr || !runtime_->valid() || !resource){
		return {};
	}
	const auto lease = runtime_->allocate_lease();
	std::scoped_lock lock{runtime_->state_mutex_};
	if(auto iter = runtime_->resources_.find(resource); iter != runtime_->resources_.end()){
		++iter->second.active_lease_count;
	}else{
		auto& entry = runtime_->resources_[resource];
		entry.handle = resource;
		++entry.active_lease_count;
	}
	return audio_play_token{*runtime_, resource, lease};
}

bool audio_channel::play_detached(audio_play_token token, play_settings settings) const{
	if(player_ == nullptr){
		return false;
	}
	return player_->play_detached(*this, std::move(token), std::move(settings));
}

playback_control_handle audio_channel::play_controlled(
	audio_play_token token,
	play_settings settings,
	playback_control_options options) const{
	if(player_ == nullptr){
		return {};
	}
	return player_->play_controlled(*this, std::move(token), std::move(settings), options);
}

void audio_channel::set_volume(const float volume) const{
	if(player_ == nullptr){
		return;
	}
	if(!is_owner_thread()){
		throw std::runtime_error{"audio_channel::set_volume called from a non-owner audio thread"};
	}
	player_->post_set_channel_volume_(id_, volume);
}

audio_channel audio_player::register_channel(const channel_id channel){
	if(runtime_ == nullptr || !runtime_->valid()){
		throw std::runtime_error{"audio runtime is not accepting channel registrations"};
	}

	const auto owner_thread = current_audio_thread_id();
	auto registration = runtime_->reserve_channel_slot_(channel, owner_thread);
	if(registration.inserted){
		if(!runtime_->post(cmd::register_channel{.channel = channel})){
			runtime_->rollback_channel_slot_(*registration.slot);
			throw std::runtime_error{"audio channel registration command was not accepted"};
		}
		runtime_->commit_channel_slot_(*registration.slot, channel);
	}
	return audio_channel{*this, channel, owner_thread};
}

std::optional<audio_channel> audio_player::find_channel(const channel_id channel) noexcept{
	if(runtime_ == nullptr || !channel || !runtime_->channel_registered_to_current_thread_(channel)){
		return std::nullopt;
	}
	return audio_channel{*this, channel, current_audio_thread_id()};
}

audio_channel audio_player::default_channel(){
	auto found = find_channel(channel_id_from_bus(bus::ui));
	if(!found){
		throw std::runtime_error{"default audio channel is not registered"};
	}
	return *found;
}

bool audio_player::play_detached(
	audio_channel channel,
	audio_play_token token,
	play_settings settings){
	if(runtime_ == nullptr || !runtime_->valid() || !channel || !channel.is_owner_thread() || !token){
		return false;
	}
	const auto playback = runtime_->allocate_playback();
	const auto resource = token.resource_;
	const auto lease = token.lease_;
	token.runtime_ = nullptr;
	token.resource_ = {};
	token.lease_ = {};
	if(!runtime_->post(cmd::start_playback{
		   .resource = resource,
		   .lease = lease,
		   .channel = channel.id(),
		   .playback = playback,
		   .settings = std::move(settings),
		   .controlled = false
	   })){
		runtime_->release_lease_(resource, lease);
		return false;
	}
	return true;
}

bool audio_player::play_detached(audio_play_token token, play_settings settings){
	return play_detached(default_channel(), std::move(token), std::move(settings));
}

playback_control_handle audio_player::play_controlled(
	audio_channel channel,
	audio_play_token token,
	play_settings settings,
	playback_control_options options){
	if(runtime_ == nullptr || !runtime_->valid() || !channel || !channel.is_owner_thread() || !token){
		return {};
	}

	const auto playback = runtime_->allocate_playback();
	playback_control_handle control{};
	try{
		control = playback_control_handle{new playback_control{*this, playback, options}};
	}catch(...){
		return {};
	}

	const auto resource = token.resource_;
	const auto lease = token.lease_;
	token.runtime_ = nullptr;
	token.resource_ = {};
	token.lease_ = {};
	if(runtime_->post(cmd::start_playback{
		   .resource = resource,
		   .lease = lease,
		   .channel = channel.id(),
		   .playback = playback,
		   .settings = std::move(settings),
		   .controlled = true
	   })){
		return control;
	}

	runtime_->release_lease_(resource, lease);
	control->invalidate_();
	return {};
}

playback_control_handle audio_player::play_controlled(
	audio_play_token token,
	play_settings settings,
	playback_control_options options){
	return play_controlled(default_channel(), std::move(token), std::move(settings), options);
}

bool audio_player::set_bus_volume(const bus target_bus, const float volume) noexcept{
	if(runtime_ == nullptr){
		return false;
	}
	const auto channel = channel_id_from_bus(target_bus);
	if(!runtime_->channel_registered_to_current_thread_(channel)){
		return false;
	}
	return runtime_->post(cmd::set_channel_volume{
		.channel = channel,
		.volume = volume
	});
}

void audio_player::post_stop_playback_(const playback_id playback) noexcept{
	if(runtime_ != nullptr && playback){
		(void)runtime_->post(cmd::stop_playback{.playback = playback});
	}
}

void audio_player::post_detach_playback_(const playback_id playback) noexcept{
	if(runtime_ != nullptr && playback){
		(void)runtime_->post(cmd::release_playback{
			.playback = playback,
			.policy = playback_release_policy::detach_on_release
		});
	}
}

void audio_player::post_pause_playback_(const playback_id playback) noexcept{
	if(runtime_ != nullptr && playback){
		(void)runtime_->post(cmd::pause_playback{.playback = playback});
	}
}

void audio_player::post_resume_playback_(const playback_id playback) noexcept{
	if(runtime_ != nullptr && playback){
		(void)runtime_->post(cmd::resume_playback{.playback = playback});
	}
}

void audio_player::post_set_playback_params_(const playback_id playback, voice_params params) noexcept{
	if(runtime_ != nullptr && playback && !params.empty()){
		(void)runtime_->post(cmd::set_playback_params{
			.playback = playback,
			.params = params
		});
	}
}

void audio_player::post_release_playback_(
	const playback_id playback,
	const playback_release_policy policy) noexcept{
	if(runtime_ != nullptr && playback){
		(void)runtime_->post(cmd::release_playback{
			.playback = playback,
			.policy = policy
		});
	}
}

void audio_player::post_set_channel_volume_(const channel_id channel, const float volume) noexcept{
	if(runtime_ != nullptr && channel){
		(void)runtime_->post(cmd::set_channel_volume{
			.channel = channel,
			.volume = volume
		});
	}
}

}
