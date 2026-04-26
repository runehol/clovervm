#ifndef CL_BUILTIN_FUNCTION_H
#define CL_BUILTIN_FUNCTION_H

#include "builtin_class_registry.h"
#include "object.h"
#include "value.h"
#include <cstdint>

namespace cl
{
    class ClassObject;
    class ThreadState;
    class VirtualMachine;

    struct CallArguments
    {
        CallArguments(Value *_callable_slot, uint32_t _n_args)
            : callable_slot(_callable_slot), n_args(_n_args)
        {
        }

        Value operator[](uint32_t i) const
        {
            assert(i < n_args);
            return callable_slot[-1 - int32_t(i)];
        }

        Value *callable_slot;
        uint32_t n_args;
    };

    using BuiltinCallback = Value (*)(ThreadState *, const CallArguments &args);

    class BuiltinFunction : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::BuiltinFunction;
        static constexpr uint32_t VarArgs = UINT32_MAX;

        BuiltinFunction(ClassObject *cls, BuiltinCallback _callback,
                        uint32_t _min_arity, uint32_t _max_arity)
            : Object(cls, native_layout_id, compact_layout()),
              callback(_callback), min_arity(_min_arity), max_arity(_max_arity)
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

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(BuiltinFunction, Object,
                                                     0);
    };

    BuiltinClassDefinition make_builtin_function_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_BUILTIN_FUNCTION_H
