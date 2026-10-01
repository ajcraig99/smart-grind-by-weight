#include "refill_confirm_screen.h"
#include <cstdio>

namespace {
lv_obj_t* create_centered_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, LV_PCT(92));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}
}  // namespace

void RefillConfirmScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    // Leave the bottom 120 px free so the shared STOP and CONTINUE buttons
    // below stay touchable (as on the purge prompt).
    lv_obj_set_width(screen, LV_PCT(100));
    lv_obj_set_height(screen, lv_display_get_vertical_resolution(lv_display_get_default()) - 120);
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_top(screen, 16, 0);
    lv_obj_set_style_pad_bottom(screen, 8, 0);
    lv_obj_set_style_pad_left(screen, 0, 0);
    lv_obj_set_style_pad_right(screen, 0, 0);
    lv_obj_set_style_pad_row(screen, 6, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    title_label = create_centered_label(screen, &lv_font_montserrat_36, THEME_COLOR_WARNING);
    lv_label_set_text(title_label, "Out of beans");

    // Dose so far, large, with the target underneath.
    weight_label = create_centered_label(screen, &lv_font_montserrat_56, THEME_COLOR_TEXT_PRIMARY);
    lv_obj_set_style_pad_top(weight_label, 8, 0);
    lv_label_set_text(weight_label, "0.0g");
    target_label = create_centered_label(screen, &lv_font_montserrat_24, THEME_COLOR_TEXT_SECONDARY);
    lv_label_set_text(target_label, "of 0.0g");

    message_label = create_centered_label(screen, &lv_font_montserrat_24, THEME_COLOR_TEXT_PRIMARY);
    lv_obj_set_style_pad_top(message_label, 14, 0);
    lv_label_set_text(message_label, "");

    status_label = create_centered_label(screen, &lv_font_montserrat_24, THEME_COLOR_ACCENT);
    lv_obj_set_style_pad_top(status_label, 8, 0);
    lv_label_set_text(status_label, "");
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void RefillConfirmScreen::show(bool no_beans_at_start, float target_weight_g, float current_weight_g) {
    lv_label_set_text(title_label, no_beans_at_start ? "No beans" : "Out of beans");
    lv_label_set_text(message_label, no_beans_at_start
                                         ? "Load beans, then press " LV_SYMBOL_OK " to start."
                                         : "Add beans, then press " LV_SYMBOL_OK " to finish the dose.");
    char target_text[24];
    std::snprintf(target_text, sizeof(target_text), "of " SYS_WEIGHT_DISPLAY_FORMAT, static_cast<double>(target_weight_g));
    lv_label_set_text(target_label, target_text);
    update_weight(current_weight_g);
    set_status(nullptr);

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(screen);
    visible = true;
}

void RefillConfirmScreen::hide() {
    if (screen) lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void RefillConfirmScreen::update_weight(float weight_g) {
    if (!weight_label) return;
    // A cup lifted off the scale reads negative; the dose so far is never below zero.
    if (weight_g < 0.0f) weight_g = 0.0f;
    char weight_text[16];
    std::snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, static_cast<double>(weight_g));
    lv_label_set_text(weight_label, weight_text);
}

void RefillConfirmScreen::set_status(const char* status) {
    if (!status_label) return;
    if (!status || !status[0]) {
        lv_label_set_text(status_label, "");
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(status_label, status);
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}
