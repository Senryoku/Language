#include <cstdlib>

// DEBUG
#include <cstdint>
#include <cstdio>

extern "C" {
void* __malloc(size_t size) {
    auto ptr = malloc(size);
    printf("\t<std/memory> malloc(%llu) -> 0x%016x\n", size, ptr);
    return ptr;
}

void __free(void* ptr) {
    printf("\t<std/memory> free(0x%016x)\n", ptr);
    printf("\t<std/memory>   0: %llu\n", *static_cast<uint64_t*>(ptr));
    printf("\t<std/memory>   1: %llu\n", *(static_cast<uint64_t*>(ptr) + 1));
    free(ptr);
    printf("\t<std/memory> -> free(0x%016x) done\n", ptr);
}

void* null_pointer() {
    return nullptr;
}
}
