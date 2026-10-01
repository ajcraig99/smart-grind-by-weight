#include "layout_audit.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <lvgl.h>
#include <core/lv_obj_private.h>             // lv_obj_t::w_layout and h_layout
#include <misc/lv_area_private.h>            // lv_area_intersect is private API in LVGL 9.5
#include <widgets/label/lv_label_private.h>  // lv_label_t::dot_begin

#include "config/constants.h"

namespace sim {
namespace {

struct Item {
    lv_obj_t* obj;
    lv_area_t area;           // where LVGL placed it
    bool leaf;
    lv_area_t drawn;          // a label's drawn text, otherwise the same as area
    lv_area_t drawn_visible;  // the part of `drawn` not scrolled out of view
    bool drawn_seen;          // false when `drawn` is wholly out of view
};

std::string g_json;

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
    if (text.size() > 40) {
        size_t cut = 40;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;  // UTF-8 boundary
        text = text.substr(0, cut) + "...";
    }
    return std::string(kind) + " \"" + text + "\"" + buf;
}

void add_issue(const char* rule, const std::string& a, const std::string& b = "") {
    auto esc = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            const unsigned char byte = static_cast<unsigned char>(c);
            if (byte < 0x20) {  // control characters, newline included
                char hex[8];
                std::snprintf(hex, sizeof(hex), "\\u%04x", byte);
                out += hex;
                continue;
            }
            if (c == '"' || c == '\\') out += '\\';
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

// Whether a label wraps its text at its content width, as lv_label.c decides when it
// sizes the label: it is unwrapped only when its width follows the content and no
// layout sets that width.
bool wraps_at_box(lv_obj_t* label) {
    return lv_obj_get_style_width(label, LV_PART_MAIN) != LV_SIZE_CONTENT || label->w_layout;
}

// The width lv_label.c wraps at, clamped to the label's max width as LVGL clamps it.
int32_t wrap_width(lv_obj_t* label) {
    const int32_t width = wraps_at_box(label) ? lv_obj_get_content_width(label) : LV_COORD_MAX;
    return std::min(width, lv_obj_get_style_max_width(label, LV_PART_MAIN));
}

// The size of a label's text block, wrapped as LVGL wraps it.
lv_point_t text_block(lv_obj_t* label) {
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(label, LV_PART_MAIN), wrap_width(label), LV_TEXT_FLAG_NONE);
    return size;
}

// Where a label's text is drawn: the text block placed in the label's content area
// by its text alignment, clipped to that area (LVGL never draws a label's text
// outside it). A label's box is often far wider than its text. Empty text gives an
// area with x2 < x1.
lv_area_t text_extent(lv_obj_t* label) {
    lv_area_t content;
    lv_obj_get_content_coords(label, &content);
    const lv_point_t size = text_block(label);
    lv_area_t text = content;
    switch (lv_obj_get_style_text_align(label, LV_PART_MAIN)) {
        case LV_TEXT_ALIGN_CENTER:
            text.x1 = content.x1 + (lv_area_get_width(&content) - size.x) / 2;
            break;
        case LV_TEXT_ALIGN_RIGHT:
            text.x1 = content.x2 + 1 - size.x;
            break;
        default:  // LEFT, AUTO
            break;
    }
    text.x2 = text.x1 + size.x - 1;
    text.y2 = text.y1 + size.y - 1;
    lv_area_t clipped;
    if (!lv_area_intersect(&clipped, &text, &content)) {
        clipped = content;
        clipped.x2 = clipped.x1 - 1;
    }
    return clipped;
}

void collect(lv_obj_t* obj, lv_area_t view, std::vector<Item>& out) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_area_t seen;
    if (!lv_area_intersect(&seen, &area, &view)) return;  // scrolled out of view
    const uint32_t children = lv_obj_get_child_count(obj);
    if (obj != lv_screen_active() && (children == 0 || filled(obj)) && !backdrop(area)) {
        Item it{obj, area, children == 0, area, seen, true};
        if (lv_obj_check_type(obj, &lv_label_class)) {
            it.drawn = text_extent(obj);
            it.drawn_seen = lv_area_intersect(&it.drawn_visible, &it.drawn, &view);
        }
        out.push_back(it);
    }
    lv_area_t child_view = view;
    if (scrolls(obj)) lv_area_intersect(&child_view, &view, &area);
    for (uint32_t i = 0; i < children; ++i) collect(lv_obj_get_child(obj, i), child_view, out);
}

// Text: a label cut off with dots, a single word broken across lines, or wrapped
// text taller than a label whose height does not follow its content.
void check_text(const Item& it) {
    if (!lv_obj_check_type(it.obj, &lv_label_class)) return;
    constexpr uint32_t kNoDots = 0xFFFFFFFF;  // LV_LABEL_DOT_BEGIN_INV, private to lv_label.c
    const char* text = lv_label_get_text(it.obj);
    const int32_t width = lv_obj_get_content_width(it.obj);
    const lv_label_long_mode_t mode = lv_label_get_long_mode(it.obj);
    if (mode == LV_LABEL_LONG_DOT &&
        (reinterpret_cast<lv_label_t*>(it.obj)->dot_begin != kNoDots || text_width(it.obj, text) > width)) {
        add_issue("truncated", describe(it.obj));
    }
    if (mode == LV_LABEL_LONG_WRAP && wraps_at_box(it.obj)) {
        const bool fixed_height = lv_obj_get_style_height(it.obj, LV_PART_MAIN) != LV_SIZE_CONTENT || it.obj->h_layout;
        if (fixed_height && text_block(it.obj).y > lv_obj_get_content_height(it.obj)) {
            add_issue("clipped-bottom", describe(it.obj));
        }
        const int32_t wrap = wrap_width(it.obj);
        std::string word;
        for (const char* p = text;; ++p) {
            if (*p == ' ' || *p == '\n' || *p == '\0') {
                if (!word.empty() && text_width(it.obj, word.c_str()) > wrap) {
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
    if (it.drawn.x2 < it.drawn.x1) return;  // empty label: nothing drawn
    const int32_t panel_w = lv_display_get_horizontal_resolution(lv_display_get_default());
    if (it.drawn.x1 < THEME_SCREEN_MARGIN_PX || it.drawn.x2 > panel_w - 1 - THEME_SCREEN_MARGIN_PX) {
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
            if (!a.drawn_seen || !b.drawn_seen) continue;
            if (overlaps(a.drawn_visible, b.drawn_visible)) add_issue("overlap", describe(a.obj), describe(b.obj));
        }
    }
    check_scroll_needed(scr);
    g_json += "]";
    return g_json;
}

}  // namespace sim
