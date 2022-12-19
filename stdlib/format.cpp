#include <stdlib.h>

extern "C" {

// Caller is responsible for freeing the memory.
const char* __itoa(int n) {
    char* buffer = (char*)malloc(sizeof(int) * 8 + 1);
    return itoa(n, buffer, 10);
}

} // extern "C"
