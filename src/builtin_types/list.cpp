#include "builtin_types/list.h"
#include "builtin_types/int.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/module_object.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "object_model/shape.h"
#include "object_model/typed_value.h"
#include "runtime/exception_propagation.h"
#include "runtime/ordering_op.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include "util/fixed_wide_string.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace cl
{
    NOINLINE TValue<List> allocate_default_list(ThreadState *thread,
                                                TValue<ClassObject> cls)
    {
        assert(cls.extract()->instance_native_layout_id() ==
               NativeLayoutId::List);
        Shape *root = cls.extract()->get_instance_root_shape();
        assert(root != nullptr);
        assert(cls.extract()->get_instance_inline_slot_capacity() ==
               root->get_inline_slot_capacity());
        assert(native_inline_slot_capacity_is_valid(
            NativeLayoutId::List, root->get_inline_slot_capacity()));
        (void)root;
        return thread->make_internal_value<List>(cls.extract());
    }

    static Value native_list_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<List>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"list.__repr__ expects a list receiver");
        }

        List *list = self.get_ptr<List>();
        StringBuilder builder;
        builder.append_char(L'[');
        for(size_t idx = 0; idx < list->size(); ++idx)
        {
            if(idx != 0)
            {
                builder.append_c_str(L", ");
            }
            CL_PROPAGATE_EXCEPTION(
                builder.append_repr(list->item_unchecked(idx)));
        }
        builder.append_char(L']');
        return builder.finish();
    }

    static Value native_list_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<List>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"list.__len__ expects a list receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<List>()->size()));
    }

    static Value native_list_iter(ThreadState *thread, Value self)
    {
        TValue<List> list = CL_TRY(TValue<List>::from_value_or_raise(
            self, L"TypeError", L"list.__iter__ expects a list receiver"));
        return make_object_value<ListIterator>(list).raw_value();
    }

    static Value require_list_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<List>(self))
        {
            std::wstring message = L"list.";
            message += method_name;
            message += L" expects a list receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    template <OrderingOp Operation>
    static bool list_order_sizes(size_t left, size_t right)
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
    static Value list_order(ThreadState *thread, TValue<List> left,
                            TValue<List> right)
    {
        // Keep containers and elements alive across arbitrary Python callbacks.
        Owned<TValue<List>> left_owner(left);
        Owned<TValue<List>> right_owner(right);
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
            return list_order_sizes<Operation>(left.extract()->size(),
                                               right.extract()->size())
                       ? Value::True()
                       : Value::False();
        }
        // Equality may have replaced items or resized a list. Reload only after
        // checking current bounds; never retain a backing-storage reference.
        Owned<Value> left_item(left.extract()->item_unchecked(index));
        Owned<Value> right_item(right.extract()->item_unchecked(index));
        return thread->call_clovervm_function(
            thread->get_machine()->ordering_helper_function(Operation),
            left_item.value(), right_item.value());
    }

    template <OrderingOp Operation, FixedWideString ReceiverError>
    static Value native_list_order(ThreadState *thread, Value self, Value other)
    {
        if(!can_convert_to<List>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", ReceiverError.c_str());
        }
        if(!can_convert_to<List>(other))
            return Value::NotImplemented();
        return list_order<Operation>(thread,
                                     TValue<List>::from_value_assumed(self),
                                     TValue<List>::from_value_assumed(other));
    }

    template <OrderingOp Operation, bool Reflected>
    static Value trusted_list_order(ThreadState *thread, Value left,
                                    Value right)
    {
        if constexpr(Reflected)
            std::swap(left, right);
        return list_order<Operation>(thread,
                                     TValue<List>::from_value_assumed(left),
                                     TValue<List>::from_value_assumed(right));
    }

    template <OrderingOp Operation, bool Reflected>
    using ListOrderHandler = TrustedHandlerDefinition<
        trusted_list_order<Operation, Reflected>,
        TrustedHandlerEffects::Allocate | TrustedHandlerEffects::Safepoint |
            TrustedHandlerEffects::Raise | TrustedHandlerEffects::CallPython>;

    template <OrderingOp Operation>
    class ListOrderResolver final
        : public TrustedHandlerResolverBase<ListOrderHandler<Operation, false>,
                                            ListOrderHandler<Operation, true>>
    {
    public:
        static TrustedResolution resolve(VirtualMachine *vm, ShapeKey left,
                                         ShapeKey right,
                                         TrustedHandlerOperandOrder order,
                                         TrustedHandlerArity arity)
        {
            if(arity != TrustedHandlerArity::Binary ||
               vm->shape_for_key(left)->get_class() != vm->list_class() ||
               vm->shape_for_key(right)->get_class() != vm->list_class())
            {
                return TrustedResolution::no_trusted_handler_call_untrusted();
            }
            if(order == TrustedHandlerOperandOrder::Reflected)
                return ListOrderHandler<Operation, true>::resolution();
            return ListOrderHandler<Operation, false>::resolution();
        }
    };

    static Value require_smi_index(Value value, const wchar_t *message,
                                   int64_t &out)
    {
        TValue<SMI> index = CL_TRY(try_intlike_value_to_smi(value, message));
        out = index.extract();
        return Value::None();
    }

    static Value native_list_getitem(ThreadState *thread, Value self,
                                     Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"__getitem__"));
        if(can_convert_to<Slice>(index_value))
        {
            TValue<Slice> slice =
                TValue<Slice>::from_value_assumed(index_value);
            if(slice.extract()->step.raw_value().is_none())
            {
                NormalizedNonstridedSlice normalized =
                    CL_TRY(normalize_nonstrided_slice_for_length(
                        thread, slice,
                        static_cast<int64_t>(self.get_ptr<List>()->size())));
                return self.get_ptr<List>()->get_slice(normalized).raw_value();
            }
            NormalizedGeneralSlice normalized =
                CL_TRY(normalize_general_slice_for_length(
                    thread, slice,
                    static_cast<int64_t>(self.get_ptr<List>()->size())));
            return self.get_ptr<List>()->get_slice(normalized).raw_value();
        }
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers or slices", py_idx));
        return self.get_ptr<List>()->get_item(py_idx);
    }

    static Value native_list_setitem(ThreadState *thread, Value self,
                                     Value index_value, Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"__setitem__"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        return self.get_ptr<List>()->set_item(py_idx, value);
    }

    static Value native_list_delitem(ThreadState *thread, Value self,
                                     Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"__delitem__"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        CL_PROPAGATE_EXCEPTION(self.get_ptr<List>()->pop_item(py_idx));
        return Value::None();
    }

    static Value trusted_list_getitem_smi_handler(ThreadState *thread,
                                                  Value self, Value index_value)
    {
        (void)thread;
        return self.get_ptr<List>()->get_item(index_value.get_smi());
    }

    static Value
    trusted_list_getitem_nonstrided_slice_handler(ThreadState *thread,
                                                  Value self, Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedNonstridedSlice normalized =
            CL_TRY(normalize_nonstrided_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<List>()->size())));
        return self.get_ptr<List>()->get_slice(normalized).raw_value();
    }

    static Value trusted_list_getitem_general_slice_handler(ThreadState *thread,
                                                            Value self,
                                                            Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedGeneralSlice normalized =
            CL_TRY(normalize_general_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<List>()->size())));
        return self.get_ptr<List>()->get_slice(normalized).raw_value();
    }

    static Value trusted_list_setitem_smi_handler(ThreadState *thread,
                                                  Value self, Value index_value,
                                                  Value value)
    {
        (void)thread;
        return self.get_ptr<List>()->set_item(index_value.get_smi(), value);
    }

    static Value trusted_list_delitem_smi_handler(ThreadState *thread,
                                                  Value self, Value index_value)
    {
        (void)thread;
        CL_PROPAGATE_EXCEPTION(
            self.get_ptr<List>()->pop_item(index_value.get_smi()));
        return Value::None();
    }

    static TrustedResolution resolve_trusted_list_getitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() != vm->list_class())
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            return TrustedResolution::call_trusted(
                trusted_list_getitem_smi_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_step_none_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_list_getitem_nonstrided_slice_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_general_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_list_getitem_general_slice_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_list_setitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Ternary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() == vm->list_class() &&
           key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            return TrustedResolution::call_trusted(
                trusted_list_setitem_smi_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_list_delitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() == vm->list_class() &&
           key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            return TrustedResolution::call_trusted(
                trusted_list_delitem_smi_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value native_list_append(ThreadState *thread, Value self,
                                    Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"append"));
        self.get_ptr<List>()->append(value);
        return Value::None();
    }

    static Value native_list_clear(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"clear"));
        self.get_ptr<List>()->clear();
        return Value::None();
    }

    static Value native_list_copy(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"copy"));
        return self.get_ptr<List>()->copy().raw_value();
    }

    static Value native_list_extend(ThreadState *thread, Value self,
                                    Value other)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"extend"));
        List *list = self.get_ptr<List>();
        if(can_convert_to<List>(other))
        {
            list->extend_from_list(other.get_ptr<List>());
            return Value::None();
        }
        if(can_convert_to<Tuple>(other))
        {
            list->extend_from_tuple(other.get_ptr<Tuple>());
            return Value::None();
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"list.extend expects a list or tuple");
    }

    static Value native_list_insert(ThreadState *thread, Value self,
                                    Value index_value, Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"insert"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        self.get_ptr<List>()->insert_item(py_idx, value);
        return Value::None();
    }

    static Value native_list_pop(ThreadState *thread, Value self,
                                 Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"pop"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        return self.get_ptr<List>()->pop_item(py_idx);
    }

    static Value native_list_reverse(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"reverse"));
        self.get_ptr<List>()->reverse();
        return Value::None();
    }

    static Value native_list_sort(ThreadState *thread, Value self, Value key,
                                  Value sort_items, Value reverse)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"sort"));
        assert(can_convert_to<Function>(sort_items));
        assert(reverse.is_bool());
        return self.get_ptr<List>()->sort(
            thread, key, TValue<Function>::from_value_assumed(sort_items),
            reverse == Value::True());
    }

    static Value native_list_add(ThreadState *thread, Value left, Value right)
    {
        if(!can_convert_to<List>(left) || !can_convert_to<List>(right))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"can only concatenate list to list");
        }
        return left.get_ptr<List>()->concat(right.get_ptr<List>()).raw_value();
    }

    static TValue<Tuple> list_default_single(VirtualMachine *vm, Value value)
    {
        TValue<Tuple> defaults =
            vm->get_default_thread()->make_object_value<Tuple>(1);
        defaults.extract()->initialize_item_unchecked(0, value);
        return defaults;
    }

    List::List(ClassObject *cls, size_t size) : Object(cls, native_layout)
    {
        items.resize(size, Value::not_present());
    }

    BuiltinClassDefinition make_list_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::List};
        ClassObject *cls = ClassObject::make_builtin_class<List>(
            vm->get_or_create_interned_string_value(L"list"), 0, nullptr, 0,
            vm->object_class(), SubclassPolicy::Allowed);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_list_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> pop_defaults(
            list_default_single(vm, Value::from_smi(-1)));
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_list_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_list_repr,
                                     L"Return repr(self)."),
            with_trusted_handler_resolver<ListOrderResolver<OrderingOp::Less>>(
                vm,
                builtin_intrinsic_method(
                    L"__lt__",
                    native_list_order<OrderingOp::Less,
                                      L"list.__lt__ expects a list receiver">)),
            with_trusted_handler_resolver<
                ListOrderResolver<OrderingOp::LessEqual>>(
                vm,
                builtin_intrinsic_method(
                    L"__le__",
                    native_list_order<OrderingOp::LessEqual,
                                      L"list.__le__ expects a list receiver">)),
            with_trusted_handler_resolver<
                ListOrderResolver<OrderingOp::Greater>>(
                vm,
                builtin_intrinsic_method(
                    L"__gt__",
                    native_list_order<OrderingOp::Greater,
                                      L"list.__gt__ expects a list receiver">)),
            with_trusted_handler_resolver<
                ListOrderResolver<OrderingOp::GreaterEqual>>(
                vm,
                builtin_intrinsic_method(
                    L"__ge__",
                    native_list_order<OrderingOp::GreaterEqual,
                                      L"list.__ge__ expects a list receiver">)),
            builtin_intrinsic_method(L"__len__", native_list_len,
                                     L"Return len(self)."),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__getitem__", native_list_getitem,
                                         L"Return self[index]."),
                resolve_trusted_list_getitem_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__setitem__", native_list_setitem,
                                         L"Set self[index] to value."),
                resolve_trusted_list_setitem_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__delitem__", native_list_delitem,
                                         L"Delete self[index]."),
                resolve_trusted_list_delitem_handler),
            builtin_intrinsic_method(L"__iter__", native_list_iter,
                                     L"Implement iter(self)."),
            builtin_intrinsic_method(L"__add__", native_list_add,
                                     L"Return self+value."),
            builtin_intrinsic_method(L"append", native_list_append,
                                     L"Append object to the end of the list."),
            builtin_intrinsic_method(L"clear", native_list_clear,
                                     L"Remove all items from list."),
            builtin_intrinsic_method(L"copy", native_list_copy,
                                     L"Return a shallow copy of the list."),
            builtin_intrinsic_method(L"extend", native_list_extend,
                                     L"Extend list by appending items."),
            builtin_intrinsic_method(L"insert", native_list_insert,
                                     L"Insert object before index."),
            builtin_intrinsic_method(L"reverse", native_list_reverse,
                                     L"Reverse list in place."),
            with_defaults(builtin_intrinsic_method(L"pop", native_list_pop),
                          pop_defaults.value()),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->list_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

    void install_list_python_methods(VirtualMachine *vm,
                                     ModuleObject *implementation_module)
    {
        ClassObject *cls = vm->list_class();
        const wchar_t *method_names[] = {L"__init__", L"__eq__", L"__ne__",
                                         L"count",    L"index",  L"remove",
                                         L"sort"};
        const wchar_t *helper_names[] = {
            L"_clover_list_init",  L"_clover_list_eq",
            L"_clover_list_ne",    L"_clover_list_count",
            L"_clover_list_index", L"_clover_list_remove",
            L"_clover_list_sort"};
        bool installed = unwrap_bootstrap_expected(
            vm,
            implementation_module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_list_sort_impl"),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, native_list_sort),
                    "installing list sort implementation")
                    .raw_value()),
            "installing list sort binding");
        assert(installed);
        (void)installed;
        DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly) |
                                descriptor_flag(DescriptorFlag::StableSlot);
        for(size_t idx = 0; idx < std::size(method_names); ++idx)
        {
            TValue<String> helper_name =
                vm->get_or_create_interned_string_value(helper_names[idx]);
            Value function =
                implementation_module->get_own_property(helper_name);
            if(!can_convert_to<Function>(function))
            {
                fatal("trusted builtin_implementations.py did not define list "
                      "method");
            }
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(method_names[idx]),
                function, flags);
            assert(stored);
            (void)stored;
        }
    }

    TValue<List> List::copy() const
    {
        TValue<List> result = make_object_value<List>(size());
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->set_item_unchecked(idx, item_unchecked(idx));
        }
        return result;
    }

    TValue<Tuple> List::to_tuple(ThreadState *thread) const
    {
        TValue<Tuple> result = thread->make_object_value<Tuple>(size());
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->initialize_item_unchecked(idx,
                                                        item_unchecked(idx));
        }
        return result;
    }

    void List::extend_from_list(const List *other)
    {
        size_t original_size = other->size();
        for(size_t idx = 0; idx < original_size; ++idx)
        {
            append(other->item_unchecked(idx));
        }
    }

    void List::extend_from_tuple(const Tuple *other)
    {
        for(size_t idx = 0; idx < other->size(); ++idx)
        {
            append(other->item_unchecked(idx));
        }
    }

    void List::reverse()
    {
        size_t left = 0;
        size_t right = size();
        while(left < right)
        {
            --right;
            Value left_value = item_unchecked(left);
            Value right_value = item_unchecked(right);
            set_item_unchecked(left, right_value);
            set_item_unchecked(right, left_value);
            ++left;
        }
    }

    Value List::sort(ThreadState *thread, Value key,
                     TValue<Function> sort_items, bool reverse)
    {
        Owned<TValue<List>> self(TValue<List>::from_oop(this));
        Owned<Value> key_owner(key);
        Owned<TValue<Function>> sort_items_owner(sort_items);
        Owned<TValue<List>> original(thread->make_object_value<List>());
        items.swap(original.extract()->items);
        bool enclosing_mutated = sort_mutated_;
        sort_mutated_ = false;

        // Enter Python once for the whole generic algorithm. Its frame roots
        // keys and permutation temporaries; List retains the detached backing.
        Value result = thread->call_clovervm_function(
            sort_items, original.raw_value(), key,
            reverse ? Value::True() : Value::False());
        bool mutated = sort_mutated_;
        // Callback-created contents are discarded, with their backing left in
        // the temporary owned list for ordinary deferred reclamation.
        items.swap(original.extract()->items);
        sort_mutated_ = enclosing_mutated || mutated;
        if(result.is_exception_marker())
            return result;
        if(mutated)
            return thread->set_pending_builtin_exception_string(
                L"ValueError", L"list modified during sort");
        return Value::None();
    }

    TValue<List> List::concat(const List *other) const
    {
        TValue<List> result = make_object_value<List>(size() + other->size());
        size_t write_idx = 0;
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->set_item_unchecked(write_idx++,
                                                 item_unchecked(idx));
        }
        for(size_t idx = 0; idx < other->size(); ++idx)
        {
            result.extract()->set_item_unchecked(write_idx++,
                                                 other->item_unchecked(idx));
        }
        return result;
    }

    void List::insert_item_unchecked(size_t idx, Value value)
    {
        assert(idx <= size());
        value.assert_not_vm_sentinel();
        sort_mutated_ = true;

        size_t old_size = size();
        items.resize(old_size + 1, Value::None());
        for(size_t shift_idx = old_size; shift_idx > idx; --shift_idx)
        {
            items.set(shift_idx, items[shift_idx - 1]);
        }
        items.set(idx, value);
    }

    Value List::pop_item_unchecked(size_t idx)
    {
        assert(idx < size());
        sort_mutated_ = true;

        Value removed = items[idx];
        size_t old_size = size();
        for(size_t shift_idx = idx; shift_idx + 1 < old_size; ++shift_idx)
        {
            items.set(shift_idx, items[shift_idx + 1]);
        }
        items.resize(old_size - 1);
        return removed;
    }

    Value List::get_item(int64_t py_idx) const
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return item_unchecked(idx);
    }

    TValue<List> List::get_slice(const NormalizedNonstridedSlice &slice) const
    {
        TValue<List> result =
            make_object_value<List>(slice.selected_sequence_length);
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->set_item_unchecked(
                write_idx, item_unchecked(static_cast<size_t>(
                               slice.start + static_cast<int64_t>(write_idx))));
        }
        return result;
    }

    TValue<List> List::get_slice(const NormalizedGeneralSlice &slice) const
    {
        TValue<List> result =
            make_object_value<List>(slice.selected_sequence_length);
        int64_t read_idx = slice.start;
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->set_item_unchecked(
                write_idx, item_unchecked(static_cast<size_t>(read_idx)));
            read_idx += slice.step;
        }
        return result;
    }

    Value List::set_item(int64_t py_idx, Value value)
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        set_item_unchecked(idx, value);
        return Value::None();
    }

    void List::insert_item(int64_t py_idx, Value value)
    {
        insert_item_unchecked(normalize_insertion_index(py_idx), value);
    }

    Value List::pop_item(int64_t py_idx)
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return pop_item_unchecked(idx);
    }

    size_t List::wrap_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        return static_cast<size_t>(normalized);
    }

    Value List::check_index(size_t idx) const
    {
        if(idx >= size())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"IndexError", L"list index out of range");
        }
        return Value::None();
    }

    size_t List::normalize_insertion_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        normalized = std::max<int64_t>(0, normalized);
        normalized = std::min(normalized, n_items);
        return static_cast<size_t>(normalized);
    }

}  // namespace cl
