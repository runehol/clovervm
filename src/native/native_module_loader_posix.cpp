#include "native/native_module_loader_internal.h"

#include <dlfcn.h>
#include <filesystem>

namespace cl
{
    const char *native_library_last_error() { return dlerror(); }

    void *native_library_open(const std::wstring &path)
    {
        std::string narrow_path = std::filesystem::path(path).string();
        return dlopen(narrow_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    }

    void *native_library_symbol(void *handle, const char *symbol_name)
    {
        dlerror();
        void *symbol = dlsym(handle, symbol_name);
        if(dlerror() != nullptr)
        {
            return nullptr;
        }
        return symbol;
    }

}  // namespace cl
