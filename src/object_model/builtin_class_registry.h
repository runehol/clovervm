#ifndef CL_BUILTIN_CLASS_REGISTRY_H
#define CL_BUILTIN_CLASS_REGISTRY_H

#include "memory/native_layout_id.h"
#include <cstddef>

namespace cl
{
    class ClassObject;

    enum class BuiltinsVisibility
    {
        Internal,
        Public,
    };

    struct BuiltinClassDefinition
    {
        ClassObject *cls;
        const NativeLayoutId *native_layout_ids;
        size_t native_layout_id_count;
        BuiltinsVisibility builtins_visibility;
    };

    inline BuiltinClassDefinition
    builtin_class_definition(ClassObject *cls,
                             BuiltinsVisibility builtins_visibility)
    {
        return BuiltinClassDefinition{cls, nullptr, 0, builtins_visibility};
    }

    template <size_t N>
    BuiltinClassDefinition
    builtin_class_definition(ClassObject *cls,
                             const NativeLayoutId (&native_layout_ids)[N],
                             BuiltinsVisibility builtins_visibility)
    {
        return BuiltinClassDefinition{cls, native_layout_ids, N,
                                      builtins_visibility};
    }

}  // namespace cl

#endif  // CL_BUILTIN_CLASS_REGISTRY_H
