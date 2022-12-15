#include <cstdlib>

extern "C" {
	void* __malloc(size_t size) {
		return malloc(size);
	}

	void __free(void* ptr) {
        free(ptr);
	}
}
