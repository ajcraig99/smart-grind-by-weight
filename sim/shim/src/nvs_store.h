#pragma once

#include <Preferences.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sim {

struct NvsEntry {
    PreferenceType type = PT_INVALID;
    std::vector<uint8_t> data;
};

struct NvsStore {
    std::map<std::string, std::map<std::string, NvsEntry>> namespaces;
    void clear();
    std::vector<uint8_t> serialize() const;
    bool deserialize(const uint8_t* data, size_t len);
};

NvsStore& nvs_store();
bool nvs_key_valid(const char* key);

}  // namespace sim
