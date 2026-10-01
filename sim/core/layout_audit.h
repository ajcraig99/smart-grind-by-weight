// Geometry audit of the firmware UI as LVGL has laid it out: text that does not
// fit its widget, widgets that overlap, content that sticks out of its container
// or crowds the panel edge. Twin-only; never compiled into the firmware.
#pragma once

#include <string>

namespace sim {

// JSON array of defects on the active screen; "[]" when the layout is clean.
// Each entry: {"rule": "...", "a": "...", "b": "..."} (b may be empty).
std::string layout_audit_json();

}  // namespace sim
