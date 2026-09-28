#pragma once

#include <lvgl.h>

class UIManager;

// Tracks OTA progress, handles failures, and manages data export UI

class OtaDataExportController {
public:
    explicit OtaDataExportController(UIManager* manager);

    void register_events();

    // Returns true when OTA handling consumed the frame (skip further updates)
    bool update();

    void update_progress(int percent);
    void update_status(const char* status);
    void show_failure_warning(const char* expected_build);
    void set_failure_info(const char* expected_build);
    void show_failure_screen();

private:
    void handle_failure_acknowledged();
    void start_data_export_ui();
    void poll_data_export();
    void stop_data_export_ui();
    void clear_failure_info();

    UIManager* ui_manager_;
    bool data_export_active_;
    bool firmware_update_screen_active_ = false;  // Update screen shown for a web or BLE update
    char expected_build_[16];
};
