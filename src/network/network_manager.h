#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class NetworkState : uint8_t {
    WIFI_DISABLED,
    WIFI_NO_CREDENTIALS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_RETRY_WAIT,
    WIFI_SETUP_REQUIRED,
    WIFI_SETUP_AP
};

class SmartGrindNetworkManager {
public:
    void init(Preferences* preferences);
    void update();

    bool set_enabled(bool enabled);
    bool set_credentials(const String& ssid, const String& password);
    bool connect_saved_credentials();
    void clear_credentials();
    bool start_setup_access_point(const String& ssid, const String& password);

    // Touchscreen requests. The Wi-Fi driver is only driven from the task that
    // calls update(), so other tasks queue the change and update() applies it.
    void request_enabled(bool enabled);
    void request_forget_network();
    // Enabled state including a queued request, for UI toggles.
    bool desired_enabled() const;

    bool is_enabled() const { return enabled_.load(); }
    bool is_connected() const { return state() == NetworkState::WIFI_CONNECTED; }
    bool has_credentials() const;
    NetworkState state() const { return state_.load(); }
    String hostname() const;
    String device_id() const;
    String network_name() const;
    String ip_address() const;

private:
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
    static constexpr uint32_t RETRY_DELAY_MS = 30000;

    enum class PendingRequest : uint8_t { NONE, ENABLE, DISABLE, FORGET };

    Preferences* preferences_ = nullptr;
    std::atomic<NetworkState> state_{NetworkState::WIFI_DISABLED};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<PendingRequest> pending_request_{PendingRequest::NONE};
    bool ever_connected_ = false;
    bool mdns_started_ = false;
    uint32_t state_changed_at_ms_ = 0;
    String ssid_;
    String password_;
    String hostname_;
    mutable SemaphoreHandle_t settings_mutex_ = nullptr;

    void load_settings();
    void apply_pending_request();
    void begin_connection();
    void handle_connected();
    void stop_network();
    void set_state(NetworkState state);
    static String default_hostname();
    static String sanitize_hostname(const String& hostname);
};

extern SmartGrindNetworkManager network_manager;
