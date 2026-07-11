#ifndef CL_NATIVE_EXTENSION_LOADER_INTERNAL_H
#define CL_NATIVE_EXTENSION_LOADER_INTERNAL_H

#include "absl/container/flat_hash_map.h"
#include <memory>
#include <mutex>
#include <string>

namespace cl
{
    struct NativeLibraryHandle
    {
        void *platform_handle = nullptr;
        std::mutex init_mutex;
    };

    struct NativeLibraryHandleCache
    {
        std::mutex mutex;
        absl::flat_hash_map<std::wstring, std::unique_ptr<NativeLibraryHandle>>
            handles;
    };

    const char *native_library_last_error();
    void *native_library_open(const std::wstring &path);
    void *native_library_symbol(void *handle, const char *symbol_name);

}  // namespace cl

#endif  // CL_NATIVE_EXTENSION_LOADER_INTERNAL_H
