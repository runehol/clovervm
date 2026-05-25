#ifndef CL_FUNCTION_H
#define CL_FUNCTION_H

#include "builtin_class_registry.h"
#include "code_object.h"
#include "object.h"
#include "owned.h"
#include "tuple.h"
#include "typed_value.h"
#include "value.h"
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    // may need closures and stuff later. TBD
    class Function : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::Function;
        static constexpr uint32_t VarArgs = UINT32_MAX;
        static constexpr uint32_t kCodeObjectSlot = 0;
        static constexpr uint32_t kDefaultParametersSlot = 1;
        static constexpr uint32_t kDocstringSlot = 2;
        static constexpr uint32_t kInlineSlotCount = 3;

        Function(ClassObject *cls, TValue<CodeObject> _code_object,
                 Optional<TValue<String>> _docstring,
                 Optional<TValue<Tuple>> _default_parameters =
                     Optional<TValue<Tuple>>::none())
            : SlotObject(cls, native_layout), code_object(_code_object),
              default_parameters(_default_parameters), docstring(_docstring)
        {
            refresh_signature_from_code_object();
            assert_parameter_layout(_code_object);
            if(_default_parameters.has_value())
            {
                assert(call_signature.min_positional_arity <=
                       call_signature.function.n_positional_parameters);
                assert(_default_parameters.value().extract()->size() ==
                       call_signature.function.n_positional_parameters -
                           call_signature.min_positional_arity);
            }
        }

        bool accepts_arity(uint32_t n_args) const
        {
            return n_args >= call_signature.min_positional_arity &&
                   n_args <= call_signature.max_positional_arity;
        }

        bool has_varargs() const
        {
            return call_signature.function.has_varargs();
        }

        void refresh_signature_from_code_object()
        {
            call_signature.function = code_object.extract()->function_signature;
            keyword_remap.copy_from(
                code_object.extract()->function_keyword_remap);
            call_signature.min_positional_arity =
                min_arity_for_code(code_object.value(), default_parameters);
            call_signature.max_positional_arity =
                max_arity_for_code(code_object.value());
        }

        Member<TValue<CodeObject>> code_object;
        Member<Optional<TValue<Tuple>>> default_parameters;
        Member<Optional<TValue<String>>> docstring;
        FunctionKeywordRemap keyword_remap;
        FunctionCallSignature call_signature;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            Function, SlotObject,
            3 + FunctionKeywordRemap::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(Function);

    private:
        static uint32_t
        min_arity_for_code(TValue<CodeObject> code_object,
                           Optional<TValue<Tuple>> default_parameters)
        {
            if(!default_parameters.has_value())
            {
                return code_object.extract()
                    ->function_signature.n_positional_parameters;
            }
            assert(default_parameters.value().extract()->size() <=
                   code_object.extract()
                       ->function_signature.n_positional_parameters);
            return code_object.extract()
                       ->function_signature.n_positional_parameters -
                   uint32_t(default_parameters.value().extract()->size());
        }

        static uint32_t max_arity_for_code(TValue<CodeObject> code_object)
        {
            return code_object.extract()->has_varargs()
                       ? VarArgs
                       : code_object.extract()
                             ->function_signature.n_positional_parameters;
        }

        static void assert_parameter_layout(TValue<CodeObject> code_object)
        {
            assert(code_object.extract()->function_signature.n_parameters ==
                   code_object.extract()
                           ->function_signature.n_positional_parameters +
                       code_object.extract()
                           ->function_signature.n_kwonly_parameters +
                       (code_object.extract()->has_varargs() ? 1 : 0));
        }
    };

    static_assert(std::is_trivially_destructible_v<Function>);

    BuiltinClassDefinition make_function_class(VirtualMachine *vm);

};  // namespace cl

#endif  // CL_FUNCTION_H
