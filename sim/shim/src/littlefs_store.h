#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sim {

struct FsStore {
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> dirs;
    bool mounted = false;
    bool is_dir(const std::string& path) const;
    std::vector<std::string> children(const std::string& dir) const;
    std::vector<uint8_t> serialize() const;
    bool deserialize(const uint8_t* data, size_t len);
};

FsStore& fs_store();

}  // namespace sim
