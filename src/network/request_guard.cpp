#include "request_guard.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "../config/constants.h"
#include "network_manager.h"
#include "request_policy.h"

namespace {

constexpr uint32_t REJECTION_LOG_INTERVAL_MS = 10000;

size_t firmware_body_limit() {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    return target ? target->size + NETWORK_MAX_FIRMWARE_BODY_SLACK_BYTES : 0;
}

size_t body_limit(const AsyncWebServerRequest* request) {
    const String& url = request->url();
    if (request->method() == HTTP_POST) {
        if (url == "/api/v1/ota") return firmware_body_limit();
        if (url == "/api/v1/screensaver/image") return NETWORK_MAX_SCREENSAVER_BODY_BYTES;
    }
    return NETWORK_MAX_FORM_BODY_BYTES;
}

// Claims requests whose declared body is too large for their route. The server
// consults handlers when the headers end, before the body arrives, and a
// trivial handler makes it count and discard the body without buffering form
// fields or upload chunks.
class OversizedBodyRejector : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        return request && request->contentLength() > body_limit(request);
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        request->send(413, "application/json", "{\"error\":\"Request body is too large\"}");
    }

    bool isRequestHandlerTrivial() const override { return true; }
};

bool is_safe_method(WebRequestMethodComposite method) {
    return method == HTTP_GET || method == HTTP_HEAD || method == HTTP_OPTIONS;
}

void log_rejection(const AsyncWebServerRequest* request, RequestPolicy::Verdict verdict) {
    static uint32_t last_log_ms = 0;
    const uint32_t now = millis();
    if (last_log_ms != 0 && now - last_log_ms < REJECTION_LOG_INTERVAL_MS) return;
    last_log_ms = now;
    LOG_BLE("[WEB] Rejected %s %s: %s\n", request->methodToString(), request->url().c_str(),
            verdict == RequestPolicy::Verdict::REJECT_HOST ? "unrecognised host" : "foreign origin");
}

}  // namespace

namespace RequestGuard {

void install(AsyncWebServer& server) {
    server.addHandler(new OversizedBodyRejector());
    server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
        // An upload callback may already have answered; keep that response.
        if (request->getResponse() || admit(request)) next();
    });
}

bool admit(AsyncWebServerRequest* request) {
    if (!request) return false;

    const String hostname = network_manager.hostname();
    const String station_ip = network_manager.ip_address();
    const bool setup_access_point = network_manager.state() == NetworkState::WIFI_SETUP_AP;
    const String setup_ip = setup_access_point ? WiFi.softAPIP().toString() : String();
    RequestPolicy::DeviceNames names;
    names.hostname = hostname.c_str();
    names.station_ip = station_ip.c_str();
    names.setup_ip = setup_ip.c_str();
    names.setup_access_point = setup_access_point;

    const AsyncWebHeader* origin = request->getHeader("Origin");
    RequestPolicy::RequestHeaders headers;
    headers.host = request->host().c_str();
    headers.origin = origin ? origin->value().c_str() : nullptr;
    headers.safe_method = is_safe_method(request->method());
    headers.websocket = request->isWebSocketUpgrade();

    const RequestPolicy::Verdict verdict = RequestPolicy::evaluate(headers, names);
    if (verdict == RequestPolicy::Verdict::ALLOW) return true;

    log_rejection(request, verdict);
    request->send(403, "application/json",
                  verdict == RequestPolicy::Verdict::REJECT_HOST
                      ? "{\"error\":\"Open the grinder by its own name or IP address\"}"
                      : "{\"error\":\"Request origin is not allowed\"}");
    return false;
}

}  // namespace RequestGuard
