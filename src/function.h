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
        static constexpr uint32_t VarArgs = UINT32_MAX;

        Function(ClassObject *cls, TValue<CodeObject> _code_object)
            : Object(cls, native_layout_id, compact_layout()),
              code_object(_code_object), default_parameters(Value::None()),
              min_positional_arity(
                  _code_object.extract()->n_positional_parameters),
              max_positional_arity(max_arity_for_code(_code_object)),
              n_positional_parameters(
                  _code_object.extract()->n_positional_parameters),
              parameter_flags(_code_object.extract()->parameter_flags)
        {
            assert_parameter_layout(_code_object);
        }

        Function(ClassObject *cls, TValue<CodeObject> _code_object,
                 TValue<Tuple> _default_parameters)
            : Object(cls, native_layout_id, compact_layout()),
              code_object(_code_object),
              default_parameters(_default_parameters),
              min_positional_arity(
                  min_arity_for_code(_code_object, _default_parameters)),
              max_positional_arity(max_arity_for_code(_code_object)),
              n_positional_parameters(
                  _code_object.extract()->n_positional_parameters),
              parameter_flags(_code_object.extract()->parameter_flags)
        {
            assert_parameter_layout(_code_object);
            assert(min_positional_arity <= n_positional_parameters);
            assert(_default_parameters.extract()->size() ==
                   n_positional_parameters - min_positional_arity);
        }

        bool accepts_arity(uint32_t n_args) const
        {
            return n_args >= min_positional_arity &&
                   n_args <= max_positional_arity;
        }

        bool has_varargs() const
        {
            return has_function_parameter_flag(
                parameter_flags, FunctionParameterFlags::HasVarArgs);
        }

        MemberTValue<CodeObject> code_object;
        MemberValue default_parameters;
        uint32_t min_positional_arity;
        uint32_t max_positional_arity;
        uint32_t n_positional_parameters;
        FunctionParameterFlags parameter_flags;

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(Function, Object, 2);

    private:
        static uint32_t min_arity_for_code(TValue<CodeObject> code_object,
                                           TValue<Tuple> default_parameters)
        {
            assert(default_parameters.extract()->size() <=
                   code_object.extract()->n_positional_parameters);
            return code_object.extract()->n_positional_parameters -
                   uint32_t(default_parameters.extract()->size());
        }

        static uint32_t max_arity_for_code(TValue<CodeObject> code_object)
        {
            return code_object.extract()->has_varargs()
                       ? VarArgs
                       : code_object.extract()->n_positional_parameters;
        }

        static void assert_parameter_layout(TValue<CodeObject> code_object)
        {
            uint32_t expected_n_parameters =
                code_object.extract()->n_positional_parameters +
                (code_object.extract()->has_varargs() ? 1 : 0);
            assert(code_object.extract()->n_parameters ==
                   expected_n_parameters);
        }
    };

    static_assert(std::is_trivially_destructible_v<Function>);

    BuiltinClassDefinition make_function_class(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_FUNCTION_H
