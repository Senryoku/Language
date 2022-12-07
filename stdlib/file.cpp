#define __STDC_WANT_LIB_EXT1__ 1 
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <io.h>

extern "C" {
	int __open_file(const char* path, const char* mode) {
        auto fd = _open(path, _O_RDONLY);
		if (fd < 0) {
            char errmsg[2048]; 
            strerror_s(errmsg, sizeof(errmsg), errno);
            printf("Error %d opening '%s': %s.\n", errno, path, errmsg);
		}
        return fd;
	}

	void __close_file(int fd) {
        _close(fd);
	}
}