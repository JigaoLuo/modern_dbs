#include "moderndbs/file.h"
#include <fcntl.h>
#include <stdlib.h>  // NOLINT
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <memory>
#include <system_error>


namespace moderndbs {

namespace {

[[noreturn]] static void throw_errno() {
    throw std::system_error{errno, std::system_category()};
}

}  // namespace


class PosixFile
: public File {
private:
    Mode mode;
    int fd;
    size_t cached_size;

    size_t read_size() {
        struct ::stat file_stat;
        if (::fstat(fd, &file_stat) < 0) {
            throw_errno();
        }
        return file_stat.st_size;
    }

public:
    PosixFile(Mode mode, int fd, size_t size) : mode(mode), fd(fd), cached_size(size) {}

    PosixFile(const char* filename, Mode mode) : mode(mode) {
        switch (mode) {
            case READ:
                fd = ::open(filename, O_RDONLY | O_SYNC);
                break;
            case WRITE:
                fd = ::open(filename, O_RDWR | O_CREAT | O_SYNC, 0666);
        }
        if (fd < 0) {
            throw_errno();
        }
        cached_size = read_size();
    }

    ~PosixFile() override {
        // Don't check return value here, as we don't want a throwing
        // destructor. Also, even when close() fails, the fd will always be
        // freed (see man 2 close).
        ::close(fd);
    }

    Mode get_mode() const override {
        return mode;
    }

    size_t size() const override {
        return cached_size;
    }

    void resize(size_t new_size) override {
        if (new_size == cached_size) {
            return;
        }
        if (::ftruncate(fd, new_size) < 0) {
            throw_errno();
        }
        cached_size = new_size;
    }

    void read_block(size_t offset, size_t size, char* block) override {
        size_t total_bytes_read = 0;
        while (total_bytes_read < size) {
            ssize_t bytes_read = ::pread(
                fd,
                block + total_bytes_read,
                size - total_bytes_read,
                offset + total_bytes_read
            );
            if (bytes_read == 0) {
                // end of file, i.e. size was probably larger than the file
                // size
                return;
            }
            if (bytes_read < 0) {
                throw_errno();
            }
            total_bytes_read += static_cast<size_t>(bytes_read);
        }
    }

    void write_block(const char* block, size_t offset, size_t size) override {
        size_t total_bytes_written = 0;
        while (total_bytes_written < size) {
            ssize_t bytes_written = ::pwrite(
                fd,
                block + total_bytes_written,
                size - total_bytes_written,
                offset + total_bytes_written
            );
            if (bytes_written == 0) {
                // This should probably never happen. Return here to prevent
                // an infinite loop.
                return;
            }
            if (bytes_written < 0) {
                throw_errno();
            }
            total_bytes_written += static_cast<size_t>(bytes_written);
        }
    }
};


std::unique_ptr<File> File::open_file(const char* filename, Mode mode) {
    return std::make_unique<PosixFile>(filename, mode);
}


std::unique_ptr<File> File::make_temporary_file() {
    char file_template[] = ".tmpfile-XXXXXX";
    int fd = ::mkstemp(file_template);
    if (fd < 0) {
        throw_errno();
    }
    if (::unlink(file_template) < 0) {
        ::close(fd);
        throw_errno();
    }
    return std::make_unique<PosixFile>(File::WRITE, fd, 0);
}

}  // namespace moderndbs
