// Stand-ins for the firmware's communication subsystems (Bluetooth LE, Wi-Fi, web server, remote
// API, GaggiMate client, provisioning). The twin has no radio, so these classes are compiled
// from their real headers but implemented here as "radio absent, never connected, no transfer
// in progress". Every function the rest of the firmware links against is defined with that
// meaning; nothing here takes part in grind control. See sim/ARCHITECTURE.md.
#include "bluetooth/manager.h"
#include "bluetooth/ota_handler.h"
#include "network/device_api.h"
#include "network/device_web_server.h"
#include "network/gaggimate_status_client.h"
#include "network/network_manager.h"
#include "network/provisioning_service.h"

#include <WiFi.h>

WiFiClass WiFi;

// ---------------------------------------------------------------- Bluetooth

OTAHandler::OTAHandler()
    : ota_in_progress(false), patch_size(0), received_size(0), current_status(BLE_OTA_IDLE),
      is_full_update(false), power_state(NORMAL_POWER), normal_cpu_freq_mhz(240), patch_writer{} {}
OTAHandler::~OTAHandler() = default;
float OTAHandler::get_progress() const { return 0.0f; }

BluetoothManager::BluetoothManager()
    : ble_server(nullptr), ota_service(nullptr), data_service(nullptr), debug_service(nullptr),
      sysinfo_service(nullptr), ota_data_characteristic(nullptr), ota_control_characteristic(nullptr),
      ota_status_characteristic(nullptr), build_number_characteristic(nullptr),
      data_control_characteristic(nullptr), data_transfer_characteristic(nullptr),
      data_status_characteristic(nullptr), debug_rx_characteristic(nullptr), debug_tx_characteristic(nullptr),
      sysinfo_system_characteristic(nullptr), sysinfo_performance_characteristic(nullptr),
      sysinfo_hardware_characteristic(nullptr), sysinfo_sessions_characteristic(nullptr),
      sysinfo_diagnostics_characteristic(nullptr), device_connected(false), ble_enabled(false),
      debug_stream_active(false), enable_time(0), timeout_ms(BLE_AUTO_DISABLE_TIMEOUT_MS),
      last_disconnect_time(0), data_export_in_progress(false), data_status(BLE_DATA_IDLE), current_chunk(0),
      next_chunk_time(0), current_file_session_id(0), sessions_info_dirty(true), last_session_storage_version(0),
      last_reported_export_state(false), ui_status_queue(nullptr), diagnostic_report_pending(false),
      diagnostic_report_in_progress(false) {}
BluetoothManager::~BluetoothManager() = default;
void BluetoothManager::init(Preferences*) { LOG_BLE("[SIM] Bluetooth: no radio in the digital twin\n"); }
void BluetoothManager::enable(unsigned long) {}
void BluetoothManager::enable_during_bootup() {}
void BluetoothManager::disable() {}
void BluetoothManager::handle() {}
void BluetoothManager::stop_data_export() {}
float BluetoothManager::get_data_export_progress() const { return 0.0f; }
unsigned long BluetoothManager::get_bluetooth_timeout_remaining_ms() const { return 0; }
void BluetoothManager::set_ui_status_callback(UIStatusCallback callback) { ui_status_callback = callback; }
bool BluetoothManager::check_ota_failure_after_boot(String&) { return false; }
void BluetoothManager::forget_update_check() {}
bool BluetoothManager::dequeue_ui_status(char*, size_t) { return false; }
void BluetoothManager::onConnect(BLEServer*) {}
void BluetoothManager::onDisconnect(BLEServer*) {}
void BluetoothManager::onWrite(BLECharacteristic*) {}
void BluetoothManager::onRead(BLECharacteristic*) {}

// ---------------------------------------------------------------- Wi-Fi manager (disabled)

SmartGrindNetworkManager network_manager;
void SmartGrindNetworkManager::init(Preferences* preferences) { preferences_ = preferences; }
void SmartGrindNetworkManager::update() {}
void SmartGrindNetworkManager::request_enabled(bool) {}
void SmartGrindNetworkManager::request_forget_network() {}
bool SmartGrindNetworkManager::desired_enabled() const { return false; }
bool SmartGrindNetworkManager::has_credentials() const { return false; }
String SmartGrindNetworkManager::hostname() const { return String("smartgrind"); }
String SmartGrindNetworkManager::device_id() const { return String("digital-twin"); }
String SmartGrindNetworkManager::network_name() const { return String(); }
String SmartGrindNetworkManager::ip_address() const { return String(); }

// ---------------------------------------------------------------- web server and API (not started)

DeviceWebServer device_web_server;
void DeviceWebServer::init(HardwareManager* hardware, GrindController* grind, BluetoothManager* bluetooth,
                           ProfileController*) {
    hardware_manager_ = hardware;
    grind_controller_ = grind;
    bluetooth_manager_ = bluetooth;
    initialized_ = true;
}
void DeviceWebServer::begin() {}
void DeviceWebServer::update() {}
uint8_t DeviceWebServer::ota_progress_percent() const { return 0; }
String DeviceWebServer::latest_release_tag() const { return String(); }
bool DeviceWebServer::install_available_update() { return false; }

DeviceApi device_api;
bool DeviceApi::process_commands(bool) { return false; }
void DeviceApi::complete_settings_application(bool) {}
bool DeviceApi::set_remote_start_enabled(bool) { return false; }

ProvisioningService provisioning_service;
void ProvisioningService::init(Preferences*, AsyncWebServer*) {}
void ProvisioningService::update() {}

GaggiMateStatusClient gaggimate_status_client;
void GaggiMateStatusClient::init() {}
GaggiMateStatus GaggiMateStatusClient::status() const { return GaggiMateStatus{}; }
