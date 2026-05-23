#ifndef CL_MODULE_FINDER_H
#define CL_MODULE_FINDER_H

#include "typed_value.h"
#include <optional>
#include <string>
#include <vector>

namespace cl
{
    class List;
    class String;
    class ThreadState;

    enum class ModuleSpecKind
    {
        Source,
        Builtin,
        Namespace,
        NativeExtension,
    };

    struct ModuleSpec
    {
        ModuleSpecKind kind = ModuleSpecKind::Source;
        std::wstring name;
        std::wstring origin;
        bool is_package = false;
        std::vector<std::wstring> submodule_search_locations;
    };

    List *sys_path(ThreadState *thread);
    std::optional<ModuleSpec> find_module_spec(ThreadState *thread,
                                               const std::wstring &full_name,
                                               const std::wstring &leaf_name,
                                               List *path);
    std::optional<ModuleSpec> find_source_module_spec(ThreadState *thread,
                                                      TValue<String> name);

}  // namespace cl

#endif  // CL_MODULE_FINDER_H
