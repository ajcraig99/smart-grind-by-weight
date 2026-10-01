// Test hooks that drive the firmware UI through its own entry points (UIManager,
// LVGL events), so screens and dialogs carry their real text. Twin-only.
#pragma once

namespace sim {

// Commands:
//   "ready"  arg = tab index        ready screen on that page
//   "menu"   arg = ""               main menu page, scrolled to the top
//   "state"  arg = UIState name     switch_to_state (e.g. "CALIBRATION")
//   "tap"    arg = label text       click the nearest clickable ancestor of that label
//   "toggle" arg = row label text   flip the switch in that row and send VALUE_CHANGED
//   "scroll" arg = "" or "top"      scroll the shown menu page down half a view (0 once at
//                                   the bottom), or back to its top (0 if no menu page shows)
// Returns 1 on success, 0 if the command or its target was not found.
int ui_command(const char* cmd, const char* arg);

}  // namespace sim
