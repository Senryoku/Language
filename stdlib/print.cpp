#include <cstdint>
#include <stdio.h>

extern "C" {

void __print(char* ptr, uint64_t size) {
    printf("%.*s", size, ptr);
}

} // extern "C"
