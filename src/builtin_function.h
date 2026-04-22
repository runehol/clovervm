#ifndef CL_BUILTIN_FUNCTION_H
#define CL_BUILTIN_FUNCTION_H

#include "klass.h"
#include "object.h"
#include "value.h"
#include <cstddef>
#include <cstdint>

namespace cl
{
    using BuiltinCallback = Value (*)(Value *parameters, size_t n_parameters);

    class BuiltinFunction : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"BuiltinFunction", nullptr);
        static constexpr uint32_t VarArgs = UINT32_MAX;

        BuiltinFunction(BuiltinCallback _callback, uint32_t _min_arity,
                        uint32_t _max_arity)
            : Object(&klass, compact_layout()), callback(_callback),
              min_arity(_min_arity), max_arity(_max_arity)
        {
        }

        bool accepts_arity(uint32_t n_args) const
        {
            return n_args >= min_arity &&
                   (max_arity == VarArgs || n_args <= max_arity);
        }

        BuiltinCallback callback;
        uint32_t min_arity;
        uint32_t max_arity;

        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(BuiltinFunction);
    };

}  // namespace cl

#endif  // CL_BUILTIN_FUNCTION_H
