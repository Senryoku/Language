#include <cstdlib>

// #define DEBUG_TRACE

#ifdef DEBUG_TRACE
#include <cstdio>
#endif

extern "C" {
void* __malloc(size_t size) {
    auto ptr = malloc(size);
#ifdef DEBUG_TRACE
    printf("\t<std/memory> malloc(%llu) -> 0x%016x\n", size, ptr);
#endif
    return ptr;
}

void __free(void* ptr) {
#ifdef DEBUG_TRACE
    printf("\t<std/memory> free(0x%016x)\n", ptr);
#endif
    free(ptr);
}

void* null_pointer() {
    return nullptr;
}
}
