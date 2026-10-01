#include "grinding_controller.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../config/constants.h"
#include "../../controllers/grind_events.h"
#include "../../controllers/grind_mode.h"
#include "../../logging/grind_logging.h"
#include "../ui_manager.h"

GrindingUIController* GrindingUIController::instance_ = nullptr;

GrindingUIController::GrindingUIController(UIManager* manager)
    : ui_manager_(manager) {
    instance_ = this;
}

void GrindingUIController::build_controls() {
    if (!ui_manager_) {
        return;
    }

    grind_button_ = lv_btn_create(lv_scr_act());
    lv_obj_set_size(grind_button_, 100, 100);
    lv_obj_align(grind_button_, LV_ALIGN_BOTTOM_MID, -60, -10);
    lv_obj_set_style_radius(grind_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(grind_button_, 0, 0);
    lv_obj_set_style_shadow_width(grind_button_, 0, 0);

    grind_icon_ = lv_img_create(grind_button_);
    lv_img_set_src(grind_icon_, LV_SYMBOL_PLAY);
    lv_obj_center(grind_icon_);
    lv_obj_set_style_text_font(grind_icon_, &lv_font_montserrat_32, 0);

    pulse_button_ = lv_btn_create(lv_scr_act());
    lv_obj_set_size(pulse_button_, 100, 100);
    lv_obj_align(pulse_button_, LV_ALIGN_BOTTOM_MID, 60, -10);
    lv_obj_set_style_radius(pulse_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(pulse_button_, 0, 0);
    lv_obj_set_style_shadow_width(pulse_button_, 0, 0);

    pulse_icon_ = lv_img_create(pulse_button_);
    lv_img_set_src(pulse_icon_, LV_SYMBOL_PLUS);
    lv_obj_center(pulse_icon_);
    lv_obj_set_style_text_font(pulse_icon_, &lv_font_montserrat_32, 0);

    lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
}

void GrindingUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    auto record_press = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
            return;
        }
        if (auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e))) {
            controller->record_press(e);
        }
    };

    if (grind_button_) {
        lv_obj_add_event_cb(grind_button_, record_press, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(grind_button_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e));
            // STOP always acts at once; any other meaning needs a deliberate tap.
            if (controller &&
                (controller->grind_button_stops() ||
                 controller->is_deliberate_tap(e, controller->grind_button_changed_ms_,
                                               controller->grind_button_needs_rearm()))) {
                controller->handle_grind_button();
            }
        }, LV_EVENT_CLICKED, this);
    }

    if (pulse_button_) {
        lv_obj_add_event_cb(pulse_button_, record_press, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(pulse_button_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e));
            // PAUSE stops the motor, so like STOP it acts at once.
            if (controller &&
                (controller->pulse_button_stops() ||
                 controller->is_deliberate_tap(e, controller->pulse_button_changed_ms_, true))) {
                controller->handle_pulse_button();
            }
        }, LV_EVENT_CLICKED, this);
    }

    // The layout area sits directly above STOP; a press and hold switches
    // layouts so a slightly high tap aimed at STOP does not.
    if (lv_obj_t* arc = ui_manager_->grinding_screen.get_arc_screen_obj()) {
        lv_obj_add_event_cb(arc, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
                if (auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e))) {
                    controller->handle_layout_toggle();
                }
            }
        }, LV_EVENT_LONG_PRESSED, this);
    }

    if (lv_obj_t* chart = ui_manager_->grinding_screen.get_chart_screen_obj()) {
        lv_obj_add_event_cb(chart, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
                if (auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e))) {
                    controller->handle_layout_toggle();
                }
            }
        }, LV_EVENT_LONG_PRESSED, this);
    }

    // NOTE: Purge confirm reuses existing grind_button_ (CANCEL) and pulse_button_ (CONTINUE)
    // No additional event registration needed - handle_pulse_button() checks for PURGE_CONFIRM phase
}

void GrindingUIController::on_state_changed(UIState new_state) {
    if (!ui_manager_) {
        return;
    }

    if (new_state != UIState::GRIND_COMPLETE && grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
        grind_complete_timer_ = nullptr;
    }
    if (new_state != UIState::GRIND_TIMEOUT && grind_timeout_timer_) {
        lv_timer_del(grind_timeout_timer_);
        grind_timeout_timer_ = nullptr;
    }

    switch (new_state) {
        case UIState::READY:
            enter_ready_state();
            break;
        case UIState::EDIT:
            enter_edit_state();
            break;
        case UIState::GRINDING:
            enter_grinding_state();
            break;
        case UIState::GRIND_COMPLETE:
            enter_grind_complete_state();
            break;
        case UIState::GRIND_TIMEOUT:
            enter_grind_timeout_state();
            break;
        case UIState::PURGE_CONFIRM:
        case UIState::REFILL_CONFIRM:
            enter_purge_confirm_state();
            break;
        case UIState::MENU:
        case UIState::CALIBRATION:
        case UIState::CONFIRM:
        case UIState::OTA_UPDATE:
        case UIState::OTA_UPDATE_FAILED:
            enter_menu_state();
            break;
        default:
            break;
    }

    update_grind_button_icon();
}

void GrindingUIController::update(UIState current_state) {
    if (!ui_manager_) {
        return;
    }

    switch (current_state) {
        case UIState::GRIND_COMPLETE: {
            // Keep showing the result when the cup is lifted. In time mode the
            // reading may still rise with extra pulses, so follow it upwards.
            float shown_weight = final_grind_weight_;
            WeightSensor* weight_sensor = ui_manager_->hardware_manager->get_weight_sensor();
            if (weight_sensor && ui_manager_->current_mode == GrindMode::TIME) {
                shown_weight = std::max(shown_weight, weight_sensor->get_display_weight());
            }
            ui_manager_->grinding_screen.update_current_weight(shown_weight);
            ui_manager_->grinding_screen.update_progress(final_grind_progress_);
            break;
        }
        case UIState::GRIND_TIMEOUT: {
            ui_manager_->grinding_screen.update_current_weight(error_grind_weight_);
            ui_manager_->grinding_screen.update_progress(error_grind_progress_);
            char error_display[64];
            const char* message = error_message_[0] ? error_message_ : "Error";
            std::snprintf(error_display, sizeof(error_display), "%s", message);
            ui_manager_->grinding_screen.update_target_weight_text(error_display);
            break;
        }
        case UIState::REFILL_CONFIRM:
            // A CONTINUE that had to wait for the scale is finished by the
            // control loop; show its outcome.
            if (ui_manager_->grind_controller) {
                handle_refill_result(ui_manager_->grind_controller->take_refill_outcome());
            }
            break;
        default:
            break;
    }
}

void GrindingUIController::handle_grind_button() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    LOG_BLE("[%lums BUTTON_PRESS] Grind button pressed in state: %s\n",
            millis(), ui_manager_->state_machine->is_state(UIState::READY) ? "READY" :
                       ui_manager_->state_machine->is_state(UIState::GRINDING) ? "GRINDING" :
                       ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) ? "GRIND_COMPLETE" :
                       ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT) ? "GRIND_TIMEOUT" :
                       ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM) ? "PURGE_CONFIRM" : "OTHER");

    if (ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM)) {
        // Cancel grind during purge confirmation
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->stop_grind();
        }
    } else if (ui_manager_->state_machine->is_state(UIState::REFILL_CONFIRM)) {
        // End the grind as "No beans?", keeping what is in the cup on record.
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->decline_refill();
        }
    } else if (ui_manager_->state_machine->is_state(UIState::READY)) {
        if (ui_manager_->current_tab == ReadyScreen::MENU_TAB_INDEX) {
            ui_manager_->switch_to_state(UIState::MENU);
            return;
        }
        if (ui_manager_->current_tab == ReadyScreen::WIFI_TAB_INDEX) return;

        const bool manual = ui_manager_->current_tab == ReadyScreen::MANUAL_TAB_INDEX;
        if (!manual && ui_manager_->grind_controller && ui_manager_->profile_controller) {
            ui_manager_->grind_controller->set_grind_profile_id(ui_manager_->profile_controller->get_current_profile());
        }

        LOG_BLE("[%lums GRIND_START] About to call start_grind()\n", millis());
        error_message_[0] = '\0';
        error_grind_weight_ = 0.0f;
        error_grind_progress_ = 0;

        bool started = false;
        if (manual && ui_manager_->grind_controller) {
            started = ui_manager_->grind_controller->start_grind(0.0f, 0, GrindMode::MANUAL);
        } else if (ui_manager_->profile_controller && ui_manager_->grind_controller) {
            float target_weight = ui_manager_->profile_controller->get_current_weight();
            float target_time_seconds = ui_manager_->profile_controller->get_current_time();
            uint32_t target_time_ms = static_cast<uint32_t>((target_time_seconds * 1000.0f) + 0.5f);
            started = ui_manager_->grind_controller->start_grind(target_weight, target_time_ms, ui_manager_->current_mode);
        }
        if (!started) {
            show_start_failure();
        }
        LOG_BLE("[%lums GRIND_START] start_grind() returned\n", millis());
    } else if (ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->stop_grind();
        }
    } else if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) ||
               ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->return_to_idle();
        }
    }
}

// Explains a refused start using what the UI can see; the controller only
// reports success or failure.
void GrindingUIController::show_start_failure() {
    if (ui_manager_->show_motor_safety_stop_notice()) {
        return;
    }
    HardwareManager* hardware = ui_manager_->get_hardware_manager();
    WeightSensor* sensor = hardware ? hardware->get_weight_sensor() : nullptr;
    const bool weight_grind = ui_manager_->current_tab != ReadyScreen::MANUAL_TAB_INDEX &&
                              ui_manager_->current_mode == GrindMode::WEIGHT;
    if (weight_grind && sensor &&
        (sensor->has_hardware_fault() || !sensor->has_recent_sample())) {
        ui_manager_->show_confirmation(
            "Scale not ready", "No reading from the load cell. Check its wiring, then see Diagnostics in the menu.",
            "OK", lv_color_hex(THEME_COLOR_WARNING), nullptr, "BACK");
    } else {
        ui_manager_->show_confirmation(
            "Could not start", "Check the scale and grinder. An update may be running.",
            "OK", lv_color_hex(THEME_COLOR_WARNING), nullptr, "BACK");
    }
}

void GrindingUIController::handle_pulse_button() {
    if (!ui_manager_ || !ui_manager_->grind_controller) {
        return;
    }

    // PURGE_CONFIRM: pulse button acts as CONTINUE
    if (ui_manager_->purge_confirm_screen.is_visible()) {
        handle_purge_confirm_continue();
        return;
    }

    // REFILL_CONFIRM: pulse button acts as CONTINUE
    if (ui_manager_->refill_confirm_screen.is_visible()) {
        continue_after_refill(false);
        return;
    }

    // TIME_GRINDING: pulse button toggles pause/resume
    if (ui_manager_->grind_controller->get_phase() == GrindPhase::TIME_GRINDING) {
        if (ui_manager_->grind_controller->is_grind_paused()) {
            LOG_BLE("[UIManager] Resume button clicked\n");
            ui_manager_->grind_controller->resume_grind();
        } else {
            LOG_BLE("[UIManager] Pause button clicked\n");
            ui_manager_->grind_controller->pause_grind();
        }
        update_grind_button_icon();
        return;
    }

    // GRIND_COMPLETE: additional time mode pulse
    if (ui_manager_->grind_controller->can_pulse()) {
        LOG_BLE("[UIManager] Pulse button clicked - requesting additional pulse\n");
        ui_manager_->grind_controller->start_additional_pulse();
        reset_grind_complete_timer();
    } else {
        LOG_BLE("[UIManager] Pulse button clicked but pulsing not allowed\n");
    }
}

void GrindingUIController::handle_layout_toggle() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::GRINDING) ||
        ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) ||
        ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        GrindScreenLayout current_layout = ui_manager_->grinding_screen.get_layout();
        if (current_layout == GrindScreenLayout::MINIMAL_ARC) {
            ui_manager_->grinding_screen.set_layout(GrindScreenLayout::NERDY_CHART);
        } else {
            ui_manager_->grinding_screen.set_layout(GrindScreenLayout::MINIMAL_ARC);
        }
    }
}

void GrindingUIController::handle_purge_confirm_continue() {
    continue_after_purge(true);
}

void GrindingUIController::continue_after_purge(bool check_vessel) {
    if (!ui_manager_ || !ui_manager_->grind_controller) {
        return;
    }

    // The controller holds while the scale reads as if the cup were still
    // off; a lighter replacement cup can still be confirmed.
    switch (ui_manager_->grind_controller->continue_from_purge(check_vessel)) {
        case PurgeContinueResult::VESSEL_MISSING:
            ui_manager_->show_confirmation(
                "Cup missing?", "The scale is lighter than at the start. Put the cup back, or resume with this one.",
                "RESUME", lv_color_hex(THEME_COLOR_WARNING),
                [this]() { continue_after_purge(false); }, "BACK");
            return;
        case PurgeContinueResult::SCALE_NOT_READY:
            // The prompt stays; the control loop ends the grind if the scale
            // stays silent.
            return;
        case PurgeContinueResult::NOT_WAITING:
            // The grind already moved on; its own event sets the screen.
            ui_manager_->purge_confirm_screen.hide();
            return;
        case PurgeContinueResult::CONTINUED:
            break;
    }

    // Save "Always keep" only once the grind really continues: BACK from the
    // cup dialog shows the prompt again with the box cleared.
    if (ui_manager_->purge_confirm_screen.is_checkbox_checked()) {
        LOG_BLE("[%lums PURGE] User chose to keep grinds - switching to Prime mode\n", millis());

        // Switch grinder purge mode from Purge to Prime in preferences
        auto* hardware = ui_manager_->get_hardware_manager();
        Preferences* prefs = hardware ? hardware->get_preferences() : nullptr;
        if (prefs) {
            prefs->putInt(GrindController::PREF_KEY_GRINDER_MODE, static_cast<int>(GrinderPurgeMode::PRIME));
        }
    }

    // Hide the purge confirmation screen and continue grinding
    ui_manager_->purge_confirm_screen.hide();
    ui_manager_->switch_to_state(UIState::GRINDING);
}

void GrindingUIController::continue_after_refill(bool accept_current_reading) {
    if (!ui_manager_ || !ui_manager_->grind_controller) {
        return;
    }
    handle_refill_result(ui_manager_->grind_controller->continue_from_refill(accept_current_reading));
}

// Shows what happened to CONTINUE on the refill prompt, whether it was
// decided at once or after the scale settled.
void GrindingUIController::handle_refill_result(RefillContinueResult result) {
    if (!ui_manager_) {
        return;
    }
    RefillConfirmScreen& prompt = ui_manager_->refill_confirm_screen;
    switch (result) {
        case RefillContinueResult::NONE:
        case RefillContinueResult::SCALE_NOT_READY:
            // Nothing new; the control loop ends the grind if the scale stays silent.
            return;
        case RefillContinueResult::CONTINUED:
            prompt.hide();
            ui_manager_->switch_to_state(UIState::GRINDING);
            return;
        case RefillContinueResult::NOT_WAITING:
            // The grind already moved on; its own event sets the screen.
            prompt.hide();
            return;
        case RefillContinueResult::WAITING_FOR_SETTLE:
            prompt.set_status("Hold still while the scale settles...");
            break;
        case RefillContinueResult::SETTLE_TIMEOUT:
            prompt.set_status("Scale not steady. Keep still, then press " LV_SYMBOL_OK " again.");
            break;
        case RefillContinueResult::CUP_LIFTED:
            prompt.set_status("Cup lifted. Press " LV_SYMBOL_OK " when it is back.");
            break;
        case RefillContinueResult::VESSEL_MISSING:
            // The grind keeps its zero, which includes the cup, so it can only
            // go on once that cup is back: there is nothing to continue with.
            ui_manager_->show_confirmation(
                "Cup missing?", "The scale is lighter than at the start. Put the cup back, then press " LV_SYMBOL_OK " again.",
                "OK", lv_color_hex(THEME_COLOR_WARNING), nullptr, "BACK");
            return;
        case RefillContinueResult::READING_MOVED: {
            WeightSensor* sensor = ui_manager_->hardware_manager->get_weight_sensor();
            const float now_g = sensor ? sensor->get_display_weight() : 0.0f;
            const float paused_g = ui_manager_->grind_controller
                                       ? ui_manager_->grind_controller->get_refill_prompt_info().pause_weight_g
                                       : 0.0f;
            char message[160];
            std::snprintf(message, sizeof(message),
                          "The scale reads " SYS_WEIGHT_DISPLAY_FORMAT "; it read " SYS_WEIGHT_DISPLAY_FORMAT
                          " when grinding stopped. Put the cup back, or resume from " SYS_WEIGHT_DISPLAY_FORMAT ".",
                          static_cast<double>(now_g), static_cast<double>(paused_g), static_cast<double>(now_g));
            ui_manager_->show_confirmation("Cup moved?", message, "RESUME", lv_color_hex(THEME_COLOR_WARNING),
                                           [this]() { continue_after_refill(true); }, "BACK");
            return;
        }
    }
    update_button_layout();
}

// CONTINUE is greyed out while an earlier press waits for the scale to settle.
bool GrindingUIController::refill_continue_enabled() const {
    return !(ui_manager_ && ui_manager_->grind_controller &&
             ui_manager_->grind_controller->get_refill_prompt_info().waiting_for_settle);
}

void GrindingUIController::update_grind_button_icon() {
    if (!ui_manager_ || !grind_button_ || !grind_icon_) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM) ||
        ui_manager_->state_machine->is_state(UIState::REFILL_CONFIRM)) {
        // At a prompt, show STOP icon (user can end the grind)
        set_grind_icon(LV_SYMBOL_STOP);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_ERROR), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        set_grind_icon(LV_SYMBOL_STOP);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE)) {
        set_grind_icon(LV_SYMBOL_OK);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        set_grind_icon(LV_SYMBOL_CLOSE);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_WARNING), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::READY) &&
               ui_manager_->current_tab == ReadyScreen::MENU_TAB_INDEX) {
        set_grind_icon(LV_SYMBOL_SETTINGS);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::READY) &&
               ui_manager_->current_tab == ReadyScreen::WIFI_TAB_INDEX) {
        set_grind_icon(LV_SYMBOL_WIFI);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    } else {
        set_grind_icon(LV_SYMBOL_PLAY);
        lv_obj_set_style_bg_color(grind_button_,
                                  ui_manager_->current_mode == GrindMode::TIME
                                      ? lv_color_hex(THEME_COLOR_ACCENT)
                                      : lv_color_hex(THEME_COLOR_PRIMARY),
                                  0);
    }

    update_button_layout();
}

void GrindingUIController::update_button_layout() {
    if (!ui_manager_ || !grind_button_) {
        return;
    }

    bool in_purge_confirm = ui_manager_->purge_confirm_screen.is_visible();
    bool in_refill_confirm = ui_manager_->refill_confirm_screen.is_visible();

    bool in_time_grinding = (ui_manager_->current_mode == GrindMode::TIME &&
                             ui_manager_->grind_controller &&
                             ui_manager_->grind_controller->get_phase() == GrindPhase::TIME_GRINDING &&
                             ui_manager_->state_machine->is_state(UIState::GRINDING));
    bool is_time_grind_paused = in_time_grinding && ui_manager_->grind_controller->is_grind_paused();

    bool should_show_pulse = (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) &&
                              ui_manager_->current_mode == GrindMode::TIME);

    if (in_purge_confirm || in_refill_confirm || in_time_grinding || should_show_pulse) {
        // Dual button layout: left=STOP/CANCEL, right=context-specific action
        lv_obj_align(grind_button_, LV_ALIGN_BOTTOM_MID, -60, -10);
        if (pulse_button_) {
            lv_obj_align(pulse_button_, LV_ALIGN_BOTTOM_MID, 60, -10);
            if (lv_obj_has_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN)) {
                // Appearing under a finger counts as a change of meaning.
                pulse_button_changed_ms_ = millis();
            }
            lv_obj_clear_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);

            if (in_purge_confirm) {
                set_pulse_icon(LV_SYMBOL_OK);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
                lv_obj_clear_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_COVER, 0);
            } else if (in_refill_confirm) {
                set_pulse_icon(LV_SYMBOL_OK);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
                const bool enabled = refill_continue_enabled();
                if (enabled) {
                    // Re-enabled under a finger counts as a change of meaning.
                    if (!refill_continue_was_enabled_) pulse_button_changed_ms_ = millis();
                    lv_obj_clear_state(pulse_button_, LV_STATE_DISABLED);
                    lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_COVER, 0);
                } else {
                    lv_obj_add_state(pulse_button_, LV_STATE_DISABLED);
                    lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_50, LV_STATE_DISABLED);
                }
                refill_continue_was_enabled_ = enabled;
            } else if (in_time_grinding) {
                // Pause / Resume toggle
                if (is_time_grind_paused) {
                    set_pulse_icon(LV_SYMBOL_PLAY);
                    lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
                } else {
                    set_pulse_icon(LV_SYMBOL_PAUSE);
                    lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
                }
                lv_obj_clear_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_COVER, 0);
            } else if (ui_manager_->grind_controller && ui_manager_->grind_controller->can_pulse()) {
                set_pulse_icon(LV_SYMBOL_PLUS);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
                lv_obj_clear_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_COVER, 0);
            } else {
                set_pulse_icon(LV_SYMBOL_PLUS);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
                lv_obj_add_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_50, LV_STATE_DISABLED);
            }
        }
    } else {
        // Single button layout: centered
        lv_obj_align(grind_button_, LV_ALIGN_BOTTOM_MID, 0, -10);
        if (pulse_button_) {
            lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void GrindingUIController::update_grinding_targets() {
    if (!ui_manager_ || !ui_manager_->grind_controller || !ui_manager_->profile_controller) {
        return;
    }

    const auto& session = ui_manager_->grind_controller->get_session_descriptor();
    if (session.mode == GrindMode::MANUAL) {
        ui_manager_->grinding_screen.update_target_weight_text("Elapsed: 0.0s");
        return;
    }
    ui_manager_->grinding_screen.set_chart_time_prediction(session.target_time_ms);
    ui_manager_->grinding_screen.update_target_weight(session.target_weight);
    if (session.mode == GrindMode::TIME && session.target_time_ms > 0) {
        float target_time_seconds = static_cast<float>(session.target_time_ms) / 1000.0f;
        ui_manager_->grinding_screen.update_target_time(target_time_seconds);
    }
}

void GrindingUIController::reset_grind_complete_timer() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) && grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
        grind_complete_timer_ = nullptr;
        start_grind_complete_timer();
    }
}

void GrindingUIController::handle_grind_event(const GrindEventData& event_data) {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    switch (event_data.event) {
        case UIGrindEvent::PHASE_CHANGED: {
            ui_manager_->current_mode = event_data.mode;

            // Handle PURGE_CONFIRM phase specially - show purge confirmation popup
            if (event_data.phase == GrindPhase::PURGE_CONFIRM) {
                LOG_UI_DEBUG("[%lums UI_TRANSITION] Switching to PURGE_CONFIRM state\n", millis());
                ui_manager_->switch_to_state(UIState::PURGE_CONFIRM);
                update_grind_button_icon();  // Update button icon to STOP and reposition for dual-button layout
            } else if (event_data.phase == GrindPhase::REFILL_CONFIRM) {
                LOG_UI_DEBUG("[%lums UI_TRANSITION] Switching to REFILL_CONFIRM state\n", millis());
                ui_manager_->switch_to_state(UIState::REFILL_CONFIRM);
                update_grind_button_icon();
            } else if (event_data.phase != GrindPhase::IDLE &&
                       event_data.phase != GrindPhase::TIME_ADDITIONAL_PULSE &&
                       (event_data.phase == GrindPhase::INITIALIZING ||
                        !ui_manager_->state_machine->is_state(UIState::GRINDING))) {
                LOG_UI_DEBUG("[%lums UI_TRANSITION] Switching to GRINDING state due to phase: %s\n",
                             millis(), event_data.phase_display_text);
                WeightSensor* weight_sensor = ui_manager_->hardware_manager->get_weight_sensor();
                ui_manager_->grinding_screen.update_profile_name(
                    event_data.mode == GrindMode::MANUAL
                        ? "MANUAL"
                        : ui_manager_->profile_controller->get_current_name());
                ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
                chart_updates_enabled_ = true;
                if (event_data.phase == GrindPhase::INITIALIZING) {
                    // A stop/restart can coalesce before the UI leaves GRINDING.
                    ui_manager_->grinding_screen.reset_chart_data();
                }
                update_grinding_targets();
                if (weight_sensor) {
                    ui_manager_->grinding_screen.update_current_weight(weight_sensor->get_display_weight());
                }
                ui_manager_->grinding_screen.update_progress(0);
                ui_manager_->switch_to_state(UIState::GRINDING);

                if (event_data.phase == GrindPhase::INITIALIZING) {
                    ui_manager_->grind_controller->ui_acknowledge_phase_transition();
                    LOG_UI_DEBUG("[%lums UI_ACKNOWLEDGMENT] INITIALIZING phase confirmed, ready for SETUP\n", millis());
                }
            }

            if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE)) {
                update_button_layout();
            }

            // Update button layout when phase changes within TIME mode GRINDING
            // (e.g. entering/exiting TIME_GRINDING to show/hide pause button)
            if (event_data.mode == GrindMode::TIME &&
                ui_manager_->state_machine->is_state(UIState::GRINDING) &&
                event_data.phase != GrindPhase::PURGE_CONFIRM) {
                update_grind_button_icon();
            }

            if (event_data.show_taring_text) {
                ui_manager_->grinding_screen.update_tare_display();
            } else {
                ui_manager_->grinding_screen.update_current_weight(event_data.current_weight);
                ui_manager_->grinding_screen.update_progress(event_data.progress_percent);

                if (chart_updates_enabled_ &&
                    event_data.phase != GrindPhase::IDLE && event_data.phase != GrindPhase::TARING &&
                    event_data.phase != GrindPhase::TARE_CONFIRM && event_data.phase != GrindPhase::INITIALIZING &&
                    event_data.phase != GrindPhase::SETUP && event_data.phase != GrindPhase::COMPLETED &&
                    event_data.phase != GrindPhase::TIMEOUT && event_data.phase != GrindPhase::TIME_ADDITIONAL_PULSE &&
                    event_data.phase != GrindPhase::PURGE_CONFIRM &&
                    event_data.phase != GrindPhase::REFILL_CONFIRM) {
                    ui_manager_->grinding_screen.add_chart_data_point(event_data.current_weight, event_data.flow_rate, millis());
                }
            }
            break;
        }
        case UIGrindEvent::PROGRESS_UPDATED: {
            if (event_data.show_taring_text) {
                ui_manager_->grinding_screen.update_tare_display();
            } else {
                ui_manager_->current_mode = event_data.mode;
                ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
                ui_manager_->grinding_screen.update_current_weight(event_data.current_weight);
                ui_manager_->grinding_screen.update_progress(event_data.progress_percent);
                if (event_data.mode == GrindMode::MANUAL) {
                    char elapsed_text[32];
                    std::snprintf(elapsed_text, sizeof(elapsed_text), "Elapsed: %.1fs",
                                  static_cast<double>(event_data.elapsed_ms) / 1000.0);
                    ui_manager_->grinding_screen.update_target_weight_text(elapsed_text);
                }
                if (event_data.phase == GrindPhase::REFILL_CONFIRM) {
                    ui_manager_->refill_confirm_screen.update_weight(event_data.current_weight);
                }

                if (chart_updates_enabled_ &&
                    event_data.phase != GrindPhase::IDLE && event_data.phase != GrindPhase::TARING &&
                    event_data.phase != GrindPhase::TARE_CONFIRM && event_data.phase != GrindPhase::INITIALIZING &&
                    event_data.phase != GrindPhase::SETUP && event_data.phase != GrindPhase::COMPLETED &&
                    event_data.phase != GrindPhase::TIMEOUT && event_data.phase != GrindPhase::TIME_ADDITIONAL_PULSE &&
                    event_data.phase != GrindPhase::PURGE_CONFIRM &&
                    event_data.phase != GrindPhase::REFILL_CONFIRM) {
                    ui_manager_->grinding_screen.add_chart_data_point(event_data.current_weight, event_data.flow_rate, millis());
                }
            }
            break;
        }
        case UIGrindEvent::COMPLETED: {
            ui_manager_->current_mode = event_data.mode;
            ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
            final_grind_weight_ = event_data.final_weight;
            final_grind_progress_ = event_data.progress_percent;
            LOG_BLE("GRIND COMPLETE - Final settled weight captured: %.2fg (Progress: %d%%)\n",
                    final_grind_weight_, final_grind_progress_);
            chart_updates_enabled_ = false;
            ui_manager_->switch_to_state(UIState::GRIND_COMPLETE);
            start_grind_complete_timer();
            break;
        }
        case UIGrindEvent::TIMEOUT: {
            ui_manager_->current_mode = event_data.mode;
            ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
            error_grind_weight_ = event_data.error_weight;
            error_grind_progress_ = event_data.error_progress;
            const char* message = event_data.error_message[0] ? event_data.error_message : "Error";
            std::strncpy(error_message_, message, sizeof(error_message_) - 1);
            error_message_[sizeof(error_message_) - 1] = '\0';
            LOG_BLE("GRIND ERROR - %s, Weight: %.2fg (Progress: %d%%)\n",
                    error_message_, error_grind_weight_, error_grind_progress_);
            chart_updates_enabled_ = false;
            ui_manager_->switch_to_state(UIState::GRIND_TIMEOUT);
            start_grind_timeout_timer();
            break;
        }
        case UIGrindEvent::STOPPED: {
            cancel_timers();
            chart_updates_enabled_ = false;
            ui_manager_->switch_to_state(UIState::READY);
            break;
        }
        case UIGrindEvent::BACKGROUND_CHANGE:
#if DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR
        {
            static lv_style_t style_bg;
            static bool style_initialized = false;

            if (!style_initialized) {
                lv_style_init(&style_bg);
                style_initialized = true;
            }

#if defined(DEBUG_ENABLE_LOADCELL_MOCK) && (DEBUG_ENABLE_LOADCELL_MOCK != 0)
            lv_color_t inactive_color = lv_color_hex(THEME_COLOR_BACKGROUND_MOCK);
#else
            lv_color_t inactive_color = lv_color_hex(THEME_COLOR_BACKGROUND);
#endif
            lv_color_t bg_color = event_data.background_active ?
                lv_color_hex(THEME_COLOR_GRINDER_ACTIVE) :
                inactive_color;

            lv_style_set_bg_color(&style_bg, bg_color);
            lv_obj_add_style(lv_scr_act(), &style_bg, 0);

            LOG_UI_DEBUG("[UIManager] Background: %s\n", event_data.background_active ? "ACTIVE" : "INACTIVE");
        }
#endif
            break;
        case UIGrindEvent::PULSE_AVAILABLE:
            LOG_BLE("[UIManager] Pulse available - updating button layout\n");
            update_button_layout();
            break;
        case UIGrindEvent::PULSE_STARTED:
            LOG_BLE("[UIManager] Pulse #%d started (%.1fms)\n",
                    event_data.pulse_count, (float)event_data.pulse_duration_ms);
#if DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR
        {
            static lv_style_t style_bg;
            lv_style_init(&style_bg);
            lv_style_set_bg_color(&style_bg, lv_color_hex(THEME_COLOR_GRINDER_ACTIVE));
            lv_obj_add_style(lv_scr_act(), &style_bg, 0);
        }
#endif
            break;
        case UIGrindEvent::PULSE_COMPLETED:
            LOG_BLE("[UIManager] Pulse #%d completed - weight: %.2fg\n",
                    event_data.pulse_count, event_data.current_weight);
            ui_manager_->grinding_screen.update_current_weight(event_data.current_weight);
            update_button_layout();
            break;
        default:
            break;
    }
}

void GrindingUIController::dispatch_event(const GrindEventData& event_data) {
    if (instance_) {
        instance_->handle_grind_event(event_data);
    }
}

void GrindingUIController::enter_ready_state() {
    // A layout chosen during the grind is saved now that the motor is off.
    ui_manager_->grinding_screen.save_layout_if_changed();

    if (!grind_button_) {
        return;
    }

    lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    if (pulse_button_) {
        lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
    }
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
}

void GrindingUIController::enter_edit_state() {
    if (grind_button_) {
        lv_obj_add_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    if (pulse_button_) {
        lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::enter_grinding_state() {
    WeightSensor* weight_sensor = ui_manager_->hardware_manager->get_weight_sensor();
    ui_manager_->grinding_screen.reset_chart_data();
    ui_manager_->grinding_screen.update_profile_name(
        ui_manager_->current_mode == GrindMode::MANUAL
            ? "MANUAL"
            : ui_manager_->profile_controller->get_current_name());
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    chart_updates_enabled_ = true;
    update_grinding_targets();
    if (weight_sensor) {
        ui_manager_->grinding_screen.update_current_weight(weight_sensor->get_display_weight());
    }
    ui_manager_->grinding_screen.update_progress(0);
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::enter_grind_complete_state() {
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    ui_manager_->grinding_screen.update_profile_name(ui_manager_->profile_controller->get_current_name());
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    ui_manager_->grinding_screen.update_current_weight(final_grind_weight_);
    ui_manager_->grinding_screen.update_progress(final_grind_progress_);
}

void GrindingUIController::enter_grind_timeout_state() {
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    ui_manager_->grinding_screen.update_profile_name("ERROR");
    char error_display[64];
    const char* message = error_message_[0] ? error_message_ : "Error";
    std::snprintf(error_display, sizeof(error_display), "%s", message);
    ui_manager_->grinding_screen.update_target_weight_text(error_display);
    ui_manager_->grinding_screen.update_current_weight(error_grind_weight_);
    ui_manager_->grinding_screen.update_progress(error_grind_progress_);
}

void GrindingUIController::enter_purge_confirm_state() {
    // Also reached back from a dialog (for example "Cup missing?"), which hid
    // the buttons; the prompt needs its STOP button.
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::enter_menu_state() {
    if (grind_button_) {
        lv_obj_add_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    if (pulse_button_) {
        lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::record_press(lv_event_t* e) {
    press_ms_ = millis();
    if (lv_indev_t* indev = lv_event_get_indev(e)) {
        lv_indev_get_point(indev, &press_point_);
    }
}

// A tap counts only if the finger stayed near where it went down (a swipe
// that starts on a button is not a tap) and the press began after the button
// took on its current meaning: at least USER_BUTTON_REARM_MS after it when
// `rearm` is set, which rejects the second tap of a double tap.
bool GrindingUIController::is_deliberate_tap(lv_event_t* e, uint32_t changed_ms, bool rearm) const {
    const int32_t press_after_change_ms = static_cast<int32_t>(press_ms_ - changed_ms);
    if (press_after_change_ms < 0 ||
        (rearm && static_cast<uint32_t>(press_after_change_ms) < USER_BUTTON_REARM_MS)) {
        return false;
    }
    lv_indev_t* indev = lv_event_get_indev(e);
    if (!indev) {
        return true;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    return LV_ABS(point.x - press_point_.x) <= USER_BUTTON_DRAG_CANCEL_PX &&
           LV_ABS(point.y - press_point_.y) <= USER_BUTTON_DRAG_CANCEL_PX;
}

bool GrindingUIController::grind_button_stops() const {
    return ui_manager_ && ui_manager_->state_machine &&
           (ui_manager_->state_machine->is_state(UIState::GRINDING) ||
            ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM) ||
            ui_manager_->state_machine->is_state(UIState::REFILL_CONFIRM));
}

// Opening the menu or the Wi-Fi page needs no re-arm delay.
bool GrindingUIController::grind_button_needs_rearm() const {
    return !grind_symbol_ || (std::strcmp(grind_symbol_, LV_SYMBOL_SETTINGS) != 0 &&
                              std::strcmp(grind_symbol_, LV_SYMBOL_WIFI) != 0);
}

bool GrindingUIController::pulse_button_stops() const {
    return pulse_symbol_ && std::strcmp(pulse_symbol_, LV_SYMBOL_PAUSE) == 0;
}

void GrindingUIController::set_grind_icon(const char* symbol) {
    if (grind_symbol_ && std::strcmp(grind_symbol_, symbol) == 0) {
        return;
    }
    grind_symbol_ = symbol;
    grind_button_changed_ms_ = millis();
    lv_img_set_src(grind_icon_, symbol);
}

void GrindingUIController::set_pulse_icon(const char* symbol) {
    if (pulse_symbol_ && std::strcmp(pulse_symbol_, symbol) == 0) {
        return;
    }
    pulse_symbol_ = symbol;
    pulse_button_changed_ms_ = millis();
    lv_img_set_src(pulse_icon_, symbol);
}

void GrindingUIController::start_grind_complete_timer() {
    if (grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
    }
    grind_complete_timer_ = lv_timer_create(grind_complete_timer_cb, 60000, this);
    lv_timer_set_repeat_count(grind_complete_timer_, 1);
}

void GrindingUIController::start_grind_timeout_timer() {
    if (grind_timeout_timer_) {
        lv_timer_del(grind_timeout_timer_);
    }
    grind_timeout_timer_ = lv_timer_create(grind_timeout_timer_cb, 60000, this);
    lv_timer_set_repeat_count(grind_timeout_timer_, 1);
}

void GrindingUIController::cancel_timers() {
    if (grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
        grind_complete_timer_ = nullptr;
    }
    if (grind_timeout_timer_) {
        lv_timer_del(grind_timeout_timer_);
        grind_timeout_timer_ = nullptr;
    }
}

void GrindingUIController::grind_complete_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<GrindingUIController*>(lv_timer_get_user_data(timer));
    if (!controller || !controller->ui_manager_ || !controller->ui_manager_->grind_controller) {
        return;
    }

    controller->ui_manager_->grind_controller->return_to_idle();
    controller->grind_complete_timer_ = nullptr;
}

void GrindingUIController::grind_timeout_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<GrindingUIController*>(lv_timer_get_user_data(timer));
    if (!controller || !controller->ui_manager_ || !controller->ui_manager_->grind_controller) {
        return;
    }

    controller->ui_manager_->grind_controller->return_to_idle();
    controller->grind_timeout_timer_ = nullptr;
}
