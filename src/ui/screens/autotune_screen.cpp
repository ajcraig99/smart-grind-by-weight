#include "autotune_screen.h"
#include "../ui_helpers.h"
#include <Arduino.h>
#include <cstring>

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
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int32_t content_width = lv_display_get_horizontal_resolution(lv_display_get_default()) -
                                  2 * THEME_SCREEN_MARGIN_PX;

    // Title label (shared across all screens)
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Pulse Tune");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    // === Console Screen === fills the space between the title and the buttons.
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

    // === Result Screen === message, then two lines whose order depends on success or failure.
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
    lv_label_set_text(message_label, "New Motor Latency:");
    lv_obj_set_width(message_label, LV_PCT(100));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);

    final_latency_label = lv_label_create(result_container);
    lv_label_set_text(final_latency_label, "110 ms");
    lv_obj_set_style_text_font(final_latency_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);

    previous_latency_label = lv_label_create(result_container);
    lv_label_set_text(previous_latency_label, "Previous Value: 150 ms");
    lv_obj_set_style_text_font(previous_latency_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(previous_latency_label, LV_TEXT_ALIGN_CENTER, 0);

    // Buttons: the row keeps fixed left and right slots (cancel left, OK right).
    button_row = create_dual_button_row(screen, &cancel_button, &ok_button,
                                        LV_SYMBOL_CLOSE, LV_SYMBOL_OK,
                                        lv_color_hex(0x888888), lv_color_hex(THEME_COLOR_SUCCESS),
                                        80, &lv_font_montserrat_32);

    visible = false;
    current_state = AutoTuneScreenState::CONSOLE;
    lv_obj_add_flag(result_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void AutoTuneScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void AutoTuneScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void AutoTuneScreen::show_console_screen() {
    current_state = AutoTuneScreenState::CONSOLE;

    // Clear console
    lv_textarea_set_text(console_textarea, "");

    // Show console elements
    lv_obj_clear_flag(console_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    // Hide result screen
    lv_obj_add_flag(result_container, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Pulse Tune");
}

void AutoTuneScreen::show_success_screen(float new_latency_ms, float previous_latency_ms) {
    current_state = AutoTuneScreenState::RESULT;

    // Hide console screen
    lv_obj_add_flag(console_container, LV_OBJ_FLAG_HIDDEN);

    // Show result elements
    lv_obj_clear_flag(result_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Tune\nComplete");
    lv_label_set_text(message_label, "New Motor Latency:");

    char latency_text[32];
    snprintf(latency_text, sizeof(latency_text), "%.0f ms", new_latency_ms);
    lv_label_set_text(final_latency_label, latency_text);

    char previous_text[64];
    snprintf(previous_text, sizeof(previous_text), "Previous Value: %.0f ms", previous_latency_ms);
    lv_label_set_text(previous_latency_label, previous_text);

    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);

    // Message, the new value, then the previous one.
    lv_obj_move_to_index(final_latency_label, 1);
    lv_obj_move_to_index(previous_latency_label, 2);
}

void AutoTuneScreen::show_failure_screen(const char* error_message) {
    current_state = AutoTuneScreenState::RESULT;

    // Hide console screen
    lv_obj_add_flag(console_container, LV_OBJ_FLAG_HIDDEN);

    // Show result elements
    lv_obj_clear_flag(result_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Tune Failed");

    lv_label_set_text(message_label, (error_message && error_message[0])
                                         ? error_message
                                         : "No reliable pulse found. Check power, beans and the cup.");

    char default_text[32];
    snprintf(default_text, sizeof(default_text), "%.0f ms", (float)GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS);
    lv_label_set_text(final_latency_label, default_text);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_WARNING), 0);

    lv_label_set_text(previous_latency_label, "Using default:");

    // Message, "Using default:", then the value.
    lv_obj_move_to_index(previous_latency_label, 1);
    lv_obj_move_to_index(final_latency_label, 2);
}

void AutoTuneScreen::append_console_message(const char* message) {
    if (current_state != AutoTuneScreenState::CONSOLE) {
        return;
    }

    // Append new message with newline
    lv_textarea_add_text(console_textarea, message);
    lv_textarea_add_text(console_textarea, "\n");

    // Auto-scroll to bottom
    lv_obj_scroll_to_y(console_textarea, LV_COORD_MAX, LV_ANIM_OFF);
}

void AutoTuneScreen::update_progress(const AutoTuneProgress& progress) {
    // Append new console messages if available
    if (progress.has_new_message && progress.last_message[0] != '\0') {
        append_console_message(progress.last_message);
        // Note: We can't clear the flag here since progress is const.
        // The flag will be cleared on the next log_message() call anyway,
        // which overwrites the buffer with new content.
    }
}
