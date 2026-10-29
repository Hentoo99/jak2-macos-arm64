#pragma once

/*!
 * @file touch_controls.h
 * On-screen DualShock 2 for touch screens (iOS), with the full PS2 layout: a floating left stick
 * (movement), a floating right stick (camera), the four face buttons, L1/L2/R1/R2 and
 * Start/Select.  Touches are written into the port's PadData like any other input device; the
 * overlay is drawn with ImGui's foreground draw list, so it needs no renderer of its own.
 *
 * Enabled on iOS, or anywhere with OPENGOAL_TOUCH_CONTROLS=1 (on a desktop SDL then turns the
 * mouse into a finger).  Hidden while a game controller is connected.
 */

#include <array>
#include <memory>
#include <unordered_map>

#include "game/system/hid/input_bindings.h"
#include "game/system/hid/sdl_util.h"

struct ImDrawList;

class TouchControls {
 public:
  explicit TouchControls(SDL_Window* window);

  static bool enabled_for_platform();

  // Touch events update `data`; other events are ignored.
  void process_event(const SDL_Event& event, PadData& data);
  // Release everything (for example when a controller takes over).
  void release_all(PadData& data);
  // Lays the controls out for the current window size, then draws them.
  void draw(ImDrawList* draw_list);
  // Lay the controls out for a window of `width` x `height` points with the given safe area.
  // Called from the window size on every event and frame; public for tests (window == nullptr).
  void set_layout(float width, float height, const SDL_Rect& safe_area);
  // Center of a button, by PadData button index (tests).
  std::pair<float, float> button_center(PadData::ButtonIndex index) const;

 private:
  enum class ControlKind { NONE, LEFT_STICK, RIGHT_STICK, BUTTON };

  struct Button {
    PadData::ButtonIndex index;
    const char* label;   // nullptr for the face buttons, which are drawn as shapes
    float x = 0, y = 0;  // center, in window points
    float w = 0, h = 0;  // size, in window points (w == h: circle of diameter w)
  };

  struct Finger {
    ControlKind kind = ControlKind::NONE;
    int button = -1;                   // index into m_buttons
    float origin_x = 0, origin_y = 0;  // stick center
    float x = 0, y = 0;                // current position
  };

  struct Stick {
    bool active = false;
    float origin_x = 0, origin_y = 0;
    float x = 0, y = 0;
  };

  void update_layout();
  int button_at(float x, float y) const;
  void set_button(PadData& data, int button, bool pressed);
  void set_stick(PadData& data, bool right, float dx, float dy);

  SDL_Window* m_window;
  float m_width = 0, m_height = 0;  // window size in points
  SDL_Rect m_safe = {};
  float m_stick_radius = 0;
  std::array<Button, 10> m_buttons;
  std::array<int, 10> m_button_presses = {};  // fingers holding each button
  std::unordered_map<SDL_FingerID, Finger> m_fingers;
  Stick m_sticks[2];
  // analog offsets currently applied to PadData::analog_data (LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y)
  std::array<int, 4> m_applied_analog = {0, 0, 0, 0};
};
