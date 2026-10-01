#include "layout_audit.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <lvgl.h>
#include <misc/lv_area_private.h>  // lv_area_intersect is private API in LVGL 9.5

#include "config/constants.h"

// The firmware gains this constant with the screen-margin change; until then the
// audit uses the same 8 px so the screen-edge rule is in force from the start.
#ifndef THEME_SCREEN_MARGIN_PX
#define THEME_SCREEN_MARGIN_PX 8
#endif

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
