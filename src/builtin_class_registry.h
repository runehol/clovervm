#ifndef CL_BUILTIN_CLASS_REGISTRY_H
#define CL_BUILTIN_CLASS_REGISTRY_H

#include "native_layout_id.h"
#include <cstddef>

namespace cl
{
    class ClassObject;

    struct BuiltinClassDefinition
    {
        ClassObject *cls;
        const NativeLayoutId *native_layout_ids;
        size_t native_layout_id_count;
    };

    inline BuiltinClassDefinition builtin_class_definition(ClassObject *cls)
    {
        return BuiltinClassDefinition{cls, nullptr, 0};
    }

    template <size_t N>
    BuiltinClassDefinition
    builtin_class_definition(ClassObject *cls,
                             const NativeLayoutId (&native_layout_ids)[N])
    {
        return BuiltinClassDefinition{cls, native_layout_ids, N};
    }

}  // namespace cl

#endif  // CL_BUILTIN_CLASS_REGISTRY_H
