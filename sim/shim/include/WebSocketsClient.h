#pragma once
#include <Arduino.h>
class WebSocketsClient {
public:
    void loop() {}
    void disconnect() {}
    bool isConnected() { return false; }
};
typedef enum {
    WStype_ERROR, WStype_DISCONNECTED, WStype_CONNECTED, WStype_TEXT, WStype_BIN, WStype_FRAGMENT_TEXT_START,
    WStype_FRAGMENT_BIN_START, WStype_FRAGMENT, WStype_FRAGMENT_FIN, WStype_PING, WStype_PONG
} WStype_t;
