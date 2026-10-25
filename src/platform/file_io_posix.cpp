#include "platform/file_io.h"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace cl
{
    FileResult platform_file_close(int fd)
    {
        // Never retry close: the descriptor number may already be reusable.
        int result = close(fd);
        return {result, result < 0 ? errno : 0};
    }

    FileResult platform_file_open(const char *path, uint8_t access)
    {
        int flags = (access & FileReadable) && (access & FileWritable) ? O_RDWR
                    : (access & FileWritable) ? O_WRONLY
                                              : O_RDONLY;
        if(access & FileCreating)
            flags |= O_CREAT;
        if(access & FileExclusive)
            flags |= O_EXCL;
        if(access & FileTruncating)
            flags |= O_TRUNC;
        if(access & FileAppending)
            flags |= O_APPEND;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        int fd;
        do
        {
            fd = open(path, flags, 0666);
        }
        while(fd < 0 && errno == EINTR);
        if(fd < 0)
            return {-1, errno};
#ifndef O_CLOEXEC
        if(fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        {
            int error = errno;
            (void)platform_file_close(fd);
            return {-1, error};
        }
#endif
        return {fd, 0};
    }

    FileResult platform_file_validate(int fd)
    {
        struct stat st;
        int result;
        do
        {
            result = fstat(fd, &st);
        }
        while(result < 0 && errno == EINTR);
        if(result < 0)
        {
            int error = errno;
            // Like CPython, require a valid descriptor but tolerate fstat
            // failures from unusual filesystems when the descriptor is valid.
            return {0, error == EBADF ? error : 0};
        }
        if(S_ISDIR(st.st_mode))
            return {-1, EISDIR};
        return {0, 0};
    }

    FileResult platform_file_read(int fd, void *data, size_t size)
    {
        ssize_t result;
        do
        {
            result = read(fd, data, size);
        }
        while(result < 0 && errno == EINTR);
        return {result, result < 0 ? errno : 0};
    }

    FileResult platform_file_write(int fd, const void *data, size_t size)
    {
        ssize_t result;
        do
        {
            result = write(fd, data, size);
        }
        while(result < 0 && errno == EINTR);
        return {result, result < 0 ? errno : 0};
    }

    FileResult platform_file_seek(int fd, int64_t offset, int whence)
    {
        static_assert(std::numeric_limits<off_t>::digits >= 63);
        off_t result = lseek(fd, static_cast<off_t>(offset), whence);
        return {static_cast<int64_t>(result), result < 0 ? errno : 0};
    }
}  // namespace cl
