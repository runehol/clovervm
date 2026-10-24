#include "builtin_types/none_type.h"

#include "builtin_types/str.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape_key.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cstdint>
#include <iterator>

namespace cl
{
    // CPython 3.14's fixed hash for the None singleton.
    static constexpr int64_t kNoneHash = 0xFCA86420;

    [[nodiscard]] static Value native_none_type_hash(ThreadState *thread,
                                                     Value self)
    {
        if(!self.is_none())
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"NoneType.__hash__ expects a NoneType receiver");
        }
        return Value::from_smi(kNoneHash);
    }

    static Value trusted_none_type_hash(ThreadState *, Value)
    {
        return Value::from_smi(kNoneHash);
    }

    using NoneHashHandler =
        TrustedHandlerDefinition<trusted_none_type_hash,
                                 TrustedHandlerEffects::None>;

    class NoneHashResolver final
        : public TrustedHandlerResolverBase<NoneHashHandler>
    {
    public:
        static TrustedResolution resolve(VirtualMachine *vm,
                                         ShapeKey operand0_key,
                                         ShapeKey operand1_key,
                                         TrustedHandlerOperandOrder order,
                                         TrustedHandlerArity requested_arity)
        {
            (void)vm;
            (void)operand1_key;
            (void)order;
            if(requested_arity == TrustedHandlerArity::Unary &&
               operand0_key == ShapeKey::from_value(Value::None()))
            {
                return NoneHashHandler::resolution();
            }
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
    };

    static Value native_none_type_str(ThreadState *thread, Value self)
    {
        if(!self.is_none())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"NoneType.__str__ expects a NoneType receiver");
        }
        return active_thread()->make_object_value<String>(L"None").raw_value();
    }

    BuiltinClassDefinition make_none_type_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"NoneType"), 0, nullptr, 0,
            vm->object_class(), NativeLayoutId::Invalid,
            SubclassPolicy::Disallowed, immutable_shape_flags());
        return builtin_class_definition(cls, BuiltinsVisibility::Internal);
    }

    void install_none_type_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            with_trusted_handler_resolver<NoneHashResolver>(
                vm, builtin_intrinsic_method(L"__hash__", native_none_type_hash,
                                             L"Return hash(self).")),
            builtin_intrinsic_method(L"__str__", native_none_type_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_none_type_str,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->none_type_class(),
                                              methods, std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
