#pragma once

std::string longest_common_prefix(auto arr) {
    if(arr.size() == 0)
        return "";
    std::string r = arr[0];
    for(auto i = 1; i < arr.size(); ++i) {
        auto j = 0;
        while(j < r.size() && j < arr[i].size() && r[j] == arr[i][j])
            ++j;
        if(j == 0)
            return "";
        r.resize(j);
    }
    return r;
}