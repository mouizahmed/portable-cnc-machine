#pragma once

#include <cstddef>

namespace web_assets {

struct Asset {
    const char* path;
    const char* content_type;
    const char* data;
    std::size_t size;
};

const Asset* find(const char* path);

}  // namespace web_assets
