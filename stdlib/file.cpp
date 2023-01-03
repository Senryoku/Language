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
    auto       oflag = _O_RDONLY;
    auto       shflag = _SH_DENYRW;
    const auto pmode = _S_IREAD | _S_IWRITE;
    if(strcmp(mode, "r") == 0) {
        oflag = _O_RDONLY;
        shflag = _SH_DENYWR;
    }
    if(strcmp(mode, "w") == 0) {
        oflag = _O_CREAT | _O_TRUNC | _O_WRONLY;
        shflag = _SH_DENYRD;
    }
    if(strcmp(mode, "rw") == 0) {
        oflag = _O_CREAT | _O_TRUNC | _O_RDWR;
        shflag = _SH_DENYNO;
    }
    int  fd;
    auto _errno = _sopen_s(&fd, path, oflag, shflag, pmode);
    if(fd < 0) {
        char errmsg[2048];
        strerror_s(errmsg, sizeof(errmsg), _errno);
        printf("Error %d opening '%s': %s.\n", _errno, path, errmsg);
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