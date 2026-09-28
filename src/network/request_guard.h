#pragma once

#include <ESPAsyncWebServer.h>

// Applies RequestPolicy to the local web server.
namespace RequestGuard {

// Registers the early rejector and the admission middleware. Call it before
// any route is added so the rejector is the first handler consulted.
void install(AsyncWebServer& server);

// Admission check for one request. Sends a 403 and returns false on rejection.
// Upload callbacks run before server middleware, so upload routes must call
// this themselves when the first chunk arrives.
bool admit(AsyncWebServerRequest* request);

}  // namespace RequestGuard
