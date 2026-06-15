module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

module mo_yanxi.backend.miniaudio.audio;

import std;

namespace mo_yanxi::backend::miniaudio{
namespace{

[[nodiscard]] std::string miniaudio_error(const char* operation, const ma_result result){
	return std::format("{} failed with miniaudio result {}", operation, static_cast<int>(result));
}

class engine_handle{
	ma_engine engine_{};
	bool initialized_{};

public:
	[[nodiscard]] engine_handle() = default;

	explicit engine_handle(const audio::device_config& config){
		ma_engine_config engine_config = ma_engine_config_init();
		if(config.sample_rate != 0){
			engine_config.sampleRate = config.sample_rate;
		}
		if(config.channels != 0){
			engine_config.channels = config.channels;
		}

		if(const ma_result result = ma_engine_init(&engine_config, &engine_); result != MA_SUCCESS){
			throw std::runtime_error{miniaudio_error("ma_engine_init", result)};
		}
		initialized_ = true;
	}

	~engine_handle(){
		reset();
	}

	engine_handle(const engine_handle&) = delete;
	engine_handle(engine_handle&&) = delete;
	engine_handle& operator=(const engine_handle&) = delete;
	engine_handle& operator=(engine_handle&&) = delete;

	[[nodiscard]] ma_engine* get() noexcept{
		return initialized_ ? &engine_ : nullptr;
	}

	[[nodiscard]] const ma_engine* get() const noexcept{
		return initialized_ ? &engine_ : nullptr;
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return initialized_;
	}

	void set_volume(const float volume) noexcept{
		if(initialized_){
			ma_engine_set_volume(&engine_, volume);
		}
	}

	void reset() noexcept{
		if(initialized_){
			ma_engine_uninit(&engine_);
			initialized_ = false;
		}
	}
};

class sound_group_handle{
	ma_sound_group group_{};
	bool initialized_{};

public:
	[[nodiscard]] sound_group_handle() = default;

	~sound_group_handle(){
		reset();
	}

	sound_group_handle(const sound_group_handle&) = delete;
	sound_group_handle(sound_group_handle&&) = delete;
	sound_group_handle& operator=(const sound_group_handle&) = delete;
	sound_group_handle& operator=(sound_group_handle&&) = delete;

	void init(engine_handle& engine, ma_sound_group* parent, const char* operation){
		if(engine.get() == nullptr){
			throw std::runtime_error{"miniaudio engine is not initialized"};
		}
		if(const ma_result result = ma_sound_group_init(engine.get(), 0, parent, &group_); result != MA_SUCCESS){
			throw std::runtime_error{miniaudio_error(operation, result)};
		}
		initialized_ = true;
	}

	[[nodiscard]] ma_sound_group* get() noexcept{
		return initialized_ ? &group_ : nullptr;
	}

	[[nodiscard]] const ma_sound_group* get() const noexcept{
		return initialized_ ? &group_ : nullptr;
	}

	void set_volume(const float volume) noexcept{
		if(initialized_){
			ma_sound_group_set_volume(&group_, volume);
		}
	}

	void reset() noexcept{
		if(initialized_){
			ma_sound_group_uninit(&group_);
			initialized_ = false;
		}
	}
};

struct decoder_deleter{
	void operator()(ma_decoder* decoder) const noexcept{
		ma_decoder_uninit(decoder);
		delete decoder;
	}
};

class decoder_handle{
	std::unique_ptr<ma_decoder, decoder_deleter> decoder_{};

public:
	[[nodiscard]] decoder_handle() = default;

	decoder_handle(const decoder_handle&) = delete;
	decoder_handle(decoder_handle&&) noexcept = default;
	decoder_handle& operator=(const decoder_handle&) = delete;
	decoder_handle& operator=(decoder_handle&&) noexcept = default;

	void init_memory(std::span<const std::byte> memory){
		auto decoder = std::make_unique<ma_decoder>();
		if(const ma_result result = ma_decoder_init_memory(
			   memory.data(),
			   memory.size(),
			   nullptr,
			   decoder.get()); result != MA_SUCCESS){
			throw std::runtime_error{miniaudio_error("ma_decoder_init_memory", result)};
		}
		decoder_ = std::unique_ptr<ma_decoder, decoder_deleter>{decoder.release()};
	}

	[[nodiscard]] ma_decoder* get() noexcept{
		return decoder_.get();
	}

	[[nodiscard]] const ma_decoder* get() const noexcept{
		return decoder_.get();
	}
};

struct sound_deleter{
	void operator()(ma_sound* sound) const noexcept{
		ma_sound_uninit(sound);
		delete sound;
	}
};

class sound_handle{
	std::unique_ptr<ma_sound, sound_deleter> sound_{};

public:
	[[nodiscard]] sound_handle() = default;

	sound_handle(const sound_handle&) = delete;
	sound_handle(sound_handle&&) noexcept = default;
	sound_handle& operator=(const sound_handle&) = delete;
	sound_handle& operator=(sound_handle&&) noexcept = default;

	void init_from_data_source(
		engine_handle& engine,
		ma_data_source* source,
		const ma_uint32 flags,
		ma_sound_group* group){
		auto sound = std::make_unique<ma_sound>();
		if(const ma_result result = ma_sound_init_from_data_source(
			   engine.get(),
			   source,
			   flags,
			   group,
			   sound.get()); result != MA_SUCCESS){
			throw std::runtime_error{miniaudio_error("ma_sound_init_from_data_source", result)};
		}
		sound_ = std::unique_ptr<ma_sound, sound_deleter>{sound.release()};
	}

	void init_from_file(
		engine_handle& engine,
		const std::filesystem::path& path,
		const ma_uint32 flags,
		ma_sound_group* group){
		const std::string path_string = path.string();
		auto sound = std::make_unique<ma_sound>();
		if(const ma_result result = ma_sound_init_from_file(
			   engine.get(),
			   path_string.c_str(),
			   flags,
			   group,
			   nullptr,
			   sound.get()); result != MA_SUCCESS){
			throw std::runtime_error{miniaudio_error("ma_sound_init_from_file", result)};
		}
		sound_ = std::unique_ptr<ma_sound, sound_deleter>{sound.release()};
	}

	[[nodiscard]] ma_sound* get() noexcept{
		return sound_.get();
	}

	[[nodiscard]] const ma_sound* get() const noexcept{
		return sound_.get();
	}

	void set_volume(const float volume) noexcept{
		ma_sound_set_volume(sound_.get(), volume);
	}

	void set_pan(const float pan) noexcept{
		ma_sound_set_pan(sound_.get(), pan);
	}

	void set_pitch(const float pitch) noexcept{
		ma_sound_set_pitch(sound_.get(), pitch);
	}

	void set_looping(const bool loop) noexcept{
		ma_sound_set_looping(sound_.get(), loop ? MA_TRUE : MA_FALSE);
	}

	void start(){
		if(const ma_result result = ma_sound_start(sound_.get()); result != MA_SUCCESS){
			throw std::runtime_error{miniaudio_error("ma_sound_start", result)};
		}
	}

	void stop() noexcept{
		ma_sound_stop(sound_.get());
	}

	[[nodiscard]] bool at_end() const noexcept{
		return ma_sound_at_end(sound_.get()) == MA_TRUE;
	}
};

struct resource_entry{
	audio::backend_resource_token token{};
	audio::load_source source{};
	bool stream{};
	bool preload{};
};

struct voice_entry{
	std::vector<std::byte> memory_owner{};
	decoder_handle decoder{};
	sound_handle sound{};
	bool manually_stopped{};
};

[[nodiscard]] std::vector<std::byte> read_file_bytes(const std::filesystem::path& path){
	std::ifstream file{path, std::ios::binary | std::ios::ate};
	if(!file){
		throw std::runtime_error{std::format("audio resource could not be opened: {}", path.string())};
	}

	const auto end = file.tellg();
	if(end < std::streampos{}){
		throw std::runtime_error{std::format("audio resource size could not be read: {}", path.string())};
	}

	std::vector<std::byte> bytes(static_cast<std::size_t>(end));
	file.seekg(0, std::ios::beg);
	if(!bytes.empty()){
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if(!file){
			throw std::runtime_error{std::format("audio resource could not be read: {}", path.string())};
		}
	}
	return bytes;
}

class driver final : public audio::audio_driver_backend{
	engine_handle engine_{};
	std::unordered_map<audio::channel_id, std::unique_ptr<sound_group_handle>, audio::channel_id_hash> channel_groups_{};

	std::atomic<std::uint64_t> next_token_{1};
	std::vector<voice_entry> detached_voices_{};
	std::unordered_map<audio::playback_id, voice_entry, audio::playback_id_hash> controlled_voices_{};

public:
	explicit driver(const audio::device_config& config)
		: engine_(config){
	}

	~driver() override{
		shutdown();
	}

	audio::backend_resource_result load_resource(audio::load_desc desc) override{
		const auto metadata = audio::make_audio_resource_metadata(desc);
		auto entry = std::make_unique<resource_entry>(resource_entry{
			.token = audio::backend_resource_token{next_token_.fetch_add(1, std::memory_order_relaxed)},
			.stream = desc.stream,
			.preload = desc.preload
		});

		if(auto* memory = desc.memory()){
			if(memory->empty()){
				throw std::invalid_argument{"audio memory resource is empty"};
			}
			entry->source = std::move(*memory);
		}else{
			auto* path = desc.path();
			if(path == nullptr || path->empty()){
				throw std::invalid_argument{"audio resource path is empty"};
			}
			if(!std::filesystem::exists(*path)){
				throw std::runtime_error{std::format("audio resource does not exist: {}", path->string())};
			}
			if(desc.preload && !desc.stream){
				entry->source = read_file_bytes(*path);
			}else{
				entry->source = std::move(*path);
			}
		}

		const auto token = entry->token;
		return audio::backend_resource_result{
			.handle = entry.release(),
			.token = token,
			.metadata = std::move(metadata)
		};
	}

	void release_resource(audio::backend_resource_handle handle) noexcept override{
		delete static_cast<resource_entry*>(handle);
	}

	void register_channel(const audio::channel_id channel) override{
		if(!channel || channel == audio::channel_id_from_role(audio::channel_role::master) || channel_groups_.contains(channel)){
			return;
		}

		auto group = std::make_unique<sound_group_handle>();
		group->init(engine_, nullptr, "audio channel");
		channel_groups_.emplace(channel, std::move(group));
	}

	void play_detached(
		const audio::audio_resource& resource,
		const audio::channel_id channel,
		const audio::play_settings& settings) override{
		detached_voices_.push_back(make_voice_entry(resource, channel, settings));
	}

	void play_controlled(
		const audio::audio_resource& resource,
		const audio::channel_id channel,
		const audio::playback_id playback,
		const audio::play_settings& settings) override{
		controlled_voices_[playback] = make_voice_entry(resource, channel, settings);
	}

	void stop(const audio::playback_id playback) noexcept override{
		if(auto iter = controlled_voices_.find(playback); iter != controlled_voices_.end()){
			iter->second.sound.stop();
			iter->second.manually_stopped = true;
		}
	}

	void detach(const audio::playback_id playback) noexcept override{
		if(auto node = controlled_voices_.extract(playback); node){
			detached_voices_.push_back(std::move(node.mapped()));
		}
	}

	void pause(const audio::playback_id playback) noexcept override{
		if(auto iter = controlled_voices_.find(playback); iter != controlled_voices_.end()){
			iter->second.sound.stop();
		}
	}

	void resume(const audio::playback_id playback) noexcept override{
		if(auto iter = controlled_voices_.find(playback); iter != controlled_voices_.end()){
			try{
				iter->second.sound.start();
			}catch(...){
			}
		}
	}

	void set_playback_params(const audio::playback_id playback, const audio::voice_params& params) noexcept override{
		if(auto iter = controlled_voices_.find(playback); iter != controlled_voices_.end()){
			if(params.has_volume()){
				iter->second.sound.set_volume(std::max(0.f, params.volume));
			}
			if(params.has_pan()){
				iter->second.sound.set_pan(std::clamp(params.pan, -1.f, 1.f));
			}
			if(params.has_pitch()){
				iter->second.sound.set_pitch(std::max(0.01f, params.pitch));
			}
			if(params.has_loop()){
				iter->second.sound.set_looping(params.loop);
			}
		}
	}

	void set_channel_volume(const audio::channel_id channel, const float volume) noexcept override{
		const float safe_volume = std::max(0.f, volume);
		if(channel == audio::channel_id_from_role(audio::channel_role::master)){
			engine_.set_volume(safe_volume);
			return;
		}
		if(auto iter = channel_groups_.find(channel); iter != channel_groups_.end() && iter->second){
			iter->second->set_volume(safe_volume);
		}
	}

	void update(std::vector<audio::audio_event>& out_events) override{
		for(auto iter = controlled_voices_.begin(); iter != controlled_voices_.end();){
			const bool stopped = iter->second.manually_stopped ||
				iter->second.sound.at_end();
			if(stopped){
				out_events.push_back(audio::audio_event{
					.type = audio::audio_event_type::playback_stopped,
					.playback = iter->first
				});
				iter = controlled_voices_.erase(iter);
			}else{
				++iter;
			}
		}

		std::erase_if(detached_voices_, [](const voice_entry& entry){
			return entry.manually_stopped || entry.sound.at_end();
		});
	}

	void shutdown() noexcept override{
		controlled_voices_.clear();
		detached_voices_.clear();
		channel_groups_.clear();
		engine_.reset();
	}

private:
	[[nodiscard]] voice_entry make_voice_entry(
		const audio::audio_resource& resource,
		const audio::channel_id channel,
		const audio::play_settings& settings){
		if(resource.native_handle() == nullptr){
			throw std::runtime_error{"audio resource token is not loaded"};
		}
		const auto& loaded_resource = *static_cast<const resource_entry*>(resource.native_handle());

		voice_entry entry{};
		const bool memory_source = std::holds_alternative<std::vector<std::byte>>(loaded_resource.source);
		const ma_uint32 flags = loaded_resource.stream && !memory_source ? MA_SOUND_FLAG_STREAM : 0;
		ma_sound_group* group = group_for(channel);

		if(const auto* memory = std::get_if<std::vector<std::byte>>(&loaded_resource.source)){
			entry.memory_owner = *memory;
			entry.decoder.init_memory(entry.memory_owner);
			entry.sound.init_from_data_source(engine_, entry.decoder.get(), flags, group);
		}else{
			entry.sound.init_from_file(
				engine_,
				std::get<std::filesystem::path>(loaded_resource.source),
				flags,
				group);
		}

		entry.sound.set_volume(std::max(0.f, settings.volume));
		entry.sound.set_pan(std::clamp(settings.pan, -1.f, 1.f));
		entry.sound.set_pitch(std::max(0.01f, settings.pitch));
		entry.sound.set_looping(settings.loop);
		entry.sound.start();
		return entry;
	}

	[[nodiscard]] ma_sound_group* group_for(const audio::channel_id channel){
		if(channel == audio::channel_id_from_role(audio::channel_role::master)){
			return nullptr;
		}
		if(auto iter = channel_groups_.find(channel); iter != channel_groups_.end() && iter->second){
			return iter->second->get();
		}
		throw std::runtime_error{"audio channel is not registered in miniaudio driver"};
	}

};

}

std::unique_ptr<audio::audio_driver_backend> make_audio_driver(audio::device_config config){
	if(!config.enabled){
		return audio::make_null_audio_driver();
	}

	try{
		return std::make_unique<driver>(config);
	}catch(...){
		if(config.require_device){
			throw;
		}
		return audio::make_null_audio_driver();
	}
}

}
