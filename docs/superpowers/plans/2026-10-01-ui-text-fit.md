# UI Text Fit and Button Placement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every on-device screen shows its text in full, without overlaps, with buttons that never move under a finger and a consistent margin from the panel edge.

**Architecture:** Most defects come from five shared builders (`create_menu_item`, `create_toggle_row`, `create_slider_row`, `create_separator` in `menu_screen.cpp`, and `create_dual_button_row` in `ui_helpers.cpp`) plus the menu header. Fix those first, then the individual screens, then the copy. A layout audit compiled into the digital twin walks the live LVGL object tree and reports clipped text, overlaps, truncation and edge crowding; it is the regression test for this work and stays in the repo.

**Tech Stack:** C++17, LVGL 9.5.0, ESP32-S3 (PlatformIO), digital twin (Emscripten WASM + Playwright page), Python `unittest` host tests compiled with g++.

**Source of findings:** the UI review of 2026-10-01 (twin page built at commit `f85158e`; text widths measured from the LVGL Montserrat glyph tables).

---

## Decisions this plan rests on

| # | Decision | Source |
|---|----------|--------|
| D1 | Write the plan starting with the shared helpers | User, 2026-10-01 |
| D2 | Status icons only on the ready screen and the main menu | User, 2026-10-01 — **see open question Q1: they do not fit beside the "Menu" title** |
| D3 | Dialog buttons: 24 px text, less padding, shorter labels | User, 2026-10-01 |
| D4 | Screen margin 8 px (`THEME_SCREEN_MARGIN_PX`) | PDV engineering judgement, below |
| D5 | Copy changes in the table below | **Open question Q2 — needs user approval before Task 7** |

**D4 reasoning.** Panel: 280 x 456 px on a 22.34 x 36.06 mm active area (user's drawing), so about 12.5 px/mm. The drawing dimensions the outer glass (R3.7, R2.50) but not the active-area corner radius [TO CONFIRM]. A rectangle inset by `m` px from both edges clears a corner arc of radius `r` when `m >= r * (1 - 1/sqrt(2)) = 0.293 r`. With `m = 8`, any active-area radius up to 27 px (about 2.2 mm) is clear. Assumption: the active-area radius is no more than 2.2 mm (the drawing suggests about 1 mm).

### Copy table (Q2)

Widths measured at the stated font; "fits" is against the space after this plan's padding changes.

| Where | Now | Proposed | Width | Space |
|-------|-----|----------|-------|-------|
| Menu entry | Firmware Update | Firmware | 137 @28 | 205 |
| Menu entry | Tune Pulses | Pulse Tune | ~157 @28 | 205 |
| Toggle row (Wi-Fi) | Remote start | Remote | 114 @28 | 152 |
| Toggle row (Display) | Idle Screensaver | On idle | 102 @28 | 152 |
| Toggle row (Display) | Turn Display Off | Display off | 149 @28 | 152 |
| Slider (Display) | Brightness: 100% | Normal: 100% | 192 @28 | 228 |
| Slider (Grind) | Cup threshold: 50g | Trigger: 50g | 171 @28 | 228 |
| Slider (Grind) | Motor latency: 250ms | Latency: 250ms | 221 @28 | 228 |
| Slider (Grind) | Coast Ratio: 150% | Coast: 150% | 163 @28 | 228 |
| Button (Diagnostics) | Reset Diagnostics | Clear Warnings | 220 @28 | 228 |
| Section heading | Load Cell Status | Load Cell | 112 @24 | ~150 |
| Section heading | Motor Response (x2) | Motor | 73 @24 | ~150 |
| Section heading | Remote Control | Remote | ~80 @24 | ~150 |
| Section heading | Mode Selection | Mode | 69 @24 | ~150 |
| Section heading | Coast Compensation | Coast | 69 @24 | ~150 |
| Section heading | Custom Image | Screensaver | 148 @24 | ~150 |
| Section heading | Lifetime Statistics | (removed; the page title says it) | — | — |
| Dialog button | CONTINUE | RESUME | 106 @24 | 108 |
| Dialog button | PURGE LOGS | PURGE | 88 @24 | 108 |
| Dialog button | TURN ON | ENABLE | 102 @24 | 108 |
| Dialog title | FORGET NETWORK | FORGET WI-FI | 233 @32 | 264 |
| Dialog title | Auto-Tune Setup | Pulse Tune | 202 @36 | 264 |
| Dialog title | Reset Diagnostics | Clear Warnings | ~251 @32 | 264 |
| Network button | FORGET NETWORK | FORGET WI-FI | 174 @24 | 228 |
| Ready Wi-Fi page | STARTING WI-FI SETUP | STARTING SETUP | 211 @24 | 272 |
| Bluetooth page | Auto-disable in: 30 min | Off in 30 min | 158 @24 | 260 |

"Firmware Update" is also quoted in `src/network/device_web_server.cpp:725`, `src/network/device_page.h` (two places), `src/bluetooth/manager.cpp:728`, `tools/web-flasher/flasher.js:298`, `tools/tests/ota_web_test.mjs:54`, `CLAUDE.md` and several docs; Task 7 updates all of them.

---

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `src/config/theme.h` | Layout constants | Add margin, padding, switch and dialog constants |
| `src/ui/ui_helpers.{h,cpp}` | Shared widgets | `pick_font_that_fits`, fixed-slot dual button row, row padding |
| `src/ui/screens/menu_screen.{h,cpp}` | Menu and its pages | Row builders, separator, header title fit, page setup helper |
| `src/ui/screens/confirm_screen.{h,cpp}` | Two-button dialog | 24 px buttons, title fit, message alignment |
| `src/ui/screens/autotune_screen.cpp` | Pulse Tune | Flex layout, result labels, failure copy |
| `src/ui/screens/calibration_screen.cpp` | Calibration | Flex layout, no padding switches |
| `src/ui/screens/purge_confirm_screen.cpp` | Purge prompt | Message width, title fit |
| `src/ui/screens/refill_confirm_screen.cpp` | Out-of-beans prompt | Status replaces message |
| `src/ui/screens/ota_update_failed_screen.cpp` | Update failed | Fit on one screen |
| `src/ui/screens/ready_screen.cpp`, `grinding_screen_chart.cpp` | Ready pages, chart | Wi-Fi status wrap, chart margin |
| `src/ui/controllers/status_indicator_controller.{h,cpp}`, `src/ui/ui_manager.cpp` | Status icons | Show per UI state |
| `src/ui/controllers/grinding_controller.cpp`, `menu_controller.cpp`, `calibration_controller.cpp`, `ui_manager.cpp` | Dialog text | No hand line breaks, shorter labels |
| `include/lv_conf.h` | LVGL config | `.` no longer a line-break character |
| `sim/core/layout_audit.{h,cpp}` (new) | Twin-only geometry audit | Reports layout defects as JSON |
| `sim/core/ui_probe.{h,cpp}` (new) | Twin-only UI driver | Shows states, taps by label text |
| `sim/core/js_api.cpp`, `sim/cmake/twin_sources.cmake`, `sim/wasm/CMakeLists.txt`, `sim/web/src/twin.js`, `sim/web/src/main.js` | Twin wiring | Export the two hooks |
| `sim/qa/layout_audit.mjs` (new) | Layout regression run | Visits every screen, fails on any defect |
| `tools/tests/ui_text_fit_test.py` (new) | Host test | Source rules and `pick_font_that_fits` |

---

## Build and test commands (Windows box)

Host tests run in WSL (`archlinux`) because Windows has no g++. Put commands in an LF script and run it with `wsl -d archlinux -- bash <script>` from the PowerShell tool.

`scratch/host_tests.sh`:
```bash
set -e
cd /mnt/c/Users/arron.craig/projects/smart-grind-by-weight
python3 -m unittest discover -s tools/tests -p '*_test.py'
node tools/tests/settings_web_test.mjs
node tools/tests/ota_web_test.mjs
node tools/tests/web_flasher_status_test.mjs
```

`scratch/build_twin.sh` (the tracked `sim/wasm/build.sh` has CRLF endings, so run its steps directly):
```bash
set -e
cd /mnt/c/Users/arron.craig/projects/smart-grind-by-weight
source ~/emsdk/emsdk_env.sh > /dev/null 2>&1
export PATH="$HOME/.venvs/simtools/bin:$PATH"
emcmake cmake -S sim/wasm -B sim/out/wasm -G Ninja > /dev/null
ninja -C sim/out/wasm grindtwin
```
Then, in Windows: `npm --prefix sim/web run build` (writes `sim/dist/index.html`).

Layout audit (Windows): `node sim/qa/layout_audit.mjs` (Chrome at `C:/Program Files/Google/Chrome/Application/chrome.exe`, override with `CHROME_PATH`).

Firmware: `tools/venv/Scripts/python.exe tools/grinder.py build --hardware v1 --jobs 8` and `... --hardware v2 --jobs 8`.

---

### Task 0: Branch

- [ ] **Step 1:** The current branch `claude/code-review-ui-improvements-9e89pk` is the default branch. Create a working branch.

```bash
git checkout -b ui-text-fit
```

---

### Task 1: Layout audit in the twin (the regression test)

**Files:**
- Create: `sim/core/layout_audit.h`, `sim/core/layout_audit.cpp`, `sim/core/ui_probe.h`, `sim/core/ui_probe.cpp`, `sim/qa/layout_audit.mjs`
- Modify: `sim/cmake/twin_sources.cmake` (TWIN_CORE_SRC), `sim/core/js_api.cpp`, `sim/wasm/CMakeLists.txt:21-26`, `sim/web/src/twin.js:31-57`, `sim/web/src/main.js:584-594`

- [ ] **Step 1: Write `sim/core/layout_audit.h`**

```cpp
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
```

- [ ] **Step 2: Write `sim/core/layout_audit.cpp`**

```cpp
#include "layout_audit.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <lvgl.h>

#include "config/theme.h"

namespace sim {
namespace {

struct Item {
    lv_obj_t* obj;
    lv_area_t area;     // where LVGL placed it
    lv_area_t visible;  // the part not scrolled out of view
    bool leaf;
};

std::string g_json;

bool hidden(lv_obj_t* obj) {
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return true;
    }
    return false;
}

int32_t overlap_len(int32_t a1, int32_t a2, int32_t b1, int32_t b2) {
    return std::min(a2, b2) - std::max(a1, b1) + 1;
}

// More than one pixel of overlap in both directions; touching edges is fine.
bool overlaps(const lv_area_t& a, const lv_area_t& b) {
    return overlap_len(a.x1, a.x2, b.x1, b.x2) > 1 && overlap_len(a.y1, a.y2, b.y1, b.y2) > 1;
}

bool is_ancestor(lv_obj_t* ancestor, lv_obj_t* obj) {
    for (lv_obj_t* o = lv_obj_get_parent(obj); o; o = lv_obj_get_parent(o)) {
        if (o == ancestor) return true;
    }
    return false;
}

bool filled(lv_obj_t* obj) {
    return lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) > LV_OPA_10 ||
           lv_obj_get_style_border_width(obj, LV_PART_MAIN) > 0;
}

bool scrolls(lv_obj_t* obj) {
    return lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// A background or overlay covering most of the panel is not a control.
bool backdrop(const lv_area_t& a) {
    const int32_t w = lv_area_get_width(&a);
    const int32_t h = lv_area_get_height(&a);
    const int32_t panel_w = lv_display_get_horizontal_resolution(lv_display_get_default());
    const int32_t panel_h = lv_display_get_vertical_resolution(lv_display_get_default());
    return w * h >= (panel_w * panel_h * 9) / 10;
}

std::string text_of(lv_obj_t* obj) {
    if (lv_obj_check_type(obj, &lv_label_class)) return lv_label_get_text(obj);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        std::string t = text_of(lv_obj_get_child(obj, i));
        if (!t.empty()) return t;
    }
    return "";
}

std::string describe(lv_obj_t* obj) {
    const char* kind = lv_obj_check_type(obj, &lv_label_class)    ? "label"
                       : lv_obj_check_type(obj, &lv_button_class) ? "button"
                       : lv_obj_check_type(obj, &lv_switch_class) ? "switch"
                       : lv_obj_check_type(obj, &lv_slider_class) ? "slider"
                                                                   : "obj";
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    char buf[64];
    std::snprintf(buf, sizeof(buf), " [%d,%d..%d,%d]", (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
    std::string text = text_of(obj);
    if (text.size() > 40) text = text.substr(0, 40) + "...";
    return std::string(kind) + " \"" + text + "\"" + buf;
}

void add_issue(const char* rule, const std::string& a, const std::string& b = "") {
    auto esc = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            if (c == '\n') { out += "\\n"; continue; }
            out += c;
        }
        return out;
    };
    if (g_json.size() > 1) g_json += ",";
    g_json += "{\"rule\":\"" + std::string(rule) + "\",\"a\":\"" + esc(a) + "\",\"b\":\"" + esc(b) + "\"}";
}

int32_t text_width(lv_obj_t* label, const char* text) {
    lv_point_t size;
    lv_text_get_size(&size, text, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    return size.x;
}

void collect(lv_obj_t* obj, lv_area_t view, std::vector<Item>& out) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_area_t seen;
    if (!lv_area_intersect(&seen, &area, &view)) return;  // scrolled out of view
    const uint32_t children = lv_obj_get_child_count(obj);
    if (obj != lv_screen_active() && (children == 0 || filled(obj)) && !backdrop(area)) {
        out.push_back({obj, area, seen, children == 0});
    }
    lv_area_t child_view = view;
    if (scrolls(obj)) lv_area_intersect(&child_view, &view, &area);
    for (uint32_t i = 0; i < children; ++i) collect(lv_obj_get_child(obj, i), child_view, out);
}

// Text: a label cut off with dots, or a single word broken across lines.
void check_text(const Item& it) {
    if (!lv_obj_check_type(it.obj, &lv_label_class)) return;
    const char* text = lv_label_get_text(it.obj);
    const int32_t width = lv_obj_get_content_width(it.obj);
    const lv_label_long_mode_t mode = lv_label_get_long_mode(it.obj);
    if (mode == LV_LABEL_LONG_DOT && text_width(it.obj, text) > width) {
        add_issue("truncated", describe(it.obj));
    }
    if (mode == LV_LABEL_LONG_WRAP && lv_obj_get_style_width(it.obj, LV_PART_MAIN) != LV_SIZE_CONTENT) {
        std::string word;
        for (const char* p = text;; ++p) {
            if (*p == ' ' || *p == '\n' || *p == '\0') {
                if (!word.empty() && text_width(it.obj, word.c_str()) > width) {
                    add_issue("word-split", describe(it.obj), word);
                }
                word.clear();
                if (*p == '\0') break;
            } else {
                word += *p;
            }
        }
    }
}

// Containment: sticking out sideways is never intended; vertically only
// containers that scroll may hold more than they show.
void check_containment(const Item& it) {
    for (lv_obj_t* a = lv_obj_get_parent(it.obj); a && a != lv_screen_active(); a = lv_obj_get_parent(a)) {
        lv_area_t box;
        lv_obj_get_coords(a, &box);
        if (it.area.x1 < box.x1 || it.area.x2 > box.x2) {
            add_issue("sticks-out-sideways", describe(it.obj), describe(a));
            return;
        }
        if (!scrolls(a) && (it.area.y1 < box.y1 || it.area.y2 > box.y2)) {
            add_issue("sticks-out-vertically", describe(it.obj), describe(a));
            return;
        }
    }
}

// Text inside a filled control must stay out of its side padding.
void check_padding(const Item& it) {
    if (!lv_obj_check_type(it.obj, &lv_label_class)) return;
    lv_obj_t* parent = lv_obj_get_parent(it.obj);
    if (!parent || !filled(parent)) return;
    lv_area_t content;
    lv_obj_get_content_coords(parent, &content);
    if (it.area.x1 < content.x1 || it.area.x2 > content.x2) {
        add_issue("into-padding", describe(it.obj), describe(parent));
    }
}

void check_edge(const Item& it) {
    if (it.leaf && !filled(it.obj) && !lv_obj_check_type(it.obj, &lv_label_class)) return;
    const int32_t panel_w = lv_display_get_horizontal_resolution(lv_display_get_default());
    if (it.area.x1 < THEME_SCREEN_MARGIN_PX || it.area.x2 > panel_w - 1 - THEME_SCREEN_MARGIN_PX) {
        add_issue("screen-edge", describe(it.obj));
    }
}

// A container that only shows part of its text unless the user scrolls.
void check_scroll_needed(lv_obj_t* obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    const bool exempt = lv_obj_check_type(obj, &lv_menu_page_class) ||
                        lv_obj_check_type(obj, &lv_textarea_class);
    if (!exempt && scrolls(obj) && obj != lv_screen_active() &&
        (lv_obj_get_scroll_top(obj) > 0 || lv_obj_get_scroll_bottom(obj) > 0) &&
        !text_of(obj).empty() && lv_obj_check_type(lv_obj_get_child(obj, 0), &lv_label_class)) {
        add_issue("needs-scrolling", describe(obj));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) check_scroll_needed(lv_obj_get_child(obj, i));
}

}  // namespace

std::string layout_audit_json() {
    g_json = "[";
    lv_obj_t* scr = lv_screen_active();
    lv_obj_update_layout(scr);
    lv_area_t panel;
    lv_obj_get_coords(scr, &panel);
    std::vector<Item> items;
    collect(scr, panel, items);
    for (const Item& it : items) {
        check_text(it);
        check_containment(it);
        check_padding(it);
        check_edge(it);
    }
    for (size_t i = 0; i < items.size(); ++i) {
        for (size_t j = i + 1; j < items.size(); ++j) {
            const Item& a = items[i];
            const Item& b = items[j];
            if (is_ancestor(a.obj, b.obj) || is_ancestor(b.obj, a.obj)) continue;
            if (overlaps(a.visible, b.visible)) add_issue("overlap", describe(a.obj), describe(b.obj));
        }
    }
    check_scroll_needed(scr);
    g_json += "]";
    return g_json;
}

}  // namespace sim
```

Notes for the implementer: `lv_label_class`, `lv_button_class`, `lv_switch_class`, `lv_slider_class`, `lv_menu_page_class` and `lv_textarea_class` are declared by `lvgl.h` in 9.5.0. `LV_LABEL_LONG_DOT` / `LV_LABEL_LONG_WRAP` are the 9.1 compatibility names the firmware already uses. Expect to tune two rules on the first run: if `screen-edge` fires on the arc widget or the page dots, exempt those classes explicitly in `check_edge` with a one-line comment saying why; do not loosen the rule globally.

- [ ] **Step 3: Write `sim/core/ui_probe.h`**

```cpp
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
// Returns 1 on success, 0 if the command or its target was not found.
int ui_command(const char* cmd, const char* arg);

}  // namespace sim
```

- [ ] **Step 4: Write `sim/core/ui_probe.cpp`**

```cpp
#include "ui_probe.h"

#include <cstdlib>
#include <cstring>

#include "firmware_access.h"
#include "ui/ui_manager.h"

namespace sim {
namespace {

lv_obj_t* g_menu_root = nullptr;

bool shown(lv_obj_t* obj) {
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    }
    return true;
}

lv_obj_t* find_label(lv_obj_t* root, const char* text) {
    if (lv_obj_check_type(root, &lv_label_class) && shown(root) &&
        std::strcmp(lv_label_get_text(root), text) == 0) {
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (lv_obj_t* hit = find_label(lv_obj_get_child(root, i), text)) return hit;
    }
    return nullptr;
}

lv_obj_t* clickable_ancestor(lv_obj_t* obj) {
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) return o;
    }
    return nullptr;
}

lv_obj_t* switch_in(lv_obj_t* row) {
    for (uint32_t i = 0; row && i < lv_obj_get_child_count(row); ++i) {
        lv_obj_t* child = lv_obj_get_child(row, i);
        if (lv_obj_check_type(child, &lv_switch_class)) return child;
    }
    return nullptr;
}

// The switch in the row whose label reads `text`. A section heading may carry
// the same words, so keep looking past labels without a switch beside them.
lv_obj_t* find_row_switch(lv_obj_t* root, const char* text) {
    if (lv_obj_check_type(root, &lv_label_class) && shown(root) &&
        std::strcmp(lv_label_get_text(root), text) == 0) {
        if (lv_obj_t* sw = switch_in(lv_obj_get_parent(root))) return sw;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (lv_obj_t* hit = find_row_switch(lv_obj_get_child(root, i), text)) return hit;
    }
    return nullptr;
}

bool parse_state(const char* name, UIState* out) {
    for (int i = 0; i < 64; ++i) {
        if (std::strcmp(firmware_ui_state_name(i), name) == 0) {
            *out = static_cast<UIState>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

int ui_command(const char* cmd, const char* arg) {
    UIManager* ui = UIManager::get_instance();
    if (!ui) return 0;
    lv_obj_t* menu = ui->menu_screen.get_tabview();
    // The menu boots on its main page; remember it before any navigation.
    if (!g_menu_root && menu) g_menu_root = lv_menu_get_cur_main_page(menu);

    if (std::strcmp(cmd, "ready") == 0) {
        ui->set_current_tab(std::atoi(arg));
        ui->switch_to_state(UIState::READY);
        return 1;
    }
    if (std::strcmp(cmd, "menu") == 0) {
        ui->switch_to_state(UIState::MENU);
        lv_menu_set_page(menu, g_menu_root);
        lv_obj_scroll_to_y(g_menu_root, 0, LV_ANIM_OFF);
        return 1;
    }
    if (std::strcmp(cmd, "state") == 0) {
        UIState state;
        if (!parse_state(arg, &state)) return 0;
        ui->switch_to_state(state);
        return 1;
    }
    if (std::strcmp(cmd, "tap") == 0) {
        lv_obj_t* label = find_label(lv_screen_active(), arg);
        lv_obj_t* target = label ? clickable_ancestor(label) : nullptr;
        if (!target) return 0;
        lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
        return 1;
    }
    if (std::strcmp(cmd, "toggle") == 0) {
        lv_obj_t* sw = find_row_switch(lv_screen_active(), arg);
        if (!sw) return 0;
        if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
        return 1;
    }
    return 0;
}

}  // namespace sim
```

`firmware_ui_state_name` returns the state machine's name for each index; out-of-range indices return a non-matching name, so the loop bound only needs to exceed the number of states.

- [ ] **Step 5: Wire the hooks into the build and the page**

`sim/cmake/twin_sources.cmake`, inside `set(TWIN_CORE_SRC ...)` after the `firmware_access.cpp` line:
```cmake
  "${SIM_DIR}/core/layout_audit.cpp"
  "${SIM_DIR}/core/ui_probe.cpp"
```

`sim/core/js_api.cpp`, add includes after `#include "world.h"`:
```cpp
#include "layout_audit.h"
#include "ui_probe.h"
```
and at the end of the file:
```cpp
// Layout regression hooks (sim/qa/layout_audit.mjs).
TWIN_EXPORT int twin_ui(const char* cmd, const char* arg) { return sim::ui_command(cmd, arg ? arg : ""); }
TWIN_EXPORT const char* twin_layout_audit() {
    g_out = sim::layout_audit_json();
    return g_out.c_str();
}
```

`sim/wasm/CMakeLists.txt`, append `_twin_ui _twin_layout_audit` to `TWIN_EXPORTS` before `_malloc`.

`sim/web/src/twin.js`, in the `this.f` table after `operatorActive`:
```js
      ui: c('ui', 'number', ['string', 'string']),
      layoutAudit: c('layout_audit', 'string', []),
```
and methods after `operatorActive()`:
```js
  ui(cmd, arg = '') { return this.f.ui(cmd, String(arg)); }
  layoutAudit() { return JSON.parse(this.f.layoutAudit()); }
```

`sim/web/src/main.js`, inside `window.twinApp` after `get scene3d()`:
```js
  // Layout regression hooks: drive the firmware UI and audit what LVGL laid out.
  ui: (cmd, arg) => app.twin.ui(cmd, arg),
  layoutAudit: () => app.twin.layoutAudit(),
```

- [ ] **Step 6: Write `sim/qa/layout_audit.mjs`**

```js
// Visits every on-device screen in the twin, runs the firmware-side layout audit
// on each and fails if any screen has a defect. PNGs go to sim/qa/out/layout/.
//   node sim/qa/layout_audit.mjs            (needs sim/dist/index.html built)
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'web', 'package.json'));
const { chromium } = require('playwright-core');
const outDir = path.join(here, 'out', 'layout');
fs.mkdirSync(outDir, { recursive: true });
const chrome = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe';

const OK = '\uF00C';
// [name, steps]; each step is [command, argument]. "wait" runs virtual time (ms).
const SCREENS = [
  ['ready-manual', [['ready', 0]]],
  ['ready-single', [['ready', 1]]],
  ['ready-double', [['ready', 2]]],
  ['ready-custom', [['ready', 3]]],
  ['ready-wifi', [['ready', 4]]],
  ['ready-menu', [['ready', 5]]],
  ['edit', [['ready', 2], ['state', 'EDIT']]],
  ['menu', [['menu']]],
  ['menu-scale', [['menu'], ['tap', 'Scale']]],
  ['menu-firmware', [['menu'], ['tap', 'Firmware']]],
  ['menu-bluetooth', [['menu'], ['tap', 'Bluetooth']]],
  ['menu-wifi', [['menu'], ['tap', 'Wi-Fi']]],
  ['menu-display', [['menu'], ['tap', 'Display']]],
  ['menu-grind', [['menu'], ['tap', 'Grind Settings']]],
  ['menu-diagnostics', [['menu'], ['tap', 'Diagnostics']]],
  ['menu-info', [['menu'], ['tap', 'System Info']]],
  ['menu-logs', [['menu'], ['tap', 'Logs & Data']]],
  ['menu-stats', [['menu'], ['tap', 'Lifetime Stats']]],
  ['dialog-motor-test', [['menu'], ['tap', 'Motor Test']]],
  ['dialog-pulse-tune', [['menu'], ['tap', 'Pulse Tune']]],
  ['dialog-purge-logs', [['menu'], ['tap', 'Logs & Data'], ['tap', 'Purge Logs']]],
  ['dialog-factory-reset', [['menu'], ['tap', 'Logs & Data'], ['tap', 'Factory Reset']]],
  ['dialog-clear-warnings', [['menu'], ['tap', 'Diagnostics'], ['tap', 'Clear Warnings']]],
  ['dialog-remote-start', [['menu'], ['tap', 'Wi-Fi'], ['toggle', 'Remote']]],
  ['calibration-empty', [['state', 'CALIBRATION']]],
  ['ota-failed', [['state', 'OTA_UPDATE_FAILED']]],
];

const browser = await chromium.launch({ executablePath: chrome, headless: true, args: ['--disable-gpu'] });
const page = await (await browser.newContext({ viewport: { width: 1400, height: 1000 } })).newPage();
await page.goto(pathToFileURL(path.join(here, '..', 'dist', 'index.html')).href);
await page.waitForFunction(() => window.twinApp && !window.twinApp.busy && window.twinApp.state()?.booted,
  null, { timeout: 90000 });

let failures = 0;
for (const [name, steps] of SCREENS) {
  for (const [cmd, arg = ''] of steps) {
    const ok = await page.evaluate(([c, a]) => window.twinApp.ui(c, a), [cmd, String(arg)]);
    if (!ok) { console.log(`FAIL  ${name}: step ${cmd} "${arg}" found nothing`); failures++; }
    await page.waitForTimeout(400);  // let the firmware loop lay out and draw
  }
  const issues = await page.evaluate(() => window.twinApp.layoutAudit());
  const png = await page.evaluate(() => document.getElementById('screen').toDataURL('image/png'));
  fs.writeFileSync(path.join(outDir, name + '.png'), Buffer.from(png.split(',')[1], 'base64'));
  if (issues.length) failures++;
  console.log(`${issues.length ? 'FAIL' : 'PASS'}  ${name}`);
  for (const i of issues) console.log(`      ${i.rule}: ${i.a}${i.b ? '  vs  ' + i.b : ''}`);
  await page.evaluate(() => window.twinApp.ui('ready', 2));
}
await browser.close();
console.log(failures ? `${failures} screen(s) with layout defects` : 'all screens clean');
process.exit(failures ? 1 : 0);
```

The grind-flow screens (grinding, purge prompt, out-of-beans prompt, Cup moved, completion, error, time mode) need the motor and the plant; they are covered in Task 11 by extending this script with physical-action steps.

- [ ] **Step 7: Build and run; expect FAIL (baseline)**

Run `scratch/build_twin.sh` in WSL, then `npm --prefix sim/web run build`, then `node sim/qa/layout_audit.mjs`.
Expected: exit 1. The menu screens report `overlap` (label vs chevron, label vs switch, header title vs back button), `sticks-out-sideways` (slider labels), `into-padding` (dialog buttons), `word-split` is absent until Task 6 removes `.` from the break characters (it reports words like `5.7g;` only once they can no longer break). Steps that tap renamed labels (`Firmware`, `Pulse Tune`, `Clear Warnings`, `Remote`) report "found nothing" until Task 7. Save the output as the baseline in the commit message body is not needed; keep it in the task notes.

- [ ] **Step 8: Run the host tests** (`scratch/host_tests.sh`). Expected: all pass (nothing in `src/` changed).

- [ ] **Step 9: Commit**

```bash
git add sim/core/layout_audit.h sim/core/layout_audit.cpp sim/core/ui_probe.h sim/core/ui_probe.cpp \
        sim/cmake/twin_sources.cmake sim/core/js_api.cpp sim/wasm/CMakeLists.txt \
        sim/web/src/twin.js sim/web/src/main.js sim/qa/layout_audit.mjs
git commit -m "Add a layout audit to the digital twin"
```

---

### Task 2: Source rules host test

**Files:**
- Create: `tools/tests/ui_text_fit_test.py`

- [ ] **Step 1: Write the test**

```python
"""Host tests for on-screen text fit: LVGL line-break rules, dialog copy, font fitting."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LV_CONF = ROOT / "include/lv_conf.h"
UI_HELPERS = ROOT / "src/ui/ui_helpers.cpp"
UI_SOURCES = sorted((ROOT / "src/ui").rglob("*.cpp"))


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def call_spans(source, name):
    """Text of every call to `name(...)`, parentheses balanced."""
    for match in re.finditer(re.escape(name) + r"\(", source):
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == "(") - (source[end] == ")")
            end += 1
        yield source[match.start():end]


def literals(text):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))


class UiTextFitTest(unittest.TestCase):
    def test_numbers_never_break_at_the_decimal_point(self):
        chars = re.search(r'#define LV_TXT_BREAK_CHARS "([^"]*)"', LV_CONF.read_text()).group(1)
        self.assertNotIn(".", chars, "LVGL would split 5.7g into '5.' and '7g'")

    def test_dialog_text_leaves_line_breaks_to_lvgl(self):
        """Hand-placed single line breaks fight the automatic wrap; paragraphs (\\n\\n) are fine."""
        for path in UI_SOURCES:
            source = path.read_text()
            for call in call_spans(source, "show_confirmation"):
                text = literals(call)
                single = re.sub(r"(\\n){2,}", "", text)
                self.assertNotIn("\\n", single, f"{path.name}: {call[:80]}")
        refill = function((ROOT / "src/ui/controllers/grinding_controller.cpp").read_text(),
                          "void GrindingUIController::handle_refill_result(")
        for call in call_spans(refill, "std::snprintf"):
            self.assertNotIn("\\n", re.sub(r"(\\n){2,}", "", literals(call)))

    def test_pick_font_that_fits_prefers_the_largest_font_that_fits(self):
        body = function(UI_HELPERS.read_text(), "const lv_font_t* pick_font_that_fits(")
        code = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
struct lv_font_t { int px; };
struct lv_point_t { int32_t x, y; };
enum lv_text_flag_t { LV_TEXT_FLAG_NONE = 0 };
#define LV_COORD_MAX 100000
// Stub: every character is half the font size wide.
void lv_text_get_size(lv_point_t* out, const char* text, const lv_font_t* font, int32_t, int32_t,
                      int32_t, lv_text_flag_t) {
    out->x = static_cast<int32_t>(std::strlen(text)) * font->px / 2;
    out->y = font->px;
}
''' + body + r'''
int main() {
    lv_font_t f36{36}, f32{32}, f28{28};
    const lv_font_t* fonts[] = {&f36, &f32, &f28};
    assert(pick_font_that_fits("abcd", 72, fonts, 3) == &f36);     // 4 * 18 = 72 fits
    assert(pick_font_that_fits("abcde", 80, fonts, 3) == &f32);    // 90 > 80, 80 fits
    assert(pick_font_that_fits("abcdefgh", 50, fonts, 3) == &f28); // nothing fits: smallest
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "fit.cpp"
            binary = Path(folder) / "fit"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it; expect FAIL**

Run in WSL: `python3 -m unittest tools.tests.ui_text_fit_test -v` (from the repo root).
Expected: three failures — `'.' found in LV_TXT_BREAK_CHARS`, a `\n` in the first `show_confirmation` call, and `ValueError: substring not found` for `pick_font_that_fits`.

- [ ] **Step 3: Commit the failing test**

```bash
git add tools/tests/ui_text_fit_test.py
git commit -m "Test that on-screen text is left to LVGL to wrap and fit"
```

---

### Task 3: Theme constants and shared helpers

**Files:**
- Modify: `src/config/theme.h` (after the page-indicator block), `src/ui/ui_helpers.h`, `src/ui/ui_helpers.cpp:4-21` and `:91-112`

- [ ] **Step 1: Add constants to `src/config/theme.h`** after `THEME_PAGE_INDICATOR_BOTTOM_OFFSET_PX`:

```c
// Spacing that keeps text inside its control and controls off the panel edge
#define THEME_SCREEN_MARGIN_PX 8                                              // Gap between controls and the panel edge; clears an active-area corner radius up to 27 px
#define THEME_ROW_PAD_HOR_PX 16                                               // Side padding inside buttons and menu rows
#define THEME_ROW_GAP_PX 6                                                    // Gap between a row's label and its chevron or switch
#define THEME_SWITCH_WIDTH_PX 64                                              // Toggle switch in a menu row
#define THEME_SWITCH_HEIGHT_PX 34
#define THEME_DIALOG_BUTTON_GAP_PX 24                                         // Between the two buttons of a dialog; a tap aimed at one must not land on the other
#define THEME_DIALOG_BUTTON_PAD_PX 6                                          // Side padding inside dialog buttons
#define THEME_PAGE_BOTTOM_PAD_PX 16                                           // Below the last control of a scrolling page
#define THEME_SEPARATOR_LABEL_MAX_PCT 60                                      // Widest a section heading may be, so its lines stay visible
```

- [ ] **Step 2: Declare the font picker in `src/ui/ui_helpers.h`** after `set_label_text_float`:

```cpp
// Largest font in `fonts` (ordered largest first) in which `text` fits on one
// line of `max_width` px; the last (smallest) font when none does.
const lv_font_t* pick_font_that_fits(const char* text, int32_t max_width,
                                     const lv_font_t* const* fonts, size_t count);
```

- [ ] **Step 3: Implement it in `src/ui/ui_helpers.cpp`** after `set_label_text_float`:

```cpp
const lv_font_t* pick_font_that_fits(const char* text, int32_t max_width,
                                     const lv_font_t* const* fonts, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        lv_point_t size;
        lv_text_get_size(&size, text, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= max_width) return fonts[i];
    }
    return fonts[count - 1];
}
```

- [ ] **Step 4: Button side padding.** In `style_as_button`, replace `lv_obj_set_style_pad_hor(object, 20, 0);` with:

```cpp
    lv_obj_set_style_pad_hor(object, THEME_ROW_PAD_HOR_PX, 0);
```

- [ ] **Step 5: Fixed-slot dual button row.** Replace the body of `create_dual_button_row` with:

```cpp
lv_obj_t* create_dual_button_row(lv_obj_t* parent, lv_obj_t** left_button, lv_obj_t** right_button, const char* left_name, const char* right_name, lv_color_t left_color, lv_color_t right_color, int height, const lv_font_t* font){
    const int32_t panel_width = lv_display_get_horizontal_resolution(lv_display_get_default());
    const int32_t row_width = panel_width - 2 * THEME_SCREEN_MARGIN_PX;
    // Each button keeps its own half even when the other is hidden, so a
    // button never slides under a finger between steps.
    const int32_t button_width = (row_width - THEME_DIALOG_BUTTON_GAP_PX) / 2;

    lv_obj_t* row_container = lv_obj_create(parent);
    lv_obj_remove_style_all(row_container);
    lv_obj_set_size(row_container, row_width, height);
    lv_obj_clear_flag(row_container, LV_OBJ_FLAG_SCROLLABLE);

    *left_button = create_button(row_container, left_name, left_color, button_width, height, font);
    lv_obj_align(*left_button, LV_ALIGN_LEFT_MID, 0, 0);

    *right_button = create_button(row_container, right_name, right_color, button_width, height, font);
    lv_obj_align(*right_button, LV_ALIGN_RIGHT_MID, 0, 0);

    return row_container;
}
```

Callers that sized or padded the row themselves are fixed in Tasks 8 and 9 (`autotune_screen.cpp:85`, `calibration_screen.cpp` `set_step`).

- [ ] **Step 6: Run the host tests.** Expected: `test_pick_font_that_fits_prefers_the_largest_font_that_fits` now passes; the other two in `ui_text_fit_test.py` still fail; everything else passes.

- [ ] **Step 7: Commit**

```bash
git add src/config/theme.h src/ui/ui_helpers.h src/ui/ui_helpers.cpp
git commit -m "Keep dialog buttons in fixed slots and add a font picker for long titles"
```

---

### Task 4: Menu row builders, separators, page padding

**Files:**
- Modify: `src/ui/screens/menu_screen.h` (private declarations), `src/ui/screens/menu_screen.cpp:1399-1542` and every `create_*_page` function

- [ ] **Step 1: Menu item label.** In `create_menu_item`, after `lv_label_set_text(label, text);` add:

```cpp
    // Take the space left of the chevron; a label that is still too long ends
    // in dots instead of running under the chevron (the layout audit flags it).
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
```
and after the `lv_obj_set_flex_align(cont, ...)` line add:
```cpp
    lv_obj_set_style_pad_column(cont, THEME_ROW_GAP_PX, 0);
```

- [ ] **Step 2: Toggle row.** In `create_toggle_row`, apply the same two label lines and the `pad_column` line, and replace `lv_obj_set_size(*out_toggle, 80, 40);` with:

```cpp
    lv_obj_set_size(*out_toggle, THEME_SWITCH_WIDTH_PX, THEME_SWITCH_HEIGHT_PX);
```

- [ ] **Step 3: Slider row.** In `create_slider_row`, replace `lv_obj_set_style_pad_all(row_container, 20, 0);` with:

```cpp
    lv_obj_set_style_pad_ver(row_container, 20, 0);
    lv_obj_set_style_pad_hor(row_container, THEME_ROW_PAD_HOR_PX, 0);
```
after `lv_label_set_text(*label, text);` add:
```cpp
    lv_obj_set_width(*label, LV_PCT(100));
    lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(*label, LV_LABEL_LONG_DOT);
```
and replace `lv_obj_set_size(*slider, 220, 40);` with:
```cpp
    lv_obj_set_size(*slider, LV_PCT(100), 40);
```

- [ ] **Step 4: Separator.** In `create_separator`, after `lv_obj_set_style_pad_right(separator_label, 10, 0);` add:

```cpp
    // Keep both lines visible: a heading wider than this ends in dots.
    lv_obj_set_style_max_width(separator_label, LV_PCT(THEME_SEPARATOR_LABEL_MAX_PCT), 0);
    lv_label_set_long_mode(separator_label, LV_LABEL_LONG_DOT);
```

- [ ] **Step 5: One setup for every sub-page.** Add to `menu_screen.h` (private, next to `create_separator`):

```cpp
    // Column layout, vertical scrolling and bottom space shared by every menu page.
    void setup_menu_page(lv_obj_t* page);
```
Implement in `menu_screen.cpp` above `create_info_page`:
```cpp
void MenuScreen::setup_menu_page(lv_obj_t* page) {
    lv_obj_set_layout(page, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(page, THEME_PAGE_BOTTOM_PAD_PX, 0);
}
```
In each of `create_info_page`, `create_bluetooth_page`, `create_network_page`, `create_firmware_page`, `create_display_page`, `create_grind_mode_page`, `create_data_page`, `create_stats_page`, `create_diagnostics_page`, replace the five lines `lv_obj_set_layout` / `set_flex_flow` / `set_flex_align` / `set_scroll_dir` / `set_scrollbar_mode` with `setup_menu_page(parent);`. Keep the existing `lv_obj_set_style_pad_all(parent, 0, 0);` lines in `create_info_page` and `create_diagnostics_page`, and move them **above** the `setup_menu_page(parent);` call so the bottom padding survives. Leave `create_scale_page` as it is (it centres its content and does not scroll).

- [ ] **Step 6: Radio rows inside the margin.** In `ui_helpers.cpp` `create_radio_button_group`, replace both `280` literals with `260` (the menu row width). In `menu_screen.cpp` `create_grind_mode_page`, change both `135, 100,  // Width, Height` arguments to `-1, 100,  // Auto width, height`.

- [ ] **Step 7: Rebuild the twin and run the audit.** Expected: the `overlap` defects between labels and chevrons/switches are gone except where copy is still too long (now reported as `truncated` for `Firmware Update`, `Remote start`, `Idle Screensaver`, `Turn Display Off`, `Cup threshold: 50g`, `Motor latency: ...`, `Coast Ratio: ...`, `Brightness: 100%`, and the long section headings). Those are fixed by Task 7.

- [ ] **Step 8: Run the host tests** (expect the two copy-related failures from Task 2 only), then **commit**:

```bash
git add src/ui/screens/menu_screen.h src/ui/screens/menu_screen.cpp src/ui/ui_helpers.cpp
git commit -m "Keep menu row labels clear of chevrons and switches"
```

---

### Task 5: Menu header title

**Files:**
- Modify: `src/ui/screens/menu_screen.h`, `src/ui/screens/menu_screen.cpp:110-150` and `:252-272`

- [ ] **Step 1: Keep pointers.** In `menu_screen.h` private members add:

```cpp
    lv_obj_t* header_title = nullptr;
    lv_obj_t* header_back = nullptr;
    void fit_header_title();
```

- [ ] **Step 2: Store them.** In `create_menu_ui`, after `lv_obj_t* header_label = lv_obj_get_child(header, -1);` add `header_title = header_label;`, and after `lv_obj_t* back_chevron = lv_menu_get_main_header_back_button(menu);` add `header_back = back_chevron;`. Add to the `header_label` styling block:

```cpp
        lv_label_set_long_mode(header_label, LV_LABEL_LONG_DOT);
```

- [ ] **Step 3: Implement the fit.** Below `create_menu_ui`:

```cpp
// The title sits between the back chevron and a spacer of the same width.
// Use the largest title font in which the page name fits there.
void MenuScreen::fit_header_title() {
    if (!header_title || !header_back) return;
    lv_obj_t* header = lv_menu_get_main_header(menu);
    lv_obj_update_layout(header);
    const int32_t space = lv_obj_get_content_width(header) - 2 * lv_obj_get_width(header_back) -
                          2 * lv_obj_get_style_pad_column(header, LV_PART_MAIN);
    static const lv_font_t* const fonts[] = {&lv_font_montserrat_36, &lv_font_montserrat_32,
                                             &lv_font_montserrat_28, &lv_font_montserrat_24};
    lv_obj_set_style_text_font(header_title,
                               pick_font_that_fits(lv_label_get_text(header_title), space, fonts, 4), 0);
    lv_obj_set_width(header_title, space);
}
```

- [ ] **Step 4: Call it on every page change.** In the `changing_page_callback` lambda, as its first line after `lv_obj_t * cur = ...`, add `self->fit_header_title();`. After `lv_menu_set_page(menu, main_page);` in `create_menu_ui` add `fit_header_title();`. (LVGL 9.5 registers its own title-updating `VALUE_CHANGED` handler at menu creation, so it runs before this callback and the title text is already current — `lv_menu.c:644` and `:867`.)

- [ ] **Step 5: Rebuild the twin, run the audit.** Expected: no `overlap` between the header title and the back button on any `menu-*` screen.

- [ ] **Step 6: Host tests, then commit**

```bash
git add src/ui/screens/menu_screen.h src/ui/screens/menu_screen.cpp
git commit -m "Shrink long menu page titles to fit beside the back button"
```

---

### Task 6: Dialogs (confirm screen) and line breaking

**Files:**
- Modify: `include/lv_conf.h:679`, `src/ui/screens/confirm_screen.h`, `src/ui/screens/confirm_screen.cpp`, `src/ui/controllers/grinding_controller.cpp:291-297,371-374,445-461`, `src/ui/controllers/calibration_controller.cpp:114-136`, `src/ui/controllers/menu_controller.cpp:184-197,276-280`, `src/ui/ui_manager.cpp:372-374`

- [ ] **Step 1: Line-break characters.** In `include/lv_conf.h` change

```c
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
```
to
```c
#define LV_TXT_BREAK_CHARS " ,;:-_)]}"   /* no '.': "5.7g" must not split into "5." and "7g" */
```

- [ ] **Step 2: Confirm screen.** In `confirm_screen.h` add a private member `lv_obj_t* message_container = nullptr;`. In `confirm_screen.cpp` `create()`:
- change `lv_obj_t *message_container = lv_obj_create(screen);` to `message_container = lv_obj_create(screen);`
- after `lv_obj_set_width(title_label, LV_PCT(100));` add `lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);` and `lv_obj_set_style_pad_hor(title_label, THEME_SCREEN_MARGIN_PX, 0);`
- after `lv_obj_set_style_border_width(message_container, 0, 0);` add `lv_obj_set_style_pad_hor(message_container, THEME_SCREEN_MARGIN_PX, 0);`
- replace the `create_dual_button_row(...)` line with:

```cpp
    create_dual_button_row(screen, &confirm_button, &cancel_button, "Confirm", "Cancel",
                           lv_color_hex(THEME_COLOR_SUCCESS), lv_color_hex(THEME_COLOR_NEUTRAL),
                           80, &lv_font_montserrat_24);
    lv_obj_set_style_pad_hor(confirm_button, THEME_DIALOG_BUTTON_PAD_PX, 0);
    lv_obj_set_style_pad_hor(cancel_button, THEME_DIALOG_BUTTON_PAD_PX, 0);
```
- add `lv_obj_set_style_pad_bottom(screen, THEME_SCREEN_MARGIN_PX, 0);` after `lv_obj_set_style_pad_ver(screen, 6, 0);`.

In `show(...)`, after `lv_label_set_text(title_label, title);` add:

```cpp
    // One line at the largest size that fits; very long titles wrap at 28 px.
    static const lv_font_t* const title_fonts[] = {&lv_font_montserrat_36, &lv_font_montserrat_32,
                                                   &lv_font_montserrat_28};
    lv_obj_update_layout(screen);
    lv_obj_set_style_text_font(title_label,
                               pick_font_that_fits(title, lv_obj_get_content_width(title_label), title_fonts, 3), 0);
```
and after `lv_label_set_text(message_label, message);` add:

```cpp
    // Centre a short message; a long one starts at the top so its first line
    // is never pushed out of reach above the container.
    lv_obj_update_layout(message_container);
    const bool overflows = lv_obj_get_height(message_label) > lv_obj_get_content_height(message_container);
    lv_obj_set_flex_align(message_container, overflows ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_scroll_to_y(message_container, 0, LV_ANIM_OFF);
```

- [ ] **Step 3: Remove hand line breaks from dialog text.** Replace each message (keep paragraph breaks `\n\n`):

`grinding_controller.cpp`:
```cpp
            "Scale not ready", "No reading from the load cell. Check its wiring, then see Diagnostics in the menu.",
```
```cpp
            "Could not start", "Check the scale and grinder. An update may be running.",
```
```cpp
                "Cup missing?", "The scale is lighter than at the start. Put the cup back, or resume with this one.",
                "RESUME", lv_color_hex(THEME_COLOR_WARNING),
```
```cpp
                "Cup missing?", "The scale is lighter than at the start. Put the cup back, then press " LV_SYMBOL_OK " again.",
```
and the Cup moved message:
```cpp
            std::snprintf(message, sizeof(message),
                          "The scale reads " SYS_WEIGHT_DISPLAY_FORMAT "; it read " SYS_WEIGHT_DISPLAY_FORMAT
                          " when grinding stopped. Put the cup back, or resume from " SYS_WEIGHT_DISPLAY_FORMAT ".",
                          static_cast<double>(now_g), static_cast<double>(paused_g), static_cast<double>(now_g));
            ui_manager_->show_confirmation("Cup moved?", message, "RESUME", lv_color_hex(THEME_COLOR_WARNING),
                                           [this]() { continue_after_refill(true); }, "BACK");
```

`calibration_controller.cpp`:
```cpp
                        has_reading ? "The scale kept moving. Keep it still and empty, then try again."
                                    : "No reading from the load cell. Check its wiring, then try again.",
```
```cpp
                        "Calibration failed", "No valid reading of the weight. The previous calibration is kept.",
```

`menu_controller.cpp`:
```cpp
        has_reading ? "The scale kept moving. Keep it still, then tap TARE again."
                    : "No reading from the load cell. Check its wiring, then tap TARE again.",
```
and the factory reset message (shorter, so the warning is visible without scrolling):
```cpp
        "Resets profiles, calibration, grind history and lifetime statistics.\n\n"
        "This cannot be undone.",
```
and the purge logs message:
```cpp
        "Removes all saved grind logs. Lifetime statistics are kept.\n\n"
        "This cannot be undone.",
        "PURGE",
```
and the Pulse Tune setup message in `handle_autotune` (its bullet list used single line breaks):
```cpp
            "Load beans and put a cup on the scale.\n\n"
            "Takes about 1 minute.",
```

`ui_manager.cpp`:
```cpp
        "Motor stopped", "The control loop stalled, so the motor was stopped. Restart the grinder to use it again.",
```

`status_indicator_controller.cpp` `prompt_firmware_update` (built before the call, so the host test cannot see it; change it anyway):
```cpp
    const String message = "Install " + tag + " now?\n\nThe grinder will restart. Do not remove power.";
```

`menu_controller.cpp` `handle_motor_test` already uses only a paragraph break; no change.

- [ ] **Step 4: Run the host tests.** Expected: all of `ui_text_fit_test.py` passes; the full suite passes. (`purge_flow_test.py`/`refill_flow_test.py` match the comment `// Between the purge and CONTINUE`, which this task does not touch.)

- [ ] **Step 5: Rebuild the twin and run the audit.** Expected: `dialog-*` screens report no `into-padding` or `needs-scrolling`; any remaining `truncated` dialog button is a copy item from Task 7.

- [ ] **Step 6: Commit**

```bash
git add include/lv_conf.h src/ui/screens/confirm_screen.h src/ui/screens/confirm_screen.cpp \
        src/ui/controllers/grinding_controller.cpp src/ui/controllers/calibration_controller.cpp \
        src/ui/controllers/menu_controller.cpp src/ui/controllers/status_indicator_controller.cpp \
        src/ui/ui_manager.cpp
git commit -m "Fit dialog titles, buttons and messages on screen"
```

---

### Task 7: Copy changes (after Q2 is approved)

**Files:** `src/ui/screens/menu_screen.cpp`, `src/ui/screens/ready_screen.cpp`, `src/ui/controllers/menu_controller.cpp`, `src/network/device_web_server.cpp:725`, `src/network/device_page.h` (2 strings), `src/bluetooth/manager.cpp:728`, `tools/web-flasher/flasher.js:298`, `tools/tests/ota_web_test.mjs:54`

- [ ] **Step 1: Apply the copy table** exactly as approved. Exact edits in `menu_screen.cpp`:

| Line (now) | Replace with |
|------------|--------------|
| `create_menu_item(main_page, "Tune Pulses")` | `create_menu_item(main_page, "Pulse Tune")` |
| `create_menu_item(main_page, "Firmware Update")` | `create_menu_item(main_page, "Firmware")` |
| `create_separator(parent, "Remote Control")` | `create_separator(parent, "Remote")` |
| `create_toggle_row(parent, "Remote start", &remote_start_toggle)` | `create_toggle_row(parent, "Remote", &remote_start_toggle)` |
| `"FORGET NETWORK"` (button) | `"FORGET WI-FI"` |
| `create_separator(parent, "Custom Image")` | `create_separator(parent, "Screensaver")` |
| `create_toggle_row(parent, "Idle Screensaver", ...)` | `create_toggle_row(parent, "On idle", ...)` |
| `create_toggle_row(parent, "Turn Display Off", ...)` | `create_toggle_row(parent, "Display off", ...)` |
| `create_separator(parent, "Mode Selection")` | `create_separator(parent, "Mode")` |
| `create_slider_row(parent, "Cup threshold", ...)` | `create_slider_row(parent, "Trigger", ...)` |
| `create_separator(parent, "Motor Response")` (2x) | `create_separator(parent, "Motor")` |
| `create_slider_row(parent, "Motor latency", ...)` | `create_slider_row(parent, "Latency", ...)` |
| `create_separator(parent, "Coast Compensation")` | `create_separator(parent, "Coast")` |
| `create_slider_row(parent, "Coast Ratio", ...)` | `create_slider_row(parent, "Coast", ...)` |
| `create_separator(parent, "Lifetime Statistics");` | delete the line |
| `create_separator(parent, "Load Cell Status")` | `create_separator(parent, "Load Cell")` |
| `"Reset Diagnostics"` (button) | `"Clear Warnings"` |
| `"Auto-disable in: %lu min"` | `"Off in %lu min"` |
| `"Brightness: %d%%"` | `"Normal: %d%%"` |
| `"Motor latency: %.0fms"` | `"Latency: %.0fms"` |
| `"Cup threshold: %.0fg"` | `"Trigger: %.0fg"` |
| `"Coast Ratio: %d%%"` | `"Coast: %d%%"` |

Also add `lv_obj_set_width(ble_timer_label, 260); lv_label_set_long_mode(ble_timer_label, LV_LABEL_LONG_WRAP);` after the `ble_timer_label` font line in `create_bluetooth_page`.

`menu_controller.cpp`: dialog title `"FORGET NETWORK"` → `"FORGET WI-FI"`; the `"REMOTE START"` title is unchanged (255 px at 32 px fits), its button `"TURN ON"` → `"ENABLE"`; `"Auto-Tune Setup"` → `"Pulse Tune"`; `"Reset Diagnostics"` → `"Clear Warnings"`.

`ready_screen.cpp`: `"STARTING WI-FI SETUP"` → `"STARTING SETUP"`, and in `create_wifi_page` after the `wifi_status_label` align line add:
```cpp
    lv_obj_set_width(wifi_status_label, LV_PCT(100));
    lv_label_set_long_mode(wifi_status_label, LV_LABEL_LONG_WRAP);
```

The four non-UI strings: replace `Menu > Firmware Update` with `Menu > Firmware` (and `Menu &gt; Firmware Update` with `Menu &gt; Firmware`) in `device_web_server.cpp`, `device_page.h`, `bluetooth/manager.cpp`, `tools/web-flasher/flasher.js` and `tools/tests/ota_web_test.mjs`.

- [ ] **Step 2: Host tests** (all pass, including `ota_web_test.mjs` with its updated string). **Rebuild the twin, run the audit:** all `menu-*` and `dialog-*` screens PASS.

- [ ] **Step 3: Commit**

```bash
git add -u src tools
git commit -m "Shorten on-screen labels that did not fit"
```

---

### Task 8: Pulse Tune screen

**Files:** Modify `src/ui/screens/autotune_screen.cpp` (whole `create`, `show_success_screen`, `show_failure_screen`)

- [ ] **Step 1: Flex column layout.** Replace `create()` with a column: title, console (grows), result (grows, alternates with console), button row. Key lines:

```cpp
void AutoTuneScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_top(screen, 20, 0);
    lv_obj_set_style_pad_bottom(screen, 10, 0);
    lv_obj_set_style_pad_row(screen, 12, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int32_t content_width = lv_display_get_horizontal_resolution(lv_display_get_default()) -
                                  2 * THEME_SCREEN_MARGIN_PX;

    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Pulse Tune");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    // Console: fills the space between the title and the buttons.
    console_container = lv_obj_create(screen);
    lv_obj_remove_style_all(console_container);
    lv_obj_set_width(console_container, content_width);
    lv_obj_set_flex_grow(console_container, 1);
    lv_obj_clear_flag(console_container, LV_OBJ_FLAG_SCROLLABLE);

    console_textarea = lv_textarea_create(console_container);
    lv_obj_set_size(console_textarea, LV_PCT(100), LV_PCT(100));
    lv_textarea_set_text(console_textarea, "");
    lv_obj_set_style_text_font(console_textarea, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(console_textarea, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_color(console_textarea, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_border_width(console_textarea, 1, 0);
    lv_obj_set_style_border_color(console_textarea, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(console_textarea, 8, 0);
    lv_textarea_set_cursor_click_pos(console_textarea, false);
    lv_obj_add_flag(console_textarea, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Result: message, then two lines whose order depends on success or failure.
    result_container = lv_obj_create(screen);
    lv_obj_remove_style_all(result_container);
    lv_obj_set_width(result_container, content_width);
    lv_obj_set_flex_grow(result_container, 1);
    lv_obj_clear_flag(result_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(result_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(result_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(result_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(result_container, 10, 0);

    message_label = lv_label_create(result_container);
    lv_obj_set_width(message_label, LV_PCT(100));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);

    final_latency_label = lv_label_create(result_container);
    lv_obj_set_style_text_font(final_latency_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);

    previous_latency_label = lv_label_create(result_container);
    lv_obj_set_style_text_font(previous_latency_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(0x888888), 0);

    button_row = create_dual_button_row(screen, &cancel_button, &ok_button,
                                        LV_SYMBOL_CLOSE, LV_SYMBOL_OK,
                                        lv_color_hex(0x888888), lv_color_hex(THEME_COLOR_SUCCESS),
                                        80, &lv_font_montserrat_32);

    visible = false;
    current_state = AutoTuneScreenState::CONSOLE;
    lv_obj_add_flag(result_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}
```

- [ ] **Step 2: Result screens.** In `show_success_screen`, replace the title text with `"Tune Complete"` (single line at 36 px would be 259 px with "!", which crowds; without it fits) and add `lv_obj_move_to_index(final_latency_label, 1); lv_obj_move_to_index(previous_latency_label, 2);`. In `show_failure_screen`:
- title `"Tune Failed"`;
- replace the whole message-building block with
```cpp
    lv_label_set_text(message_label, (error_message && error_message[0])
                                         ? error_message
                                         : "No reliable pulse found. Check power, beans and the cup.");
```
- remove the three `lv_obj_align(...)` calls;
- order "Using default" above the value: `lv_obj_move_to_index(previous_latency_label, 1); lv_obj_move_to_index(final_latency_label, 2);`.

- [ ] **Step 3: Audit coverage.** Add to `SCREENS` in `sim/qa/layout_audit.mjs` (the console and both results are reached by real tuning in Task 11; here add the setup dialog only — already present as `dialog-pulse-tune`).

- [ ] **Step 4: Rebuild the twin; run a tune in the page** (Menu → Pulse Tune → START with beans and a cup; then with the "Relay stuck off" fault for the failure screen) and confirm by screenshot that the console's last line is above the ✕ button, the success screen shows "New Motor Latency:", and the failure screen shows the message, "Using default:" and the value. Host tests; commit:

```bash
git add src/ui/screens/autotune_screen.cpp
git commit -m "Lay out the Pulse Tune screens so every line is visible"
```

---

### Task 9: Calibration screen

**Files:** Modify `src/ui/screens/calibration_screen.cpp:18-82,94-156`

- [ ] **Step 1: Flex layout.** In `create()`, after `lv_obj_set_style_pad_ver(screen, 6, 0);` add:

```cpp
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
```
Create a body container between the two button rows and parent the four text labels and `weight_label` to it instead of `screen`:
```cpp
    // Title, instructions and readings stack in the space between the button rows.
    lv_obj_t* body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 10, 0);
```
Then change `lv_label_create(screen)` to `lv_label_create(body)` for `title_label`, `instruction_label`, `weight_label`, `noise_status_label`, `noise_metric_label`, and delete their five `lv_obj_align(..., LV_ALIGN_CENTER, 0, N)` lines. Delete `lv_obj_align(bottom_button_row, LV_ALIGN_BOTTOM_MID, 0, 0);`. The hidden `weight_input` textarea gets `lv_obj_add_flag(weight_input, LV_OBJ_FLAG_IGNORE_LAYOUT);` so it takes no space.

- [ ] **Step 2: Step texts and padding.** In `set_step`: delete both `lv_obj_set_style_pad_hor(top_button_row, ...)` lines; change `"Remove all weight\nPress OK when empty"` to `"Remove all weight, then press " LV_SYMBOL_OK`; change `"Place known weight\nAdjust weight value\n with +/- buttons"` to `"Place a known weight and set its value with - and +"`; change the noise text to `"Let vibrations settle. Don't touch the grinder or scale. About 5-10 s."`. Give `instruction_label` `lv_obj_set_width(instruction_label, LV_PCT(92)); lv_label_set_long_mode(instruction_label, LV_LABEL_LONG_WRAP);` in `create()`.

- [ ] **Step 3: Audit coverage.** In `sim/qa/layout_audit.mjs` add after `calibration-empty`:
```js
  ['calibration-weight', [['state', 'CALIBRATION'], ['tap', OK], ['wait', 3000]]],
```
(The `wait` step needs handling in the loop: add `if (cmd === 'wait') { await page.waitForTimeout(+arg); continue; }` before the `ui` call.)

- [ ] **Step 4: Rebuild, audit (calibration screens PASS), host tests, commit**

```bash
git add src/ui/screens/calibration_screen.cpp sim/qa/layout_audit.mjs
git commit -m "Stack calibration text between fixed button rows"
```

---

### Task 10: Status icons only where they fit (after Q1)

**Files:** Modify `src/ui/controllers/status_indicator_controller.h`, `src/ui/controllers/status_indicator_controller.cpp:13-70`, `src/ui/ui_manager.cpp` (`switch_to_state`)

- [ ] **Step 1: One container for the icons.** In the header add a private member `lv_obj_t* bar_ = nullptr;` and a public method:
```cpp
    // Icons show only on screens with room for them at the top.
    void on_state_changed(UIState state);
```
(include `"../../system/state_machine.h"` for `UIState`). In `build()`, create the bar first and parent the four icons to it instead of `lv_scr_act()`:
```cpp
    bar_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(bar_);
    lv_obj_set_size(bar_, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bar_, LV_OBJ_FLAG_SCROLLABLE);
```
(icon alignment offsets are unchanged: they are relative to the full-screen bar.)

- [ ] **Step 2: Visibility per state.**
```cpp
void StatusIndicatorController::on_state_changed(UIState state) {
    if (!bar_) return;
    // Only the ready screen has an empty strip at the top (Q1 decision).
    if (state == UIState::READY) {
        lv_obj_clear_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    }
}
```
If Q1 is answered "ready screen and main menu with a strip", replace this task's Step 2 with the strip variant before starting.

- [ ] **Step 3: Call it.** In `UIManager::switch_to_state`, next to the `grinding_controller_->on_state_changed(new_state);` block, add:
```cpp
    if (status_indicator_controller_) {
        status_indicator_controller_->on_state_changed(new_state);
    }
```

- [ ] **Step 4: Audit with the warning icon on.** Extend `sim/qa/layout_audit.mjs` with a second pass that first clicks "Bump scale" ten times (DOM button in `#actions`, as `sim/qa/scenarios.mjs` does) to raise the mechanical-instability warning, then re-runs `edit`, `menu`, `calibration-empty`, `dialog-motor-test`. Expected: no `overlap` involving the warning icon.

- [ ] **Step 5: Host tests, commit**

```bash
git add src/ui/controllers/status_indicator_controller.h src/ui/controllers/status_indicator_controller.cpp src/ui/ui_manager.cpp sim/qa/layout_audit.mjs
git commit -m "Show status icons only on the ready screen"
```

---

### Task 11: Remaining screens and grind-flow audit

**Files:** `src/ui/screens/purge_confirm_screen.cpp`, `refill_confirm_screen.cpp`, `ota_update_failed_screen.cpp`, `grinding_screen_chart.cpp:22`, `src/ui/controllers/grinding_controller.cpp:37`, `sim/qa/layout_audit.mjs`

- [ ] **Step 1: Purge prompt.** After `lv_obj_set_style_border_width(message_container, 0, 0);` add `lv_obj_set_style_pad_all(message_container, 0, 0);`. Title: replace the font line with a fit:
```cpp
    static const lv_font_t* const title_fonts[] = {&lv_font_montserrat_36, &lv_font_montserrat_32};
    lv_obj_set_style_text_font(title_label,
        pick_font_that_fits("Grinder Purged", lv_display_get_horizontal_resolution(lv_display_get_default()) -
                                                  2 * THEME_SCREEN_MARGIN_PX, title_fonts, 2), 0);
```

- [ ] **Step 2: Out-of-beans prompt.** In `RefillConfirmScreen::set_status`, show the status instead of the message so the column fits above the buttons:
```cpp
void RefillConfirmScreen::set_status(const char* status) {
    if (!status_label) return;
    const bool show = status && status[0];
    lv_label_set_text(status_label, show ? status : "");
    // The status replaces the instruction; both at once do not fit above the buttons.
    if (show) {
        lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    }
}
```

- [ ] **Step 3: Update Failed screen.** Set `lv_obj_set_style_pad_gap(screen, 12, 0);`; title via `pick_font_that_fits` over {36, 32}; message text `"The update failed. The previous version is still running."`; details font `&lv_font_montserrat_16`. Audit screen `ota-failed` must PASS (no `needs-scrolling`, button inside the panel).

- [ ] **Step 4: Chart margin.** In `grinding_screen_chart.cpp` replace `lv_obj_set_size(chart, LV_PCT(100), 140);` with
```cpp
    lv_obj_set_size(chart, lv_display_get_horizontal_resolution(lv_display_get_default()) - 2 * THEME_SCREEN_MARGIN_PX, 140);
```

- [ ] **Step 5: Grind button icon size.** In `grinding_controller.cpp` `build_controls`, change the grind icon font from `&lv_font_montserrat_24` to `&lv_font_montserrat_32` to match the pulse button.

- [ ] **Step 6: Grind-flow audit.** Add a third section to `sim/qa/layout_audit.mjs` that drives the plant with DOM actions and taps the round buttons by panel coordinate, auditing at each stop. Helper and sequence:
```js
const act = (label) => page.evaluate((t) => [...document.querySelectorAll('#actions button')]
  .find((b) => b.textContent === t).click(), label);
async function tapPanel(x, y) {
  const r = await page.locator('#screen').boundingBox();
  await page.mouse.move(r.x + (x * r.width) / 280, r.y + (y * r.height) / 456);
  await page.mouse.down(); await page.waitForTimeout(150); await page.mouse.up();
}
const phase = () => page.evaluate(() => window.twinApp.state().ui_state_name);
async function until(names, ms = 60000) {
  await page.waitForFunction((n) => n.includes(window.twinApp.state().ui_state_name), names, { timeout: ms, polling: 'raf' });
}
async function audit(name) { /* same body as the per-screen block above: audit, PNG, print */ }

await page.evaluate(() => window.twinApp.ui('ready', 2));
await act('Place cup'); await act('Load beans'); await page.waitForTimeout(1500);
await tapPanel(140, 396); await until(['PURGE_CONFIRM']); await audit('flow-purge');
await tapPanel(200, 396); await until(['GRINDING']); await page.waitForTimeout(1500); await audit('flow-grinding');
await until(['GRIND_COMPLETE'], 90000); await audit('flow-complete');
await tapPanel(140, 396); await until(['READY']);
await act('Remove cup'); await act('Empty cup'); await act('Place cup'); await page.waitForTimeout(1500);
await tapPanel(140, 396); await until(['REFILL_CONFIRM'], 90000); await audit('flow-refill');
await act('Load beans'); await act('Press on scale'); await page.waitForTimeout(3000);
await tapPanel(200, 396); await until(['CONFIRM']); await audit('flow-cup-moved');
await page.evaluate(() => window.twinApp.ui('tap', 'BACK')); await act('Release press');
await tapPanel(80, 396); await until(['GRIND_TIMEOUT']); await audit('flow-error');
```
Refactor the per-screen loop's audit/PNG/print block into the `audit(name)` function so both sections share it (move it above the `SCREENS` loop and call it there).

- [ ] **Step 7: Rebuild, run the full audit; expected `all screens clean` and exit 0.** Host tests; commit:

```bash
git add src/ui sim/qa/layout_audit.mjs
git commit -m "Fit the purge, out-of-beans and update-failed screens and audit the grind flow"
```

---

### Task 12: Docs, firmware builds, live check

- [ ] **Step 1: Docs.** Update every renamed label in `CLAUDE.md` (UIManager bullet: "Tune Pulses" → "Pulse Tune", "Firmware Update" → "Firmware"; "Menu → Firmware Update → Allow Update" → "Menu → Firmware → Allow Update"), `README.md`, `docs/USER_GUIDE.md`, `docs/FIRMWARE_SETUP.md`, `docs/TROUBLESHOOTING.md`, `docs/WIFI_ARCHITECTURE.md`, `docs/HOW_IT_WORKS.md`. Leave `CHANGELOG.md` history entries as written; add a new entry describing the label changes. Add a short "Layout audit" section to `sim/README.md` under "Checks": `node sim/qa/layout_audit.mjs` and what each rule means.

- [ ] **Step 2: Rebuild the twin page** (`scratch/build_twin.sh`, `npm --prefix sim/web run build`) so `sim/dist/index.html` matches the firmware, and run `node sim/qa/layout_audit.mjs` one final time (exit 0).

- [ ] **Step 3: Full host test suite** (`scratch/host_tests.sh`). All pass.

- [ ] **Step 4: Firmware builds.** `tools/venv/Scripts/python.exe tools/grinder.py build --hardware v1 --jobs 8` and `--hardware v2`. Both succeed; report the build number to the user.

- [ ] **Step 5: Commit**

```bash
git add -A docs CLAUDE.md README.md CHANGELOG.md sim/README.md sim/dist/index.html
git commit -m "Document the shorter labels and the layout audit; rebuild the twin page"
```

- [ ] **Step 6: Live check on the grinder (user).** Upload with `python tools/grinder.py upload` after Menu → Firmware → Allow Update. Walk: every menu page, each dialog, a grind with purge, an out-of-beans pause, calibration, Pulse Tune. Watch the rounded corners and any text near the edges; the twin cannot show the glass.

---

## Not in this plan

- Time mode shows grams in the ring while grinding although `GrindingScreenArc::update_progress` is written to show elapsed seconds (`update_current_weight` overwrites it). Behaviour, not layout; worth its own ticket.
- MANUAL title styling (white 36 px versus grey 32 px on the profile pages) and the "Info" page title versus the "System Info" menu entry: consistency only, nothing clipped.
