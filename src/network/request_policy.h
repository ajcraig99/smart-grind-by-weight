#pragma once

#include <cctype>
#include <cstddef>
#include <cstring>

// Admission rules for browser-reachable requests to the local web API.
//
// A web page on another site cannot read this API, but it can still send
// requests to it: form posts and WebSocket handshakes are not blocked by the
// browser's same-origin policy, and DNS rebinding can point a foreign host
// name at the grinder. Two checks close those paths:
//   - Host must name this grinder: its hostname (bare, .local or a common
//     home-router suffix), its station IP or, during setup, the setup AP IP.
//   - Origin, when a browser sends one, must be this same host over http.
// Clients that send no Origin (Home Assistant, scripts) are not browsers
// acting for another site and are admitted; the on-device permissions for
// remote starts and firmware updates cover them.
namespace RequestPolicy {

struct DeviceNames {
    const char* hostname = nullptr;    // DHCP/mDNS label, for example "smartgrind"
    const char* station_ip = nullptr;  // Dotted IPv4 address on the home network
    const char* setup_ip = nullptr;    // Setup access point address while it runs
    bool setup_access_point = false;   // Captive setup network is active
};

struct RequestHeaders {
    const char* host = nullptr;        // Host header, possibly with ":80"
    const char* origin = nullptr;      // Origin header, nullptr when absent
    const char* path = nullptr;        // Request path without the query
    bool safe_method = true;           // GET, HEAD or OPTIONS
    bool websocket = false;            // WebSocket upgrade
};

enum class Verdict : unsigned char {
    ALLOW,
    REJECT_HOST,
    REJECT_ORIGIN,
};

namespace detail {

// Router-assigned suffixes that are not delegated in the public DNS, so a
// remote site cannot register "<hostname><suffix>" and rebind it.
constexpr const char* LOCAL_SUFFIXES[] = {
    ".local", ".lan", ".home", ".home.arpa", ".localdomain", ".internal",
};

inline bool equal_ignore_case(const char* left, size_t left_length,
                              const char* right, size_t right_length) {
    if (left_length != right_length) return false;
    for (size_t i = 0; i < left_length; ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

// Returns the length of the host part of an authority ("name[:80]"), without
// a trailing FQDN dot. Returns 0 for an empty or malformed authority, IPv6
// literals and any port other than the default HTTP port.
inline size_t host_length(const char* authority) {
    if (!authority) return 0;
    const size_t length = std::strlen(authority);
    size_t host = length;
    const char* colon = std::strchr(authority, ':');
    if (colon) {
        if (std::strchr(colon + 1, ':')) return 0;
        if (std::strcmp(colon + 1, "80") != 0) return 0;
        host = static_cast<size_t>(colon - authority);
    }
    if (host > 0 && authority[host - 1] == '.') --host;
    if (host == 0 || host > 253) return 0;
    for (size_t i = 0; i < host; ++i) {
        const unsigned char ch = static_cast<unsigned char>(authority[i]);
        if (!std::isalnum(ch) && ch != '.' && ch != '-') return 0;
    }
    return host;
}

inline bool matches_value(const char* host, size_t length, const char* value) {
    return value && *value && equal_ignore_case(host, length, value, std::strlen(value));
}

inline bool starts_with(const char* text, const char* prefix) {
    return text && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

}  // namespace detail

// True when the Host header names this grinder.
inline bool host_allowed(const char* authority, const DeviceNames& names) {
    const size_t length = detail::host_length(authority);
    if (length == 0) return false;
    if (detail::matches_value(authority, length, names.station_ip)) return true;
    if (names.setup_access_point && detail::matches_value(authority, length, names.setup_ip)) {
        return true;
    }
    if (!names.hostname || !*names.hostname) return false;
    const size_t name_length = std::strlen(names.hostname);
    if (length < name_length ||
        !detail::equal_ignore_case(authority, name_length, names.hostname, name_length)) {
        return false;
    }
    if (length == name_length) return true;
    const char* suffix = authority + name_length;
    const size_t suffix_length = length - name_length;
    for (const char* allowed : detail::LOCAL_SUFFIXES) {
        if (detail::equal_ignore_case(suffix, suffix_length, allowed, std::strlen(allowed))) {
            return true;
        }
    }
    return false;
}

// True when a browser Origin is "http://" plus the same host the request used.
inline bool origin_matches(const char* origin, const char* authority) {
    static constexpr char SCHEME[] = "http://";
    constexpr size_t scheme_length = sizeof(SCHEME) - 1;
    if (!origin || std::strlen(origin) <= scheme_length ||
        !detail::equal_ignore_case(origin, scheme_length, SCHEME, scheme_length)) {
        return false;
    }
    const char* origin_authority = origin + scheme_length;
    const size_t origin_host = detail::host_length(origin_authority);
    const size_t request_host = detail::host_length(authority);
    return origin_host != 0 &&
           detail::equal_ignore_case(origin_authority, origin_host, authority, request_host);
}

inline Verdict evaluate(const RequestHeaders& request, const DeviceNames& names) {
    // Captive-portal probes use arbitrary host names; plain page reads are
    // answered (with a redirect to the setup address) so the phone opens the
    // setup page. The API and anything that changes state still have to
    // address the setup AP itself: a client on both networks could otherwise
    // be steered there by a page on another site.
    const bool api = !request.path || detail::starts_with(request.path, "/api/");
    const bool captive_read =
        names.setup_access_point && request.safe_method && !request.websocket && !api;
    if (!captive_read && !host_allowed(request.host, names)) return Verdict::REJECT_HOST;
    if (request.origin) {
        // Some captive-portal browsers send "null" for the setup page they
        // opened; accept it for the setup API only.
        const bool setup_null_origin =
            names.setup_access_point && detail::starts_with(request.path, "/api/v1/setup/") &&
            std::strcmp(request.origin, "null") == 0;
        if (!setup_null_origin && !origin_matches(request.origin, request.host)) {
            return Verdict::REJECT_ORIGIN;
        }
    }
    return Verdict::ALLOW;
}

}  // namespace RequestPolicy
