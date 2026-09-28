"""Compile the production network-security policies and check their decisions."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "network/request_policy.h"
#include "network/remote_start_policy.h"
#include "system/update_authorization.h"
#include "bluetooth/diagnostic_report_policy.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <thread>

using namespace RequestPolicy;

DeviceNames home() {
    DeviceNames names;
    names.hostname = "smartgrind";
    names.station_ip = "192.168.1.50";
    names.setup_ip = "";
    return names;
}

DeviceNames setup() {
    DeviceNames names;
    names.hostname = "smartgrind";
    names.station_ip = "";
    names.setup_ip = "4.4.4.1";
    names.setup_access_point = true;
    return names;
}

Verdict check(const DeviceNames& names, const char* host, const char* origin,
              bool safe, bool websocket = false) {
    RequestHeaders request;
    request.host = host;
    request.origin = origin;
    request.safe_method = safe;
    request.websocket = websocket;
    return evaluate(request, names);
}

void host_rules() {
    const DeviceNames names = home();
    for (const char* ok : {"smartgrind.local", "SmartGrind.Local", "smartgrind.local:80",
                           "smartgrind.local.", "smartgrind", "smartgrind.lan", "smartgrind.home",
                           "smartgrind.home.arpa", "smartgrind.localdomain", "smartgrind.internal",
                           "192.168.1.50", "192.168.1.50:80"}) {
        assert(host_allowed(ok, names));
    }
    for (const char* bad : {"", "evil.com", "smartgrind.evil.com", "smartgrind.local.evil.com",
                            "smartgrindx.local", "xsmartgrind.local", "smartgrind.local:8080",
                            "smartgrind.local:80:80", "smartgrind.local:", "192.168.1.5",
                            "192.168.1.500", "4.4.4.1", "[::1]", "[::1]:80", "smart grind.local",
                            "smartgrind.local/", "smartgrind..local"}) {
        assert(!host_allowed(bad, names));
    }
    assert(!host_allowed(nullptr, names));
    assert(host_allowed("4.4.4.1", setup()));
    DeviceNames unnamed = home();
    unnamed.hostname = "";
    assert(!host_allowed("smartgrind.local", unnamed) && host_allowed("192.168.1.50", unnamed));
}

void origin_rules() {
    assert(origin_matches("http://smartgrind.local", "smartgrind.local"));
    assert(origin_matches("http://smartgrind.local:80", "smartgrind.local"));
    assert(origin_matches("http://smartgrind.local", "smartgrind.local:80"));
    assert(origin_matches("HTTP://SmartGrind.local", "smartgrind.local"));
    assert(origin_matches("http://192.168.1.50", "192.168.1.50"));
    for (const char* bad : {"http://evil.com", "https://smartgrind.local", "null", "http://",
                            "http://smartgrind.local/", "http://smartgrind.local:8080",
                            "ws://smartgrind.local", "http://smartgrind.local.evil.com", ""}) {
        assert(!origin_matches(bad, "smartgrind.local"));
    }
    // Same-origin only: two names for the grinder are still different origins.
    assert(!origin_matches("http://192.168.1.50", "smartgrind.local"));
    assert(!origin_matches(nullptr, "smartgrind.local"));
}

void station_admission() {
    const DeviceNames names = home();
    // DNS rebinding: the browser addresses the grinder by the attacker's name.
    assert(check(names, "evil.com", nullptr, true) == Verdict::REJECT_HOST);
    assert(check(names, "evil.com", "http://evil.com", false) == Verdict::REJECT_HOST);
    assert(check(names, "evil.com", "http://evil.com", true, true) == Verdict::REJECT_HOST);
    // Cross-site form post or WebSocket from a page on another site.
    assert(check(names, "smartgrind.local", "http://evil.com", false) == Verdict::REJECT_ORIGIN);
    assert(check(names, "smartgrind.local", "http://evil.com", true, true) == Verdict::REJECT_ORIGIN);
    assert(check(names, "192.168.1.50", "null", false) == Verdict::REJECT_ORIGIN);
    // The grinder's own page and clients that are not browsers.
    assert(check(names, "smartgrind.local", "http://smartgrind.local", false) == Verdict::ALLOW);
    assert(check(names, "smartgrind.local", "http://smartgrind.local", true, true) == Verdict::ALLOW);
    assert(check(names, "192.168.1.50", nullptr, false) == Verdict::ALLOW);
    assert(check(names, "smartgrind.local", nullptr, true) == Verdict::ALLOW);
}

void setup_admission() {
    const DeviceNames names = setup();
    // Captive-portal probes read with arbitrary host names.
    assert(check(names, "connectivitycheck.gstatic.com", nullptr, true) == Verdict::ALLOW);
    assert(check(names, "captive.apple.com", nullptr, true) == Verdict::ALLOW);
    // Writes must address the setup network itself.
    assert(check(names, "example.com", "http://example.com", false) == Verdict::REJECT_HOST);
    assert(check(names, "example.com", nullptr, false) == Verdict::REJECT_HOST);
    assert(check(names, "4.4.4.1", "http://example.com", false) == Verdict::REJECT_ORIGIN);
    assert(check(names, "4.4.4.1", "http://4.4.4.1", false) == Verdict::ALLOW);
    assert(check(names, "4.4.4.1", "null", false) == Verdict::ALLOW);
    assert(check(names, "4.4.4.1", nullptr, false) == Verdict::ALLOW);
    assert(check(names, "evil.com", nullptr, true, true) == Verdict::REJECT_HOST);
    // "null" is tolerated only on the setup network.
    assert(check(home(), "smartgrind.local", "null", true) == Verdict::REJECT_ORIGIN);
}

void update_authorization_rules() {
    UpdateAuthorization permission;
    assert(!permission.is_granted(0) && permission.remaining_ms(0) == 0);
    assert(!permission.consume(0));

    permission.grant(1000, 120000);
    assert(permission.is_granted(1000) && permission.remaining_ms(1000) == 120000);
    assert(permission.is_granted(120999) && permission.remaining_ms(120999) == 1);
    assert(!permission.is_granted(121000) && permission.remaining_ms(121000) == 0);

    permission.grant(1000, 120000);
    assert(permission.consume(5000));
    assert(!permission.is_granted(5000) && !permission.consume(5000));  // One update only.

    permission.grant(1000, 120000);
    assert(!permission.consume(121000));  // Expired permission cannot be used...
    assert(!permission.is_granted(1000));  // ...and is gone afterwards.

    permission.grant(1000, 120000);
    permission.revoke();
    assert(!permission.is_granted(2000) && !permission.consume(2000));

    // millis() wraps after about 49.7 days.
    permission.grant(0xFFFFFF00U, 0x200U);
    assert(permission.is_granted(0xFFFFFFF0U) && permission.is_granted(0x50U));
    assert(!permission.is_granted(0x100U));

    // Concurrent web and Bluetooth starts: exactly one wins.
    for (int round = 0; round < 200; ++round) {
        permission.grant(10, 1000);
        std::atomic<int> winners{0};
        std::thread web([&] { if (permission.consume(20)) ++winners; });
        std::thread ble([&] { if (permission.consume(20)) ++winners; });
        web.join();
        ble.join();
        assert(winners == 1);
    }
}

void remote_start_rules() {
    using RemoteStartPolicy::Decision;
    using RemoteStartPolicy::evaluate;
    assert(evaluate(false, true, false, 0, 5000, 3000) == Decision::SWITCHED_OFF);
    assert(evaluate(false, false, false, 0, 5000, 3000) == Decision::SWITCHED_OFF);
    assert(evaluate(true, false, false, 0, 5000, 3000) == Decision::UI_BUSY);
    assert(evaluate(true, true, false, 0, 0, 3000) == Decision::ALLOW);
    assert(evaluate(true, true, true, 1000, 3999, 3000) == Decision::TOO_SOON);
    assert(evaluate(true, true, true, 1000, 4000, 3000) == Decision::ALLOW);
    assert(evaluate(true, true, true, 0xFFFFFF00U, 0x100U, 3000) == Decision::TOO_SOON);
    // Every refusal fits the fixed acknowledgement frame with room to spare.
    for (Decision decision : {Decision::SWITCHED_OFF, Decision::UI_BUSY, Decision::TOO_SOON}) {
        const char* reason = RemoteStartPolicy::rejection_reason(decision);
        assert(reason && std::strlen(reason) > 0 && std::strlen(reason) < 80);
    }
}

void diagnostic_report_rules() {
    for (const char* secret : {"wifi_pass", "wifi_ap_pass", "wifi_ssid", "api_token", "unknown"}) {
        assert(!nvs_string_value_is_reportable(secret));
    }
    assert(!nvs_string_value_is_reportable(nullptr));
    for (const char* shown : {"style", "gm_host", "wifi_host", "new_build_nr", "new_fw_ver"}) {
        assert(nvs_string_value_is_reportable(shown));
    }
}

int main() {
    host_rules();
    origin_rules();
    station_admission();
    setup_admission();
    update_authorization_rules();
    remote_start_rules();
    diagnostic_report_rules();
}
'''


class NetworkSecurityTest(unittest.TestCase):
    def test_policies(self):
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "policies.cpp", Path(tmp) / "policies"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

    def test_secret_writes_stay_hidden(self):
        # Wi-Fi and setup-network passwords must never become reportable.
        policy = (ROOT / "src/bluetooth/diagnostic_report_policy.h").read_text()
        for key in ("wifi_pass", "wifi_ap_pass", "wifi_ssid"):
            self.assertNotIn(f'"{key}"', policy)

    def test_guard_covers_every_route(self):
        # The admission middleware must be registered before any route, and no
        # handler may opt out of server middleware.
        web = (ROOT / "src/network/device_web_server.cpp").read_text()
        init = web[web.index("void DeviceWebServer::init("):web.index("void DeviceWebServer::begin()")]
        self.assertLess(init.index("RequestGuard::install(server_)"), init.index("device_api.init("))
        self.assertLess(init.index("RequestGuard::install(server_)"), init.index("configure_routes()"))
        main = (ROOT / "src/main.cpp").read_text()
        self.assertLess(main.index("device_web_server.init("), main.index("provisioning_service.init("))
        for path in (ROOT / "src").rglob("*.cpp"):
            text = path.read_text()
            self.assertNotIn("skipServerMiddlewares", text, path)
            self.assertNotIn("setSkipServerMiddlewares", text, path)

    def test_upload_routes_admit_before_writing(self):
        # Upload callbacks run before server middleware, so each upload route
        # must admit the request itself before accepting the first chunk.
        source = (ROOT / "src/network/device_web_server.cpp").read_text()
        for route in ('"/api/v1/ota"', '"/api/v1/screensaver/image"), HTTP_POST'):
            start = source.index(route)
            upload = source[start:source.index("});", source.index("index == 0", start))]
            self.assertIn("RequestGuard::admit(request)", upload, route)
        ota = source[source.index('"/api/v1/ota"'):]
        self.assertIn("take_upload_token", ota[:ota.index("handle_ota_upload(request")])
        prepare = source[source.index('"/api/v1/ota/prepare"'):source.index('"/api/v1/ota/github"')]
        self.assertIn("update_authorization().consume", prepare)


if __name__ == "__main__":
    unittest.main()
