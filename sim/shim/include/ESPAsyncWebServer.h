// Declarations-only ESPAsyncWebServer surface; the twin runs no network stack.
#pragma once
#include <Arduino.h>
#include <functional>

class AsyncWebServerRequest;
class AsyncWebSocketClient;

typedef enum { WS_EVT_CONNECT, WS_EVT_DISCONNECT, WS_EVT_PONG, WS_EVT_ERROR, WS_EVT_DATA } AwsEventType;

class AsyncWebServer {
public:
    explicit AsyncWebServer(uint16_t port) : port_(port) {}
    void begin() {}
    void end() {}
private:
    uint16_t port_;
};

class AsyncWebSocket {
public:
    explicit AsyncWebSocket(const char* url) : url_(url) {}
    size_t count() const { return 0; }
private:
    const char* url_;
};
