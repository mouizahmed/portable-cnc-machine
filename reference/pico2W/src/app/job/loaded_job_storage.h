#pragma once

#include <cstddef>

class LoadedJobStorage {
public:
    bool load(char* name, std::size_t size) const;
    bool save(const char* name) const;
    bool clear() const;
};
