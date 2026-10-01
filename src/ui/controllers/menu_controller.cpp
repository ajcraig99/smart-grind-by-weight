#include "menu_controller.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <algorithm>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <cstdint>
#include "../../config/constants.h"
#include "../../controllers/grind_controller.h"
#include "../../controllers/grind_mode_traits.h"
#include "../../logging/grind_logging.h"
#include "../../network/device_api.h"
#include "../../network/network_manager.h"
#include "../../system/diagnostics_controller.h"
#include "../../system/screensaver_settings.h"
#include "../../system/statistics_manager.h"
#include "../../system/update_authorization.h"
#include "../components/blocking_overlay.h"
#include "../components/ui_operations.h"
#include "../event_bridge_lvgl.h"
#include "../ui_helpers.h"
#include "../ui_manager.h"
#include "../screens/menu_screen.h"

MenuUIController::MenuUIController(UIManager* manager)
    : ui_manager_(manager) {}

void MenuUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    using ET = EventBridgeLVGL::EventType;

    EventBridgeLVGL::register_handler(ET::MENU_CALIBRATE, [this](lv_event_t*) { handle_calibrate(); });
    EventBridgeLVGL::register_handler(ET::MENU_RESET, [this](lv_event_t*) { handle_reset(); });
    EventBridgeLVGL::register_handler(ET::MENU_PURGE, [this](lv_event_t*) { handle_purge(); });
    EventBridgeLVGL::register_handler(ET::MENU_MOTOR_TEST, [this](lv_event_t*) { handle_motor_test(); });
    EventBridgeLVGL::register_handler(ET::MENU_SCALE_OPEN, [this](lv_event_t*) { handle_scale_open(); });
    EventBridgeLVGL::register_handler(ET::MENU_SCALE_TARE, [this](lv_event_t*) { handle_scale_tare(); });
    EventBridgeLVGL::register_handler(ET::MENU_AUTOTUNE, [this](lv_event_t*) { handle_autotune(); });
    EventBridgeLVGL::register_handler(ET::MENU_DIAGNOSTIC_RESET, [this](lv_event_t*) { handle_diagnostics_reset(); });
    EventBridgeLVGL::register_handler(ET::MENU_BACK, [this](lv_event_t*) { handle_back(); });
    EventBridgeLVGL::register_handler(ET::MENU_REFRESH_STATS, [this](lv_event_t*) { handle_refresh_stats(); });
    EventBridgeLVGL::register_handler(ET::MENU_INSTALL_UPDATE, [this](lv_event_t*) { handle_install_update(); });
    EventBridgeLVGL::register_handler(ET::MENU_ALLOW_UPDATE, [this](lv_event_t*) { handle_allow_update(); });
    EventBridgeLVGL::register_handler(ET::WIFI_TOGGLE, [this](lv_event_t*) { handle_wifi_toggle(); });
    EventBridgeLVGL::register_handler(ET::WIFI_FORGET, [this](lv_event_t*) { handle_wifi_forget(); });
    EventBridgeLVGL::register_handler(ET::REMOTE_START_TOGGLE, [this](lv_event_t*) { handle_remote_start_toggle(); });

    EventBridgeLVGL::register_handler(ET::BLE_TOGGLE, [this](lv_event_t*) { handle_ble_toggle(); });
    EventBridgeLVGL::register_handler(ET::BLE_STARTUP_TOGGLE, [this](lv_event_t*) { handle_ble_startup_toggle(); });
    EventBridgeLVGL::register_handler(ET::LOGGING_TOGGLE, [this](lv_event_t*) { handle_logging_toggle(); });

    EventBridgeLVGL::register_handler(ET::GRIND_MODE_SWIPE_TOGGLE, [this](lv_event_t*) { handle_grind_mode_swipe_toggle(); });
    EventBridgeLVGL::register_handler(ET::GRIND_MODE_RADIO_BUTTON, [this](lv_event_t*) { handle_grind_mode_radio_button(); });
    EventBridgeLVGL::register_handler(ET::AUTO_START_TOGGLE, [this](lv_event_t*) { handle_auto_start_toggle(); });
    EventBridgeLVGL::register_handler(ET::AUTO_START_THRESHOLD_SLIDER, [this](lv_event_t*) { handle_auto_start_threshold_slider(); });
    EventBridgeLVGL::register_handler(ET::AUTO_START_THRESHOLD_SLIDER_RELEASED, [this](lv_event_t*) { handle_auto_start_threshold_slider_released(); });
    EventBridgeLVGL::register_handler(ET::AUTO_RETURN_TOGGLE, [this](lv_event_t*) { handle_auto_return_toggle(); });
    EventBridgeLVGL::register_handler(ET::GRINDER_PURGE_MODE_RADIO_BUTTON, [this](lv_event_t*) { handle_grinder_purge_mode_radio_button(); });
    EventBridgeLVGL::register_handler(ET::GRINDER_PURGE_AMOUNT_SLIDER, [this](lv_event_t*) { handle_grinder_purge_amount_slider(); });
    EventBridgeLVGL::register_handler(ET::GRINDER_PURGE_AMOUNT_SLIDER_RELEASED, [this](lv_event_t*) { handle_grinder_purge_amount_slider_released(); });
    EventBridgeLVGL::register_handler(ET::GRIND_FRESHNESS_HOURS_SLIDER, [this](lv_event_t*) { handle_grind_freshness_hours_slider(); });
    EventBridgeLVGL::register_handler(ET::GRIND_FRESHNESS_HOURS_SLIDER_RELEASED, [this](lv_event_t*) { handle_grind_freshness_hours_slider_released(); });
    EventBridgeLVGL::register_handler(ET::COAST_RATIO_SLIDER, [this](lv_event_t*) { handle_coast_ratio_slider(); });
    EventBridgeLVGL::register_handler(ET::COAST_RATIO_SLIDER_RELEASED, [this](lv_event_t*) { handle_coast_ratio_slider_released(); });
    EventBridgeLVGL::register_handler(ET::MOTOR_LATENCY_SLIDER, [this](lv_event_t*) { handle_motor_latency_slider(); });
    EventBridgeLVGL::register_handler(ET::MOTOR_LATENCY_SLIDER_RELEASED, [this](lv_event_t*) { handle_motor_latency_slider_released(); });

    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_NORMAL_SLIDER, [this](lv_event_t*) { handle_brightness_normal_slider(); });
    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_NORMAL_SLIDER_RELEASED, [this](lv_event_t*) { handle_brightness_normal_slider_released(); });
    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_SCREENSAVER_SLIDER, [this](lv_event_t*) { handle_brightness_screensaver_slider(); });
    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED, [this](lv_event_t*) { handle_brightness_screensaver_slider_released(); });

    EventBridgeLVGL::register_handler(ET::SCREENSAVER_STARTUP_TOGGLE, [this](lv_event_t*) { handle_screensaver_startup_toggle(); });
    EventBridgeLVGL::register_handler(ET::SCREENSAVER_SLEEP_TOGGLE, [this](lv_event_t*) { handle_screensaver_sleep_toggle(); });
    EventBridgeLVGL::register_handler(ET::DISPLAY_OFF_TOGGLE, [this](lv_event_t*) { handle_display_off_toggle(); });

    // Note: Event registration for menu widgets is done in the page creation functions
    // (menu_screen.cpp) because the menu is created lazily and destroyed on hide.
    // Attempting to register events here would fail silently since widgets don't exist yet.
}

void MenuUIController::handle_install_update() {
    if (ui_manager_ && ui_manager_->status_indicator_controller_) {
        ui_manager_->status_indicator_controller_->prompt_firmware_update();
    }
}

void MenuUIController::handle_allow_update() {
    if (!ui_manager_) return;
    auto& authorization = update_authorization();
    const uint32_t now = millis();
    if (authorization.is_granted(now)) {
        authorization.revoke();
        LOG_BLE("[UPDATE] Firmware update permission withdrawn at the grinder\n");
    } else if (!ui_manager_->grind_controller || !ui_manager_->grind_controller->is_active()) {
        authorization.grant(now, NETWORK_UPDATE_AUTHORIZATION_WINDOW_MS);
        LOG_BLE("[UPDATE] One firmware update allowed at the grinder for %lus\n",
                static_cast<unsigned long>(NETWORK_UPDATE_AUTHORIZATION_WINDOW_MS / 1000UL));
    }
    ui_manager_->menu_screen.update_firmware_update_page();
}

void MenuUIController::handle_wifi_toggle() {
    if (!ui_manager_) return;
    auto* toggle = ui_manager_->menu_screen.get_wifi_toggle();
    if (!toggle) return;
    network_manager.request_enabled(lv_obj_has_state(toggle, LV_STATE_CHECKED));
}

void MenuUIController::handle_wifi_forget() {
    if (!ui_manager_) return;
    const String network = network_manager.network_name();
    String message = network.isEmpty() ? String("Remove the saved network?")
                                       : String("Remove " + network + "?");
    message += "\n\nWith Wi-Fi on, the grinder then opens its setup network so you can join another one.";
    ui_manager_->show_confirmation(
        "FORGET WI-FI", message.c_str(), "FORGET", lv_color_hex(THEME_COLOR_WARNING),
        []() { network_manager.request_forget_network(); });
}

void MenuUIController::handle_remote_start_toggle() {
    if (!ui_manager_) return;
    auto* toggle = ui_manager_->menu_screen.get_remote_start_toggle();
    if (!toggle) return;
    if (!lv_obj_has_state(toggle, LV_STATE_CHECKED)) {
        if (!device_api.set_remote_start_enabled(false)) {
            ui_manager_->menu_screen.update_network_toggles();
        }
        return;
    }
    // Enabling hands motor starts to the network, so confirm it. Cancelling
    // returns to the menu, which re-reads the stored (off) state.
    ui_manager_->show_confirmation(
        "REMOTE START",
        "The web page and Home Assistant will be able to start the motor.\n\n"
        "Turn this on only on a trusted network, with the grinder always ready to run.",
        "ENABLE", lv_color_hex(THEME_COLOR_WARNING),
        []() { device_api.set_remote_start_enabled(true); });
}

void MenuUIController::update() {
    if (!ui_manager_) {
        return;
    }

    WeightSensor* sensor = ui_manager_->hardware_manager->get_weight_sensor();
    const uint32_t now = millis();

    // These labels used to be rewritten at the 62 Hz render rate, invalidating
    // large parts of the LVGL menu while the user was dragging it. Status data
    // does not need frame-rate polling; a 4 Hz refresh keeps it current and
    // leaves the render loop free to scroll smoothly.
    if (now - last_status_update_ms_ >= 250) {
        last_status_update_ms_ = now;
        ui_manager_->menu_screen.update_info(sensor, now, ESP.getFreeHeap());
        ui_manager_->menu_screen.update_diagnostics(sensor);
        ui_manager_->menu_screen.update_ble_status();
        ui_manager_->menu_screen.update_network_status();
        ui_manager_->menu_screen.update_network_toggles();
        ui_manager_->menu_screen.update_firmware_update_page();
    }

    if (ui_manager_->menu_screen.is_scale_page_active()) {
        float display_weight = sensor ? sensor->get_display_weight() : 0.0f;
        ui_manager_->menu_screen.update_scale_weight(display_weight);
    }
}

void MenuUIController::handle_calibrate() {
    if (ui_manager_) {
        ui_manager_->switch_to_state(UIState::CALIBRATION);
    }
}

void MenuUIController::handle_reset() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "FACTORY RESET",
        "Erases all settings, including Wi-Fi and calibration, plus grind history and lifetime statistics.\n\n"
        "This cannot be undone.",
        "RESET",
        lv_color_hex(THEME_COLOR_ERROR),
        [this]() { perform_factory_reset(); },
        "CANCEL",
        [this]() { return_to_menu(); }
    );
}

void MenuUIController::handle_purge() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "PURGE LOGS",
        "Removes all saved grind logs. Lifetime statistics are kept.\n\n"
        "This cannot be undone.",
        "PURGE",
        lv_color_hex(THEME_COLOR_ERROR),
        [this]() { execute_purge_operation(); },
        "CANCEL",
        [this]() { return_to_menu(); }
    );
}

void MenuUIController::handle_motor_test() {
    if (!ui_manager_) return;
    if (ui_manager_->show_motor_safety_stop_notice()) return;

    ui_manager_->show_confirmation(
        "MOTOR TEST",
        "Motor will be engaged for 1 second."
        "\n\n"
        "Make sure grinder is safe to run.",
        "RUN",
        lv_color_hex(THEME_COLOR_SUCCESS),
        [this]() { run_motor_test(); },
        "CANCEL",
        [this]() { return_to_menu(); }
    );
}

void MenuUIController::handle_scale_open() {
    if (!ui_manager_) return;
    auto* hardware = ui_manager_->get_hardware_manager();
    if (!hardware) return;

    ui_manager_->menu_screen.reset_scale_display();

    UIOperations::execute_tare(hardware, [this](bool tared) {
        if (!ui_manager_) return;
        ui_manager_->refresh_auto_action_settings();
        if (!tared) show_tare_failed();

        auto* sensor = ui_manager_->hardware_manager->get_weight_sensor();
        float weight = sensor ? sensor->get_display_weight() : 0.0f;
        if (ui_manager_->menu_screen.is_scale_page_active()) {
            ui_manager_->menu_screen.update_scale_weight(weight);
        }
    });
}

void MenuUIController::handle_scale_tare() {
    if (!ui_manager_) return;
    auto* hardware = ui_manager_->get_hardware_manager();
    if (!hardware) return;

    UIOperations::execute_tare(hardware, [this](bool tared) {
        if (!ui_manager_) return;
        ui_manager_->refresh_auto_action_settings();
        if (!tared) show_tare_failed();

        auto* sensor = ui_manager_->hardware_manager->get_weight_sensor();
        float weight = sensor ? sensor->get_display_weight() : 0.0f;
        if (ui_manager_->menu_screen.is_scale_page_active()) {
            ui_manager_->menu_screen.update_scale_weight(weight);
        }
    });
}

void MenuUIController::show_tare_failed() {
    auto* hardware = ui_manager_->get_hardware_manager();
    WeightSensor* sensor = hardware ? hardware->get_weight_sensor() : nullptr;
    const bool has_reading = sensor && sensor->has_recent_sample();
    ui_manager_->show_confirmation(
        "Tare incomplete",
        has_reading ? "The scale kept moving. Keep it still, then tap TARE again."
                    : "No reading from the load cell. Check its wiring, then tap TARE again.",
        "OK", lv_color_hex(THEME_COLOR_WARNING), nullptr, "BACK");
}

void MenuUIController::handle_autotune() {
    if (!ui_manager_) return;
    if (ui_manager_->show_motor_safety_stop_notice()) return;

    // Show confirmation screen with setup instructions
    auto autotune_controller = ui_manager_->autotune_controller_.get();
    if (autotune_controller) {
        ui_manager_->show_confirmation(
            "Pulse Tune",
            "Load beans and put a cup on the scale.\n\n"
            "Takes about 1 minute.",
            "START",
            lv_color_hex(THEME_COLOR_ACCENT),
            [autotune_controller]() { autotune_controller->confirm_and_begin(); },
            "CANCEL",
            [this]() { return_to_menu(); }
        );
    }
}

void MenuUIController::handle_back() {
    if (!ui_manager_) return;
    ui_manager_->set_current_tab(ReadyScreen::MENU_TAB_INDEX);
    ui_manager_->switch_to_state(UIState::READY);
}

void MenuUIController::handle_refresh_stats() {
    if (!ui_manager_) return;
    ui_manager_->menu_screen.refresh_statistics();
}

void MenuUIController::handle_diagnostics_reset() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "Clear Warnings",
        "This will clear all active diagnostic warnings.\n\nContinue?",
        "CLEAR",
        lv_color_hex(THEME_COLOR_WARNING),
        [this]() { perform_diagnostics_reset(); },
        "CANCEL",
        [this]() { return_to_menu(); }
    );
}

void MenuUIController::perform_diagnostics_reset() {
    if (!ui_manager_) return;

    auto* diagnostics = ui_manager_->diagnostics_controller_.get();
    if (diagnostics) {
        diagnostics->reset_diagnostic(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED);
        diagnostics->reset_diagnostic(DiagnosticCode::MECHANICAL_INSTABILITY);
        diagnostics->reset_noise_tracking();
    }

    auto* grind_controller = ui_manager_->get_grind_controller();
    if (grind_controller) {
        grind_controller->reset_mechanical_anomaly_count();
    }

    auto* hardware = ui_manager_->get_hardware_manager();
    auto* sensor = hardware ? hardware->get_weight_sensor() : nullptr;
    if (sensor) {
        ui_manager_->menu_screen.update_diagnostics(sensor);
    }
}

void MenuUIController::handle_ble_toggle() {
    if (!ui_manager_ || !ui_manager_->bluetooth_manager) return;

    auto* ble = ui_manager_->bluetooth_manager;
    if (ble->is_enabled()) {
        ble->disable();
        LOG_DEBUG_PRINTLN("Bluetooth disabled by user");
        ui_manager_->menu_screen.update_ble_status();
        return;
    }

    auto completion = [this]() {
        ui_manager_->menu_screen.update_ble_status();
    };

    auto operation = [ble]() {
        ble->enable();
        LOG_DEBUG_PRINTLN("Bluetooth enabled by user (30 minute timeout)");
    };

    auto& overlay = BlockingOperationOverlay::getInstance();
    overlay.show_and_execute(BlockingOperation::BLE_ENABLING, operation, completion);
}

void MenuUIController::handle_ble_startup_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->menu_screen.get_ble_startup_toggle();
    if (!toggle) return;

    bool startup_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("bluetooth", false);
    prefs.putBool("startup", startup_enabled);
    prefs.end();

    LOG_DEBUG_PRINTLN(startup_enabled ? "Bluetooth startup enabled" : "Bluetooth startup disabled");
}

void MenuUIController::handle_logging_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->menu_screen.get_logging_toggle();
    if (!toggle) return;

    bool logging_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("logging", false);
    prefs.putBool("enabled", logging_enabled);
    prefs.end();

    LOG_DEBUG_PRINTLN(logging_enabled ? "Logging enabled" : "Logging disabled");
}

void MenuUIController::handle_grind_mode_swipe_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->menu_screen.get_grind_mode_swipe_toggle();
    if (!toggle) return;

    bool swipe_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("swipe", false);
    prefs.putBool("enabled", swipe_enabled);
    prefs.end();

    LOG_DEBUG_PRINTLN(swipe_enabled ? "Grind mode swipe gestures enabled" : "Grind mode swipe gestures disabled");
}

void MenuUIController::handle_grind_mode_radio_button() {
    if (!ui_manager_ || !ui_manager_->profile_controller) return;

    lv_obj_t* radio_group = ui_manager_->menu_screen.get_grind_mode_radio_group();
    if (!radio_group) return;

    int selected_index = radio_button_group_get_selection(radio_group);
    if (selected_index < 0) return;

    GrindMode new_mode = (selected_index == 0) ? GrindMode::WEIGHT : GrindMode::TIME;
    ui_manager_->profile_controller->set_grind_mode(new_mode);
    ui_manager_->current_mode = new_mode;
    if (ui_manager_->ready_controller_) {
        ui_manager_->ready_controller_->refresh_profiles();
    }
    ui_manager_->edit_target = get_current_profile_target(*ui_manager_->profile_controller, new_mode);
    if (ui_manager_->state_machine->is_state(UIState::EDIT)) {
        if (ui_manager_->edit_controller_) {
            ui_manager_->edit_controller_->update_display();
        }
    }

    LOG_DEBUG_PRINTLN(selected_index == 0 ? "Grind mode set to WEIGHT via radio button" : "Grind mode set to TIME via radio button");
}

void MenuUIController::handle_auto_start_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->menu_screen.get_auto_start_toggle();
    if (!toggle) return;

    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("autogrind", false);
    prefs.putBool("auto_start", enabled);
    prefs.end();

    if (ui_manager_) {
        ui_manager_->refresh_auto_action_settings();
    }

    LOG_DEBUG_PRINTLN(enabled ? "Auto-start on cup enabled" : "Auto-start on cup disabled");
}

void MenuUIController::handle_auto_start_threshold_slider() {
    if (!ui_manager_) return;
    auto* slider = ui_manager_->menu_screen.get_auto_start_threshold_slider();
    if (!slider) return;
    const float threshold_g = lv_slider_get_value(slider) / MenuScreen::kAutoStartThresholdSliderScale;
    ui_manager_->menu_screen.update_auto_start_threshold_label(threshold_g);
}

void MenuUIController::handle_auto_start_threshold_slider_released() {
    if (!ui_manager_) return;
    auto* slider = ui_manager_->menu_screen.get_auto_start_threshold_slider();
    if (!slider) return;

    float threshold_g = lv_slider_get_value(slider) / MenuScreen::kAutoStartThresholdSliderScale;
    threshold_g = std::clamp(threshold_g,
                             USER_AUTO_GRIND_TRIGGER_MIN_G,
                             USER_AUTO_GRIND_TRIGGER_MAX_G);
    Preferences prefs;
    if (prefs.begin("autogrind", false)) {
        prefs.putFloat("start_delta_g", threshold_g);
        prefs.end();
    }
    ui_manager_->refresh_auto_action_settings();
    ui_manager_->menu_screen.update_auto_start_threshold_label(threshold_g);
    LOG_DEBUG_PRINT("Auto-start cup threshold set to: ");
    LOG_DEBUG_PRINT(threshold_g);
    LOG_DEBUG_PRINTLN("g");
}

void MenuUIController::handle_auto_return_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->menu_screen.get_auto_return_toggle();
    if (!toggle) return;

    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("autogrind", false);
    prefs.putBool("auto_return", enabled);
    prefs.end();

    if (ui_manager_) {
        ui_manager_->refresh_auto_action_settings();
    }

    LOG_DEBUG_PRINTLN(enabled ? "Auto return on cup removal enabled" : "Auto return on cup removal disabled");
}

void MenuUIController::handle_grinder_purge_mode_radio_button() {
    if (!ui_manager_) return;

    auto* radio_group = ui_manager_->menu_screen.get_grinder_purge_mode_radio_group();
    if (!radio_group) return;

    int selected_index = radio_button_group_get_selection(radio_group);

    auto* hardware = ui_manager_->get_hardware_manager();
    Preferences* prefs = hardware ? hardware->get_preferences() : nullptr;
    if (prefs) {
        prefs->putInt(GrindController::PREF_KEY_GRINDER_MODE, selected_index);
    }

    LOG_DEBUG_PRINTLN(selected_index == 0 ? "Grinder purge mode: Prime (keep coffee)" : "Grinder purge mode: Purge (discard grinds)");
}

void MenuUIController::handle_grinder_purge_amount_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_grinder_purge_amount_slider();
    if (!slider) return;

    int slider_value = lv_slider_get_value(slider);
    float amount_g = slider_value / MenuScreen::kPurgeSliderScale;
    if (amount_g < GRIND_PURGE_AMOUNT_MIN_G) amount_g = GRIND_PURGE_AMOUNT_MIN_G;
    if (amount_g > GRIND_PURGE_AMOUNT_MAX_G) amount_g = GRIND_PURGE_AMOUNT_MAX_G;

    // Update the label via MenuScreen method
    ui_manager_->menu_screen.update_grinder_purge_amount_label(amount_g);
}

void MenuUIController::handle_grinder_purge_amount_slider_released() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_grinder_purge_amount_slider();
    if (!slider) return;

    int slider_value = lv_slider_get_value(slider);
    float amount_g = slider_value / MenuScreen::kPurgeSliderScale;
    if (amount_g < GRIND_PURGE_AMOUNT_MIN_G) {
        amount_g = GRIND_PURGE_AMOUNT_MIN_G;
        lv_slider_set_value(slider, static_cast<int>(GRIND_PURGE_AMOUNT_MIN_G * MenuScreen::kPurgeSliderScale + 0.5f), LV_ANIM_OFF);
    } else if (amount_g > GRIND_PURGE_AMOUNT_MAX_G) {
        amount_g = GRIND_PURGE_AMOUNT_MAX_G;
        lv_slider_set_value(slider, static_cast<int>(GRIND_PURGE_AMOUNT_MAX_G * MenuScreen::kPurgeSliderScale + 0.5f), LV_ANIM_OFF);
    }

    auto* hardware = ui_manager_->get_hardware_manager();
    Preferences* prefs = hardware ? hardware->get_preferences() : nullptr;
    if (prefs) {
        prefs->putFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, amount_g);
    }

    LOG_DEBUG_PRINT("Grinder purge amount set to: ");
    LOG_DEBUG_PRINT(amount_g);
    LOG_DEBUG_PRINTLN("g");

    ui_manager_->menu_screen.update_grinder_purge_amount_label(amount_g);
}

void MenuUIController::handle_grind_freshness_hours_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_grind_freshness_hours_slider();
    if (!slider) return;

    // Map slider value to hours (discrete steps: 0.5, 1, 2, 3, 4, 8, 12, 24, 48)
    static const float freshness_steps[] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f, 12.0f, 24.0f, 48.0f};
    int slider_index = lv_slider_get_value(slider);
    if (slider_index < 0) slider_index = 0;
    if (slider_index > 8) slider_index = 8;
    float hours = freshness_steps[slider_index];

    // Update the label via MenuScreen method
    ui_manager_->menu_screen.update_grind_freshness_hours_label(hours);
}

void MenuUIController::handle_grind_freshness_hours_slider_released() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_grind_freshness_hours_slider();
    if (!slider) return;

    // Map slider value to hours (discrete steps: 0.5, 1, 2, 3, 4, 8, 12, 24, 48)
    static const float freshness_steps[] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f, 12.0f, 24.0f, 48.0f};
    int slider_index = lv_slider_get_value(slider);
    if (slider_index < 0) slider_index = 0;
    if (slider_index > 8) slider_index = 8;
    float hours = freshness_steps[slider_index];

    auto* hardware = ui_manager_->get_hardware_manager();
    Preferences* prefs = hardware ? hardware->get_preferences() : nullptr;
    if (prefs) {
        prefs->putFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, hours);
    }

    LOG_DEBUG_PRINT("Grind freshness hours set to: ");
    LOG_DEBUG_PRINT(hours);
    LOG_DEBUG_PRINTLN("h");

    ui_manager_->menu_screen.update_grind_freshness_hours_label(hours);
}

void MenuUIController::handle_coast_ratio_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_coast_ratio_slider();
    if (!slider) return;

    int slider_value = lv_slider_get_value(slider);
    float ratio = slider_value / MenuScreen::kCoastRatioSliderScale;
    if (ratio < GRIND_LATENCY_TO_COAST_RATIO_MIN) ratio = GRIND_LATENCY_TO_COAST_RATIO_MIN;
    if (ratio > GRIND_LATENCY_TO_COAST_RATIO_MAX) ratio = GRIND_LATENCY_TO_COAST_RATIO_MAX;

    ui_manager_->menu_screen.update_coast_ratio_label(ratio);
}

void MenuUIController::handle_coast_ratio_slider_released() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_coast_ratio_slider();
    if (!slider) return;

    int slider_value = lv_slider_get_value(slider);
    float ratio = slider_value / MenuScreen::kCoastRatioSliderScale;

    const int coast_slider_min = static_cast<int>(GRIND_LATENCY_TO_COAST_RATIO_MIN * MenuScreen::kCoastRatioSliderScale + 0.5f);
    const int coast_slider_max = static_cast<int>(GRIND_LATENCY_TO_COAST_RATIO_MAX * MenuScreen::kCoastRatioSliderScale + 0.5f);

    if (ratio < GRIND_LATENCY_TO_COAST_RATIO_MIN) {
        ratio = GRIND_LATENCY_TO_COAST_RATIO_MIN;
        lv_slider_set_value(slider, coast_slider_min, LV_ANIM_OFF);
    } else if (ratio > GRIND_LATENCY_TO_COAST_RATIO_MAX) {
        ratio = GRIND_LATENCY_TO_COAST_RATIO_MAX;
        lv_slider_set_value(slider, coast_slider_max, LV_ANIM_OFF);
    }

    auto* grind_controller = ui_manager_->get_grind_controller();
    if (grind_controller) {
        if (!grind_controller->save_coast_ratio(ratio)) {
            ratio = grind_controller->get_coast_ratio();
            lv_slider_set_value(slider, static_cast<int>(ratio * MenuScreen::kCoastRatioSliderScale + 0.5f), LV_ANIM_OFF);
            LOG_BLE("Could not save coast ratio; retaining active value\n");
        }
    }

    LOG_DEBUG_PRINT("Coast ratio set to: ");
    LOG_DEBUG_PRINTLN(ratio);

    ui_manager_->menu_screen.update_coast_ratio_label(ratio);
}

void MenuUIController::handle_motor_latency_slider() {
    if (!ui_manager_) return;
    auto* slider = ui_manager_->menu_screen.get_motor_latency_slider();
    if (!slider) return;
    ui_manager_->menu_screen.update_motor_latency_label(
        static_cast<float>(lv_slider_get_value(slider)));
}

void MenuUIController::handle_motor_latency_slider_released() {
    if (!ui_manager_) return;
    auto* slider = ui_manager_->menu_screen.get_motor_latency_slider();
    if (!slider) return;

    int latency_ms = lv_slider_get_value(slider);
    latency_ms = ((latency_ms + MenuScreen::kMotorLatencySliderStepMs / 2) /
                  MenuScreen::kMotorLatencySliderStepMs) *
                 MenuScreen::kMotorLatencySliderStepMs;
    latency_ms = std::clamp(latency_ms,
                            static_cast<int>(GRIND_AUTOTUNE_LATENCY_MIN_MS),
                            static_cast<int>(GRIND_AUTOTUNE_LATENCY_MAX_MS));
    lv_slider_set_value(slider, latency_ms, LV_ANIM_OFF);

    auto* grind_controller = ui_manager_->get_grind_controller();
    if (grind_controller) {
        if (!grind_controller->save_motor_latency(static_cast<float>(latency_ms))) {
            latency_ms = static_cast<int>(grind_controller->get_motor_response_latency());
            lv_slider_set_value(slider, latency_ms, LV_ANIM_OFF);
            LOG_BLE("Could not save motor latency; retaining active value\n");
        }
    }
    ui_manager_->menu_screen.update_motor_latency_label(static_cast<float>(latency_ms));
    LOG_DEBUG_PRINTF("Motor response latency manually set to: %dms\n", latency_ms);
}

void MenuUIController::handle_brightness_normal_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_brightness_normal_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    ui_manager_->get_hardware_manager()->get_display()->set_brightness(brightness);
    ui_manager_->menu_screen.update_brightness_labels(brightness_percent, -1);
    LOG_DEBUG_PRINTF("Normal brightness set to %d%% (%.2f)\n", brightness_percent, brightness);
}

void MenuUIController::handle_brightness_normal_slider_released() {
    auto* slider = ui_manager_->menu_screen.get_brightness_normal_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    Preferences prefs;
    prefs.begin("brightness", false);
    prefs.putFloat("normal", brightness);
    prefs.end();
}

void MenuUIController::handle_brightness_screensaver_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->menu_screen.get_brightness_screensaver_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    ui_manager_->get_hardware_manager()->get_display()->set_brightness(brightness);
    ui_manager_->menu_screen.update_brightness_labels(-1, brightness_percent);
    LOG_DEBUG_PRINTF("Screensaver brightness set to %d%% (%.2f)\n", brightness_percent, brightness);
}

void MenuUIController::handle_brightness_screensaver_slider_released() {
    auto* slider = ui_manager_->menu_screen.get_brightness_screensaver_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    Preferences prefs;
    prefs.begin("brightness", false);
    prefs.putFloat("screensaver", brightness);
    prefs.end();

    float normal = get_normal_brightness();
    ui_manager_->get_hardware_manager()->get_display()->set_brightness(normal);
    LOG_DEBUG_PRINTF("Touch released - restored normal brightness to %.2f\n", normal);
}

void MenuUIController::handle_screensaver_startup_toggle() {
    auto* toggle = ui_manager_->menu_screen.get_screensaver_startup_toggle();
    if (!toggle) return;

    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    Preferences prefs;
    prefs.begin("screensaver", false);
    prefs.putBool("startup", enabled);
    prefs.end();

    LOG_BLE("Screensaver startup: %s\n", enabled ? "enabled" : "disabled");
}

void MenuUIController::handle_screensaver_sleep_toggle() {
    auto* toggle = ui_manager_->menu_screen.get_screensaver_sleep_toggle();
    if (!toggle) return;

    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    Preferences prefs;
    prefs.begin("screensaver", false);
    prefs.putBool("sleep", enabled);
    prefs.end();

}

void MenuUIController::handle_display_off_toggle() {
    auto* toggle = ui_manager_->menu_screen.get_display_off_toggle();
    if (!toggle) return;

    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    const ScreensaverTimingSettings settings = ScreensaverSettings::load_timing();
    if (!ScreensaverSettings::save_timing(settings.idle_timeout_s,
                                          settings.startup_timeout_s,
                                          enabled,
                                          settings.display_off_delay_s)) {
        ui_manager_->menu_screen.update_screensaver_toggles();
        return;
    }
    if (ui_manager_->screen_timeout_controller_) {
        ui_manager_->screen_timeout_controller_->apply_runtime_settings();
    }
    LOG_BLE("Display off after screensaver: %s (delay %us)\n",
            enabled ? "enabled" : "disabled", settings.display_off_delay_s);
}

void MenuUIController::perform_factory_reset() {
    if (!ui_manager_) return;

    LOG_DEBUG_PRINTLN("Factory reset: clearing grind history and NVS preferences, then rebooting...");

    // Grind history lives on LittleFS, outside NVS. Session numbering restarts
    // with NVS, so old files would otherwise be overwritten by new sessions.
    extern GrindLogger grind_logger;
    if (!grind_logger.clear_all_sessions_from_flash()) {
        LOG_DEBUG_PRINTLN("Factory reset: some grind history files could not be removed");
    }

    nvs_flash_deinit();
    esp_err_t erase_result = nvs_flash_erase();

    if (erase_result == ESP_OK) {
        LOG_DEBUG_PRINTLN("Factory reset: NVS erase successful. Restarting device...");
    } else {
        LOG_DEBUG_PRINTF("Factory reset: NVS erase failed (code %d). Forcing restart...\n",
                         static_cast<int>(erase_result));
    }

    delay(100);
    esp_restart();
}

void MenuUIController::execute_purge_operation() {
    if (!ui_manager_) return;

    auto completion = [this]() {
        return_to_menu();
        ui_manager_->menu_screen.refresh_statistics(false);
    };

    auto purge_task = []() {
        LOG_DEBUG_PRINTLN("\n=== PURGE GRIND LOGS INITIATED ===");
        extern GrindLogger grind_logger;
        bool success = grind_logger.clear_all_sessions_from_flash();
        if (success) {
            LOG_DEBUG_PRINTLN("Grind logs purged successfully - reinitializing logger...");
        } else {
            LOG_DEBUG_PRINTLN("ERROR: Failed to purge all grind log data!");
        }
    };

    auto& overlay = BlockingOperationOverlay::getInstance();
    overlay.show_and_execute(BlockingOperation::CUSTOM, purge_task, completion,
                             "PURGING LOGS...\nPlease wait");
}

void MenuUIController::run_motor_test() {
    if (!ui_manager_) return;

    auto* hardware = ui_manager_->get_hardware_manager();
    auto* grinder = hardware ? hardware->get_grinder() : nullptr;
    if (!grinder || grinder->has_safety_stop()) return;

    const auto token = operation_interlock().try_acquire();
    if (!token) return;
    motor_test_token_ = token;

    // Allocate the completion callback before starting the motor. If allocation
    // fails there must be neither a pulse nor an abandoned reservation.
    motor_timer_ = lv_timer_create(static_motor_timer_cb, 2000, this);
    if (!motor_timer_) {
        operation_interlock().release(motor_test_token_);
        motor_test_token_ = 0;
        return;
    }
    ui_manager_->set_background_active(true);
    grinder->start_pulse_rmt(1000);

    // Update statistics for motor test (1000ms = 1 second)
    statistics_manager.update_motor_test(1000);

}

void MenuUIController::return_to_menu() {
    if (!ui_manager_) return;
    ui_manager_->set_current_tab(ReadyScreen::MENU_TAB_INDEX);
    ui_manager_->switch_to_state(UIState::MENU);
}

float MenuUIController::get_normal_brightness() const {
    if (!ui_manager_ || !ui_manager_->hardware_manager) {
        return USER_SCREEN_BRIGHTNESS_NORMAL;
    }

    Preferences prefs;
    prefs.begin("brightness", true);
    float brightness = prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
    prefs.end();

    if (brightness < 0.15f) {
        brightness = 0.15f;
    }
    return brightness;
}

float MenuUIController::get_screensaver_brightness() const {
    if (!ui_manager_ || !ui_manager_->hardware_manager) {
        return USER_SCREEN_BRIGHTNESS_DIMMED;
    }

    Preferences prefs;
    prefs.begin("brightness", true);
    float brightness = prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
    prefs.end();

    if (brightness < 0.15f) {
        brightness = 0.15f;
    }
    return brightness;
}

void MenuUIController::stop_motor_timer() {
    if (motor_timer_) {
        lv_timer_del(motor_timer_);
        motor_timer_ = nullptr;
    }
}

void MenuUIController::motor_timer_cb(lv_timer_t* timer) {
    if (!ui_manager_ || timer != motor_timer_ ||
        !operation_interlock().owns(motor_test_token_)) {
        return;
    }

    auto* grinder = ui_manager_->get_hardware_manager()->get_grinder();
    if (!grinder) return; // Cannot verify a safe stop; keep the reservation.
    grinder->stop();

    stop_motor_timer();
    ui_manager_->set_background_active(false);
    // The test started from the menu; do not pull the user back there from
    // wherever they went during the pulse.
    operation_interlock().release(motor_test_token_);
    motor_test_token_ = 0;
}

void MenuUIController::static_motor_timer_cb(lv_timer_t* timer) {
    if (!timer) {
        return;
    }
    auto* controller = static_cast<MenuUIController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->motor_timer_cb(timer);
    }
}
