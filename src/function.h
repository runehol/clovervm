#ifndef CL_FUNCTION_H
#define CL_FUNCTION_H

#include "builtin_class_registry.h"
#include "code_object.h"
#include "object.h"
#include "owned_typed_value.h"
#include "value.h"
#include <vector>

namespace cl
{
    class VirtualMachine;

    // may need closures and stuff later. TBD
    class Function : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Function;

        Function(TValue<CodeObject> _code_object)
            : Object(native_layout_id, compact_layout()),
              code_object(_code_object)
        {
        }

        MemberTValue<CodeObject> code_object;

        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(Function, code_object, 1);
    };

    static_assert(std::is_trivially_destructible_v<Function>);

    BuiltinClassDefinition make_function_class(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_FUNCTION_H
