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

bool is_safe_method(WebRequestMethodComposite method) {
    return method == HTTP_GET || method == HTTP_HEAD || method == HTTP_OPTIONS;
}

RequestPolicy::Verdict verdict_for(AsyncWebServerRequest* request) {
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
    headers.path = request->url().c_str();
    headers.safe_method = is_safe_method(request->method());
    headers.websocket = request->isWebSocketUpgrade();
    return RequestPolicy::evaluate(headers, names);
}

// The upload routes take a multipart form; any other body there would be
// parsed into form fields in memory.
bool upload_body_is_not_multipart(const AsyncWebServerRequest* request) {
    if (request->method() != HTTP_POST || request->contentLength() == 0) return false;
    const String& url = request->url();
    return (url == "/api/v1/ota" || url == "/api/v1/screensaver/image") && !request->multipart();
}

// Claims every request that will be refused, as soon as its headers end and
// before its body arrives: a body too large for its route, a request that
// fails admission, or an upload that is not a multipart form. A trivial
// handler makes the server count and discard the body instead of buffering
// form fields or upload chunks.
class EarlyRejector : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        return request && (request->contentLength() > body_limit(request) ||
                           verdict_for(request) != RequestPolicy::Verdict::ALLOW ||
                           upload_body_is_not_multipart(request));
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        if (request->contentLength() > body_limit(request)) {
            request->send(413, "application/json", "{\"error\":\"Request body is too large\"}");
        } else if (RequestGuard::admit(request)) {
            request->send(415, "application/json", "{\"error\":\"Upload the file as a multipart form\"}");
        }
    }

    bool isRequestHandlerTrivial() const override { return true; }
};

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
    server.addHandler(new EarlyRejector());
    server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
        // An upload callback may already have answered; keep that response.
        if (request->getResponse() || admit(request)) next();
    });
}

bool admit(AsyncWebServerRequest* request) {
    if (!request) return false;

    const RequestPolicy::Verdict verdict = verdict_for(request);
    if (verdict == RequestPolicy::Verdict::ALLOW) return true;

    log_rejection(request, verdict);
    request->send(403, "application/json",
                  verdict == RequestPolicy::Verdict::REJECT_HOST
                      ? "{\"error\":\"Open the grinder by its own name or IP address\"}"
                      : "{\"error\":\"Request origin is not allowed\"}");
    return false;
}

}  // namespace RequestGuard
