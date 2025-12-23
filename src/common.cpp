using namespace ::std;

#include "common.hpp"

#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <cstring>

// Wrapper for read system call
ssize_t safe_read(int fd, void* buf, size_t count) {
    return read(fd, buf, count);
}

// Wrapper for write system call
ssize_t safe_write(int fd, const void* buf, size_t count) {
    return write(fd, buf, count);
}

// Print error message and exit program with failure status
[[noreturn]] void die(const string& msg) {
    cerr << msg;
    // Append errno description if available
    if (errno != 0) {
        cerr << ": " << strerror(errno);
    }
    cerr << endl;
    exit(EXIT_FAILURE);
}