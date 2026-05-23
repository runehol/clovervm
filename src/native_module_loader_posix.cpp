#include "native_module_loader_internal.h"

#include <dlfcn.h>

namespace cl
{
    const char *native_library_last_error() { return dlerror(); }

    void *native_library_open(const std::wstring &path)
    {
        std::string narrow_path;
        for(wchar_t ch: path)
        {
            narrow_path += static_cast<char>(ch);
        }
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
