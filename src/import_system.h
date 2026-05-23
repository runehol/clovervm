#ifndef CL_IMPORT_SYSTEM_H
#define CL_IMPORT_SYSTEM_H

#include "typed_value.h"
#include <optional>
#include <string>
#include <vector>

namespace cl
{
    class CodeObject;
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
    [[nodiscard]] Value import_name_from_code(ThreadState *thread,
                                              CodeObject *code_object,
                                              TValue<String> name,
                                              Value fromlist, int64_t level);
    [[nodiscard]] Value import_from(ThreadState *thread, Value module,
                                    TValue<String> name);
    [[nodiscard]] Value import_star(ThreadState *thread,
                                    CodeObject *code_object, Value module);

}  // namespace cl

#endif  // CL_IMPORT_SYSTEM_H
