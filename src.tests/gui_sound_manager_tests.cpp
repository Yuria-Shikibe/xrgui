#include <gtest/gtest.h>

import std;
import mo_yanxi.audio.resources;
import mo_yanxi.gui.sound.manager;

namespace{

mo_yanxi::audio::audio_asset_handle register_lazy_audio(
	mo_yanxi::audio::audio_resource_manager& manager,
	std::string name){
	auto desc = mo_yanxi::audio::load_desc::from_file(name + ".wav");
	desc.debug_name = std::move(name);
	return manager.register_audio(
		std::move(desc),
		mo_yanxi::audio::audio_resource_options{
			.load_priority = mo_yanxi::audio::lazy_audio_load_priority
		});
}

}

TEST(GuiSoundAssetGroup, SparseSlotsDoNotFallback) {
	mo_yanxi::audio::audio_system system{};
	mo_yanxi::audio::audio_resource_manager resources{system};
	auto drag = register_lazy_audio(resources, "drag");

	auto group = mo_yanxi::gui::sound::make_asset_group();
	ASSERT_TRUE(group);

	EXPECT_FALSE(group->get(mo_yanxi::gui::sound::play_event::on_press));
	group->set(mo_yanxi::gui::sound::play_event::on_drag, drag);

	EXPECT_FALSE(group->get(mo_yanxi::gui::sound::play_event::on_press));
	auto resolved_drag = group->get(mo_yanxi::gui::sound::play_event::on_drag);
	ASSERT_TRUE(resolved_drag);
	EXPECT_EQ(drag->asset_id(), resolved_drag->asset_id());

	group->clear(mo_yanxi::gui::sound::play_event::on_drag);
	EXPECT_FALSE(group->get(mo_yanxi::gui::sound::play_event::on_drag));
}

TEST(GuiSoundAssetGroup, GroupRetainsAudioAssetHandle) {
	mo_yanxi::audio::audio_system system{};
	mo_yanxi::audio::audio_resource_manager resources{system};
	auto press = register_lazy_audio(resources, "press");
	const auto press_id = press->asset_id();

	auto group = mo_yanxi::gui::sound::make_asset_group();
	group->set(mo_yanxi::gui::sound::play_event::on_press, press);
	EXPECT_EQ(2u, press->external_ref_count());

	press.reset();
	auto resolved_press = group->get(mo_yanxi::gui::sound::play_event::on_press);
	ASSERT_TRUE(resolved_press);
	EXPECT_EQ(press_id, resolved_press->asset_id());
}

TEST(GuiSoundManager, ResolvesAndErasesGroupsWithoutInvalidatingHeldHandles) {
	mo_yanxi::gui::sound::manager manager{};
	auto group = mo_yanxi::gui::sound::make_asset_group();

	auto [itr, inserted] = manager.insert_or_assign(std::string{"button"}, group);
	EXPECT_TRUE(inserted);
	EXPECT_EQ(1u, manager.size());
	EXPECT_TRUE(manager.contains("button"));
	EXPECT_EQ(group, manager.lookup("button"));

	auto held = manager.lookup("button");
	EXPECT_TRUE(manager.erase("button"));
	EXPECT_FALSE(manager.contains("button"));
	EXPECT_FALSE(manager.lookup("button"));
	EXPECT_TRUE(held);
	EXPECT_THROW(static_cast<void>(manager.at("button")), std::out_of_range);
}
