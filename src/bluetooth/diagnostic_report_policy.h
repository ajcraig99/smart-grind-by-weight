#pragma once

#include <cstring>

// The Bluetooth diagnostic report can be requested by any client in radio
// range, so it prints stored string values only for keys known to hold no
// secret. Every other string (Wi-Fi and setup-network passwords, network name)
// is reported by length alone. Add a key here only if its value is harmless to
// disclose.
inline bool nvs_string_value_is_reportable(const char* key) {
    static constexpr const char* REPORTABLE_KEYS[] = {
        "new_build_nr",  // Pending OTA build number
        "new_fw_ver",    // Pending OTA firmware version
        "wifi_host",     // Configured hostname
        "style",         // Screensaver style
        "gm_host",       // GaggiMate host name
    };
    if (!key) return false;
    for (const char* reportable : REPORTABLE_KEYS) {
        if (std::strcmp(key, reportable) == 0) return true;
    }
    return false;
}
