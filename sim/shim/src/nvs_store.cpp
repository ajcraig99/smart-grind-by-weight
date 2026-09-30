// In-memory NVS: typed entries per namespace, shared by Preferences and the nvs_* C API.
// Keys and namespaces longer than 15 characters are rejected as on the ESP32.
#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "nvs_store.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace sim {

namespace {
NvsStore g_store;
constexpr size_t kMaxKeyLength = 15;
}

NvsStore& nvs_store() { return g_store; }

bool nvs_key_valid(const char* key) { return key && key[0] && std::strlen(key) <= kMaxKeyLength; }

void NvsStore::clear() { namespaces.clear(); }

std::vector<uint8_t> NvsStore::serialize() const {
    std::vector<uint8_t> out;
    auto put_u32 = [&out](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto put_str = [&](const std::string& s) { put_u32(static_cast<uint32_t>(s.size())); out.insert(out.end(), s.begin(), s.end()); };
    put_u32(static_cast<uint32_t>(namespaces.size()));
    for (const auto& ns : namespaces) {
        put_str(ns.first);
        put_u32(static_cast<uint32_t>(ns.second.size()));
        for (const auto& kv : ns.second) {
            put_str(kv.first);
            out.push_back(static_cast<uint8_t>(kv.second.type));
            put_u32(static_cast<uint32_t>(kv.second.data.size()));
            out.insert(out.end(), kv.second.data.begin(), kv.second.data.end());
        }
    }
    return out;
}

bool NvsStore::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0;
    auto get_u32 = [&](uint32_t& v) {
        if (pos + 4 > len) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data[pos++]) << (8 * i);
        return true;
    };
    auto get_str = [&](std::string& s) {
        uint32_t n;
        if (!get_u32(n) || pos + n > len) return false;
        s.assign(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return true;
    };
    namespaces.clear();
    uint32_t ns_count;
    if (!get_u32(ns_count)) return false;
    for (uint32_t i = 0; i < ns_count; ++i) {
        std::string ns;
        uint32_t key_count;
        if (!get_str(ns) || !get_u32(key_count)) return false;
        auto& entries = namespaces[ns];
        for (uint32_t k = 0; k < key_count; ++k) {
            std::string key;
            if (!get_str(key) || pos + 1 > len) return false;
            NvsEntry entry;
            entry.type = static_cast<PreferenceType>(data[pos++]);
            uint32_t n;
            if (!get_u32(n) || pos + n > len) return false;
            entry.data.assign(data + pos, data + pos + n);
            pos += n;
            entries[key] = entry;
        }
    }
    return true;
}

}  // namespace sim

// ---------------------------------------------------------------- Preferences

Preferences::~Preferences() { end(); }

bool Preferences::begin(const char* name, bool readOnly, const char*) {
    if (started_) return false;
    if (!sim::nvs_key_valid(name)) return false;
    auto& namespaces = sim::nvs_store().namespaces;
    if (readOnly && namespaces.find(name) == namespaces.end()) return false;  // NVS: missing namespace
    if (!readOnly) namespaces[name];
    namespace_ = name;
    read_only_ = readOnly;
    started_ = true;
    return true;
}

void Preferences::end() {
    started_ = false;
    namespace_.clear();
}

bool Preferences::clear() {
    if (!started_ || read_only_) return false;
    sim::nvs_store().namespaces[namespace_].clear();
    return true;
}

bool Preferences::remove(const char* key) {
    if (!started_ || read_only_ || !key) return false;
    return sim::nvs_store().namespaces[namespace_].erase(key) > 0;
}

size_t Preferences::put_raw(const char* key, PreferenceType type, const void* data, size_t len) {
    if (!started_ || read_only_ || !sim::nvs_key_valid(key)) return 0;
    sim::NvsEntry entry;
    entry.type = type;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    entry.data.assign(bytes, bytes + len);
    sim::nvs_store().namespaces[namespace_][key] = entry;
    return len;
}

bool Preferences::get_raw(const char* key, PreferenceType type, void* out, size_t len) {
    if (!started_ || !key) return false;
    auto& ns = sim::nvs_store().namespaces[namespace_];
    auto it = ns.find(key);
    if (it == ns.end() || it->second.type != type || it->second.data.size() != len) return false;
    std::memcpy(out, it->second.data.data(), len);
    return true;
}

size_t Preferences::putChar(const char* key, int8_t v) { return put_raw(key, PT_I8, &v, 1); }
size_t Preferences::putUChar(const char* key, uint8_t v) { return put_raw(key, PT_U8, &v, 1); }
size_t Preferences::putShort(const char* key, int16_t v) { return put_raw(key, PT_I16, &v, 2); }
size_t Preferences::putUShort(const char* key, uint16_t v) { return put_raw(key, PT_U16, &v, 2); }
size_t Preferences::putInt(const char* key, int32_t v) { return put_raw(key, PT_I32, &v, 4); }
size_t Preferences::putUInt(const char* key, uint32_t v) { return put_raw(key, PT_U32, &v, 4); }
size_t Preferences::putLong(const char* key, int32_t v) { return putInt(key, v); }
size_t Preferences::putULong(const char* key, uint32_t v) { return putUInt(key, v); }
size_t Preferences::putLong64(const char* key, int64_t v) { return put_raw(key, PT_I64, &v, 8); }
size_t Preferences::putULong64(const char* key, uint64_t v) { return put_raw(key, PT_U64, &v, 8); }
// Arduino-ESP32 stores float/double as blobs.
size_t Preferences::putFloat(const char* key, float v) { return put_raw(key, PT_BLOB, &v, sizeof(v)); }
size_t Preferences::putDouble(const char* key, double v) { return put_raw(key, PT_BLOB, &v, sizeof(v)); }
size_t Preferences::putBool(const char* key, bool v) { uint8_t b = v ? 1 : 0; return put_raw(key, PT_U8, &b, 1); }
size_t Preferences::putString(const char* key, const char* v) {
    if (!v) return 0;
    const size_t n = std::strlen(v);
    std::vector<uint8_t> data(v, v + n + 1);
    return put_raw(key, PT_STR, data.data(), data.size()) ? n : 0;
}
size_t Preferences::putString(const char* key, const String& v) { return putString(key, v.c_str()); }
size_t Preferences::putBytes(const char* key, const void* v, size_t len) {
    if (!v || !len) return 0;
    return put_raw(key, PT_BLOB, v, len);
}

bool Preferences::isKey(const char* key) {
    if (!started_ || !key) return false;
    auto& ns = sim::nvs_store().namespaces[namespace_];
    return ns.find(key) != ns.end();
}

PreferenceType Preferences::getType(const char* key) {
    if (!started_ || !key) return PT_INVALID;
    auto& ns = sim::nvs_store().namespaces[namespace_];
    auto it = ns.find(key);
    return it == ns.end() ? PT_INVALID : it->second.type;
}

#define SIM_GET(T, PT, def) { T v; return get_raw(key, PT, &v, sizeof(T)) ? v : def; }
int8_t Preferences::getChar(const char* key, int8_t d) SIM_GET(int8_t, PT_I8, d)
uint8_t Preferences::getUChar(const char* key, uint8_t d) SIM_GET(uint8_t, PT_U8, d)
int16_t Preferences::getShort(const char* key, int16_t d) SIM_GET(int16_t, PT_I16, d)
uint16_t Preferences::getUShort(const char* key, uint16_t d) SIM_GET(uint16_t, PT_U16, d)
int32_t Preferences::getInt(const char* key, int32_t d) SIM_GET(int32_t, PT_I32, d)
uint32_t Preferences::getUInt(const char* key, uint32_t d) SIM_GET(uint32_t, PT_U32, d)
int32_t Preferences::getLong(const char* key, int32_t d) { return getInt(key, d); }
uint32_t Preferences::getULong(const char* key, uint32_t d) { return getUInt(key, d); }
int64_t Preferences::getLong64(const char* key, int64_t d) SIM_GET(int64_t, PT_I64, d)
uint64_t Preferences::getULong64(const char* key, uint64_t d) SIM_GET(uint64_t, PT_U64, d)
float Preferences::getFloat(const char* key, float d) SIM_GET(float, PT_BLOB, d)
double Preferences::getDouble(const char* key, double d) SIM_GET(double, PT_BLOB, d)
bool Preferences::getBool(const char* key, bool d) { return getUChar(key, d ? 1 : 0) != 0; }
#undef SIM_GET

size_t Preferences::getString(const char* key, char* value, size_t maxLen) {
    if (!started_ || !key || !value || maxLen == 0) return 0;
    auto& ns = sim::nvs_store().namespaces[namespace_];
    auto it = ns.find(key);
    if (it == ns.end() || it->second.type != PT_STR || it->second.data.size() > maxLen) return 0;
    std::memcpy(value, it->second.data.data(), it->second.data.size());
    return it->second.data.size();
}

String Preferences::getString(const char* key, String defaultValue) {
    if (!started_ || !key) return defaultValue;
    auto& ns = sim::nvs_store().namespaces[namespace_];
    auto it = ns.find(key);
    if (it == ns.end() || it->second.type != PT_STR || it->second.data.empty()) return defaultValue;
    return String(reinterpret_cast<const char*>(it->second.data.data()));
}

size_t Preferences::getBytesLength(const char* key) {
    if (!started_ || !key) return 0;
    auto& ns = sim::nvs_store().namespaces[namespace_];
    auto it = ns.find(key);
    return it == ns.end() || it->second.type != PT_BLOB ? 0 : it->second.data.size();
}

size_t Preferences::getBytes(const char* key, void* buf, size_t maxLen) {
    const size_t len = getBytesLength(key);
    if (!len || !buf || len > maxLen) return 0;
    std::memcpy(buf, sim::nvs_store().namespaces[namespace_][key].data.data(), len);
    return len;
}

size_t Preferences::freeEntries() { return 400; }

// ---------------------------------------------------------------- nvs C API (read-only use)

struct nvs_opaque_iterator_t {
    std::vector<nvs_entry_info_t> entries;
    size_t index = 0;
};

namespace {
nvs_type_t to_nvs_type(PreferenceType t) {
    switch (t) {
        case PT_I8: return NVS_TYPE_I8;
        case PT_U8: return NVS_TYPE_U8;
        case PT_I16: return NVS_TYPE_I16;
        case PT_U16: return NVS_TYPE_U16;
        case PT_I32: return NVS_TYPE_I32;
        case PT_U32: return NVS_TYPE_U32;
        case PT_I64: return NVS_TYPE_I64;
        case PT_U64: return NVS_TYPE_U64;
        case PT_STR: return NVS_TYPE_STR;
        default: return NVS_TYPE_BLOB;
    }
}
std::map<nvs_handle_t, std::string> g_open_handles;
nvs_handle_t g_next_handle = 1;

sim::NvsEntry* find_entry(nvs_handle_t handle, const char* key) {
    auto h = g_open_handles.find(handle);
    if (h == g_open_handles.end() || !key) return nullptr;
    auto& ns = sim::nvs_store().namespaces[h->second];
    auto it = ns.find(key);
    return it == ns.end() ? nullptr : &it->second;
}
}  // namespace

template <typename T>
static esp_err_t get_scalar(nvs_handle_t handle, const char* key, T* out) {
    sim::NvsEntry* e = find_entry(handle, key);
    if (!e || e->data.size() != sizeof(T) || !out) return ESP_ERR_NVS_NOT_FOUND;
    std::memcpy(out, e->data.data(), sizeof(T));
    return ESP_OK;
}

extern "C" {

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_deinit(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) {
    sim::nvs_store().clear();
    return ESP_OK;
}

esp_err_t nvs_entry_find(const char*, const char* namespace_name, nvs_type_t type, nvs_iterator_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    auto* it = new nvs_opaque_iterator_t();
    for (const auto& ns : sim::nvs_store().namespaces) {
        if (namespace_name && ns.first != namespace_name) continue;
        for (const auto& kv : ns.second) {
            const nvs_type_t t = to_nvs_type(kv.second.type);
            if (type != NVS_TYPE_ANY && t != type) continue;
            nvs_entry_info_t info{};
            std::strncpy(info.namespace_name, ns.first.c_str(), sizeof(info.namespace_name) - 1);
            std::strncpy(info.key, kv.first.c_str(), sizeof(info.key) - 1);
            info.type = t;
            it->entries.push_back(info);
        }
    }
    if (it->entries.empty()) {
        delete it;
        *out = nullptr;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = it;
    return ESP_OK;
}

esp_err_t nvs_entry_next(nvs_iterator_t* iterator) {
    if (!iterator || !*iterator) return ESP_ERR_INVALID_ARG;
    if (++(*iterator)->index >= (*iterator)->entries.size()) {
        delete *iterator;
        *iterator = nullptr;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t nvs_entry_info(const nvs_iterator_t iterator, nvs_entry_info_t* out_info) {
    if (!iterator || !out_info || iterator->index >= iterator->entries.size()) return ESP_ERR_INVALID_ARG;
    *out_info = iterator->entries[iterator->index];
    return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t iterator) { delete iterator; }

esp_err_t nvs_open(const char* namespace_name, nvs_open_mode_t, nvs_handle_t* out) {
    if (!namespace_name || !out) return ESP_ERR_INVALID_ARG;
    const nvs_handle_t h = g_next_handle++;
    g_open_handles[h] = namespace_name;
    *out = h;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) { g_open_handles.erase(handle); }

esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length) {
    sim::NvsEntry* e = find_entry(handle, key);
    if (!e || e->type != PT_STR) return ESP_ERR_NVS_NOT_FOUND;
    if (!length) return ESP_ERR_INVALID_ARG;
    if (!out_value) { *length = e->data.size(); return ESP_OK; }
    if (*length < e->data.size()) return ESP_ERR_INVALID_SIZE;
    std::memcpy(out_value, e->data.data(), e->data.size());
    *length = e->data.size();
    return ESP_OK;
}

esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out) { return get_scalar(handle, key, out); }
esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out) { return get_scalar(handle, key, out); }
esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out) { return get_scalar(handle, key, out); }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* length) {
    sim::NvsEntry* e = find_entry(handle, key);
    if (!e || !length) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *length = e->data.size(); return ESP_OK; }
    if (*length < e->data.size()) return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, e->data.data(), e->data.size());
    *length = e->data.size();
    return ESP_OK;
}

}  // extern "C"
