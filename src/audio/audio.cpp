module;

#include <cassert>

module mo_yanxi.audio;

import std;
import mo_yanxi.utility;

namespace mo_yanxi::audio{

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

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
	audio_system& system,
	const playback_id playback,
	const playback_control_options options) noexcept
	: system_(std::addressof(system)),
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
	assert(is_owner_thread());
	const playback_id playback = std::exchange(playback_, {});
	if(!playback){
		return;
	}
	if(system_ != nullptr){
		system_->post_release_playback_(playback, policy);
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
	if(system_ != nullptr){
		system_->post_pause_playback_(playback);
	}
}

void playback_control::resume() const{
	require_owner_thread("playback_control::resume");
	const auto playback = id();
	if(!playback){
		return;
	}
	if(system_ != nullptr){
		system_->post_resume_playback_(playback);
	}
}

void playback_control::set_params(voice_params params) const{
	require_owner_thread("playback_control::set_params");
	const auto playback = id();
	if(!playback || params.empty()){
		return;
	}
	if(system_ != nullptr){
		system_->post_set_playback_params_(playback, params);
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
	if(!accepting_.load(std::memory_order_acquire)){
		return false;
	}

	try{
		commands_.push(std::move(command));
		return true;
	}catch(...){
		return false;
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

void audio_system::run(const std::stop_token& stop_token){
	while(!stop_token.stop_requested()){
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

		std::vector<audio_event> driver_events{};
		try{
			driver_->update(driver_events);
		}catch(...){
			driver_events.push_back(audio_event{
				.type = audio_event_type::backend_error
			});
		}
		push_events(driver_events);

		if(!accepting_.load(std::memory_order_acquire) && commands_.empty()){
			break;
		}
		if(!processed_command){
			std::this_thread::sleep_for(std::chrono::milliseconds{16});
		}
	}

	events_.clear();
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
			if(auto node = resources_.extract(command.resource); node){
				backend_resource = std::move(node.mapped());
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
		[this](const cmd::stop_playback& command){
			driver_->stop(command.playback);
		},
		[this](const cmd::pause_playback& command){
			driver_->pause(command.playback);
		},
		[this](const cmd::resume_playback& command){
			driver_->resume(command.playback);
		},
		[this](const cmd::set_playback_params& command){
			driver_->set_playback_params(command.playback, command.params);
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
	while(!pending_loads_.empty()){
		std::pop_heap(pending_loads_.begin(), pending_loads_.end(), pending_load_less{});
		auto command = std::move(pending_loads_.back());
		pending_loads_.pop_back();
		if(pending_load_resources_.erase(command.resource) == 0){
			fail_pending_playbacks_(command.resource);
			continue;
		}
		process_load_(std::move(command));
		processed = true;
	}
	return processed;
}

void audio_system::process_load_(cmd::load_resource command){
	try{
		auto loaded_resource = driver_->load_resource(std::move(command.desc));
		if(!loaded_resource){
			push_event(audio_event{
				.type = audio_event_type::resource_failed,
				.resource = command.resource
			});
			fail_pending_playbacks_(command.resource);
			return;
		}

		const auto native_handle = std::exchange(loaded_resource.handle, nullptr);
		audio_resource_ptr resource{};
		try{
			resource = audio_resource_ptr{new audio_resource{this, native_handle}};
		}catch(...){
			release_backend_resource_(native_handle);
			throw;
		}

		resources_.erase(command.resource);
		auto [iter, inserted] = resources_.emplace(command.resource, resource);
		(void)inserted;
		push_event(audio_event{
			.type = audio_event_type::resource_loaded,
			.resource = command.resource,
			.backend_resource = iter->second,
			.backend_handle = loaded_resource.token,
			.backend_metadata = std::move(loaded_resource.metadata)
		});
		flush_pending_playbacks_(command.resource);
	}catch(...){
		push_event(audio_event{
			.type = audio_event_type::resource_failed,
			.resource = command.resource
		});
		fail_pending_playbacks_(command.resource);
	}
}

bool audio_system::is_pending_load_(const resource_handle resource) const noexcept{
	return pending_load_resources_.contains(resource);
}

void audio_system::flush_pending_playbacks_(const resource_handle resource){
	if(auto node = pending_playbacks_.extract(resource)){
		for(auto& playback : node.mapped()){
			std::visit([this](auto& command){
				this->process(cmd::command{std::move(command)});
			}, playback);
		}
	}
}

void audio_system::fail_pending_playbacks_(const resource_handle resource){
	if(auto node = pending_playbacks_.extract(resource)){
		for(auto& playback : node.mapped()){
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
}

void audio_system::release_backend_resource_(const backend_resource_handle handle) noexcept{
	if(handle == nullptr || driver_ == nullptr){
		return;
	}
	if(worker_.joinable() && worker_.get_id() != std::this_thread::get_id()){
		if(post(cmd::release_backend_resource{.handle = handle})){
			commands_.notify();
			return;
		}
	}
	if(worker_.joinable() && worker_.get_id() == std::this_thread::get_id()){
		driver_->release_resource(handle);
		return;
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
	return post(cmd::play_detached{
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
		control = playback_control_handle{new playback_control{*this, playback, options}};
	}catch(...){
		return {};
	}

	if(post(cmd::play_controlled{
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
	static_cast<void>(post(cmd::set_channel_volume{
		.channel = channel,
		.volume = volume
	}));
}

void audio_system::post_stop_playback_(const playback_id playback) noexcept{
	if(!playback){
		return;
	}
	static_cast<void>(post(cmd::stop_playback{.playback = playback}));
}

void audio_system::post_detach_playback_(const playback_id playback) noexcept{
	if(!playback){
		return;
	}
	static_cast<void>(post(cmd::release_playback{
		.playback = playback,
		.policy = playback_release_policy::detach_on_release
	}));
}

void audio_system::post_pause_playback_(const playback_id playback) noexcept{
	if(!playback){
		return;
	}
	static_cast<void>(post(cmd::pause_playback{.playback = playback}));
}

void audio_system::post_resume_playback_(const playback_id playback) noexcept{
	if(!playback){
		return;
	}
	static_cast<void>(post(cmd::resume_playback{.playback = playback}));
}

void audio_system::post_set_playback_params_(const playback_id playback, voice_params params) noexcept{
	if(!playback || params.empty()){
		return;
	}
	static_cast<void>(post(cmd::set_playback_params{
		.playback = playback,
		.params = params
	}));
}

void audio_system::post_release_playback_(
	const playback_id playback,
	const playback_release_policy policy) noexcept{
	if(!playback){
		return;
	}
	static_cast<void>(post(cmd::release_playback{
		.playback = playback,
		.policy = policy
	}));
}

audio_system::audio_system(std::unique_ptr<audio_driver_backend> driver)
	: driver_(std::move(driver)){
	if(driver_ == nullptr){
		driver_ = make_null_audio_driver();
	}
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
	static_cast<void>(post(cmd::unload_resource{
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

audio_channel audio_system::get_channel(const channel_id channel){
	if(!channel){
		throw std::invalid_argument{"audio channel id must not be empty"};
	}

	const auto owner_thread = std::this_thread::get_id();
	const auto* slot = find_channel_slot_(channel);
	if(slot == nullptr){
		throw std::runtime_error{"audio channel is not registered"};
	}
	if(slot->owner_thread != owner_thread){
		throw std::runtime_error{"audio channel is bound to another thread"};
	}
	return audio_channel{*this, channel, owner_thread};
}

void audio_system::shutdown() noexcept{
	const bool was_accepting = accepting_.exchange(false, std::memory_order_acq_rel);
	if(was_accepting){
		try{
			commands_.push(cmd::shutdown_audio{});
			commands_.notify();
		}catch(...){
		}
	}

	if(worker_.joinable()){
		worker_.request_stop();
		commands_.notify();
		if(worker_.get_id() != std::this_thread::get_id()){
			worker_.join();
			commands_.clear();
		}
	}
	events_.clear();
}

}
