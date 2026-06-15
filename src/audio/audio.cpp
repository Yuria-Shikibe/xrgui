module;

#include <cassert>

module mo_yanxi.audio;

import std;
import mo_yanxi.utility;

namespace mo_yanxi::audio{

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

namespace{

constexpr auto audio_worker_idle_tick = std::chrono::milliseconds{16};
constexpr std::size_t max_concurrent_audio_loads{1};

}

struct audio_control_sink{
	std::mutex mutex{};
	audio_system* system{};

	explicit audio_control_sink(audio_system& target_system) noexcept
		: system(std::addressof(target_system)){
	}

	void detach(audio_system& target_system) noexcept{
		std::scoped_lock lock{mutex};
		if(system == std::addressof(target_system)){
			system = nullptr;
		}
	}

	template <typename Fn>
	void with_system(Fn&& fn) noexcept{
		std::scoped_lock lock{mutex};
		if(system != nullptr){
			std::invoke(std::forward<Fn>(fn), *system);
		}
	}
};

audio_resource::~audio_resource(){
	release();
}

void audio_resource::release() noexcept{
	const auto handle = std::exchange(handle_, nullptr);
	const auto system = std::exchange(system_, nullptr);
	if(handle != nullptr && system != nullptr){
		system->release_backend_resource_(handle);
	}
}

void audio_resource_deleter::operator()(audio_resource* resource) const noexcept{
	delete resource;
}

playback_control::playback_control(
	std::shared_ptr<audio_control_sink> control_sink,
	const channel_id channel,
	const playback_id playback,
	const playback_control_options options) noexcept
	: control_sink_(std::move(control_sink)),
	  channel_(channel),
	  playback_(playback.value),
	  release_policy_(options.release_policy){
}

playback_control::~playback_control(){
	release_(release_policy());
}

void playback_control::set_release_policy(const playback_release_policy policy){
	release_policy_.store(policy, std::memory_order_release);
}

void playback_control::release_(const playback_release_policy policy) noexcept{
	const playback_id playback{playback_.exchange(0, std::memory_order_acq_rel)};
	if(!playback){
		return;
	}
	if(control_sink_){
		control_sink_->with_system([&](audio_system& system) noexcept{
			system.post_release_playback_(channel_, playback, policy);
		});
	}
}

void playback_control::stop(){
	release_(playback_release_policy::stop_on_release);
}

void playback_control::detach(){
	release_(playback_release_policy::detach_on_release);
}

void playback_control::pause() const{
	const auto playback = id();
	if(!playback){
		return;
	}
	if(control_sink_){
		control_sink_->with_system([&](audio_system& system) noexcept{
			system.post_pause_playback_(channel_, playback);
		});
	}
}

void playback_control::resume() const{
	const auto playback = id();
	if(!playback){
		return;
	}
	if(control_sink_){
		control_sink_->with_system([&](audio_system& system) noexcept{
			system.post_resume_playback_(channel_, playback);
		});
	}
}

void playback_control::set_params(voice_params params) const{
	const auto playback = id();
	if(!playback || params.empty()){
		return;
	}
	if(control_sink_){
		control_sink_->with_system([&](audio_system& system) noexcept{
			system.post_set_playback_params_(channel_, playback, params);
		});
	}
}

void playback_control_deleter::operator()(playback_control* control) const noexcept{
	delete control;
}

void audio_channel::require_owner_thread(const char* operation) const{
	if(system_ == nullptr || !id_){
		throw std::runtime_error{std::format("{} called on an empty audio channel", operation)};
	}
	if(!is_owner_thread()){
		throw std::runtime_error{std::format("{} called from a non-owner audio channel thread", operation)};
	}
	assert(is_owner_thread());
}

bool audio_channel::play_detached(const resource_handle resource, play_settings settings) const{
	if(!resource){
		return false;
	}
	require_owner_thread("audio_channel::play_detached");
	return system_->post_play_detached_(id_, resource, std::move(settings));
}

playback_control_handle audio_channel::play_controlled(
	const resource_handle resource,
	play_settings settings,
	playback_control_options options) const{
	if(!resource){
		return {};
	}
	require_owner_thread("audio_channel::play_controlled");
	return system_->post_play_controlled_(id_, resource, std::move(settings), options);
}

void audio_channel::set_volume(const float volume) const{
	require_owner_thread("audio_channel::set_volume");
	system_->post_set_channel_volume_(id_, volume);
}

void audio_system::start(){
	worker_ = std::jthread{[this](std::stop_token stop_token){
		run(stop_token);
	}};
	loader_ = std::jthread{[this](std::stop_token stop_token){
		run_loader(stop_token);
	}};
}

const audio_system::channel_slot* audio_system::find_channel_slot_(const channel_id channel) const noexcept{
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

audio_system::channel_registration audio_system::reserve_channel_slot_(
	const channel_id channel,
	const std::thread::id owner_thread){
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
		std::this_thread::yield();
	}
}

void audio_system::require_registered_channel_(const channel_id channel) const{
	if(!channel){
		throw std::invalid_argument{"audio channel id must not be empty"};
	}
	const auto* slot = find_channel_slot_(channel);
	if(slot == nullptr){
		throw std::runtime_error{"audio channel is not registered"};
	}
	if(slot->owner_thread != std::this_thread::get_id()){
		throw std::runtime_error{"audio channel is bound to another thread"};
	}
}

bool audio_system::post(cmd::command command) noexcept{
	cmd::command_batch batch{};
	try{
		batch.push_back(std::move(command));
	}catch(...){
		return false;
	}
	return post(std::move(batch));
}

bool audio_system::post(cmd::command_batch batch) noexcept{
	if(!accepting_.load(std::memory_order_acquire)){
		return false;
	}
	if(batch.empty()){
		return true;
	}

	try{
		commands_.push(std::move(batch));
		wake_worker_();
		return true;
	}catch(...){
		return false;
	}
}

bool audio_system::post_channel_command_(const channel_id channel, cmd::command command) noexcept{
	if(!channel || !valid()){
		return false;
	}
	return post(std::move(command));
}

void audio_system::detach_control_sink_() noexcept{
	if(control_sink_){
		control_sink_->detach(*this);
	}
}

void audio_system::push_event(audio_event event){
	events_.push(std::move(event));
}

void audio_system::push_events(std::vector<audio_event>& new_events){
	for(auto& event : new_events){
		events_.push(std::move(event));
	}
	new_events.clear();
}

void audio_system::pop_events(std::vector<audio_event>& out){
	out.clear();
	if(auto fetched = events_.fetch()){
		out.reserve(fetched->size());
		for(auto& event : *fetched){
			out.push_back(std::move(event));
		}
	}
}

void audio_system::poll_events_into(std::vector<audio_event>& out){
	pop_events(out);
}

void audio_system::run(const std::stop_token& stop_token){
	for(;;){
		if(stop_token.stop_requested()){
			accepting_.store(false, std::memory_order_release);
		}

		bool processed_command = false;
		while(auto batch = commands_.try_consume()){
			processed_command = true;
			process(std::move(*batch));
		}
		processed_command = process_load_completions_() || processed_command;
		if(accepting_.load(std::memory_order_acquire)){
			processed_command = process_pending_loads_() || processed_command;
		}
		drain_deferred_backend_releases_();

		std::vector<audio_event> driver_events{};
		try{
			driver_->update(driver_events);
		}catch(...){
			driver_events.push_back(audio_event{
				.type = audio_event_type::backend_error
			});
		}
		push_events(driver_events);

		if(!accepting_.load(std::memory_order_acquire) &&
			commands_.empty() &&
			load_completions_.empty() &&
			active_load_count_ == 0){
			break;
		}
		if(!processed_command){
			static_cast<void>(worker_wake_.try_acquire_for(audio_worker_idle_tick));
			while(worker_wake_.try_acquire()){
			}
		}
	}

	events_.clear();
	drain_deferred_backend_releases_();
	for(auto&& [resource, backend_resource] : resources_){
		(void)resource;
		if(backend_resource){
			backend_resource->release();
		}
	}
	resources_.clear();
	if(driver_){
		driver_->shutdown();
	}
}

void audio_system::run_loader(const std::stop_token& stop_token){
	while(!stop_token.stop_requested()){
		auto command = load_requests_.consume([&] noexcept{
			return stop_token.stop_requested();
		});
		if(!command){
			break;
		}
		process_load_request_(std::move(*command));
	}
}

void audio_system::process(cmd::command_batch batch){
	for(auto& command : batch){
		if(std::holds_alternative<cmd::shutdown_audio>(command)){
			accepting_.store(false, std::memory_order_release);
			cancel_pending_loads_();
			break;
		}
		process(std::move(command));
	}
}

void audio_system::process(cmd::command command){
	std::visit(overload{
		[this](cmd::load_resource command){
			enqueue_load_(std::move(command));
		},
		[this](const cmd::unload_resource& command){
			if(!command.resource){
				return;
			}

			if(pending_load_resources_.erase(command.resource) != 0){
				fail_pending_playbacks_(command.resource);
			}

			audio_resource_ptr backend_resource{};
			if(const auto resource_iter = resources_.find(command.resource); resource_iter != resources_.end()){
				backend_resource = std::move(resource_iter->second);
				resources_.erase(resource_iter);
				if(backend_resource){
					backend_resource->release();
				}
			}
			push_event(audio_event{
				.type = audio_event_type::resource_unloaded,
				.resource = command.resource
			});
		},
		[this](const cmd::release_backend_resource& command){
			if(command.handle != nullptr && driver_ != nullptr){
				driver_->release_resource(command.handle);
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
		[this](const cmd::play_detached& command){
			if(!command.resource || !command.channel){
				return;
			}

			const auto resource = resources_.find(command.resource);
			if(resource == resources_.end()){
				if(is_pending_load_(command.resource)){
					pending_playbacks_[command.resource].push_back(command);
					return;
				}
				push_event(audio_event{
					.type = audio_event_type::playback_failed,
					.resource = command.resource
				});
				return;
			}

			try{
				driver_->play_detached(*resource->second, command.channel, command.settings);
			}catch(...){
				push_event(audio_event{
					.type = audio_event_type::playback_failed,
					.resource = command.resource
				});
			}
		},
		[this](const cmd::play_controlled& command){
			if(!command.resource || !command.channel || !command.playback){
				return;
			}

			const auto resource = resources_.find(command.resource);
			if(resource == resources_.end()){
				if(is_pending_load_(command.resource)){
					pending_playbacks_[command.resource].push_back(command);
					return;
				}
				push_event(audio_event{
					.type = audio_event_type::playback_failed,
					.resource = command.resource,
					.playback = command.playback
				});
				return;
			}

			try{
				driver_->play_controlled(*resource->second, command.channel, command.playback, command.settings);
				push_event(audio_event{
					.type = audio_event_type::playback_started,
					.resource = command.resource,
					.playback = command.playback
				});
			}catch(...){
				push_event(audio_event{
					.type = audio_event_type::playback_failed,
					.resource = command.resource,
					.playback = command.playback
				});
			}
		},
		[this](const cmd::pause_playback& command){
			driver_->pause(command.playback);
		},
		[this](const cmd::resume_playback& command){
			driver_->resume(command.playback);
		},
		[this](const cmd::set_playback_params& command){
			if(apply_pending_playback_params_(command.playback, command.params)){
				return;
			}
			driver_->set_playback_params(command.playback, command.params);
		},
		[this](const cmd::release_playback& command){
			if(!command.playback){
				return;
			}
			if(release_pending_playback_(command.playback, command.policy)){
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
			if(!command.channel){
				return;
			}
			driver_->set_channel_volume(command.channel, command.volume);
		},
		[](const cmd::shutdown_audio&) noexcept{
		}
	}, std::move(command));
}

void audio_system::enqueue_load_(cmd::load_resource command){
	if(!command.resource){
		return;
	}
	if(command.priority == lazy_audio_load_priority){
		push_event(audio_event{
			.type = audio_event_type::resource_failed,
			.resource = command.resource
		});
		return;
	}
	command.sequence = next_load_sequence_++;
	pending_load_resources_.insert(command.resource);
	pending_loads_.push_back(std::move(command));
	std::push_heap(pending_loads_.begin(), pending_loads_.end(), pending_load_less{});
}

bool audio_system::process_pending_loads_(){
	bool processed{};
	while(active_load_count_ < max_concurrent_audio_loads && !pending_loads_.empty()){
		std::pop_heap(pending_loads_.begin(), pending_loads_.end(), pending_load_less{});
		auto command = std::move(pending_loads_.back());
		pending_loads_.pop_back();
		if(!pending_load_resources_.contains(command.resource)){
			fail_pending_playbacks_(command.resource);
			continue;
		}
		try{
			load_requests_.push(std::move(command));
			++active_load_count_;
		}catch(...){
			pending_load_resources_.erase(command.resource);
			push_event(audio_event{
				.type = audio_event_type::resource_failed,
				.resource = command.resource
			});
			fail_pending_playbacks_(command.resource);
		}
		processed = true;
	}
	return processed;
}

bool audio_system::process_load_completions_(){
	bool processed{};
	while(auto completion = load_completions_.try_consume()){
		process_load_completion_(std::move(*completion));
		processed = true;
	}
	return processed;
}

void audio_system::process_load_request_(cmd::load_resource command) noexcept{
	load_completion completion{
		.resource = command.resource
	};
	try{
		completion.result = driver_->load_resource(std::move(command.desc));
	}catch(...){
	}

	try{
		load_completions_.push(std::move(completion));
		wake_worker_();
	}catch(...){
		if(completion.result.handle != nullptr && driver_ != nullptr){
			driver_->release_resource(completion.result.handle);
		}
	}
}

void audio_system::process_load_completion_(load_completion completion){
	if(active_load_count_ != 0){
		--active_load_count_;
	}

	auto loaded_resource = std::move(completion.result);
	if(pending_load_resources_.erase(completion.resource) == 0){
		if(loaded_resource.handle != nullptr){
			release_backend_resource_(loaded_resource.handle);
		}
		return;
	}

	if(!loaded_resource){
		push_event(audio_event{
			.type = audio_event_type::resource_failed,
			.resource = completion.resource
		});
		fail_pending_playbacks_(completion.resource);
		return;
	}

	const auto native_handle = std::exchange(loaded_resource.handle, nullptr);
	audio_resource_ptr resource{};
	try{
		resource = audio_resource_ptr{new audio_resource{this, native_handle}};
	}catch(...){
		release_backend_resource_(native_handle);
		push_event(audio_event{
			.type = audio_event_type::resource_failed,
			.resource = completion.resource
		});
		fail_pending_playbacks_(completion.resource);
		return;
	}

	resources_.erase(completion.resource);
	auto [iter, inserted] = resources_.emplace(completion.resource, resource);
	(void)inserted;
	push_event(audio_event{
		.type = audio_event_type::resource_loaded,
		.resource = completion.resource,
		.backend_resource = iter->second,
		.backend_handle = loaded_resource.token,
		.backend_metadata = std::move(loaded_resource.metadata)
	});
	flush_pending_playbacks_(completion.resource);
}

bool audio_system::is_pending_load_(const resource_handle resource) const noexcept{
	return pending_load_resources_.contains(resource);
}

void audio_system::flush_pending_playbacks_(const resource_handle resource){
	const auto iter = pending_playbacks_.find(resource);
	if(iter == pending_playbacks_.end()){
		return;
	}
	auto pending = std::move(iter->second);
	pending_playbacks_.erase(iter);
	for(auto& playback : pending){
		std::visit([this](auto& command){
			this->process(cmd::command{std::move(command)});
		}, playback);
	}
}

void audio_system::fail_pending_playbacks_(const resource_handle resource){
	const auto iter = pending_playbacks_.find(resource);
	if(iter == pending_playbacks_.end()){
		return;
	}
	auto pending = std::move(iter->second);
	pending_playbacks_.erase(iter);
	for(auto& playback : pending){
		std::visit([this](auto& command){
			using command_type = std::decay_t<decltype(command)>;
			audio_event event{
				.type = audio_event_type::playback_failed,
				.resource = command.resource
			};
			if constexpr(std::same_as<command_type, cmd::play_controlled>){
				event.playback = command.playback;
			}
			this->push_event(std::move(event));
		}, playback);
	}
}

void audio_system::cancel_pending_loads_() noexcept{
	std::vector<resource_handle> resources{};
	resources.reserve(pending_load_resources_.size());
	for(const auto resource : pending_load_resources_){
		resources.push_back(resource);
	}
	for(const auto resource : resources){
		fail_pending_playbacks_(resource);
	}
	pending_load_resources_.clear();
	pending_loads_.clear();
}

bool audio_system::release_pending_playback_(
	const playback_id playback,
	const playback_release_policy policy){
	if(!playback){
		return false;
	}

	for(auto resource_iter = pending_playbacks_.begin(); resource_iter != pending_playbacks_.end(); ++resource_iter){
		auto& pending = resource_iter->second;
		for(auto playback_iter = pending.begin(); playback_iter != pending.end(); ++playback_iter){
			auto* command = std::get_if<cmd::play_controlled>(std::addressof(*playback_iter));
			if(command == nullptr || command->playback != playback){
				continue;
			}

			if(policy == playback_release_policy::detach_on_release){
				*playback_iter = cmd::play_detached{
					.resource = command->resource,
					.channel = command->channel,
					.settings = command->settings
				};
			}else{
				pending.erase(playback_iter);
				if(pending.empty()){
					pending_playbacks_.erase(resource_iter);
				}
			}
			return true;
		}
	}
	return false;
}

bool audio_system::apply_pending_playback_params_(
	const playback_id playback,
	const voice_params params) noexcept{
	if(!playback || params.empty()){
		return false;
	}

	for(auto& pending : pending_playbacks_ | std::views::values){
		for(auto& playback_command : pending){
			auto* command = std::get_if<cmd::play_controlled>(std::addressof(playback_command));
			if(command == nullptr || command->playback != playback){
				continue;
			}
			if(params.has_volume()){
				command->settings.volume = params.volume;
			}
			if(params.has_pan()){
				command->settings.pan = params.pan;
			}
			if(params.has_pitch()){
				command->settings.pitch = params.pitch;
			}
			if(params.has_loop()){
				command->settings.loop = params.loop;
			}
			return true;
		}
	}
	return false;
}

void audio_system::release_backend_resource_(const backend_resource_handle handle) noexcept{
	if(handle == nullptr || driver_ == nullptr){
		return;
	}
	if(worker_.joinable() && worker_.get_id() != std::this_thread::get_id()){
		if(post(cmd::release_backend_resource{.handle = handle})){
			return;
		}
		defer_backend_resource_release_(handle);
		commands_.notify();
		return;
	}
	if(worker_.joinable() && worker_.get_id() == std::this_thread::get_id()){
		driver_->release_resource(handle);
		return;
	}
	driver_->release_resource(handle);
}

void audio_system::defer_backend_resource_release_(const backend_resource_handle handle) noexcept{
	if(handle == nullptr){
		return;
	}
	try{
		std::scoped_lock lock{deferred_backend_releases_mutex_};
		deferred_backend_releases_.push_back(handle);
	}catch(...){
	}
}

void audio_system::drain_deferred_backend_releases_() noexcept{
	if(driver_ == nullptr){
		return;
	}
	std::vector<backend_resource_handle> handles{};
	{
		std::scoped_lock lock{deferred_backend_releases_mutex_};
		handles = std::move(deferred_backend_releases_);
		deferred_backend_releases_.clear();
	}
	for(const auto handle : handles){
		if(handle != nullptr){
			driver_->release_resource(handle);
		}
	}
}

bool audio_system::post_play_detached_(
	const channel_id channel,
	const resource_handle resource,
	play_settings settings){
	if(!resource || !valid()){
		return false;
	}
	require_registered_channel_(channel);
	return post_channel_command_(channel, cmd::play_detached{
		.resource = resource,
		.channel = channel,
		.settings = std::move(settings)
	});
}

playback_control_handle audio_system::post_play_controlled_(
	const channel_id channel,
	const resource_handle resource,
	play_settings settings,
	playback_control_options options){
	if(!resource || !valid()){
		return {};
	}
	require_registered_channel_(channel);

	const auto playback = allocate_playback();
	playback_control_handle control{};
	try{
		control = playback_control_handle{new playback_control{control_sink_, channel, playback, options}};
	}catch(...){
		return {};
	}

	if(post_channel_command_(channel, cmd::play_controlled{
		.resource = resource,
		.channel = channel,
		.playback = playback,
		.settings = std::move(settings)
	})){
		return control;
	}

	control->invalidate_();
	return {};
}

void audio_system::post_set_channel_volume_(const channel_id channel, const float volume){
	require_registered_channel_(channel);
	(void)(post_channel_command_(channel, cmd::set_channel_volume{
		.channel = channel,
		.volume = volume
	}));
}

void audio_system::post_pause_playback_(const channel_id channel, const playback_id playback) noexcept{
	if(!playback){
		return;
	}
	(void)(post_channel_command_(channel, cmd::pause_playback{.playback = playback}));
}

void audio_system::post_resume_playback_(const channel_id channel, const playback_id playback) noexcept{
	if(!playback){
		return;
	}
	(void)(post_channel_command_(channel, cmd::resume_playback{.playback = playback}));
}

void audio_system::post_set_playback_params_(
	const channel_id channel,
	const playback_id playback,
	voice_params params) noexcept{
	if(!playback || params.empty()){
		return;
	}
	(void)(post_channel_command_(channel, cmd::set_playback_params{
		.playback = playback,
		.params = params
	}));
}

void audio_system::post_release_playback_(
	const channel_id channel,
	const playback_id playback,
	const playback_release_policy policy) noexcept{
	if(!playback){
		return;
	}
	(void)(post_channel_command_(channel, cmd::release_playback{
		.playback = playback,
		.policy = policy
	}));
}

audio_system::audio_system(std::unique_ptr<audio_driver_backend> driver)
	: driver_(std::move(driver)){
	if(driver_ == nullptr){
		driver_ = make_null_audio_driver();
	}
	control_sink_ = std::make_shared<audio_control_sink>(*this);
	start();
}

audio_system::~audio_system(){
	shutdown();
}

resource_handle audio_system::load(load_desc desc, const audio_load_priority priority){
	if(!valid() || priority == lazy_audio_load_priority){
		return {};
	}

	const auto resource = allocate_resource();
	if(post(cmd::load_resource{
		.resource = resource,
		.desc = std::move(desc),
		.priority = priority
	})){
		return resource;
	}
	return {};
}

void audio_system::unload(const resource_handle resource) noexcept{
	if(!resource){
		return;
	}
	(void)(post(cmd::unload_resource{
		.resource = resource
	}));
}

audio_channel audio_system::register_channel(const channel_id channel){
	if(!valid()){
		throw std::runtime_error{"audio system is not accepting channel registrations"};
	}

	const auto owner_thread = std::this_thread::get_id();
	auto registration = reserve_channel_slot_(channel, owner_thread);
	if(registration.inserted){
		if(!post(cmd::register_channel{.channel = channel})){
			rollback_channel_slot_(*registration.slot);
			throw std::runtime_error{"audio channel registration command was not accepted"};
		}
		commit_channel_slot_(*registration.slot, channel);
	}
	return audio_channel{*this, channel, owner_thread};
}

std::optional<audio_channel> audio_system::get_channel(const channel_id channel) noexcept{
	if(!channel || !valid()){
		return std::nullopt;
	}
	const auto owner_thread = std::this_thread::get_id();
	const auto* slot = find_channel_slot_(channel);
	if(slot == nullptr){
		return std::nullopt;
	}
	if(slot->owner_thread != owner_thread){
		return std::nullopt;
	}
	return audio_channel{*this, channel, owner_thread};
}

void audio_system::shutdown() noexcept{
	const bool was_accepting = accepting_.exchange(false, std::memory_order_acq_rel);
	if(loader_.joinable()){
		loader_.request_stop();
		load_requests_.notify();
		if(loader_.get_id() != std::this_thread::get_id()){
			loader_.join();
			load_requests_.clear();
		}
	}
	if(was_accepting){
		try{
			cmd::command_batch batch{};
			batch.push_back(cmd::shutdown_audio{});
			commands_.push(std::move(batch));
			wake_worker_();
		}catch(...){
		}
	}

	if(worker_.joinable()){
		wake_worker_();
		if(worker_.get_id() != std::this_thread::get_id()){
			worker_.join();
			commands_.clear();
		}
	}
	detach_control_sink_();
	drain_deferred_backend_releases_();
	events_.clear();
}

}
