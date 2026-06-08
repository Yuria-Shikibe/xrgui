#include <gtest/gtest.h>

import std;

import mo_yanxi.gui.elem.slider_logic;
import mo_yanxi.gui.util.animator;

namespace {

template <std::size_t N>
void expect_float_array_eq(const std::array<float, N>& expected, const std::array<float, N>& actual) {
	for(std::size_t i = 0; i < N; ++i) {
		SCOPED_TRACE(i);
		EXPECT_FLOAT_EQ(expected[i], actual[i]);
	}
}

} // namespace

TEST(Animator, SimpleAnimatorEntersAndExitsWithCallbacks) {
	using mo_yanxi::gui::util::anim_state;
	mo_yanxi::gui::util::simple_animator<float> animator;
	animator.set_speed(2.0f);

	int enter_complete_count = 0;
	int exit_complete_count = 0;
	const auto on_enter = [&] {
		++enter_complete_count;
	};
	const auto on_exit = [&] {
		++exit_complete_count;
	};

	EXPECT_EQ(anim_state::idle, animator.get_state());
	EXPECT_TRUE(animator.set_target(true));
	EXPECT_EQ(anim_state::entering, animator.get_state());

	animator.update(0.25f, on_enter, on_exit);
	EXPECT_EQ(anim_state::entering, animator.get_state());
	EXPECT_FLOAT_EQ(0.5f, animator.get_progress());
	EXPECT_EQ(0, enter_complete_count);

	animator.update(0.25f, on_enter, on_exit);
	EXPECT_EQ(anim_state::active, animator.get_state());
	EXPECT_FLOAT_EQ(1.0f, animator.get_progress());
	EXPECT_EQ(1, enter_complete_count);
	EXPECT_FALSE(animator.set_target(true));

	EXPECT_TRUE(animator.set_target(false));
	EXPECT_EQ(anim_state::exiting, animator.get_state());
	animator.update(0.25f, on_enter, on_exit);
	EXPECT_EQ(anim_state::exiting, animator.get_state());
	EXPECT_FLOAT_EQ(0.5f, animator.get_progress());
	animator.update(0.25f, on_enter, on_exit);
	EXPECT_EQ(anim_state::idle, animator.get_state());
	EXPECT_FLOAT_EQ(0.0f, animator.get_progress());
	EXPECT_EQ(1, exit_complete_count);
}

TEST(Animator, DelayedAnimatorWaitsBeforeTransitions) {
	using mo_yanxi::gui::util::anim_state;
	mo_yanxi::gui::util::delayed_animator<float> animator;
	animator.set_speed(1.0f);
	animator.set_enter_delay(0.5f);
	animator.set_exit_delay(0.25f);

	int enter_complete_count = 0;
	int exit_complete_count = 0;
	const auto on_enter = [&] {
		++enter_complete_count;
	};
	const auto on_exit = [&] {
		++exit_complete_count;
	};

	EXPECT_TRUE(animator.set_target(true));
	EXPECT_EQ(anim_state::waiting_to_enter, animator.get_state());
	animator.update(0.25f, on_enter, on_exit);
	EXPECT_EQ(anim_state::waiting_to_enter, animator.get_state());
	EXPECT_FLOAT_EQ(0.0f, animator.get_progress());
	animator.update(0.25f, on_enter, on_exit);
	EXPECT_EQ(anim_state::entering, animator.get_state());
	animator.update(1.0f, on_enter, on_exit);
	EXPECT_EQ(anim_state::active, animator.get_state());
	EXPECT_EQ(1, enter_complete_count);

	EXPECT_TRUE(animator.set_target(false));
	EXPECT_EQ(anim_state::waiting_to_exit, animator.get_state());
	animator.update(0.24f, on_enter, on_exit);
	EXPECT_EQ(anim_state::waiting_to_exit, animator.get_state());
	animator.update(0.01f, on_enter, on_exit);
	EXPECT_EQ(anim_state::exiting, animator.get_state());
	animator.update(1.0f, on_enter, on_exit);
	EXPECT_EQ(anim_state::idle, animator.get_state());
	EXPECT_EQ(1, exit_complete_count);
}

TEST(SliderSlot, FloatingProgressSnapsToSegments) {
	mo_yanxi::gui::slider_slot<float> slot;
	EXPECT_FLOAT_EQ(1.0f, slot.get_segment_unit());
	EXPECT_FALSE(slot.is_segment_move_activated());

	EXPECT_TRUE(slot.set_progress_from_segments(1, 4));
	EXPECT_EQ(4u, slot.segments);
	EXPECT_TRUE(slot.is_segment_move_activated());
	EXPECT_FLOAT_EQ(0.25f, slot.get_segment_unit());
	EXPECT_FLOAT_EQ(0.25f, slot.get_progress());

	slot.move_progress(0.20f);
	EXPECT_TRUE(slot.is_sliding());
	EXPECT_FLOAT_EQ(0.50f, slot.get_temp_progress());
	EXPECT_TRUE(slot.update(1.0f));
	EXPECT_FLOAT_EQ(0.50f, slot.get_progress());
	EXPECT_FALSE(slot.is_sliding());

	EXPECT_TRUE(slot.clamp(0.60f, 0.90f));
	EXPECT_FLOAT_EQ(0.60f, slot.get_progress());
	EXPECT_FLOAT_EQ(0.60f, slot.get_temp_progress());
}

TEST(SliderSlot, IntegralProgressClampsAndApplies) {
	mo_yanxi::gui::slider_slot<unsigned> slot;
	slot.segments = 10;
	EXPECT_EQ(1u, slot.get_segment_unit());
	EXPECT_TRUE(slot.is_segment_move_activated());
	EXPECT_EQ(10u, slot.get_max_value());

	EXPECT_TRUE(slot.set_progress(5));
	slot.move_progress(99);
	EXPECT_EQ(10u, slot.get_temp_progress());
	EXPECT_TRUE(slot.apply());
	EXPECT_EQ(10u, slot.get_progress());
	EXPECT_FALSE(slot.apply());

	slot.move_progress(-20);
	EXPECT_EQ(0u, slot.get_temp_progress());
	EXPECT_TRUE(slot.update(0.5f));
	EXPECT_EQ(5u, slot.get_progress());
	EXPECT_TRUE(slot.update(1.0f));
	EXPECT_EQ(0u, slot.get_progress());
	EXPECT_FALSE(slot.is_sliding());
}

TEST(SliderNd, AppliesIndependentDimensionTargets) {
	mo_yanxi::gui::slider_nd<float, 2> slider;

	EXPECT_TRUE(slider.set_progress(std::array<float, 2>{0.2f, 0.8f}));
	expect_float_array_eq(std::array<float, 2>{0.2f, 0.8f}, slider.get_progress());
	EXPECT_FALSE(slider.set_progress(3, 0.5f));

	slider.move_progress(
		std::array<float, 2>{0.1f, -0.5f},
		slider.get_progress());
	EXPECT_TRUE(slider.is_sliding());
	expect_float_array_eq(std::array<float, 2>{0.3f, 0.3f}, slider.get_temp_progress());
	EXPECT_TRUE(slider.apply());
	expect_float_array_eq(std::array<float, 2>{0.3f, 0.3f}, slider.get_progress());

	EXPECT_TRUE(slider.clamp(
		std::array<float, 2>{0.0f, 0.4f},
		std::array<float, 2>{1.0f, 1.0f}));
	expect_float_array_eq(std::array<float, 2>{0.3f, 0.4f}, slider.get_progress());
}
