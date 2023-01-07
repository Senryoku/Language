#pragma once

#include <string>

std::string longest_common_prefix(const auto& arr) {
    if(arr.size() == 0)
        return "";
    const std::string& r = arr[0];
    auto               prefix_size = r.size();
    for(auto i = 1; i < arr.size(); ++i) {
        auto j = 0;
        while(j < std::min(prefix_size, arr[i].size()) && r[j] == arr[i][j])
            ++j;
        if(j == 0)
            return "";
        prefix_size = j;
    }
    return r.substr(0, prefix_size);
}