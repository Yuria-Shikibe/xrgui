module;

#include <cassert>

#ifdef __RESHARPER__
#include <stdexcept>
#endif


export module mo_yanxi.audio;

import std;
export import mo_yanxi.referenced_ptr;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.concurrent.mpsc_double_buffer;

namespace mo_yanxi::audio{

export enum class channel_role : std::uint8_t{
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

export [[nodiscard]] constexpr channel_id channel_id_from_role(const channel_role value) noexcept{
	return channel_id{static_cast<std::uint64_t>(std::to_underlying(value)) + 1u};
}

export struct resource_handle{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const resource_handle&) const noexcept = default;
	[[nodiscard]] constexpr auto operator<=>(const resource_handle&) const noexcept = default;
};

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

export struct resource_handle_hash{
	[[nodiscard]] std::size_t operator()(const resource_handle handle) const noexcept{
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

export struct backend_resource_result{
	backend_resource_handle handle{};
	backend_resource_token token{};
	audio_resource_metadata metadata{};

	[[nodiscard]] explicit operator bool() const noexcept{
		return handle != nullptr;
	}
};

export class audio_system;
export class audio_channel;
export class audio_resource;
struct audio_control_sink;

export struct audio_resource_deleter{
	void operator()(audio_resource* resource) const noexcept;
};

export using audio_resource_ptr = mo_yanxi::referenced_ptr<audio_resource, audio_resource_deleter>;

export class audio_resource final : public mo_yanxi::referenced_object_atomic{
	audio_system* system_{};
	backend_resource_handle handle_{};

public:
	[[nodiscard]] audio_resource(
		audio_system* system,
		backend_resource_handle handle) noexcept
		: system_(system),
		  handle_(handle){
	}

	~audio_resource();

	audio_resource(const audio_resource&) = delete;
	audio_resource(audio_resource&&) = delete;
	audio_resource& operator=(const audio_resource&) = delete;
	audio_resource& operator=(audio_resource&&) = delete;

	[[nodiscard]] explicit operator bool() const noexcept{
		return handle_ != nullptr;
	}

	[[nodiscard]] bool valid() const noexcept{
		return handle_ != nullptr;
	}

	[[nodiscard]] audio_system* system() const noexcept{
		return system_;
	}

	[[nodiscard]] backend_resource_handle native_handle() const noexcept{
		return handle_;
	}

	[[nodiscard]] std::size_t ref_count() const noexcept{
		return referenced_object_atomic::ref_count();
	}

	void release() noexcept;
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

export class playback_control;

export struct playback_control_deleter{
	void operator()(playback_control* control) const noexcept;
};

export using playback_control_handle = mo_yanxi::referenced_ptr<playback_control, playback_control_deleter>;

export class playback_control final : public mo_yanxi::referenced_object_atomic{
	template <typename T, typename D>
	friend struct mo_yanxi::referenced_ptr;
	friend class audio_system;
	friend class audio_channel;

	std::shared_ptr<audio_control_sink> control_sink_{};
	channel_id channel_{};
	std::atomic<std::uint64_t> playback_{};
	std::atomic<playback_release_policy> release_policy_{playback_release_policy::stop_on_release};

	[[nodiscard]] playback_control(
		std::shared_ptr<audio_control_sink> control_sink,
		channel_id channel,
		playback_id playback,
		playback_control_options options) noexcept;

	void invalidate_() noexcept{
		playback_.store(0, std::memory_order_release);
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
		return static_cast<bool>(id());
	}

	[[nodiscard]] playback_id id() const noexcept{
		return playback_id{playback_.load(std::memory_order_acquire)};
	}

	[[nodiscard]] playback_release_policy release_policy() const noexcept{
		return release_policy_.load(std::memory_order_acquire);
	}

	void set_release_policy(playback_release_policy policy);
	void stop();
	void detach();
	void pause() const;
	void resume() const;
	void set_params(voice_params params) const;
	void set_volume(float value) const{
		set_params(voice_params::with_volume(value));
	}

	void set_pan(float value) const{
		set_params(voice_params::with_pan(value));
	}

	void set_pitch(float value) const{
		set_params(voice_params::with_pitch(value));
	}

	void set_loop(bool value) const{
		set_params(voice_params::with_loop(value));
	}
};

export struct device_config{
	bool enabled{true};
	bool require_device{};
	std::uint32_t sample_rate{};
	std::uint32_t channels{};
};

namespace cmd{

struct load_resource{
	resource_handle resource{};
	load_desc desc{};
	audio_load_priority priority{default_audio_load_priority};
	std::uint64_t sequence{};
};

struct unload_resource{
	resource_handle resource{};
};

struct release_backend_resource{
	backend_resource_handle handle{};
};

struct register_channel{
	channel_id channel{};
};

struct play_detached{
	resource_handle resource{};
	channel_id channel{};
	play_settings settings{};
};

struct play_controlled{
	resource_handle resource{};
	channel_id channel{};
	playback_id playback{};
	play_settings settings{};
};

using pending_playback = std::variant<play_detached, play_controlled>;

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

struct shutdown_audio{};

using command = std::variant<
	load_resource,
	unload_resource,
	release_backend_resource,
	register_channel,
	play_detached,
	play_controlled,
	pause_playback,
	resume_playback,
	set_playback_params,
	release_playback,
	set_channel_volume,
	shutdown_audio>;

using command_batch = std::vector<command>;

}

export enum class audio_event_type : std::uint8_t{
	resource_loaded,
	resource_failed,
	resource_unloaded,
	playback_started,
	playback_failed,
	playback_stopped,
	backend_error,
};

export struct audio_event{
	audio_event_type type{};
	resource_handle resource{};
	audio_resource_ptr backend_resource{};
	backend_resource_token backend_handle{};
	audio_resource_metadata backend_metadata{};
	playback_id playback{};
};

export class audio_driver_backend{
public:
	virtual ~audio_driver_backend() = default;

	virtual backend_resource_result load_resource(load_desc desc) = 0;
	virtual void release_resource(backend_resource_handle handle) noexcept = 0;
	virtual void register_channel(channel_id channel) = 0;
	virtual void play_detached(const audio_resource& resource, channel_id channel, const play_settings& settings) = 0;
	virtual void play_controlled(
		const audio_resource& resource,
		channel_id channel,
		playback_id playback,
		const play_settings& settings) = 0;
	virtual void stop(playback_id playback) noexcept = 0;
	virtual void detach(playback_id playback) noexcept = 0;
	virtual void pause(playback_id playback) noexcept = 0;
	virtual void resume(playback_id playback) noexcept = 0;
	virtual void set_playback_params(playback_id playback, const voice_params& params) noexcept = 0;
	virtual void set_channel_volume(channel_id channel, float volume) noexcept = 0;
	virtual void update(std::vector<audio_event>& out_events) = 0;
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

	void play_detached(const audio_resource&, channel_id, const play_settings&) override{
	}

	void play_controlled(const audio_resource&, channel_id, playback_id, const play_settings&) override{
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

	void update(std::vector<audio_event>&) override{
	}

	void shutdown() noexcept override{
	}
};

export [[nodiscard]] inline std::unique_ptr<audio_driver_backend> make_null_audio_driver(){
	return std::make_unique<null_audio_driver>();
}

class audio_channel{
	friend class audio_system;

	audio_system* system_{};
	channel_id id_{};
	std::thread::id owner_thread_{};

	[[nodiscard]] audio_channel(
		audio_system& system,
		channel_id id,
		std::thread::id owner_thread) noexcept
		: system_(std::addressof(system)),
		  id_(id),
		  owner_thread_(owner_thread){
	}

	void require_owner_thread(const char* operation) const;

public:
	[[nodiscard]] audio_channel() = default;

	[[nodiscard]] explicit operator bool() const noexcept{
		return system_ != nullptr && static_cast<bool>(id_);
	}

	[[nodiscard]] channel_id id() const noexcept{
		return id_;
	}

	[[nodiscard]] bool is_owner_thread() const noexcept{
		return std::this_thread::get_id() == owner_thread_;
	}

	[[nodiscard]] bool play_detached(resource_handle resource, play_settings settings = {}) const;
	[[nodiscard]] playback_control_handle play_controlled(
		resource_handle resource,
		play_settings settings = {},
		playback_control_options options = {}) const;
	void set_volume(float volume) const;
};

class audio_system{
	friend class audio_resource;
	friend class playback_control;
	friend class audio_channel;

public:
	static constexpr std::size_t default_channel_capacity{8};

	explicit audio_system(std::unique_ptr<audio_driver_backend> driver = make_null_audio_driver());
	~audio_system();

	audio_system(const audio_system&) = delete;
	audio_system& operator=(const audio_system&) = delete;
	audio_system(audio_system&&) = delete;
	audio_system& operator=(audio_system&&) = delete;

	[[nodiscard]] bool valid() const noexcept{
		return accepting_.load(std::memory_order_acquire);
	}

	[[nodiscard]] resource_handle load(load_desc desc, audio_load_priority priority = default_audio_load_priority);
	void unload(resource_handle resource) noexcept;

	[[nodiscard]] audio_channel register_channel(channel_id channel);

	[[nodiscard]] std::optional<audio_channel> get_channel(channel_id channel) noexcept;

	void shutdown() noexcept;
	void poll_events_into(std::vector<audio_event>& out);

	template <typename Fn>
		requires std::invocable<Fn&, const audio_event&>
	void poll_events(Fn&& fn);

private:
	struct channel_slot{
		std::atomic<std::uint32_t> channel_value{};
		std::thread::id owner_thread{};
	};

	struct channel_registration{
		channel_slot* slot{};
		bool inserted{};
	};

	struct resource_handle_less{
		[[nodiscard]] constexpr bool operator()(const resource_handle lhs, const resource_handle rhs) const noexcept{
			return lhs.value < rhs.value;
		}
	};

	struct load_completion{
		resource_handle resource{};
		backend_resource_result result{};
	};

	static constexpr std::uint32_t empty_channel_slot_value_{};
	static constexpr std::uint32_t pending_channel_slot_value_{std::numeric_limits<std::uint32_t>::max()};

	ccur::mpsc_queue<cmd::command_batch> commands_{};
	ccur::mpsc_queue<cmd::load_resource> load_requests_{};
	ccur::mpsc_queue<load_completion> load_completions_{};
	ccur::mpsc_double_buffer<audio_event> events_{};

	std::unique_ptr<audio_driver_backend> driver_{};
	std::shared_ptr<audio_control_sink> control_sink_{};
	std::jthread worker_{};
	std::jthread loader_{};
	std::counting_semaphore<> worker_wake_{0};
	std::atomic_bool accepting_{true};
	std::atomic<std::uint64_t> next_resource_{1};
	std::atomic<std::uint64_t> next_playback_{1};

	std::array<channel_slot, default_channel_capacity> channels_{};

	std::flat_map<resource_handle, audio_resource_ptr> resources_{};
	std::flat_set<resource_handle> pending_load_resources_{};
	std::vector<cmd::load_resource> pending_loads_{};
	std::flat_map<resource_handle, std::vector<cmd::pending_playback>> pending_playbacks_{};
	std::uint64_t next_load_sequence_{1};
	std::size_t active_load_count_{};
	std::mutex deferred_backend_releases_mutex_{};
	std::vector<backend_resource_handle> deferred_backend_releases_{};

	void start();
	void wake_worker_() noexcept{
		worker_wake_.release();
	}

	[[nodiscard]] resource_handle allocate_resource() noexcept{
		return resource_handle{next_resource_.fetch_add(1, std::memory_order_relaxed)};
	}

	[[nodiscard]] playback_id allocate_playback() noexcept{
		return playback_id{next_playback_.fetch_add(1, std::memory_order_relaxed)};
	}

	struct pending_load_less{
		[[nodiscard]] bool operator()(const cmd::load_resource& lhs, const cmd::load_resource& rhs) const noexcept{
			if(lhs.priority != rhs.priority){
				return lhs.priority < rhs.priority;
			}
			return lhs.sequence > rhs.sequence;
		}
	};

	[[nodiscard]] const channel_slot* find_channel_slot_(channel_id channel) const noexcept;
	[[nodiscard]] channel_registration reserve_channel_slot_(channel_id channel, std::thread::id owner_thread);
	void commit_channel_slot_(channel_slot& slot, channel_id channel) noexcept{
		slot.channel_value.store(static_cast<std::uint32_t>(channel.value), std::memory_order_release);
	}

	void rollback_channel_slot_(channel_slot& slot) noexcept{
		slot.owner_thread = {};
		slot.channel_value.store(empty_channel_slot_value_, std::memory_order_release);
	}

	void require_registered_channel_(channel_id channel) const;
	[[nodiscard]] bool post(cmd::command command) noexcept;
	[[nodiscard]] bool post(cmd::command_batch batch) noexcept;
	[[nodiscard]] bool post_channel_command_(channel_id channel, cmd::command command) noexcept;
	void detach_control_sink_() noexcept;
	void push_event(audio_event event);
	void push_events(std::vector<audio_event>& new_events);
	void pop_events(std::vector<audio_event>& out);
	void run(const std::stop_token& stop_token);
	void run_loader(const std::stop_token& stop_token);
	void process(cmd::command_batch batch);
	void process(cmd::command command);
	void enqueue_load_(cmd::load_resource command);
	[[nodiscard]] bool process_pending_loads_();
	[[nodiscard]] bool process_load_completions_();
	void process_load_request_(cmd::load_resource command) noexcept;
	void process_load_completion_(load_completion completion);
	[[nodiscard]] bool is_pending_load_(resource_handle resource) const noexcept;
	void flush_pending_playbacks_(resource_handle resource);
	void fail_pending_playbacks_(resource_handle resource);
	void cancel_pending_loads_() noexcept;
	[[nodiscard]] bool release_pending_playback_(playback_id playback, playback_release_policy policy);
	[[nodiscard]] bool apply_pending_playback_params_(playback_id playback, voice_params params) noexcept;
	void release_backend_resource_(backend_resource_handle handle) noexcept;
	void defer_backend_resource_release_(backend_resource_handle handle) noexcept;
	void drain_deferred_backend_releases_() noexcept;

	[[nodiscard]] bool post_play_detached_(channel_id channel, resource_handle resource, play_settings settings);
	[[nodiscard]] playback_control_handle post_play_controlled_(
		channel_id channel,
		resource_handle resource,
		play_settings settings,
		playback_control_options options);
	void post_set_channel_volume_(channel_id channel, float volume);
	void post_pause_playback_(channel_id channel, playback_id playback) noexcept;
	void post_resume_playback_(channel_id channel, playback_id playback) noexcept;
	void post_set_playback_params_(channel_id channel, playback_id playback, voice_params params) noexcept;
	void post_release_playback_(channel_id channel, playback_id playback, playback_release_policy policy) noexcept;
};

template <typename Fn>
	requires std::invocable<Fn&, const audio_event&>
void audio_system::poll_events(Fn&& fn){
	std::vector<audio_event> pending{};
	poll_events_into(pending);
	for(const auto& event : pending){
		std::invoke(fn, event);
	}
}


}
