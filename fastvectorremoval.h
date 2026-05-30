#pragma once
#include<vector>

template <typename T>
inline void quickRemoveByIndex(std::vector<T>& v, size_t index) {
    if (index >= v.size()) return;

    std::swap(v[index], v.back());

    v.pop_back();
}