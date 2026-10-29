#include "game/system/hid/devices/touch_controls.h"
#include "gtest/gtest.h"

namespace {
// iPhone 18 Pro in landscape, in points, with the notch on the left.
constexpr float kW = 874, kH = 402;
constexpr SDL_Rect kSafe = {62, 0, 750, 382};

SDL_Event finger(SDL_EventType type, SDL_FingerID id, float x, float y) {
  SDL_Event e = {};
  e.type = type;
  e.tfinger.fingerID = id;
  e.tfinger.x = x / kW;
  e.tfinger.y = y / kH;
  return e;
}
}  // namespace

TEST(TouchControls, FaceButtonPressAndRelease) {
  TouchControls touch(nullptr);
  touch.set_layout(kW, kH, kSafe);
  PadData pad;
  const auto [x, y] = touch.button_center(PadData::CROSS);
  ASSERT_GT(x, kW / 2);
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 1, x, y), pad);
  EXPECT_TRUE(pad.cross().first);
  EXPECT_EQ(pad.cross().second, 255);
  EXPECT_FALSE(pad.square().first);
  touch.process_event(finger(SDL_EVENT_FINGER_UP, 1, x, y), pad);
  EXPECT_FALSE(pad.cross().first);
  EXPECT_EQ(pad.cross().second, 0);
}

TEST(TouchControls, StartAndShoulders) {
  TouchControls touch(nullptr);
  touch.set_layout(kW, kH, kSafe);
  PadData pad;
  const auto [sx, sy] = touch.button_center(PadData::START);
  const auto [rx, ry] = touch.button_center(PadData::R1);
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 1, sx, sy), pad);
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 2, rx, ry), pad);
  EXPECT_TRUE(pad.start());
  EXPECT_TRUE(pad.r1().first);
  touch.process_event(finger(SDL_EVENT_FINGER_UP, 1, sx, sy), pad);
  EXPECT_FALSE(pad.start());
  EXPECT_TRUE(pad.r1().first);
}

TEST(TouchControls, FloatingSticks) {
  TouchControls touch(nullptr);
  touch.set_layout(kW, kH, kSafe);
  PadData pad;
  // Left stick: lands where the finger does, then follows it.
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 1, 200, 300), pad);
  EXPECT_EQ(pad.analog_left(), std::make_pair(u8(127), u8(127)));
  touch.process_event(finger(SDL_EVENT_FINGER_MOTION, 1, 300, 300), pad);  // far right
  EXPECT_EQ(pad.analog_left().first, 254);
  EXPECT_EQ(pad.analog_left().second, 127);
  touch.process_event(finger(SDL_EVENT_FINGER_MOTION, 1, 300, 200), pad);  // up
  EXPECT_LT(pad.analog_left().second, 127);
  // Right stick (camera) works at the same time.
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 2, 600, 200), pad);
  touch.process_event(finger(SDL_EVENT_FINGER_MOTION, 2, 560, 200), pad);
  EXPECT_LT(pad.analog_right().first, 127);
  // Releasing returns both to neutral without disturbing other sources' offsets.
  touch.process_event(finger(SDL_EVENT_FINGER_UP, 1, 300, 200), pad);
  touch.process_event(finger(SDL_EVENT_FINGER_UP, 2, 560, 200), pad);
  EXPECT_EQ(pad.analog_left(), std::make_pair(u8(127), u8(127)));
  EXPECT_EQ(pad.analog_right(), std::make_pair(u8(127), u8(127)));
}

TEST(TouchControls, ReleaseAll) {
  TouchControls touch(nullptr);
  touch.set_layout(kW, kH, kSafe);
  PadData pad;
  const auto [x, y] = touch.button_center(PadData::CIRCLE);
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 1, x, y), pad);
  touch.process_event(finger(SDL_EVENT_FINGER_DOWN, 2, 200, 300), pad);
  touch.process_event(finger(SDL_EVENT_FINGER_MOTION, 2, 150, 300), pad);
  touch.release_all(pad);
  EXPECT_FALSE(pad.circle().first);
  EXPECT_EQ(pad.analog_left(), std::make_pair(u8(127), u8(127)));
}
