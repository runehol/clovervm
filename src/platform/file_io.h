#ifndef CL_PLATFORM_FILE_IO_H
#define CL_PLATFORM_FILE_IO_H

#include <cstddef>
#include <cstdint>

namespace cl
{
    enum FileAccess : uint8_t
    {
        FileReadable = 1,
        FileWritable = 2,
        FileAppending = 4,
        FileCreating = 8,
        FileExclusive = 16,
        FileTruncating = 32,
    };

    // Unchecked OS operations: error is captured before any cleanup. These
    // helpers neither allocate VM objects nor set pending Python exceptions.
    struct FileResult
    {
        int64_t value;
        int error;
    };

    FileResult platform_file_open(const char *path, uint8_t access);
    FileResult platform_file_validate(int fd);
    FileResult platform_file_read(int fd, void *data, size_t size);
    FileResult platform_file_write(int fd, const void *data, size_t size);
    FileResult platform_file_seek(int fd, int64_t offset, int whence);
    FileResult platform_file_close(int fd);
}  // namespace cl

#endif  // CL_PLATFORM_FILE_IO_H
