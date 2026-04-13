#ifndef CL_FUNCTION_H
#define CL_FUNCTION_H

#include "code_object.h"
#include "indirect_dict.h"
#include "klass.h"
#include "object.h"
#include "owned_typed_value.h"
#include "value.h"
#include <vector>

namespace cl
{
    // may need closures and stuff later. TBD
    class Function : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Function", nullptr);

        Function(TValue<CodeObject> _code_object)
            : Object(&klass, 1, sizeof(Function) / 8), code_object(_code_object)
        {
        }

        MemberTValue<CodeObject> code_object;
    };

    static_assert(std::is_trivially_destructible_v<Function>);

};  // namespace cl

#endif  // CL_FUNCTION_H
