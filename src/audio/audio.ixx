module;

#include <cassert>

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

export struct resource_handle{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const resource_handle&) const noexcept = default;
};

export struct voice_handle{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const voice_handle&) const noexcept = default;
};

export struct backend_resource_token{
	std::uint64_t value{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return value != 0;
	}

	[[nodiscard]] constexpr bool operator==(const backend_resource_token&) const noexcept = default;
};

export struct resource_handle_hash{
	[[nodiscard]] std::size_t operator()(const resource_handle handle) const noexcept{
		return std::hash<std::uint64_t>{}(handle.value);
	}
};

export struct voice_handle_hash{
	[[nodiscard]] std::size_t operator()(const voice_handle handle) const noexcept{
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
	load_source_kind source{load_source_kind::file};
	std::uint64_t reserved_bytes{};
	std::string debug_name{};
	bool stream{};
	bool preload{true};
};

export [[nodiscard]] inline audio_resource_metadata make_audio_resource_metadata(const load_desc& desc){
	return audio_resource_metadata{
		.source = desc.source_kind(),
		.reserved_bytes = static_cast<std::uint64_t>(desc.source_size_hint()),
		.debug_name = desc.debug_name,
		.stream = desc.stream,
		.preload = desc.preload
	};
}

export class audio_resource;

export struct audio_resource_deleter{
	void operator()(audio_resource* resource) const noexcept;
};

export using audio_resource_ptr = mo_yanxi::referenced_ptr<audio_resource, audio_resource_deleter>;

export class audio_resource final : public mo_yanxi::referenced_object_atomic{
public:
	using release_fn = void(*)(void* backend, void* handle) noexcept;

private:
	void* backend_{};
	void* handle_{};
	release_fn release_{};
	backend_resource_token token_{};
	audio_resource_metadata metadata_{};

public:
	[[nodiscard]] audio_resource(
		void* backend,
		void* handle,
		release_fn release,
		backend_resource_token token,
		audio_resource_metadata metadata = {}) noexcept
		: backend_(backend),
		  handle_(handle),
		  release_(release),
		  token_(token),
		  metadata_(std::move(metadata)){
	}

	~audio_resource(){
		release();
	}

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

	[[nodiscard]] void* backend() const noexcept{
		return backend_;
	}

	[[nodiscard]] void* native_handle() const noexcept{
		return handle_;
	}

	[[nodiscard]] backend_resource_token token() const noexcept{
		return token_;
	}

	[[nodiscard]] const audio_resource_metadata& metadata() const noexcept{
		return metadata_;
	}

	[[nodiscard]] std::size_t ref_count() const noexcept{
		return referenced_object_atomic::ref_count();
	}

	void release() noexcept{
		const auto handle = std::exchange(handle_, nullptr);
		const auto backend = std::exchange(backend_, nullptr);
		const auto release = std::exchange(release_, nullptr);
		token_ = {};
		if(handle != nullptr && release != nullptr){
			std::invoke(release, backend, handle);
		}
	}
};

export struct play_desc{
	bus target_bus{bus::ui};
	float volume{1.f};
	float pan{};
	float pitch{1.f};
	bool loop{};
};

export struct voice_params{
	std::optional<float> volume{};
	std::optional<float> pan{};
	std::optional<float> pitch{};
	std::optional<bool> loop{};
};

export struct device_config{
	bool enabled{true};
	bool require_device{};
	std::uint32_t sample_rate{};
	std::uint32_t channels{};
};

export enum class audio_command_type : std::uint8_t{
	load_resource,
	unload_resource,
	play,
	stop,
	pause,
	resume,
	set_voice_params,
	set_bus_volume,
	shutdown,
};

export struct audio_command{
	audio_command_type type{};
	resource_handle resource{};
	voice_handle voice{};
	load_desc load{};
	play_desc play{};
	voice_params params{};
	bus target_bus{bus::ui};
	float volume{1.f};
};

export enum class audio_event_type : std::uint8_t{
	resource_loaded,
	resource_failed,
	resource_unloaded,
	voice_started,
	voice_failed,
	voice_stopped,
	backend_error,
};

export struct audio_event{
	audio_event_type type{};
	resource_handle resource{};
	audio_resource_ptr backend_resource{};
	voice_handle voice{};
	std::string message{};
};

export class audio_driver{
public:
	virtual ~audio_driver() = default;

	virtual audio_resource_ptr load_resource(load_desc desc) = 0;
	virtual void play(const audio_resource& resource, voice_handle voice, const play_desc& desc) = 0;
	virtual void stop(voice_handle voice) noexcept = 0;
	virtual void pause(voice_handle voice) noexcept = 0;
	virtual void resume(voice_handle voice) noexcept = 0;
	virtual void set_voice_params(voice_handle voice, const voice_params& params) noexcept = 0;
	virtual void set_bus_volume(bus target_bus, float volume) noexcept = 0;
	virtual void update(std::vector<audio_event>& out_events) = 0;
	virtual void shutdown() noexcept = 0;
};

export class null_audio_driver final : public audio_driver{
	std::uint64_t next_token_{1};

	static void release_resource_(void*, void* handle) noexcept{
		delete static_cast<backend_resource_token*>(handle);
	}

public:
	audio_resource_ptr load_resource(load_desc desc) override{
		auto* token = new backend_resource_token{next_token_++};
		return audio_resource_ptr{new audio_resource{
			this,
			token,
			release_resource_,
			*token,
			make_audio_resource_metadata(desc)
		}};
	}

	void play(const audio_resource&, voice_handle, const play_desc&) override{
	}

	void stop(voice_handle) noexcept override{
	}

	void pause(voice_handle) noexcept override{
	}

	void resume(voice_handle) noexcept override{
	}

	void set_voice_params(voice_handle, const voice_params&) noexcept override{
	}

	void set_bus_volume(bus, float) noexcept override{
	}

	void update(std::vector<audio_event>&) override{
	}

	void shutdown() noexcept override{
	}
};

export [[nodiscard]] inline std::unique_ptr<audio_driver> make_null_audio_driver(){
	return std::make_unique<null_audio_driver>();
}

export struct audio_system_state;
export class audio_system;

export class audio_controller{
	std::weak_ptr<audio_system_state> state_{};

	explicit audio_controller(std::weak_ptr<audio_system_state> state) noexcept
		: state_(std::move(state)){
	}

	friend class audio_system;

public:
	[[nodiscard]] audio_controller() noexcept = default;

	[[nodiscard]] bool valid() const noexcept;

	[[nodiscard]] resource_handle load(load_desc desc) const;
	void unload(resource_handle resource) const;
	[[nodiscard]] voice_handle play(resource_handle resource, play_desc desc = {}) const;
	void stop(voice_handle voice) const;
	void pause(voice_handle voice) const;
	void resume(voice_handle voice) const;
	void set_voice_params(voice_handle voice, voice_params params) const;
	void set_bus_volume(bus target_bus, float volume) const;
};

class audio_system{
	std::shared_ptr<audio_system_state> state_{};

public:
	explicit audio_system(std::unique_ptr<audio_driver> driver = make_null_audio_driver());
	~audio_system();

	audio_system(const audio_system&) = delete;
	audio_system& operator=(const audio_system&) = delete;
	audio_system(audio_system&&) noexcept = default;
	audio_system& operator=(audio_system&&) noexcept = default;

	[[nodiscard]] audio_controller controller() const noexcept;
	void shutdown() noexcept;

	template <typename Fn>
		requires std::invocable<Fn&, const audio_event&>
	void poll_events(Fn&& fn);
};

struct audio_system_state{
	ccur::mpsc_queue<audio_command> commands{};
	ccur::mpsc_double_buffer<audio_event> events{};

	std::unique_ptr<audio_driver> driver{};
	std::jthread worker{};
	std::atomic_bool accepting{true};
	std::atomic<std::uint64_t> next_resource{1};
	std::atomic<std::uint64_t> next_voice{1};

	std::unordered_map<resource_handle, audio_resource_ptr, resource_handle_hash> resources{};

	explicit audio_system_state(std::unique_ptr<audio_driver> driver_)
		: driver(std::move(driver_)){
		if(driver == nullptr){
			driver = make_null_audio_driver();
		}
	}

	~audio_system_state(){
		shutdown();
	}

	void start(){
		worker = std::jthread{[this](std::stop_token stop_token){
			run(stop_token);
		}};
	}

	[[nodiscard]] resource_handle allocate_resource() noexcept{
		return resource_handle{next_resource.fetch_add(1, std::memory_order_relaxed)};
	}

	[[nodiscard]] voice_handle allocate_voice() noexcept{
		return voice_handle{next_voice.fetch_add(1, std::memory_order_relaxed)};
	}

	[[nodiscard]] bool post(audio_command command){
		if(!accepting.load(std::memory_order_acquire)){
			return false;
		}

		commands.push(std::move(command));
		return true;
	}

	void push_event(audio_event event){
		events.push(std::move(event));
	}

	void push_events(std::vector<audio_event>& new_events){
		for(auto& event : new_events){
			events.push(std::move(event));
		}
		new_events.clear();
	}

	void pop_events(std::vector<audio_event>& out){
		out.clear();
		if(auto fetched = events.fetch()){
			out.reserve(fetched->size());
			for(auto& event : *fetched){
				out.push_back(std::move(event));
			}
		}
	}

	void shutdown() noexcept{
		const bool was_accepting = accepting.exchange(false, std::memory_order_acq_rel);
		if(was_accepting){
			try{
				commands.push(audio_command{.type = audio_command_type::shutdown});
				commands.notify();
			}catch(...){
			}
		}

		if(worker.joinable()){
			worker.request_stop();
			commands.notify();
			if(worker.get_id() != std::this_thread::get_id()){
				worker.join();
			}
		}
		events.clear();
	}

private:
	[[nodiscard]] static std::string exception_message(){
		try{
			throw;
		}catch(const std::exception& e){
			return e.what();
		}catch(...){
			return "unknown audio backend exception";
		}
	}

	void run(const std::stop_token& stop_token){
		while(!stop_token.stop_requested()){
			bool processed_command = false;
			while(auto command = commands.try_consume()){
				processed_command = true;
				if(command->type == audio_command_type::shutdown){
					accepting.store(false, std::memory_order_release);
					break;
				}
				process(std::move(*command));
			}

			std::vector<audio_event> driver_events{};
			try{
				driver->update(driver_events);
			}catch(...){
				driver_events.push_back(audio_event{
					.type = audio_event_type::backend_error,
					.message = exception_message()
				});
			}
			push_events(driver_events);

			if(!accepting.load(std::memory_order_acquire) && commands.empty()){
				break;
			}
			if(!processed_command){
				std::this_thread::sleep_for(std::chrono::milliseconds{16});
			}
		}

		events.clear();
		for(auto&& [resource, backend_resource] : resources){
			(void)resource;
			if(backend_resource){
				backend_resource->release();
			}
		}
		resources.clear();
		driver->shutdown();
	}

	void process(audio_command command){
		switch(command.type){
		case audio_command_type::load_resource:
			process_load(std::move(command));
			break;
		case audio_command_type::unload_resource:
			process_unload(command);
			break;
		case audio_command_type::play:
			process_play(command);
			break;
		case audio_command_type::stop:
			driver->stop(command.voice);
			break;
		case audio_command_type::pause:
			driver->pause(command.voice);
			break;
		case audio_command_type::resume:
			driver->resume(command.voice);
			break;
		case audio_command_type::set_voice_params:
			driver->set_voice_params(command.voice, command.params);
			break;
		case audio_command_type::set_bus_volume:
			driver->set_bus_volume(command.target_bus, command.volume);
			break;
		case audio_command_type::shutdown:
			break;
		}
	}

	void process_load(audio_command command){
		if(!command.resource){
			return;
		}

		try{
			auto resource = driver->load_resource(std::move(command.load));
			if(!resource || !resource->valid()){
				push_event(audio_event{
					.type = audio_event_type::resource_failed,
					.resource = command.resource,
					.message = "audio backend returned an invalid resource"
				});
				return;
			}

			resources.erase(command.resource);
			auto [iter, inserted] = resources.emplace(command.resource, resource);
			(void)inserted;
			push_event(audio_event{
				.type = audio_event_type::resource_loaded,
				.resource = command.resource,
				.backend_resource = iter->second
			});
		}catch(...){
			push_event(audio_event{
				.type = audio_event_type::resource_failed,
				.resource = command.resource,
				.message = exception_message()
			});
		}
	}

	void process_unload(const audio_command& command){
		if(!command.resource){
			return;
		}

		audio_resource_ptr backend_resource{};
		if(auto node = resources.extract(command.resource); node){
			backend_resource = std::move(node.mapped());
		}
		push_event(audio_event{
			.type = audio_event_type::resource_unloaded,
			.resource = command.resource,
			.backend_resource = std::move(backend_resource)
		});
	}

	void process_play(const audio_command& command){
		if(!command.resource || !command.voice){
			return;
		}

		const auto resource = resources.find(command.resource);
		if(resource == resources.end()){
			push_event(audio_event{
				.type = audio_event_type::voice_failed,
				.resource = command.resource,
				.voice = command.voice,
				.message = "audio resource is not loaded"
			});
			return;
		}

		try{
			driver->play(*resource->second, command.voice, command.play);
			push_event(audio_event{
				.type = audio_event_type::voice_started,
				.resource = command.resource,
				.voice = command.voice
			});
		}catch(...){
			push_event(audio_event{
				.type = audio_event_type::voice_failed,
				.resource = command.resource,
				.voice = command.voice,
				.message = exception_message()
			});
		}
	}
};

bool audio_controller::valid() const noexcept{
	return !state_.expired();
}

resource_handle audio_controller::load(load_desc desc) const{
	if(auto state = state_.lock()){
		const auto resource = state->allocate_resource();
		if(state->post(audio_command{
			.type = audio_command_type::load_resource,
			.resource = resource,
			.load = std::move(desc)
		})){
			return resource;
		}
	}
	return {};
}

void audio_controller::unload(const resource_handle resource) const{
	if(!resource){
		return;
	}
	if(auto state = state_.lock()){
		static_cast<void>(state->post(audio_command{
			.type = audio_command_type::unload_resource,
			.resource = resource
		}));
	}
}

voice_handle audio_controller::play(const resource_handle resource, play_desc desc) const{
	if(!resource){
		return {};
	}
	if(auto state = state_.lock()){
		const auto voice = state->allocate_voice();
		if(state->post(audio_command{
			.type = audio_command_type::play,
			.resource = resource,
			.voice = voice,
			.play = std::move(desc)
		})){
			return voice;
		}
	}
	return {};
}

void audio_controller::stop(const voice_handle voice) const{
	if(!voice){
		return;
	}
	if(auto state = state_.lock()){
		static_cast<void>(state->post(audio_command{.type = audio_command_type::stop, .voice = voice}));
	}
}

void audio_controller::pause(const voice_handle voice) const{
	if(!voice){
		return;
	}
	if(auto state = state_.lock()){
		static_cast<void>(state->post(audio_command{.type = audio_command_type::pause, .voice = voice}));
	}
}

void audio_controller::resume(const voice_handle voice) const{
	if(!voice){
		return;
	}
	if(auto state = state_.lock()){
		static_cast<void>(state->post(audio_command{.type = audio_command_type::resume, .voice = voice}));
	}
}

void audio_controller::set_voice_params(const voice_handle voice, voice_params params) const{
	if(!voice){
		return;
	}
	if(auto state = state_.lock()){
		static_cast<void>(state->post(audio_command{
			.type = audio_command_type::set_voice_params,
			.voice = voice,
			.params = std::move(params)
		}));
	}
}

void audio_controller::set_bus_volume(const bus target_bus, const float volume) const{
	if(auto state = state_.lock()){
		static_cast<void>(state->post(audio_command{
			.type = audio_command_type::set_bus_volume,
			.target_bus = target_bus,
			.volume = volume
		}));
	}
}

audio_system::audio_system(std::unique_ptr<audio_driver> driver)
	: state_(std::make_shared<audio_system_state>(std::move(driver))){
	state_->start();
}

audio_system::~audio_system(){
	shutdown();
}

audio_controller audio_system::controller() const noexcept{
	return audio_controller{state_};
}

void audio_system::shutdown() noexcept{
	if(state_){
		state_->shutdown();
		state_.reset();
	}
}

template <typename Fn>
	requires std::invocable<Fn&, const audio_event&>
void audio_system::poll_events(Fn&& fn){
	if(!state_){
		return;
	}

	std::vector<audio_event> pending{};
	state_->pop_events(pending);
	for(const auto& event : pending){
		std::invoke(fn, event);
	}
}

void audio_resource_deleter::operator()(audio_resource* resource) const noexcept{
	delete resource;
}

}
