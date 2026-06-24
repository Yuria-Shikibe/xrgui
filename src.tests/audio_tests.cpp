#include <gtest/gtest.h>

import std;
import mo_yanxi.audio;
import mo_yanxi.audio.resources;

namespace {

using namespace std::chrono_literals;

struct fake_driver_state {
	std::mutex mutex{};
	std::condition_variable cv{};
	int loads{};
	int unloads{};
	int detached_plays{};
	int controlled_plays{};
	int stops{};
	int detaches{};
	int pauses{};
	int resumes{};
	int param_sets{};
	bool saw_memory_load{};
	std::string last_debug_name{};
	mo_yanxi::audio::play_settings last_detached_settings{};
	mo_yanxi::audio::play_settings last_controlled_settings{};
	mo_yanxi::audio::channel_id last_detached_channel{};
	mo_yanxi::audio::channel_id last_controlled_channel{};
	mo_yanxi::audio::voice_params last_params{};
	std::vector<mo_yanxi::audio::playback_id> controlled_playbacks{};
	std::vector<mo_yanxi::audio::playback_id> stopped_playbacks{};
	std::vector<mo_yanxi::audio::playback_id> detached_playbacks{};
	std::vector<std::string> load_debug_names{};
	bool block_next_load{};
	bool blocked_load_started{};
	bool release_blocked_load{};

	void record_load(const mo_yanxi::audio::load_desc& desc) {
		std::scoped_lock lock{mutex};
		++loads;
		saw_memory_load = saw_memory_load || desc.source_kind() == mo_yanxi::audio::load_source_kind::memory;
		last_debug_name = desc.debug_name;
		load_debug_names.push_back(desc.debug_name);
	}

	void block_one_load() {
		std::scoped_lock lock{mutex};
		block_next_load = true;
		blocked_load_started = false;
		release_blocked_load = false;
	}

	bool wait_for_blocked_load() {
		std::unique_lock lock{mutex};
		return cv.wait_for(lock, 2s, [&] {
			return blocked_load_started;
		});
	}

	void release_load() {
		{
			std::scoped_lock lock{mutex};
			release_blocked_load = true;
		}
		cv.notify_all();
	}

	void wait_if_blocking_load() {
		std::unique_lock lock{mutex};
		if(!block_next_load) {
			return;
		}
		block_next_load = false;
		blocked_load_started = true;
		cv.notify_all();
		cv.wait(lock, [&] {
			return release_blocked_load;
		});
	}

	void record_unload() {
		std::scoped_lock lock{mutex};
		++unloads;
	}

	void record_detached_play(
		const mo_yanxi::audio::channel_id channel,
		const mo_yanxi::audio::play_settings& settings) {
		std::scoped_lock lock{mutex};
		++detached_plays;
		last_detached_channel = channel;
		last_detached_settings = settings;
	}

	void record_controlled_play(
		const mo_yanxi::audio::channel_id channel,
		const mo_yanxi::audio::playback_id playback,
		const mo_yanxi::audio::play_settings& settings) {
		std::scoped_lock lock{mutex};
		++controlled_plays;
		last_controlled_channel = channel;
		last_controlled_settings = settings;
		controlled_playbacks.push_back(playback);
	}

	void record_stop(const mo_yanxi::audio::playback_id playback) {
		std::scoped_lock lock{mutex};
		++stops;
		stopped_playbacks.push_back(playback);
	}

	void record_detach(const mo_yanxi::audio::playback_id playback) {
		std::scoped_lock lock{mutex};
		++detaches;
		detached_playbacks.push_back(playback);
	}

	void record_pause() {
		std::scoped_lock lock{mutex};
		++pauses;
	}

	void record_resume() {
		std::scoped_lock lock{mutex};
		++resumes;
	}

	void record_params(const mo_yanxi::audio::voice_params& params) {
		std::scoped_lock lock{mutex};
		++param_sets;
		last_params = params;
	}

	template <typename Fn>
	decltype(auto) read(Fn&& fn) {
		std::scoped_lock lock{mutex};
		return std::invoke(std::forward<Fn>(fn), *this);
	}
};

class fake_driver final : public mo_yanxi::audio::audio_driver_backend {
	std::shared_ptr<fake_driver_state> state_;
	std::uint64_t next_token_{1};

	struct fake_resource {
		std::shared_ptr<fake_driver_state> state{};
		mo_yanxi::audio::backend_resource_token token{};
	};

public:
	explicit fake_driver(std::shared_ptr<fake_driver_state> state)
		: state_(std::move(state)) {
	}

	mo_yanxi::audio::backend_resource_result load_resource(mo_yanxi::audio::load_desc desc) override {
		state_->record_load(desc);
		state_->wait_if_blocking_load();
		auto resource = std::make_unique<fake_resource>(fake_resource{
			.state = state_,
			.token = mo_yanxi::audio::backend_resource_token{next_token_++}
		});
		const auto token = resource->token;
		return mo_yanxi::audio::backend_resource_result{
			.handle = resource.release(),
			.token = token,
			.metadata = mo_yanxi::audio::make_audio_resource_metadata(desc)
		};
	}

	void release_resource(mo_yanxi::audio::backend_resource_handle handle) noexcept override {
		const std::unique_ptr<fake_resource> resource{static_cast<fake_resource*>(handle)};
		resource->state->record_unload();
	}

	void register_channel(mo_yanxi::audio::channel_id) override {
	}

	void play_detached(
		const mo_yanxi::audio::audio_resource&,
		mo_yanxi::audio::channel_id channel,
		const mo_yanxi::audio::play_settings& settings) override {
		state_->record_detached_play(channel, settings);
	}

	void play_controlled(
		const mo_yanxi::audio::audio_resource&,
		mo_yanxi::audio::channel_id channel,
		const mo_yanxi::audio::playback_id playback,
		const mo_yanxi::audio::play_settings& settings) override {
		state_->record_controlled_play(channel, playback, settings);
	}

	void stop(mo_yanxi::audio::playback_id playback) noexcept override {
		state_->record_stop(playback);
	}

	void detach(mo_yanxi::audio::playback_id playback) noexcept override {
		state_->record_detach(playback);
	}

	void pause(mo_yanxi::audio::playback_id) noexcept override {
		state_->record_pause();
	}

	void resume(mo_yanxi::audio::playback_id) noexcept override {
		state_->record_resume();
	}

	void set_playback_params(
		mo_yanxi::audio::playback_id,
		const mo_yanxi::audio::voice_params& params) noexcept override {
		state_->record_params(params);
	}

	void set_channel_volume(mo_yanxi::audio::channel_id, float) noexcept override {
	}

	void update(std::vector<mo_yanxi::audio::audio_event>&) override {
	}

	void shutdown() noexcept override {
	}
};

template <typename Predicate>
bool wait_until(Predicate&& predicate) {
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	while(std::chrono::steady_clock::now() < deadline) {
		if(std::invoke(predicate)) {
			return true;
		}
		std::this_thread::sleep_for(10ms);
	}
	return false;
}

template <typename Predicate>
bool wait_for_audio_event(mo_yanxi::audio::audio_system& system, Predicate&& predicate) {
	return wait_until([&] {
		bool matched = false;
		system.poll_events([&](const mo_yanxi::audio::audio_event& event) {
			if(std::invoke(predicate, event)) {
				matched = true;
			}
		});
		return matched;
	});
}

mo_yanxi::audio::audio_channel register_ui_channel(mo_yanxi::audio::audio_system& system) {
	return system.register_channel(mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::ui));
}

mo_yanxi::audio::load_desc named_file_desc(std::string name) {
	auto desc = mo_yanxi::audio::load_desc::from_file(name + ".wav");
	desc.debug_name = std::move(name);
	return desc;
}

} // namespace

static_assert(sizeof(mo_yanxi::audio::voice_params) <= 16);
static_assert(mo_yanxi::audio::audio_system::default_channel_capacity == 8);

TEST(AudioVoiceParams, TracksExplicitFieldsWithValidBits) {
	mo_yanxi::audio::voice_params params{};
	EXPECT_TRUE(params.empty());

	params.set_volume(0.5f).set_loop(true);
	EXPECT_TRUE(params.has_volume());
	EXPECT_FALSE(params.has_pan());
	EXPECT_FALSE(params.has_pitch());
	EXPECT_TRUE(params.has_loop());
	EXPECT_FLOAT_EQ(0.5f, params.volume);
	EXPECT_TRUE(params.loop);

	const auto all = mo_yanxi::audio::voice_params::all(0.75f, -0.25f, 1.25f, false);
	EXPECT_EQ(mo_yanxi::audio::voice_params::all_fields, all.valid_fields);
	EXPECT_FLOAT_EQ(0.75f, all.volume);
	EXPECT_FLOAT_EQ(-0.25f, all.pan);
	EXPECT_FLOAT_EQ(1.25f, all.pitch);
	EXPECT_FALSE(all.loop);
}

TEST(AudioSystem, DispatchesLoadControlledPlayAndUnloadEventsAsynchronously) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("click.wav"));
	ASSERT_TRUE(resource);
	EXPECT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	auto control = channel.play_controlled(resource);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	ASSERT_TRUE(playback);
	EXPECT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::playback_started &&
			event.playback == playback;
	}));

	system.unload(resource);
	EXPECT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_unloaded && event.resource == resource;
	}));
	EXPECT_EQ(1, fake_state->read([](const fake_driver_state& state) { return state.unloads; }));
}

TEST(AudioSystem, HigherPriorityLoadJumpsPendingLowerPriorityLoads) {
	auto fake_state = std::make_shared<fake_driver_state>();
	fake_state->block_one_load();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};

	const auto in_flight = system.load(named_file_desc("in-flight"), 1);
	ASSERT_TRUE(in_flight);
	ASSERT_TRUE(fake_state->wait_for_blocked_load());

	const auto low_a = system.load(named_file_desc("low-a"), 1);
	const auto high = system.load(named_file_desc("high"), 8);
	const auto low_b = system.load(named_file_desc("low-b"), 1);
	ASSERT_TRUE(low_a);
	ASSERT_TRUE(high);
	ASSERT_TRUE(low_b);

	fake_state->release_load();
	ASSERT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.load_debug_names.size() >= 4;
		});
	}));

	const auto order = fake_state->read([](const fake_driver_state& state) {
		return state.load_debug_names;
	});
	ASSERT_GE(order.size(), 4u);
	EXPECT_EQ("in-flight", order[0]);
	EXPECT_EQ("high", order[1]);
	EXPECT_EQ("low-a", order[2]);
	EXPECT_EQ("low-b", order[3]);
}

TEST(AudioSystem, PendingPlaybackWaitsForPrioritizedLoadAndFailsWhenCanceled) {
	auto fake_state = std::make_shared<fake_driver_state>();
	fake_state->block_one_load();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto in_flight = system.load(named_file_desc("in-flight"), 1);
	ASSERT_TRUE(in_flight);
	ASSERT_TRUE(fake_state->wait_for_blocked_load());

	const auto pending = system.load(named_file_desc("pending"), 1);
	ASSERT_TRUE(pending);
	auto control = channel.play_controlled(pending);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	system.unload(pending);

	fake_state->release_load();
	EXPECT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::playback_failed &&
			event.resource == pending &&
			event.playback == playback;
	}));
	EXPECT_EQ(1, fake_state->read([](const fake_driver_state& state) {
		return state.loads;
	}));
}

TEST(AudioSystem, ChannelsRequireExplicitRegistration) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};

	const auto ui_id = mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::ui);
	EXPECT_FALSE(system.get_channel(ui_id));

	const auto ui_channel = system.register_channel(ui_id);
	EXPECT_TRUE(ui_channel);
	EXPECT_TRUE(system.get_channel(ui_id));

	const auto master_channel = system.register_channel(
		mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::master));
	EXPECT_NO_THROW(master_channel.set_volume(0.5f));
}

TEST(AudioSystem, DetachedPlayDispatchesInitialSettingsWithoutControl) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("click.wav"));
	ASSERT_TRUE(resource);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	const auto channel = system.register_channel(
		mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::sfx));
	const mo_yanxi::audio::play_settings settings{
		.volume = 0.5f,
		.pan = -0.25f,
		.pitch = 1.5f,
		.loop = true
	};
	EXPECT_TRUE(channel.play_detached(resource, settings));

	EXPECT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.detached_plays == 1;
		});
	}));
	const auto recorded = fake_state->read([](const fake_driver_state& state) {
		return state.last_detached_settings;
	});
	EXPECT_EQ(
		mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::sfx),
		fake_state->read([](const fake_driver_state& state) { return state.last_detached_channel; }));
	EXPECT_FLOAT_EQ(0.5f, recorded.volume);
	EXPECT_FLOAT_EQ(-0.25f, recorded.pan);
	EXPECT_FLOAT_EQ(1.5f, recorded.pitch);
	EXPECT_TRUE(recorded.loop);
}

TEST(AudioSystem, ChannelCommandsDispatchWithoutExplicitFlush) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("click.wav"));
	ASSERT_TRUE(resource);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	const auto channel = system.register_channel(
		mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::sfx));
	ASSERT_TRUE(channel.play_detached(resource));
	EXPECT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.detached_plays == 1;
		});
	}));
}

TEST(AudioSystem, PlaybackCommandsAreNotBlockedByLoaderThread) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto ready = system.load(named_file_desc("ready"), 1);
	ASSERT_TRUE(ready);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == ready;
	}));

	fake_state->block_one_load();
	const auto blocked = system.load(named_file_desc("blocked"), 1);
	ASSERT_TRUE(blocked);
	ASSERT_TRUE(fake_state->wait_for_blocked_load());

	auto control = channel.play_controlled(ready);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	EXPECT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::playback_started &&
			event.playback == playback;
	}));
	EXPECT_EQ(1, fake_state->read([](const fake_driver_state& state) {
		return state.controlled_plays;
	}));

	fake_state->release_load();
}

TEST(AudioSystem, ShutdownWaitsForInFlightLoadAndReleasesResource) {
	auto fake_state = std::make_shared<fake_driver_state>();
	fake_state->block_one_load();
	{
		mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
		const auto resource = system.load(named_file_desc("shutdown-active"), 1);
		ASSERT_TRUE(resource);
		ASSERT_TRUE(fake_state->wait_for_blocked_load());

		std::jthread releaser{[&] {
			std::this_thread::sleep_for(20ms);
			fake_state->release_load();
		}};
		system.shutdown();
	}

	EXPECT_EQ(1, fake_state->read([](const fake_driver_state& state) {
		return state.loads;
	}));
	EXPECT_EQ(1, fake_state->read([](const fake_driver_state& state) {
		return state.unloads;
	}));
}

TEST(AudioSystem, PendingControlledPlaybackStopBeforeLoadPreventsStart) {
	auto fake_state = std::make_shared<fake_driver_state>();
	fake_state->block_one_load();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto pending = system.load(named_file_desc("pending-stop"), 1);
	ASSERT_TRUE(pending);
	ASSERT_TRUE(fake_state->wait_for_blocked_load());

	auto control = channel.play_controlled(pending);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	control->stop();
	EXPECT_FALSE(control->valid());

	fake_state->release_load();
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == pending;
	}));

	const auto controlled_playbacks = fake_state->read([](const fake_driver_state& state) {
		return state.controlled_playbacks;
	});
	EXPECT_FALSE(std::ranges::contains(controlled_playbacks, playback));
	EXPECT_EQ(0, fake_state->read([](const fake_driver_state& state) {
		return state.controlled_plays;
	}));
}

TEST(AudioSystem, PendingControlledPlaybackDetachBeforeLoadStartsDetached) {
	auto fake_state = std::make_shared<fake_driver_state>();
	fake_state->block_one_load();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto pending = system.load(named_file_desc("pending-detach"), 1);
	ASSERT_TRUE(pending);
	ASSERT_TRUE(fake_state->wait_for_blocked_load());

	auto control = channel.play_controlled(pending);
	ASSERT_TRUE(control);
	control->detach();
	EXPECT_FALSE(control->valid());

	fake_state->release_load();
	EXPECT_TRUE(wait_until([&] {
		system.poll_events([](const mo_yanxi::audio::audio_event&) {});
		return fake_state->read([](const fake_driver_state& state) {
			return state.detached_plays == 1;
		});
	}));
	EXPECT_EQ(0, fake_state->read([](const fake_driver_state& state) {
		return state.controlled_plays;
	}));
}

TEST(AudioSystem, ControlledPlayRoutesRuntimeOperationsThroughHandle) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("music.wav"));
	ASSERT_TRUE(resource);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	const auto channel = system.register_channel(
		mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::music));
	const mo_yanxi::audio::play_settings settings{
		.volume = 0.75f,
		.pitch = 1.1f,
		.loop = true
	};
	auto control = channel.play_controlled(resource, settings);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	ASSERT_TRUE(playback);

	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::playback_started &&
			event.playback == playback;
	}));

	control->set_volume(0.25f);
	control->pause();
	control->resume();
	control->stop();
	EXPECT_FALSE(control->valid());

	EXPECT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.param_sets == 1 && state.pauses == 1 && state.resumes == 1 && state.stops == 1;
		});
	}));
	EXPECT_EQ(playback, fake_state->read([](const fake_driver_state& state) {
		return state.stopped_playbacks.back();
	}));
	const auto params = fake_state->read([](const fake_driver_state& state) {
		return state.last_params;
	});
	EXPECT_TRUE(params.has_volume());
	EXPECT_FLOAT_EQ(0.25f, params.volume);
}

TEST(AudioSystem, LastControlReferenceStopsPlaybackByDefault) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("music.wav"));
	ASSERT_TRUE(resource);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	auto control = channel.play_controlled(resource);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	auto copy = control;
	control.reset();
	EXPECT_EQ(0, fake_state->read([](const fake_driver_state& state) {
		return state.stops;
	}));

	copy.reset();
	EXPECT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.stops == 1;
		});
	}));
	EXPECT_EQ(playback, fake_state->read([](const fake_driver_state& state) {
		return state.stopped_playbacks.back();
	}));
}

TEST(AudioSystem, LastControlReferenceCanDetachPlayback) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("ambient.wav"));
	ASSERT_TRUE(resource);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	auto control = channel.play_controlled(
		resource,
		{},
		mo_yanxi::audio::playback_control_options{
			.release_policy = mo_yanxi::audio::playback_release_policy::detach_on_release
		});
	ASSERT_TRUE(control);
	const auto playback = control->id();
	control.reset();

	EXPECT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.detaches == 1;
		});
	}));
	EXPECT_EQ(0, fake_state->read([](const fake_driver_state& state) {
		return state.stops;
	}));
	EXPECT_EQ(playback, fake_state->read([](const fake_driver_state& state) {
		return state.detached_playbacks.back();
	}));
}

TEST(AudioSystem, ExplicitDetachPreventsReleaseStop) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = register_ui_channel(system);

	const auto resource = system.load(mo_yanxi::audio::load_desc::from_file("ambient.wav"));
	ASSERT_TRUE(resource);
	ASSERT_TRUE(wait_for_audio_event(system, [&](const mo_yanxi::audio::audio_event& event) {
		return event.type == mo_yanxi::audio::audio_event_type::resource_loaded && event.resource == resource;
	}));

	auto control = channel.play_controlled(resource);
	ASSERT_TRUE(control);
	const auto playback = control->id();
	control->detach();
	EXPECT_FALSE(control->valid());
	control.reset();

	EXPECT_TRUE(wait_until([&] {
		return fake_state->read([](const fake_driver_state& state) {
			return state.detaches == 1;
		});
	}));
	EXPECT_EQ(0, fake_state->read([](const fake_driver_state& state) {
		return state.stops;
	}));
	EXPECT_EQ(playback, fake_state->read([](const fake_driver_state& state) {
		return state.detached_playbacks.back();
	}));
}

TEST(AudioSystem, ChannelRejectsNonOwnerThreadOperations) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto channel = system.register_channel(
		mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::sfx));

	bool set_volume_threw = false;
	std::exception_ptr unexpected{};
	std::jthread worker{[&] {
		try {
			channel.set_volume(0.5f);
		}catch(const std::runtime_error&) {
			set_volume_threw = true;
		}catch(...) {
			unexpected = std::current_exception();
		}
	}};
	worker.join();
	if(unexpected != nullptr) {
		std::rethrow_exception(unexpected);
	}
	EXPECT_TRUE(set_volume_threw);
}

TEST(AudioSystem, GetChannelReturnsEmptyForNonOwnerThread) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto id = mo_yanxi::audio::channel_id_from_role(mo_yanxi::audio::channel_role::sfx);
	static_cast<void>(system.register_channel(id));
	ASSERT_TRUE(system.get_channel(id));

	bool worker_found_channel = true;
	std::exception_ptr unexpected{};
	std::jthread worker{[&] {
		try {
			worker_found_channel = system.get_channel(id).has_value();
		}catch(...) {
			unexpected = std::current_exception();
		}
	}};
	worker.join();
	if(unexpected != nullptr) {
		std::rethrow_exception(unexpected);
	}
	EXPECT_FALSE(worker_found_channel);
}

TEST(AudioSystem, ChannelIdCannotBeRegisteredByAnotherThread) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};
	const auto id = mo_yanxi::audio::channel_id{42};
	static_cast<void>(system.register_channel(id));

	bool register_threw = false;
	std::exception_ptr unexpected{};
	std::jthread worker{[&] {
		try {
			static_cast<void>(system.register_channel(id));
		}catch(const std::runtime_error&) {
			register_threw = true;
		}catch(...) {
			unexpected = std::current_exception();
		}
	}};
	worker.join();
	if(unexpected != nullptr) {
		std::rethrow_exception(unexpected);
	}
	EXPECT_TRUE(register_threw);
}

TEST(AudioResourceManager, RegistersAndFindsResourcesByAssetId) {
	auto fake_state = std::make_shared<fake_driver_state>();
	mo_yanxi::audio::audio_system system{std::make_unique<fake_driver>(fake_state)};

	mo_yanxi::audio::audio_resource_manager manager{system};

	auto resource = manager.register_audio(
		mo_yanxi::audio::load_desc::from_file("click.wav"),
		mo_yanxi::audio::audio_resource_options{.load_priority = mo_yanxi::audio::lazy_audio_load_priority});
	ASSERT_NE(nullptr, resource);
	const auto asset = resource->asset_id();

	auto found = manager.find_audio(asset);
	ASSERT_NE(nullptr, found);
	EXPECT_EQ(asset, found->asset_id());
	EXPECT_TRUE(manager.protect_audio(asset));
	EXPECT_TRUE(resource->is_protected());
	EXPECT_TRUE(manager.erase_audio(asset));
	EXPECT_FALSE(resource->is_protected());
	found.reset();
	resource.reset();
	manager.maintain();
	EXPECT_EQ(nullptr, manager.find_audio(asset));
}
