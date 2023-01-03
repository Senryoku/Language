#define __STDC_WANT_LIB_EXT1__ 1
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <io.h>

#include <sys\stat.h> // _S_IREAD | _S_IWRITE

extern "C" {

// FIXME: Change mode to an enum.
int __open_file(const char* path, const char* mode) {
	auto m = _O_RDONLY;
	if(strcmp(mode, "r") == 0) m = _O_RDONLY;
	if(strcmp(mode, "w") == 0) m = _O_CREAT | _O_TRUNC | _O_WRONLY;
    auto fd = _open(path, m, _S_IREAD | _S_IWRITE);
    if(fd < 0) {
        char errmsg[2048];
        strerror_s(errmsg, sizeof(errmsg), errno);
        printf("Error %d opening '%s': %s.\n", errno, path, errmsg);
    }
    return fd;
}

void __close_file(int fd) {
    _close(fd);
}

void __write_file(int fd, char* buff, size_t count) {
    _write(fd, buff, count);
}

int __read_file(int fd, char* buff, size_t buff_size) {
    auto r = _read(fd, buff, buff_size);
    if(r < 1) {
        char errmsg[2048];
        strerror_s(errmsg, sizeof(errmsg), errno);
        printf("Error %d reading %d: %s.\n", errno, fd, errmsg);
    }
    return r;
}

int __read_file_buffer_offset(int fd, char* buff, size_t offset_into_buffer, size_t buff_size) {
    return __read_file(fd, buff + offset_into_buffer, buff_size - offset_into_buffer);
}

}