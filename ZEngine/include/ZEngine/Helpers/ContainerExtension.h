#pragma once
#include <vector>

namespace ZEngine::Helpers {
    template <typename T>
    std::vector<T> FindItems(const std::vector<T>& v, std::function<bool(const T&)> func) {
        std::vector<T> indices;
        auto           it = v.begin();
        while ((it = std::find_if(it, v.end(), func)) != v.end()) {
            indices.push_back(*it);
            it++;
        }
        return indices;
    }
} // namespace ZEngine::Helpers
