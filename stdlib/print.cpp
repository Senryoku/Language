#include <cstdint>
#include <stdio.h>

extern "C" {

void __print(char* ptr, uint64_t size) {
    printf("%.*s", (int) size, ptr);
}

} // extern "C"
