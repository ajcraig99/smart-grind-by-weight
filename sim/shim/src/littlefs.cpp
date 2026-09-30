// In-memory LittleFS for the twin. Files and directories live in a map keyed by absolute
// path; directories are implicit (any prefix) or explicit (mkdir). Serialisable for resets.
#include <LittleFS.h>

#include "littlefs_store.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

fs::LittleFSFS LittleFS;

namespace sim {

namespace {
FsStore g_fs;

std::string normalise(const char* path) {
    std::string p = path ? path : "/";
    if (p.empty() || p[0] != '/') p = "/" + p;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string base_name(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
}  // namespace

FsStore& fs_store() { return g_fs; }

bool FsStore::is_dir(const std::string& p) const {
    if (p == "/") return true;
    if (dirs.count(p)) return true;
    const std::string prefix = p + "/";
    auto it = files.lower_bound(prefix);
    return it != files.end() && it->first.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> FsStore::children(const std::string& dir) const {
    std::set<std::string> out;
    const std::string prefix = dir == "/" ? "/" : dir + "/";
    auto collect = [&](const std::string& path) {
        if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) return;
        const size_t next = path.find('/', prefix.size());
        out.insert(next == std::string::npos ? path : path.substr(0, next));
    };
    for (const auto& kv : files) collect(kv.first);
    for (const auto& d : dirs) collect(d);
    return std::vector<std::string>(out.begin(), out.end());
}

std::vector<uint8_t> FsStore::serialize() const {
    std::vector<uint8_t> out;
    auto put_u32 = [&out](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    put_u32(static_cast<uint32_t>(dirs.size()));
    for (const auto& d : dirs) { put_u32(static_cast<uint32_t>(d.size())); out.insert(out.end(), d.begin(), d.end()); }
    put_u32(static_cast<uint32_t>(files.size()));
    for (const auto& kv : files) {
        put_u32(static_cast<uint32_t>(kv.first.size()));
        out.insert(out.end(), kv.first.begin(), kv.first.end());
        put_u32(static_cast<uint32_t>(kv.second.size()));
        out.insert(out.end(), kv.second.begin(), kv.second.end());
    }
    return out;
}

bool FsStore::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0;
    auto get_u32 = [&](uint32_t& v) {
        if (pos + 4 > len) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data[pos++]) << (8 * i);
        return true;
    };
    auto get_blob = [&](std::vector<uint8_t>& b) {
        uint32_t n;
        if (!get_u32(n) || pos + n > len) return false;
        b.assign(data + pos, data + pos + n);
        pos += n;
        return true;
    };
    files.clear();
    dirs.clear();
    uint32_t n;
    if (!get_u32(n)) return false;
    for (uint32_t i = 0; i < n; ++i) {
        std::vector<uint8_t> b;
        if (!get_blob(b)) return false;
        dirs.insert(std::string(b.begin(), b.end()));
    }
    if (!get_u32(n)) return false;
    for (uint32_t i = 0; i < n; ++i) {
        std::vector<uint8_t> name, content;
        if (!get_blob(name) || !get_blob(content)) return false;
        files[std::string(name.begin(), name.end())] = content;
    }
    return true;
}

}  // namespace sim

namespace fs {

class FileImpl {
public:
    std::string path;
    std::string name_only;
    bool directory = false;
    bool writable = false;
    bool open = true;
    size_t pos = 0;
    std::vector<std::string> listing;
    size_t list_index = 0;

    std::vector<uint8_t>* data() {
        auto& files = sim::fs_store().files;
        auto it = files.find(path);
        return it == files.end() ? nullptr : &it->second;
    }
};

size_t File::write(uint8_t c) { return write(&c, 1); }

size_t File::write(const uint8_t* buf, size_t size) {
    if (!p_ || !p_->open || !p_->writable || p_->directory) return 0;
    auto* d = p_->data();
    if (!d) return 0;
    if (p_->pos + size > d->size()) d->resize(p_->pos + size);
    std::memcpy(d->data() + p_->pos, buf, size);
    p_->pos += size;
    return size;
}

int File::available() {
    if (!p_ || !p_->open || p_->directory) return 0;
    auto* d = p_->data();
    return d && p_->pos < d->size() ? static_cast<int>(d->size() - p_->pos) : 0;
}

int File::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

int File::peek() {
    if (!p_ || !p_->open) return -1;
    auto* d = p_->data();
    return d && p_->pos < d->size() ? (*d)[p_->pos] : -1;
}

void File::flush() {}

size_t File::read(uint8_t* buf, size_t size) {
    if (!p_ || !p_->open || p_->directory) return 0;
    auto* d = p_->data();
    if (!d || p_->pos >= d->size()) return 0;
    const size_t n = std::min(size, d->size() - p_->pos);
    std::memcpy(buf, d->data() + p_->pos, n);
    p_->pos += n;
    return n;
}

bool File::seek(uint32_t pos, SeekMode mode) {
    if (!p_ || !p_->open || p_->directory) return false;
    auto* d = p_->data();
    if (!d) return false;
    size_t target = pos;
    if (mode == SeekCur) target = p_->pos + pos;
    else if (mode == SeekEnd) target = d->size() + pos;
    if (target > d->size()) return false;
    p_->pos = target;
    return true;
}

size_t File::position() const { return p_ ? p_->pos : 0; }

size_t File::size() const {
    if (!p_ || p_->directory) return 0;
    auto* d = p_->data();
    return d ? d->size() : 0;
}

void File::close() {
    if (p_) p_->open = false;
    p_.reset();
}

File::operator bool() const { return p_ && p_->open; }
time_t File::getLastWrite() { return 0; }
const char* File::path() const { return p_ ? p_->path.c_str() : nullptr; }
const char* File::name() const { return p_ ? p_->name_only.c_str() : nullptr; }
bool File::isDirectory() const { return p_ && p_->directory; }

File File::openNextFile(const char* mode) {
    if (!p_ || !p_->directory) return File();
    while (p_->list_index < p_->listing.size()) {
        const std::string child = p_->listing[p_->list_index++];
        File f = LittleFS.open(child.c_str(), mode);
        if (f) return f;
    }
    return File();
}

String File::getNextFileName() {
    if (!p_ || !p_->directory || p_->list_index >= p_->listing.size()) return String();
    return String(p_->listing[p_->list_index++].c_str());
}

void File::rewindDirectory() {
    if (p_ && p_->directory) {
        p_->listing = sim::fs_store().children(p_->path);
        p_->list_index = 0;
    }
}

File FS::open(const char* path, const char* mode, const bool) {
    auto& store = sim::fs_store();
    if (!store.mounted) return File();
    const std::string p = sim::normalise(path);
    const std::string m = mode ? mode : "r";
    auto impl = std::make_shared<FileImpl>();
    impl->path = p;
    impl->name_only = sim::base_name(p);
    if (m[0] == 'r' && store.is_dir(p) && store.files.find(p) == store.files.end()) {
        impl->directory = true;
        impl->listing = store.children(p);
        return File(impl);
    }
    auto it = store.files.find(p);
    if (m[0] == 'r') {
        if (it == store.files.end()) return File();
        impl->writable = m.find('+') != std::string::npos;
        return File(impl);
    }
    if (m[0] == 'w') {
        store.files[p].clear();
        impl->writable = true;
        return File(impl);
    }
    if (m[0] == 'a') {
        auto& data = store.files[p];
        impl->writable = true;
        impl->pos = data.size();
        return File(impl);
    }
    return File();
}

bool FS::exists(const char* path) {
    auto& store = sim::fs_store();
    const std::string p = sim::normalise(path);
    return store.mounted && (store.files.count(p) || store.is_dir(p));
}

bool FS::remove(const char* path) {
    auto& store = sim::fs_store();
    return store.mounted && store.files.erase(sim::normalise(path)) > 0;
}

bool FS::rename(const char* from, const char* to) {
    auto& store = sim::fs_store();
    const std::string a = sim::normalise(from), b = sim::normalise(to);
    auto it = store.files.find(a);
    if (!store.mounted || it == store.files.end()) return false;
    store.files[b] = it->second;
    store.files.erase(a);
    return true;
}

bool FS::mkdir(const char* path) {
    auto& store = sim::fs_store();
    if (!store.mounted) return false;
    store.dirs.insert(sim::normalise(path));
    return true;
}

bool FS::rmdir(const char* path) {
    auto& store = sim::fs_store();
    return store.mounted && store.dirs.erase(sim::normalise(path)) > 0;
}

bool LittleFSFS::begin(bool, const char*, uint8_t, const char*) {
    sim::fs_store().mounted = true;
    return true;
}
bool LittleFSFS::format() {
    sim::fs_store().files.clear();
    sim::fs_store().dirs.clear();
    return true;
}
size_t LittleFSFS::totalBytes() { return 1536 * 1024; }
size_t LittleFSFS::usedBytes() {
    size_t used = 0;
    for (const auto& kv : sim::fs_store().files) used += ((kv.second.size() + 4095) / 4096) * 4096;
    return used;
}
void LittleFSFS::end() { sim::fs_store().mounted = false; }

}  // namespace fs
