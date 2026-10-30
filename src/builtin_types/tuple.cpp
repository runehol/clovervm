#include "builtin_types/tuple.h"
#include "builtin_types/hash.h"
#include "builtin_types/int.h"
#include "builtin_types/module_object.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple_iterator.h"
#include "bytecode/code_object_builder.h"
#include "compiler/scope.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "object_model/refcount.h"
#include "object_model/typed_value.h"
#include "runtime/exception_propagation.h"
#include "runtime/ordering_op.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include "util/fixed_wide_string.h"
#include <bit>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

namespace cl
{
    [[nodiscard]] static Value tuple_hash(ThreadState *thread,
                                          TValue<Tuple> tuple)
    {
        // Use the 64-bit tuple mixer from CPython 3.14.5, reducing the final
        // result to Clover's canonical hash representation. Do not cache it:
        // element hashing may run arbitrary Python and invalidate its methods.
        constexpr uint64_t prime1 = 11400714785074694791ULL;
        constexpr uint64_t prime2 = 14029467366897019727ULL;
        constexpr uint64_t prime5 = 2870177450012600261ULL;
        Owned<TValue<Tuple>> owner(tuple);
        const size_t size = owner.extract()->size();
        uint64_t accumulator = prime5;
        for(size_t index = 0; index < size; ++index)
        {
            Owned<Value> item(owner.extract()->item_unchecked(index));
            TValue<SMI> hash = CL_TRY(thread->hash_value(item.value()));
            accumulator += static_cast<uint64_t>(hash.extract()) * prime2;
            accumulator = std::rotl(accumulator, 31);
            accumulator *= prime1;
        }
        accumulator += static_cast<uint64_t>(size) ^ (prime5 ^ 3527539ULL);
        return canonicalize_nonnegative_raw_hash(accumulator).raw_value();
    }

    [[nodiscard]] static Value native_tuple_hash(ThreadState *thread,
                                                 Value self)
    {
        if(!has_native_layout<Tuple>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__hash__ expects a tuple receiver");
        }
        return tuple_hash(thread, TValue<Tuple>::from_value_assumed(self));
    }

    [[nodiscard]] static Value trusted_tuple_hash(ThreadState *thread,
                                                  Value self)
    {
        return tuple_hash(thread, TValue<Tuple>::from_value_assumed(self));
    }

    using TupleHashHandler = TrustedHandlerDefinition<
        trusted_tuple_hash,
        TrustedHandlerEffects::Allocate | TrustedHandlerEffects::Safepoint |
            TrustedHandlerEffects::Raise | TrustedHandlerEffects::CallPython>;

    class TupleHashResolver final
        : public TrustedHandlerResolverBase<TupleHashHandler>
    {
    public:
        static TrustedResolution resolve(VirtualMachine *vm, ShapeKey receiver,
                                         ShapeKey unused,
                                         TrustedHandlerOperandOrder order,
                                         TrustedHandlerArity arity)
        {
            (void)unused;
            if(arity != TrustedHandlerArity::Unary ||
               order != TrustedHandlerOperandOrder::Normal ||
               vm->shape_for_key(receiver)->get_class() != vm->tuple_class())
            {
                return TrustedResolution::no_trusted_handler_call_untrusted();
            }
            return TupleHashHandler::resolution();
        }
    };

    static Value native_tuple_repr(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__repr__ expects a tuple receiver");
        }

        Tuple *tuple = self.get_ptr<Tuple>();
        StringBuilder builder;
        builder.append_char(L'(');
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            if(idx != 0)
            {
                builder.append_c_str(L", ");
            }
            CL_PROPAGATE_EXCEPTION(
                builder.append_repr(tuple->item_unchecked(idx)));
        }
        if(tuple->size() == 1)
        {
            builder.append_char(L',');
        }
        builder.append_char(L')');
        return builder.finish();
    }

    static Value native_tuple_iter(ThreadState *thread, Value self)
    {
        TValue<Tuple> tuple = CL_TRY(TValue<Tuple>::from_value_or_raise(
            self, L"TypeError", L"tuple.__iter__ expects a tuple receiver"));
        return make_object_value<TupleIterator>(tuple).raw_value();
    }

    static Value native_tuple_len(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__len__ expects a tuple receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<Tuple>()->size()));
    }

    static Value native_tuple_add(ThreadState *thread, Value left, Value right)
    {
        if(!has_native_layout<Tuple>(left) || !has_native_layout<Tuple>(right))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"can only concatenate tuple to tuple");
        }

        return left.get_ptr<Tuple>()
            ->concat(right.get_ptr<Tuple>())
            .raw_value();
    }

    template <OrderingOp Operation>
    static bool tuple_order_sizes(size_t left, size_t right)
    {
        if constexpr(Operation == OrderingOp::Less)
            return left < right;
        if constexpr(Operation == OrderingOp::LessEqual)
            return left <= right;
        if constexpr(Operation == OrderingOp::Greater)
            return left > right;
        if constexpr(Operation == OrderingOp::GreaterEqual)
            return left >= right;
    }

    template <OrderingOp Operation>
    static Value tuple_order(ThreadState *thread, TValue<Tuple> left,
                             TValue<Tuple> right)
    {
        // Keep containers and elements alive across arbitrary Python callbacks.
        Owned<TValue<Tuple>> left_owner(left);
        Owned<TValue<Tuple>> right_owner(right);
        size_t index = 0;
        for(; index < left.extract()->size() && index < right.extract()->size();
            ++index)
        {
            Owned<Value> left_item(left.extract()->item_unchecked(index));
            Owned<Value> right_item(right.extract()->item_unchecked(index));
            if(left_item.value() == right_item.value())
                continue;
            if(!CL_TRY(
                   thread->test_equal(left_item.value(), right_item.value())))
                break;
        }
        if(index >= left.extract()->size() || index >= right.extract()->size())
        {
            return tuple_order_sizes<Operation>(left.extract()->size(),
                                                right.extract()->size())
                       ? Value::True()
                       : Value::False();
        }
        // Tuple payloads are immutable; retain the unequal elements while the
        // requested ordering operation runs arbitrary Python code.
        Owned<Value> left_item(left.extract()->item_unchecked(index));
        Owned<Value> right_item(right.extract()->item_unchecked(index));
        return thread->call_clovervm_function(
            thread->get_machine()->ordering_helper_function(Operation),
            left_item.value(), right_item.value());
    }

    template <OrderingOp Operation, FixedWideString ReceiverError>
    static Value native_tuple_order(ThreadState *thread, Value self,
                                    Value other)
    {
        if(!has_native_layout<Tuple>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", ReceiverError.c_str());
        }
        if(!has_native_layout<Tuple>(other))
            return Value::NotImplemented();
        return tuple_order<Operation>(thread,
                                      TValue<Tuple>::from_value_assumed(self),
                                      TValue<Tuple>::from_value_assumed(other));
    }

    template <OrderingOp Operation, bool Reflected>
    static Value trusted_tuple_order(ThreadState *thread, Value left,
                                     Value right)
    {
        if constexpr(Reflected)
            std::swap(left, right);
        return tuple_order<Operation>(thread,
                                      TValue<Tuple>::from_value_assumed(left),
                                      TValue<Tuple>::from_value_assumed(right));
    }

    template <OrderingOp Operation, bool Reflected>
    using TupleOrderHandler = TrustedHandlerDefinition<
        trusted_tuple_order<Operation, Reflected>,
        TrustedHandlerEffects::Allocate | TrustedHandlerEffects::Safepoint |
            TrustedHandlerEffects::Raise | TrustedHandlerEffects::CallPython>;

    template <OrderingOp Operation>
    class TupleOrderResolver final
        : public TrustedHandlerResolverBase<TupleOrderHandler<Operation, false>,
                                            TupleOrderHandler<Operation, true>>
    {
    public:
        static TrustedResolution resolve(VirtualMachine *vm, ShapeKey left,
                                         ShapeKey right,
                                         TrustedHandlerOperandOrder order,
                                         TrustedHandlerArity arity)
        {
            if(arity != TrustedHandlerArity::Binary ||
               vm->shape_for_key(left)->get_class() != vm->tuple_class() ||
               vm->shape_for_key(right)->get_class() != vm->tuple_class())
            {
                return TrustedResolution::no_trusted_handler_call_untrusted();
            }
            if(order == TrustedHandlerOperandOrder::Reflected)
                return TupleOrderHandler<Operation, true>::resolution();
            return TupleOrderHandler<Operation, false>::resolution();
        }
    };

    static Value require_smi_index(Value value, const wchar_t *message,
                                   int64_t &out)
    {
        TValue<SMI> index = CL_TRY(try_intlike_value_to_smi(value, message));
        out = index.extract();
        return Value::None();
    }

    static Value native_tuple_getitem(ThreadState *thread, Value self,
                                      Value index_value)
    {
        if(!has_native_layout<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__getitem__ expects a tuple receiver");
        }

        if(has_native_layout<Slice>(index_value))
        {
            TValue<Slice> slice =
                TValue<Slice>::from_value_assumed(index_value);
            if(slice.extract()->step.raw_value().is_none())
            {
                NormalizedNonstridedSlice normalized =
                    CL_TRY(normalize_nonstrided_slice_for_length(
                        thread, slice,
                        static_cast<int64_t>(self.get_ptr<Tuple>()->size())));
                return self.get_ptr<Tuple>()->get_slice(normalized).raw_value();
            }
            NormalizedGeneralSlice normalized =
                CL_TRY(normalize_general_slice_for_length(
                    thread, slice,
                    static_cast<int64_t>(self.get_ptr<Tuple>()->size())));
            return self.get_ptr<Tuple>()->get_slice(normalized).raw_value();
        }
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"tuple indices must be integers or slices", py_idx));
        return self.get_ptr<Tuple>()->get_item(py_idx);
    }

    static Value trusted_tuple_getitem_smi_handler(ThreadState *thread,
                                                   Value self,
                                                   Value index_value)
    {
        (void)thread;
        return self.get_ptr<Tuple>()->get_item(index_value.get_smi());
    }

    static Value trusted_tuple_getitem_nonstrided_slice_handler(
        ThreadState *thread, Value self, Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedNonstridedSlice normalized =
            CL_TRY(normalize_nonstrided_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<Tuple>()->size())));
        return self.get_ptr<Tuple>()->get_slice(normalized).raw_value();
    }

    static Value
    trusted_tuple_getitem_general_slice_handler(ThreadState *thread, Value self,
                                                Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedGeneralSlice normalized =
            CL_TRY(normalize_general_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<Tuple>()->size())));
        return self.get_ptr<Tuple>()->get_slice(normalized).raw_value();
    }

    static TrustedResolution resolve_trusted_tuple_getitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() != vm->tuple_class())
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            return TrustedResolution::call_trusted(
                trusted_tuple_getitem_smi_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_step_none_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_tuple_getitem_nonstrided_slice_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_general_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_tuple_getitem_general_slice_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    Tuple::Tuple(BootstrapObjectTag, size_t size)
        : Object(BootstrapObjectTag{}, native_layout),
          size_value(TValue<SMI>::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    Tuple::Tuple(ClassObject *cls, size_t size)
        : Object(cls, native_layout),
          size_value(TValue<SMI>::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    BuiltinClassDefinition make_tuple_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Tuple};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class<Tuple>(
            vm->get_or_create_interned_string_value(L"tuple"), 0, nullptr, 0,
            SubclassPolicy::Allowed, immutable_shape_flags());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_tuple_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            with_trusted_handler_resolver<TupleHashResolver>(
                vm, builtin_intrinsic_method(L"__hash__", native_tuple_hash,
                                             L"Return hash(self).")),
            builtin_intrinsic_method(L"__str__", native_tuple_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_tuple_repr,
                                     L"Return repr(self)."),
            with_trusted_handler_resolver<TupleOrderResolver<OrderingOp::Less>>(
                vm,
                builtin_intrinsic_method(
                    L"__lt__", native_tuple_order<
                                   OrderingOp::Less,
                                   L"tuple.__lt__ expects a tuple receiver">)),
            with_trusted_handler_resolver<
                TupleOrderResolver<OrderingOp::LessEqual>>(
                vm,
                builtin_intrinsic_method(
                    L"__le__", native_tuple_order<
                                   OrderingOp::LessEqual,
                                   L"tuple.__le__ expects a tuple receiver">)),
            with_trusted_handler_resolver<
                TupleOrderResolver<OrderingOp::Greater>>(
                vm,
                builtin_intrinsic_method(
                    L"__gt__", native_tuple_order<
                                   OrderingOp::Greater,
                                   L"tuple.__gt__ expects a tuple receiver">)),
            with_trusted_handler_resolver<
                TupleOrderResolver<OrderingOp::GreaterEqual>>(
                vm,
                builtin_intrinsic_method(
                    L"__ge__", native_tuple_order<
                                   OrderingOp::GreaterEqual,
                                   L"tuple.__ge__ expects a tuple receiver">)),
            builtin_intrinsic_method(L"__len__", native_tuple_len,
                                     L"Return len(self)."),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__getitem__", native_tuple_getitem,
                                         L"Return self[index]."),
                resolve_trusted_tuple_getitem_handler),
            builtin_intrinsic_method(L"__iter__", native_tuple_iter,
                                     L"Implement iter(self)."),
            builtin_intrinsic_method(L"__add__", native_tuple_add,
                                     L"Return self+value."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->tuple_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

    [[nodiscard]] static Value validate_tuple_new_class(ThreadState *thread,
                                                        Value cls_value)
    {
        ClassObject *cls = try_native_cast<ClassObject>(cls_value);
        if(cls == nullptr ||
           !is_subclass_of(cls, thread->get_machine()->tuple_class()) ||
           cls->instance_native_layout_id() != NativeLayoutId::Tuple)
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__new__ expects a tuple subclass");
        }
        return cls_value;
    }

    [[nodiscard]] static Value allocate_tuple_from_exact(ThreadState *thread,
                                                         Value cls_value,
                                                         Value items_value)
    {
        // The generated __new__ validates cls before iteration. ToExactTuple
        // supplies an exact, fully initialized tuple, retained in its frame.
        ClassObject *cls = native_cast<ClassObject>(cls_value);
        Tuple *items = native_cast<Tuple>(items_value);
        assert(cls->instance_native_layout_id() == NativeLayoutId::Tuple);
        assert(items->get_shape()->get_class() ==
               thread->get_machine()->tuple_class());
        if(cls == thread->get_machine()->tuple_class())
            return items_value;

        Owned<TValue<Tuple>> result(
            thread->make_internal_value<Tuple>(cls, items->size()));
        for(size_t idx = 0; idx < items->size(); ++idx)
            result.extract()->initialize_item_unchecked(
                idx, items->item_unchecked(idx));
        return result.raw_value();
    }

    static Expected<TValue<Function>>
    make_tuple_new_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), scope,
            vm->get_or_create_interned_string_value(L"tuple.__new__"));
        code.configure_positional_function(2);  // cls, iterable
        code.function_signature().n_posonly_parameters = 2;
        code.function_signature().n_pos_or_kw_parameters = 0;
        code.function_signature().first_default_slot = 1;
        code.function_signature().default_presence_mask = 1;

        TValue<Function> validator =
            CL_TRY(make_intrinsic_function(vm, validate_tuple_new_class));
        uint8_t validator_idx =
            CL_TRY(code.allocate_constant(validator.raw_value()));
        NativeFunctionTarget allocator;
        allocator.fixed2 = allocate_tuple_from_exact;
        uint8_t allocator_idx =
            CL_TRY(code.add_native_function_target(allocator));
        {
            CodeObjectBuilder::TemporaryReg callable(code);
            CL_TRY(code.emit_lda_constant(0, validator_idx));
            CL_TRY(code.emit_star(0, callable));
            {
                CodeObjectBuilder::TemporaryReg args(
                    code, 1, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, args, 0));
                // Ordinary call/return propagation raises validation errors
                // before any source callbacks can run.
                CL_TRY(code.emit_call_positional(0, callable, args, 1));
            }
            CL_TRY(code.emit_to_exact_tuple(0, 1));
            CL_TRY(code.emit_star(0, 1));
            CL_TRY(code.emit_call_intrinsic(0, Bytecode::CallIntrinsic2,
                                            allocator_idx));
            CL_TRY(code.emit_return_or_raise_exception(0));
        }
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        TValue<Tuple> defaults = vm->make_immortal_object_value<Tuple>(1);
        defaults.extract()->initialize_item_unchecked(
            0, vm->make_immortal_object_value<Tuple>(0).raw_value());
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::some(defaults)));
    }

    void install_tuple_python_methods(VirtualMachine *vm,
                                      ModuleObject *implementation_module)
    {
        ClassObject *cls = vm->tuple_class();
        const wchar_t *method_names[] = {L"__eq__", L"__ne__", L"count",
                                         L"index"};
        const wchar_t *helper_names[] = {
            L"_clover_tuple_eq", L"_clover_tuple_ne", L"_clover_tuple_count",
            L"_clover_tuple_index"};
        DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly) |
                                descriptor_flag(DescriptorFlag::StableSlot);
        TValue<Function> new_function = unwrap_bootstrap_expected(
            vm, make_tuple_new_function(vm), "creating tuple.__new__");
        bool new_stored = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__new__"),
            new_function.raw_value(), flags);
        assert(new_stored);
        (void)new_stored;
        for(size_t idx = 0; idx < std::size(method_names); ++idx)
        {
            TValue<String> helper_name =
                vm->get_or_create_interned_string_value(helper_names[idx]);
            Value function =
                implementation_module->get_own_property(helper_name);
            if(!has_native_layout<Function>(function))
            {
                fatal("trusted builtin_implementations.py did not define tuple "
                      "method");
            }
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(method_names[idx]),
                function, flags);
            assert(stored);
            (void)stored;
        }
    }

    Value Tuple::get_item(int64_t py_idx) const
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return item_unchecked(idx);
    }

    TValue<Tuple> Tuple::get_slice(const NormalizedNonstridedSlice &slice) const
    {
        TValue<Tuple> result =
            make_object_value<Tuple>(slice.selected_sequence_length);
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->initialize_item_unchecked(
                write_idx, item_unchecked(static_cast<size_t>(
                               slice.start + static_cast<int64_t>(write_idx))));
        }
        return result;
    }

    TValue<Tuple> Tuple::get_slice(const NormalizedGeneralSlice &slice) const
    {
        TValue<Tuple> result =
            make_object_value<Tuple>(slice.selected_sequence_length);
        int64_t read_idx = slice.start;
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->initialize_item_unchecked(
                write_idx, item_unchecked(static_cast<size_t>(read_idx)));
            read_idx += slice.step;
        }
        return result;
    }

    TValue<Tuple> Tuple::concat(const Tuple *other) const
    {
        TValue<Tuple> result = make_object_value<Tuple>(size() + other->size());
        size_t write_idx = 0;
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->initialize_item_unchecked(write_idx++,
                                                        item_unchecked(idx));
        }
        for(size_t idx = 0; idx < other->size(); ++idx)
        {
            result.extract()->initialize_item_unchecked(
                write_idx++, other->item_unchecked(idx));
        }
        return result;
    }

    size_t Tuple::wrap_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        return static_cast<size_t>(normalized);
    }

    Value Tuple::check_index(size_t idx) const
    {
        if(idx >= size())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"IndexError", L"tuple index out of range");
        }
        return Value::None();
    }

    void Tuple::initialize_items(size_t size)
    {
        for(size_t idx = 0; idx < size; ++idx)
        {
            elements[idx] = Value::not_present();
        }
    }

}  // namespace cl
