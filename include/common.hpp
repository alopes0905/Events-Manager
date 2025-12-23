#pragma once

#include <string>
#include <cstddef>
#include <unistd.h>

// Simple read/write
ssize_t safe_read(int fd, void* buf, size_t count);
ssize_t safe_write(int fd, const void* buf, size_t count);

// Fatal error helper.
[[noreturn]] void die(const std::string& msg);