#ifndef CL_IMPORT_SYSTEM_H
#define CL_IMPORT_SYSTEM_H

#include "typed_value.h"
#include <optional>
#include <string>
#include <vector>

namespace cl
{
    class String;
    class ThreadState;

    struct ModuleSpec
    {
        std::wstring name;
        std::wstring origin;
        bool is_package = false;
        std::vector<std::wstring> submodule_search_locations;
    };

    std::optional<ModuleSpec> find_source_module_spec(ThreadState *thread,
                                                      TValue<String> name);
    [[nodiscard]] Value import_module_absolute(ThreadState *thread,
                                               TValue<String> name);

}  // namespace cl

#endif  // CL_IMPORT_SYSTEM_H
