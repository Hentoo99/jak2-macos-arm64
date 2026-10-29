#include "touch_controls.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "common/platform/BuildConfig.h"

#include "third-party/imgui/imgui.h"

namespace {
constexpr float kPi = 3.14159265f;

bool is_pressure_button(PadData::ButtonIndex index) {
  switch (index) {
    case PadData::SELECT:
    case PadData::L3:
    case PadData::R3:
    case PadData::START:
      return false;
    default:
      return true;
  }
}
}  // namespace

TouchControls::TouchControls(SDL_Window* window) : m_window(window) {
  m_buttons = {{{PadData::TRIANGLE, nullptr},
                {PadData::CIRCLE, nullptr},
                {PadData::CROSS, nullptr},
                {PadData::SQUARE, nullptr},
                {PadData::L1, "L1"},
                {PadData::L2, "L2"},
                {PadData::R1, "R1"},
                {PadData::R2, "R2"},
                {PadData::SELECT, "SELECT"},
                {PadData::START, "START"}}};
  update_layout();
}

bool TouchControls::enabled_for_platform() {
  return OG_TARGET_IOS || std::getenv("OPENGOAL_TOUCH_CONTROLS") != nullptr;
}

void TouchControls::update_layout() {
  int w = 0, h = 0;
  if (!m_window || !SDL_GetWindowSize(m_window, &w, &h) || w <= 0 || h <= 0) {
    return;
  }
  SDL_Rect safe;
  if (!SDL_GetWindowSafeArea(m_window, &safe) || safe.w <= 0 || safe.h <= 0) {
    safe = {0, 0, w, h};
  }
  set_layout((float)w, (float)h, safe);
}

std::pair<float, float> TouchControls::button_center(PadData::ButtonIndex index) const {
  for (const auto& b : m_buttons) {
    if (b.index == index) {
      return {b.x, b.y};
    }
  }
  return {-1, -1};
}

void TouchControls::set_layout(float width, float height, const SDL_Rect& safe_area) {
  if (width == m_width && height == m_height && safe_area.x == m_safe.x &&
      safe_area.y == m_safe.y && safe_area.w == m_safe.w && safe_area.h == m_safe.h) {
    return;
  }
  m_safe = safe_area;
  m_width = width;
  m_height = height;

  const float u = std::min(m_width, m_height) / 100.f;  // layout unit
  const float left = (float)m_safe.x, right = (float)(m_safe.x + m_safe.w);
  const float top = (float)m_safe.y, bottom = (float)(m_safe.y + m_safe.h);
  m_stick_radius = 13 * u;

  // Face buttons: a diamond in the bottom right corner.
  const float cx = right - 21 * u, cy = bottom - 25 * u, d = 12.5f * u, face = 14 * u;
  auto place = [&](int i, float x, float y, float bw, float bh) {
    m_buttons[i].x = x;
    m_buttons[i].y = y;
    m_buttons[i].w = bw;
    m_buttons[i].h = bh;
  };
  place(0, cx, cy - d, face, face);  // triangle
  place(1, cx + d, cy, face, face);  // circle
  place(2, cx, cy + d, face, face);  // cross
  place(3, cx - d, cy, face, face);  // square
  // Shoulders in the top corners, L2/R2 above L1/R1.
  const float sw = 19 * u, sh = 9 * u;
  place(4, left + 13 * u, top + 21 * u, sw, sh);   // L1
  place(5, left + 13 * u, top + 9 * u, sw, sh);    // L2
  place(6, right - 13 * u, top + 21 * u, sw, sh);  // R1
  place(7, right - 13 * u, top + 9 * u, sw, sh);   // R2
  // Select / Start at the top center.
  place(8, m_width / 2 - 10 * u, top + 6 * u, 15 * u, 6 * u);
  place(9, m_width / 2 + 10 * u, top + 6 * u, 15 * u, 6 * u);
}

int TouchControls::button_at(float x, float y) const {
  for (int i = 0; i < (int)m_buttons.size(); i++) {
    const auto& b = m_buttons[i];
    // Generous hit boxes: fingers are not precise, and the buttons are small on a phone.
    if (std::abs(x - b.x) <= b.w * 0.6f && std::abs(y - b.y) <= b.h * 0.6f) {
      return i;
    }
  }
  return -1;
}

void TouchControls::set_button(PadData& data, int button, bool pressed) {
  auto& count = m_button_presses[button];
  count += pressed ? 1 : -1;
  count = std::max(count, 0);
  const auto index = m_buttons[button].index;
  const bool down = count > 0;
  data.button_data.at(index) = down;
  if (is_pressure_button(index)) {
    const auto pressure = data.button_index_to_pressure_index(index);
    if (pressure != PadData::INVALID_PRESSURE) {
      data.pressure_data.at(pressure) = down ? 255 : 0;
    }
  }
}

void TouchControls::set_stick(PadData& data, bool right, float dx, float dy) {
  // PadData aggregates analog sources by offsets around neutral: apply only the change.
  const int base = right ? PadData::RIGHT_X : PadData::LEFT_X;
  const float scale = 127.f / std::max(m_stick_radius, 1.f);
  const int target[2] = {std::clamp((int)std::lround(dx * scale), -127, 127),
                         std::clamp((int)std::lround(dy * scale), -127, 127)};
  for (int axis = 0; axis < 2; axis++) {
    data.analog_data.at(base + axis) += target[axis] - m_applied_analog[base + axis];
    m_applied_analog[base + axis] = target[axis];
  }
}

void TouchControls::process_event(const SDL_Event& event, PadData& data) {
  if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
      event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED) {
    return;
  }
  update_layout();
  const auto id = event.tfinger.fingerID;
  const float x = event.tfinger.x * m_width;
  const float y = event.tfinger.y * m_height;

  if (event.type == SDL_EVENT_FINGER_DOWN) {
    Finger finger;
    finger.x = finger.origin_x = x;
    finger.y = finger.origin_y = y;
    const int button = button_at(x, y);
    if (button >= 0) {
      finger.kind = ControlKind::BUTTON;
      finger.button = button;
      set_button(data, button, true);
    } else {
      const bool right = x >= m_width / 2;
      auto& stick = m_sticks[right ? 1 : 0];
      if (!stick.active) {
        finger.kind = right ? ControlKind::RIGHT_STICK : ControlKind::LEFT_STICK;
        stick = {true, x, y, x, y};
      }
    }
    m_fingers[id] = finger;
    return;
  }

  auto it = m_fingers.find(id);
  if (it == m_fingers.end()) {
    return;
  }
  auto& finger = it->second;
  const bool right_stick = finger.kind == ControlKind::RIGHT_STICK;
  if (event.type == SDL_EVENT_FINGER_MOTION) {
    if (finger.kind == ControlKind::LEFT_STICK || right_stick) {
      auto& stick = m_sticks[right_stick ? 1 : 0];
      float dx = x - stick.origin_x, dy = y - stick.origin_y;
      // Past the rim, drag the stick's center along so reversing direction is immediate.
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len > m_stick_radius) {
        stick.origin_x = x - dx / len * m_stick_radius;
        stick.origin_y = y - dy / len * m_stick_radius;
        dx = x - stick.origin_x;
        dy = y - stick.origin_y;
      }
      stick.x = x;
      stick.y = y;
      set_stick(data, right_stick, dx, dy);
    }
    return;
  }

  // up / canceled
  if (finger.kind == ControlKind::BUTTON) {
    set_button(data, finger.button, false);
  } else if (finger.kind == ControlKind::LEFT_STICK || right_stick) {
    m_sticks[right_stick ? 1 : 0] = {};
    set_stick(data, right_stick, 0, 0);
  }
  m_fingers.erase(it);
}

void TouchControls::release_all(PadData& data) {
  for (int i = 0; i < (int)m_buttons.size(); i++) {
    if (m_button_presses[i]) {
      m_button_presses[i] = 1;
      set_button(data, i, false);
    }
  }
  set_stick(data, false, 0, 0);
  set_stick(data, true, 0, 0);
  m_sticks[0] = m_sticks[1] = {};
  m_fingers.clear();
}

void TouchControls::draw(ImDrawList* dl) {
  update_layout();
  if (!dl || m_width <= 0) {
    return;
  }
  const ImU32 fill = IM_COL32(255, 255, 255, 38);
  const ImU32 fill_pressed = IM_COL32(255, 255, 255, 110);
  const ImU32 outline = IM_COL32(255, 255, 255, 120);
  const float u = std::min(m_width, m_height) / 100.f;

  // Sticks: a ring where the finger landed and a knob under the finger.
  for (const auto& stick : m_sticks) {
    if (!stick.active) {
      continue;
    }
    dl->AddCircleFilled({stick.origin_x, stick.origin_y}, m_stick_radius, fill, 40);
    dl->AddCircle({stick.origin_x, stick.origin_y}, m_stick_radius, outline, 40, 2 * u / 3);
    float dx = stick.x - stick.origin_x, dy = stick.y - stick.origin_y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > m_stick_radius) {
      dx *= m_stick_radius / len;
      dy *= m_stick_radius / len;
    }
    dl->AddCircleFilled({stick.origin_x + dx, stick.origin_y + dy}, m_stick_radius * 0.45f,
                        fill_pressed, 30);
  }

  for (int i = 0; i < (int)m_buttons.size(); i++) {
    const auto& b = m_buttons[i];
    const ImU32 bg = m_button_presses[i] ? fill_pressed : fill;
    if (!b.label) {
      // Face button: a circle with its PlayStation symbol.
      const float r = b.w / 2, s = r * 0.42f, t = 2 * u / 3;
      const ImVec2 c(b.x, b.y);
      dl->AddCircleFilled(c, r, bg, 32);
      dl->AddCircle(c, r, outline, 32, t);
      switch (b.index) {
        case PadData::TRIANGLE:
          dl->AddTriangle({c.x, c.y - s}, {c.x + s * std::sin(2 * kPi / 3), c.y + s * 0.5f},
                          {c.x - s * std::sin(2 * kPi / 3), c.y + s * 0.5f},
                          IM_COL32(64, 226, 160, 230), t * 1.4f);
          break;
        case PadData::CIRCLE:
          dl->AddCircle(c, s, IM_COL32(255, 102, 102, 230), 24, t * 1.4f);
          break;
        case PadData::CROSS:
          dl->AddLine({c.x - s, c.y - s}, {c.x + s, c.y + s}, IM_COL32(124, 178, 232, 230),
                      t * 1.4f);
          dl->AddLine({c.x - s, c.y + s}, {c.x + s, c.y - s}, IM_COL32(124, 178, 232, 230),
                      t * 1.4f);
          break;
        default:  // square
          dl->AddRect({c.x - s * 0.8f, c.y - s * 0.8f}, {c.x + s * 0.8f, c.y + s * 0.8f},
                      IM_COL32(255, 105, 248, 230), 0, 0, t * 1.4f);
          break;
      }
    } else {
      const ImVec2 a(b.x - b.w / 2, b.y - b.h / 2), z(b.x + b.w / 2, b.y + b.h / 2);
      dl->AddRectFilled(a, z, bg, b.h / 2);
      dl->AddRect(a, z, outline, b.h / 2, 0, 2 * u / 3);
      const float font_size = b.h * 0.55f;
      const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(font_size, 1e9f, 0, b.label);
      dl->AddText(ImGui::GetFont(), font_size, {b.x - text_size.x / 2, b.y - text_size.y / 2},
                  IM_COL32(255, 255, 255, 200), b.label);
    }
  }
}
