#include <stdlib.h>
#include <stdio.h>

extern "C" {

// Caller is responsible for freeing the memory.
const char* __itoa(int n) {
    char* buffer = (char*)malloc(sizeof(int) * 8 + 1);
    auto error = _itoa_s(n, buffer, sizeof(int) * 8 + 1, 10);
    if(error != 0) {
        printf("[std/format] Error: invalid parameter '%d' passed to _itoa_s.\n", n);
        free(buffer);
        return nullptr;
    }
    return buffer;
}

} // extern "C"
