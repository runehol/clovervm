#ifndef CL_FUNCTION_H
#define CL_FUNCTION_H

#include "builtin_class_registry.h"
#include "code_object.h"
#include "object.h"
#include "owned_typed_value.h"
#include "tuple.h"
#include "value.h"
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    // may need closures and stuff later. TBD
    class Function : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Function;

        Function(ClassObject *cls, TValue<CodeObject> _code_object)
            : Object(cls, native_layout_id, compact_layout()),
              code_object(_code_object), default_parameters(Value::None()),
              min_positional_arity(_code_object.extract()->n_parameters),
              max_positional_arity(_code_object.extract()->n_parameters)
        {
        }

        Function(ClassObject *cls, TValue<CodeObject> _code_object,
                 TValue<Tuple> _default_parameters,
                 uint32_t _min_positional_arity, uint32_t _max_positional_arity)
            : Object(cls, native_layout_id, compact_layout()),
              code_object(_code_object),
              default_parameters(_default_parameters),
              min_positional_arity(_min_positional_arity),
              max_positional_arity(_max_positional_arity)
        {
            assert(max_positional_arity == code_object.extract()->n_parameters);
            assert(min_positional_arity <= max_positional_arity);
            assert(_default_parameters.extract()->size() ==
                   max_positional_arity - min_positional_arity);
        }

        bool accepts_arity(uint32_t n_args) const
        {
            return n_args >= min_positional_arity &&
                   n_args <= max_positional_arity;
        }

        MemberTValue<CodeObject> code_object;
        MemberValue default_parameters;
        uint32_t min_positional_arity;
        uint32_t max_positional_arity;

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(Function, Object, 2);
    };

    static_assert(std::is_trivially_destructible_v<Function>);

    BuiltinClassDefinition make_function_class(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_FUNCTION_H
