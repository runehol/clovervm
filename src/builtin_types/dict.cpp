#include "builtin_types/dict.h"
#include "builtin_types/dict_view.h"
#include "builtin_types/list.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "compiler/scope.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "runtime/exception_object.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cassert>
#include <iterator>

namespace cl
{

    static TValue<SMI> string_keyed_hash(TValue<String> key)
    {
        assert(is_exact_string(active_thread(), key.raw_value()));
        return string_hash_normalized(key);
    }

    static bool string_keyed_equal(TValue<String> a, TValue<String> b)
    {
        assert(is_exact_string(active_thread(), a.raw_value()));
        assert(is_exact_string(active_thread(), b.raw_value()));
        return string_eq(a, b);
    }

    static bool is_exact_dict_string_key_shape(ThreadState *thread,
                                               const Dict *dict)
    {
        return dict->get_shape() == thread->get_exact_dict_string_key_shape();
    }

    Dict::Dict(ClassObject *cls)
        : Object(cls, native_layout), hash_table(min_table_size, not_present),
          n_valid_entries(0), table_generation_(TValue<SMI>::from_smi(0))
    {
    }

    Dict::Dict(ClassObject *cls, const Dict &other)
        : Object(cls, native_layout), hash_table(min_table_size, not_present),
          n_valid_entries(0), table_generation_(TValue<SMI>::from_smi(0))
    {
        ThreadState *thread = active_thread();
        if(!is_exact_dict_string_key_shape(thread, &other))
        {
            promote_to_general_shape(thread);
        }
        for(const Entry &e: other.entries)
        {
            if(e.valid())
            {
                copy_stored_entry(e.key, e.value, e.hash);
            }
        }
    }

    TrustedDictBytecodeAccess::StringKeyedSetDefaultResult
    TrustedDictBytecodeAccess::try_string_keyed_setdefault(ThreadState *thread,
                                                           Dict *dict,
                                                           Value key,
                                                           Value default_value)
    {
        key.assert_not_vm_sentinel();
        default_value.assert_not_vm_sentinel();
        assert(is_exact_dict_string_key_shape(thread, dict));
        if(!is_exact_string(thread, key))
        {
            dict->promote_to_general_shape(thread);
            return {false, Value::None()};
        }

        TValue<String> string_key = TValue<String>::from_value_unchecked(key);
        int32_t entry_idx = *dict->find_entry(string_key);
        if(entry_idx >= 0)
        {
            assert(static_cast<size_t>(entry_idx) < dict->entries.size());
            assert(dict->entries[entry_idx].valid());
            return {true, dict->entries[entry_idx].value};
        }

        dict->string_keyed_insert(string_key, default_value);
        return {true, default_value};
    }

    TrustedDictBytecodeAccess::StringKeyedPopResult
    TrustedDictBytecodeAccess::try_string_keyed_pop(ThreadState *thread,
                                                    Dict *dict, Value key)
    {
        key.assert_not_vm_sentinel();
        assert(is_exact_dict_string_key_shape(thread, dict));
        if(!is_exact_string(thread, key))
        {
            dict->promote_to_general_shape(thread);
            return {PopGeneral, Value::None()};
        }

        int32_t *entry =
            dict->find_entry(TValue<String>::from_value_unchecked(key));
        int32_t entry_idx = *entry;
        if(entry_idx < 0)
        {
            return {PopStringMiss, Value::None()};
        }

        assert(static_cast<size_t>(entry_idx) < dict->entries.size());
        assert(dict->entries[entry_idx].valid());
        Owned<Value> result(dict->entries[entry_idx].value);
        dict->entries.set(entry_idx,
                          Dict::Entry(Value::not_present(), Value::None(),
                                      TValue<SMI>::from_smi(0)));
        *entry = Dict::tombstone;
        --dict->n_valid_entries;
        return {PopStringHit, result.value()};
    }

    BuiltinClassDefinition make_dict_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Dict};
        ClassObject *cls = ClassObject::make_builtin_class<Dict>(
            vm->get_or_create_interned_string_value(L"dict"), 0, nullptr, 0,
            vm->object_class(), SubclassPolicy::Allowed);
        Shape *string_key_shape = cls->get_instance_root_shape();
        Shape *general_shape =
            string_key_shape->clone_with_flags(string_key_shape->flags());
        vm->install_exact_dict_shapes(cls, string_key_shape, general_shape);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    [[nodiscard]] static Value native_dict_new(ThreadState *thread,
                                               Value cls_value)
    {
        TValue<ClassObject> cls =
            CL_TRY(TValue<ClassObject>::from_value_or_raise(
                cls_value, L"TypeError",
                L"dict.__new__ expects a dict subclass"));
        if(!is_subclass_of(cls.extract(),
                           thread->get_machine()->dict_class()) ||
           cls.extract()->instance_native_layout_id() != NativeLayoutId::Dict)
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__new__ expects a dict subclass");
        }
        Shape *root = cls.extract()->get_instance_root_shape();
        assert(root != nullptr);
        assert(cls.extract()->get_instance_inline_slot_capacity() ==
               root->get_inline_slot_capacity());
        assert(native_inline_slot_capacity_is_valid(
            NativeLayoutId::Dict, root->get_inline_slot_capacity()));
        (void)root;
        return thread->make_internal_value<Dict>(cls.extract()).raw_value();
    }

    static Value native_dict_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Dict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__repr__ expects a dict receiver");
        }

        Dict *dict = self.get_ptr<Dict>();
        StringBuilder builder;
        builder.append_char(L'{');
        bool first = true;
        for(Dict::EntryView entry: *dict)
        {
            if(!first)
            {
                builder.append_c_str(L", ");
            }
            first = false;
            CL_PROPAGATE_EXCEPTION(builder.append_repr(entry.key));
            builder.append_c_str(L": ");
            CL_PROPAGATE_EXCEPTION(builder.append_repr(entry.value));
        }
        builder.append_char(L'}');
        return builder.finish();
    }

    static Value native_dict_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Dict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.__len__ expects a dict receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<Dict>()->size()));
    }

    static Value require_dict_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<Dict>(self))
        {
            std::wstring message = L"dict.";
            message += method_name;
            message += L" expects a dict receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    static Value native_dict_clear(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"clear"));
        self.get_ptr<Dict>()->clear();
        return Value::None();
    }

    static Value native_dict_copy(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"copy"));
        return self.get_ptr<Dict>()->copy().raw_value();
    }

    static Value trusted_dict_getitem_str_handler(ThreadState *thread,
                                                  Value self, Value key)
    {
        return CL_TRY(self.get_ptr<Dict>()->get_item_for_str(
            thread, TValue<String>::from_value_unchecked(key)));
    }

    static Value trusted_dict_setitem_str_handler(ThreadState *thread,
                                                  Value self, Value key,
                                                  Value value)
    {
        CL_TRY(self.get_ptr<Dict>()->set_item_for_str(
            thread, TValue<String>::from_value_unchecked(key), value));
        return Value::None();
    }

    static Value trusted_dict_delitem_str_handler(ThreadState *thread,
                                                  Value self, Value key)
    {
        CL_TRY(self.get_ptr<Dict>()->del_item_for_str(
            thread, TValue<String>::from_value_unchecked(key)));
        return Value::None();
    }

    static Value trusted_dict_contains_str_handler(ThreadState *thread,
                                                   Value self, Value key)
    {
        return CL_TRY(self.get_ptr<Dict>()->contains_for_str(
                   thread, TValue<String>::from_value_unchecked(key)))
                   ? Value::True()
                   : Value::False();
    }

    static bool trusted_dict_str_key_shapes_match(VirtualMachine *vm,
                                                  ShapeKey container_key,
                                                  ShapeKey key_key)
    {
        return vm->shape_for_key(container_key) ==
                   vm->exact_dict_string_key_shape() &&
               vm->shape_for_key(key_key)->get_class() == vm->str_class();
    }

    static TrustedResolution resolve_trusted_dict_getitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_getitem_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_dict_contains_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_contains_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_dict_setitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Ternary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_setitem_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_dict_delitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(trusted_dict_str_key_shapes_match(vm, container_key, key_key))
        {
            return TrustedResolution::call_trusted(
                trusted_dict_delitem_str_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value native_dict_keys(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"keys"));
        return self.get_ptr<Dict>()->keys();
    }

    static Value native_dict_values(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"values"));
        return self.get_ptr<Dict>()->values();
    }

    static Value native_dict_items(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"items"));
        return self.get_ptr<Dict>()->items();
    }

    static Value native_dict_popitem(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"popitem"));
        return self.get_ptr<Dict>()->popitem();
    }

    static Value native_dict_update(ThreadState *thread, Value self,
                                    Value other)
    {
        CL_PROPAGATE_EXCEPTION(require_dict_receiver(self, L"update"));
        if(other == Value::None())
        {
            return Value::None();
        }
        if(!can_convert_to<Dict>(other))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"dict.update expects a dict argument");
        }

        CL_TRY(self.get_ptr<Dict>()->update_from_dict(thread,
                                                      other.get_ptr<Dict>()));
        return Value::None();
    }

    static Value native_dict_fromkeys(ThreadState *thread, Value keys,
                                      Value value)
    {
        if(can_convert_to<Tuple>(keys))
        {
            Tuple *tuple = keys.get_ptr<Tuple>();
            return Dict::from_tuple_keys(tuple, value);
        }
        if(can_convert_to<List>(keys))
        {
            List *list = keys.get_ptr<List>();
            return Dict::from_list_keys(list, value);
        }

        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"dict.fromkeys expects a tuple or list");
    }

    static TValue<Tuple> make_single_default(VirtualMachine *vm, Value value)
    {
        TValue<Tuple> defaults =
            vm->get_default_thread()->make_object_value<Tuple>(1);
        defaults.extract()->initialize_item_unchecked(0, value);
        return defaults;
    }

    enum class DictReadKind
    {
        GetItem,
        Get,
        Contains,
    };

    enum class DictInsertKind
    {
        SetItem,
        SetDefault,
    };

    enum class DictDeleteKind
    {
        DelItem,
        Pop,
    };

    static Value dict_getitem_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.__getitem__ expects a dict receiver");
    }

    static Value dict_get_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.get expects a dict receiver");
    }

    static Value dict_contains_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.__contains__ expects a dict receiver");
    }

    static Value dict_setitem_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.__setitem__ expects a dict receiver");
    }

    static Value dict_setdefault_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.setdefault expects a dict receiver");
    }

    static Value dict_delitem_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.__delitem__ expects a dict receiver");
    }

    static Value dict_pop_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.pop expects a dict receiver");
    }

    [[nodiscard]] static Value dict_missing_key_error(ThreadState *thread,
                                                      Value key)
    {
        Owned<Value> live_key(key);
        return thread->set_pending_exception_object(
            make_exception_object_with_arg(
                thread,
                TValue<ClassObject>::from_oop(
                    thread->class_for_builtin_name(L"KeyError")),
                live_key.value()));
    }

    static Expected<void> emit_native_error(CodeObjectBuilder &code,
                                            uint8_t target_idx)
    {
        CL_TRY(
            code.emit_call_intrinsic(0, Bytecode::CallIntrinsic0, target_idx));
        CL_TRY(code.emit_return_or_raise_exception(0));
        return Expected<void>::ok();
    }

    static Value dict_method_missing_key_error(ThreadState *thread, Value,
                                               Value key)
    {
        return dict_missing_key_error(thread, key);
    }

    static Expected<void> emit_missing_key_error(CodeObjectBuilder &code,
                                                 uint8_t target_idx)
    {
        CL_TRY(
            code.emit_call_intrinsic(0, Bytecode::CallIntrinsic2, target_idx));
        CL_TRY(code.emit_return_or_raise_exception(0));
        return Expected<void>::ok();
    }

    static Value dict_equal_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.__eq__ expects a dict receiver");
    }

    static Value dict_not_equal_receiver_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"dict.__ne__ expects a dict receiver");
    }

    static Value dict_sizes_match(ThreadState *, Value self, Value other)
    {
        assert(can_convert_to<Dict>(self) && can_convert_to<Dict>(other));
        return self.get_ptr<Dict>()->size() == other.get_ptr<Dict>()->size()
                   ? Value::True()
                   : Value::False();
    }

    static Expected<TValue<Function>>
    make_dict_equal_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), scope,
            vm->get_or_create_interned_string_value(L"<dict.__eq__>"));
        code.configure_positional_function(2);
        code.function_signature().n_pos_or_kw_parameters = 0;
        uint8_t dict_class =
            CL_TRY(code.allocate_constant(Value::from_oop(vm->dict_class())));
        uint8_t not_implemented =
            CL_TRY(code.allocate_constant(Value::NotImplemented()));
        NativeFunctionTarget receiver_error;
        receiver_error.fixed0 = dict_equal_receiver_error;
        uint8_t receiver_error_idx =
            CL_TRY(code.add_native_function_target(receiver_error));
        NativeFunctionTarget size_target;
        size_target.fixed2 = dict_sizes_match;
        uint8_t size_idx = CL_TRY(code.add_native_function_target(size_target));
        {
            CodeObjectBuilder::TemporaryReg temps(code, 10);
            uint32_t index = temps;
            uint32_t entry = temps + 1;  // key, value, stored hash
            uint32_t value = entry + 1;
            uint32_t hash = entry + 2;
            uint32_t generation = temps + 4;
            uint32_t hash_idx = temps + 5;
            uint32_t probe_result = temps + 6;
            uint32_t candidate_key = temps + 7;
            uint32_t equal = temps + 8;
            uint32_t right_value = temps + 9;
            JumpTarget receiver_ok(&code), unsupported(&code), loop(&code),
                restart(&code), probe(&code), advance(&code), hit(&code),
                false_result(&code), true_result(&code);
            CL_TRY(code.emit_ldar(0, 0));
            CL_TRY(code.emit_is_instance_of_known_class(0, dict_class));
            CL_TRY(code.emit_jump_if_true(0, receiver_ok));
            CL_TRY(emit_native_error(code, receiver_error_idx));
            CL_TRY(receiver_ok.resolve());
            CL_TRY(code.emit_ldar(0, 1));
            CL_TRY(code.emit_is_instance_of_known_class(0, dict_class));
            CL_TRY(code.emit_jump_if_false(0, unsupported));
            CL_TRY(code.emit_call_intrinsic(0, Bytecode::CallIntrinsic2,
                                            size_idx));
            CL_TRY(code.emit_jump_if_false(0, false_result));
            CL_TRY(code.emit_lda_smi(0, 0));
            CL_TRY(code.emit_star(0, index));
            CL_TRY(loop.resolve());
            // Each step re-reads storage after callbacks. Frame slots keep the
            // current key and both values alive through key/value comparisons.
            CL_TRY(code.emit_dict_next_entry(0, 0, index, entry));
            CL_TRY(code.emit_jump_if_false(0, true_result));
            CL_TRY(restart.resolve());
            CL_TRY(code.emit_ldar(0, hash));
            CL_TRY(code.emit_dict_probe_start(0, 1, generation, hash_idx));
            CL_TRY(probe.resolve());
            CL_TRY(code.emit_ldar(0, hash));
            CL_TRY(code.emit_dict_probe_for_lookup(0, 1, hash_idx));
            CL_TRY(code.emit_jump_if_equal_smi(
                0, TrustedDictBytecodeAccess::ProbeMiss, false_result));
            CL_TRY(code.emit_jump_if_equal_smi(
                0, TrustedDictBytecodeAccess::ProbeContinue, advance));
            CL_TRY(code.emit_star(0, probe_result));
            CL_TRY(code.emit_dict_entry_key(0, 1));
            CL_TRY(code.emit_star(0, candidate_key));
            CL_TRY(code.emit_ldar(0, entry));
            CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs, candidate_key,
                                          OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_jump_if_true(0, hit));
            CL_TRY(code.emit_ldar(0, entry));
            CL_TRY(code.emit_operator_reg(
                0, Bytecode::TestEqual, candidate_key,
                OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
            CL_TRY(code.emit_to_bool(0));
            CL_TRY(code.emit_star(0, equal));
            CL_TRY(code.emit_dict_entry_still_matches(
                0, 1, generation, hash_idx, probe_result, candidate_key));
            CL_TRY(code.emit_jump_if_false(0, restart));
            CL_TRY(code.emit_ldar(0, equal));
            CL_TRY(code.emit_jump_if_true(0, hit));
            CL_TRY(advance.resolve());
            CL_TRY(code.emit_ldar(0, hash_idx));
            CL_TRY(code.emit_dict_probe_advance(0, 1));
            CL_TRY(code.emit_star(0, hash_idx));
            CL_TRY(code.emit_jump(0, probe));
            CL_TRY(hit.resolve());
            CL_TRY(code.emit_ldar(0, probe_result));
            CL_TRY(code.emit_dict_entry_value(0, 1));
            CL_TRY(code.emit_star(0, right_value));
            CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs, value,
                                          OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_jump_if_true(0, loop));
            CL_TRY(code.emit_ldar(0, right_value));
            CL_TRY(code.emit_operator_reg(
                0, Bytecode::TestEqual, value,
                OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
            CL_TRY(code.emit_to_bool(0));
            CL_TRY(code.emit_jump_if_false(0, false_result));
            CL_TRY(code.emit_jump(0, loop));
            CL_TRY(true_result.resolve());
            CL_TRY(code.emit_lda_true(0));
            CL_TRY(code.emit_return(0));
            CL_TRY(false_result.resolve());
            CL_TRY(code.emit_lda_false(0));
            CL_TRY(code.emit_return(0));
            CL_TRY(unsupported.resolve());
            CL_TRY(code.emit_lda_constant(0, not_implemented));
            CL_TRY(code.emit_return(0));
        }
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::none()));
    }

    static Expected<TValue<Function>>
    make_dict_not_equal_function(VirtualMachine *vm, TValue<Function> equal)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), scope,
            vm->get_or_create_interned_string_value(L"<dict.__ne__>"));
        code.configure_positional_function(2);
        code.function_signature().n_pos_or_kw_parameters = 0;
        uint8_t dict_class =
            CL_TRY(code.allocate_constant(Value::from_oop(vm->dict_class())));
        uint8_t equal_code = CL_TRY(
            code.allocate_constant(equal.extract()->code_object.raw_value()));
        uint8_t not_implemented =
            CL_TRY(code.allocate_constant(Value::NotImplemented()));
        NativeFunctionTarget receiver_error;
        receiver_error.fixed0 = dict_not_equal_receiver_error;
        uint8_t receiver_error_idx =
            CL_TRY(code.add_native_function_target(receiver_error));
        JumpTarget receiver_ok(&code);
        CL_TRY(code.emit_ldar(0, 0));
        CL_TRY(code.emit_is_instance_of_known_class(0, dict_class));
        CL_TRY(code.emit_jump_if_true(0, receiver_ok));
        CL_TRY(emit_native_error(code, receiver_error_idx));
        CL_TRY(receiver_ok.resolve());
        {
            CodeObjectBuilder::TemporaryReg args(code, 2,
                                                 RegisterAlignment::CallFrame);
            CL_TRY(code.emit_mov(0, args, 0));
            CL_TRY(code.emit_mov(0, args + 1, 1));
            CL_TRY(code.emit_call_code_object(0, equal_code, args, 2));
        }
        // The equality result is a bool or NotImplemented. Check the singleton
        // with a separate temporary, then invert only the bool result.
        {
            CodeObjectBuilder::TemporaryReg result(code);
            CL_TRY(code.emit_star(0, result));
            CL_TRY(code.emit_lda_constant(0, not_implemented));
            CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs, result,
                                          OperatorBytecodeFormat::Plain));
            JumpTarget invert(&code);
            CL_TRY(code.emit_jump_if_false(0, invert));
            CL_TRY(code.emit_ldar(0, result));
            CL_TRY(code.emit_return(0));
            CL_TRY(invert.resolve());
            CL_TRY(code.emit_ldar(0, result));
            CL_TRY(code.emit_to_bool_not(0));
            CL_TRY(code.emit_return(0));
        }
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::none()));
    }

    static Expected<TValue<Function>> make_dict_read_function(
        VirtualMachine *vm, DictReadKind kind, ClassObject *type_error_class,
        TrustedHandlerResolver trusted_handler_resolver = nullptr)
    {
        const wchar_t *name_text;
        IntrinsicFunction0 receiver_error_function;
        uint32_t n_parameters;
        switch(kind)
        {
            case DictReadKind::GetItem:
                name_text = L"<dict.__getitem__>";
                receiver_error_function = dict_getitem_receiver_error;
                n_parameters = 2;
                break;
            case DictReadKind::Get:
                name_text = L"<dict.get>";
                receiver_error_function = dict_get_receiver_error;
                n_parameters = 3;
                break;
            case DictReadKind::Contains:
                name_text = L"<dict.__contains__>";
                receiver_error_function = dict_contains_receiver_error;
                n_parameters = 2;
                break;
        }

        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), local_scope,
            vm->get_or_create_interned_string_value(name_text));
        code.configure_positional_function(n_parameters);
        if(kind == DictReadKind::Get)
        {
            code.function_signature().first_default_slot = 2;
            code.function_signature().default_presence_mask = 1;
        }

        uint8_t dict_class_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(vm->dict_class())));
        uint8_t hash_name_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"__hash__")));
        uint8_t type_error_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(type_error_class)));
        uint8_t unhashable_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"object is unhashable")));
        NativeFunctionTarget receiver_error_target;
        receiver_error_target.fixed0 = receiver_error_function;
        uint8_t receiver_error_target_idx =
            CL_TRY(code.add_native_function_target(receiver_error_target));
        NativeFunctionTarget key_error_target;
        key_error_target.fixed2 = dict_method_missing_key_error;
        uint8_t key_error_target_idx =
            CL_TRY(code.add_native_function_target(key_error_target));

        {
            CodeObjectBuilder::TemporaryReg temporaries(code, 6);
            uint32_t hash_reg = temporaries;
            uint32_t generation_reg = temporaries + 1;
            uint32_t hash_idx_reg = temporaries + 2;
            uint32_t probe_result_reg = temporaries + 3;
            uint32_t candidate_key_reg = temporaries + 4;
            uint32_t equality_reg = temporaries + 5;

            JumpTarget receiver_ok(&code);
            JumpTarget restart_probe(&code);
            JumpTarget probe_loop(&code);
            JumpTarget advance_probe(&code);
            JumpTarget general_hit(&code);
            JumpTarget miss(&code);

            CL_TRY(code.emit_ldar(0, 0));
            CL_TRY(code.emit_is_instance_of_known_class(0, dict_class_idx));
            CL_TRY(code.emit_jump_if_true(0, receiver_ok));
            CL_TRY(emit_native_error(code, receiver_error_target_idx));
            CL_TRY(receiver_ok.resolve());

            CL_TRY(code.emit_dict_promote_string_keyed(0, 0));
            {
                CodeObjectBuilder::TemporaryReg call_args(
                    code, 1, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, call_args, 1));
                CL_TRY(code.emit_call_special_method(
                    0, call_args, hash_name_idx, 0, type_error_idx,
                    unhashable_idx));
            }
            CL_TRY(code.emit_unary_op(0, Bytecode::CanonicalizeHash,
                                      OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_star(0, hash_reg));

            CL_TRY(restart_probe.resolve());
            CL_TRY(code.emit_ldar(0, hash_reg));
            CL_TRY(
                code.emit_dict_probe_start(0, 0, generation_reg, hash_idx_reg));

            CL_TRY(probe_loop.resolve());
            CL_TRY(code.emit_ldar(0, hash_reg));
            CL_TRY(code.emit_dict_probe_for_lookup(0, 0, hash_idx_reg));
            CL_TRY(code.emit_jump_if_equal_smi(
                0, TrustedDictBytecodeAccess::ProbeMiss, miss));
            CL_TRY(code.emit_jump_if_equal_smi(
                0, TrustedDictBytecodeAccess::ProbeContinue, advance_probe));

            CL_TRY(code.emit_star(0, probe_result_reg));
            CL_TRY(code.emit_dict_entry_key(0, 0));
            CL_TRY(code.emit_star(0, candidate_key_reg));
            CL_TRY(code.emit_ldar(0, 1));
            CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs,
                                          candidate_key_reg,
                                          OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_jump_if_true(0, general_hit));

            CL_TRY(code.emit_ldar(0, 1));
            CL_TRY(code.emit_operator_reg(
                0, Bytecode::TestEqual, candidate_key_reg,
                OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
            CL_TRY(code.emit_to_bool(0));
            CL_TRY(code.emit_star(0, equality_reg));
            CL_TRY(code.emit_dict_entry_still_matches(
                0, 0, generation_reg, hash_idx_reg, probe_result_reg,
                candidate_key_reg));
            CL_TRY(code.emit_jump_if_false(0, restart_probe));
            CL_TRY(code.emit_ldar(0, equality_reg));
            CL_TRY(code.emit_jump_if_true(0, general_hit));

            CL_TRY(advance_probe.resolve());
            CL_TRY(code.emit_ldar(0, hash_idx_reg));
            CL_TRY(code.emit_dict_probe_advance(0, 0));
            CL_TRY(code.emit_star(0, hash_idx_reg));
            CL_TRY(code.emit_jump(0, probe_loop));

            CL_TRY(general_hit.resolve());
            if(kind == DictReadKind::Contains)
            {
                CL_TRY(code.emit_lda_true(0));
            }
            else
            {
                CL_TRY(code.emit_ldar(0, probe_result_reg));
                CL_TRY(code.emit_dict_entry_value(0, 0));
            }
            CL_TRY(code.emit_return(0));

            CL_TRY(miss.resolve());
            if(kind == DictReadKind::Contains)
            {
                CL_TRY(code.emit_lda_false(0));
                CL_TRY(code.emit_return(0));
            }
            else if(kind == DictReadKind::Get)
            {
                CL_TRY(code.emit_ldar(0, 2));
                CL_TRY(code.emit_return(0));
            }
            else
            {
                CL_TRY(emit_missing_key_error(code, key_error_target_idx));
            }
        }

        TValue<CodeObject> code_object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        code_object.extract()->trusted_handler_resolver =
            trusted_handler_resolver;
        Optional<TValue<Tuple>> defaults = Optional<TValue<Tuple>>::none();
        if(kind == DictReadKind::Get)
        {
            defaults = Optional<TValue<Tuple>>::some(
                make_single_default(vm, Value::None()));
        }
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                code_object, Optional<TValue<String>>::none(), defaults));
    }

    // Emits a restartable probe. The caller owns the found/missing actions.
    static Expected<void>
    emit_dict_insert_probe(CodeObjectBuilder &code, uint32_t receiver_reg,
                           uint32_t key_reg, uint32_t hash_reg,
                           uint32_t scratch_reg, JumpTarget &insert_new,
                           JumpTarget &hit)
    {
        uint32_t generation_reg = scratch_reg;
        uint32_t hash_idx_reg = scratch_reg + 1;
        uint32_t probe_result_reg = scratch_reg + 2;
        uint32_t candidate_key_reg = scratch_reg + 3;
        uint32_t equality_reg = scratch_reg + 4;
        uint32_t first_tombstone_idx_reg = scratch_reg + 5;
        JumpTarget restart_probe(&code), probe_loop(&code), tombstone(&code),
            record_tombstone(&code), advance_probe(&code);
        CL_TRY(restart_probe.resolve());
        CL_TRY(code.emit_dict_resize_for_insert(0, receiver_reg));
        CL_TRY(code.emit_lda_smi(0, -1));
        CL_TRY(code.emit_star(0, first_tombstone_idx_reg));
        CL_TRY(code.emit_ldar(0, hash_reg));
        CL_TRY(code.emit_dict_probe_start(0, receiver_reg, generation_reg,
                                          hash_idx_reg));

        CL_TRY(probe_loop.resolve());
        CL_TRY(code.emit_ldar(0, hash_reg));
        CL_TRY(code.emit_dict_probe_for_insert(0, receiver_reg, hash_idx_reg));
        CL_TRY(code.emit_jump_if_equal_smi(
            0, TrustedDictBytecodeAccess::InsertProbeEmpty, insert_new));
        CL_TRY(code.emit_jump_if_equal_smi(
            0, TrustedDictBytecodeAccess::InsertProbeTombstone, tombstone));
        CL_TRY(code.emit_jump_if_equal_smi(
            0, TrustedDictBytecodeAccess::InsertProbeHashMiss, advance_probe));

        CL_TRY(code.emit_star(0, probe_result_reg));
        CL_TRY(code.emit_dict_entry_key(0, receiver_reg));
        CL_TRY(code.emit_star(0, candidate_key_reg));
        CL_TRY(code.emit_ldar(0, key_reg));
        CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs, candidate_key_reg,
                                      OperatorBytecodeFormat::Plain));
        CL_TRY(code.emit_jump_if_true(0, hit));

        CL_TRY(code.emit_ldar(0, key_reg));
        CL_TRY(code.emit_operator_reg(
            0, Bytecode::TestEqual, candidate_key_reg,
            OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
        CL_TRY(code.emit_to_bool(0));
        CL_TRY(code.emit_star(0, equality_reg));
        CL_TRY(code.emit_dict_entry_still_matches(
            0, receiver_reg, generation_reg, hash_idx_reg, probe_result_reg,
            candidate_key_reg));
        CL_TRY(code.emit_jump_if_false(0, restart_probe));
        CL_TRY(code.emit_ldar(0, equality_reg));
        CL_TRY(code.emit_jump_if_true(0, hit));
        CL_TRY(code.emit_jump(0, advance_probe));

        CL_TRY(tombstone.resolve());
        CL_TRY(code.emit_ldar(0, first_tombstone_idx_reg));
        CL_TRY(code.emit_jump_if_equal_smi(0, -1, record_tombstone));
        CL_TRY(code.emit_jump(0, advance_probe));

        CL_TRY(record_tombstone.resolve());
        CL_TRY(code.emit_ldar(0, hash_idx_reg));
        CL_TRY(code.emit_star(0, first_tombstone_idx_reg));

        CL_TRY(advance_probe.resolve());
        CL_TRY(code.emit_ldar(0, hash_idx_reg));
        CL_TRY(code.emit_dict_probe_advance(0, receiver_reg));
        CL_TRY(code.emit_star(0, hash_idx_reg));
        CL_TRY(code.emit_jump(0, probe_loop));

        return Expected<void>::ok();
    }

    static Expected<TValue<Function>> make_dict_insert_function(
        VirtualMachine *vm, DictInsertKind kind, ClassObject *type_error_class,
        TrustedHandlerResolver trusted_handler_resolver = nullptr)
    {
        const wchar_t *name_text;
        IntrinsicFunction0 receiver_error_function;
        switch(kind)
        {
            case DictInsertKind::SetItem:
                name_text = L"<dict.__setitem__>";
                receiver_error_function = dict_setitem_receiver_error;
                break;
            case DictInsertKind::SetDefault:
                name_text = L"<dict.setdefault>";
                receiver_error_function = dict_setdefault_receiver_error;
                break;
        }

        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), local_scope,
            vm->get_or_create_interned_string_value(name_text));
        code.configure_positional_function(3);
        if(kind == DictInsertKind::SetDefault)
        {
            code.function_signature().first_default_slot = 2;
            code.function_signature().default_presence_mask = 1;
        }

        uint8_t dict_class_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(vm->dict_class())));
        uint8_t hash_name_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"__hash__")));
        uint8_t type_error_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(type_error_class)));
        uint8_t unhashable_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"object is unhashable")));
        NativeFunctionTarget receiver_error_target;
        receiver_error_target.fixed0 = receiver_error_function;
        uint8_t receiver_error_target_idx =
            CL_TRY(code.add_native_function_target(receiver_error_target));

        {
            uint32_t temporary_count =
                kind == DictInsertKind::SetDefault ? 8 : 7;
            CodeObjectBuilder::TemporaryReg temporaries(code, temporary_count);
            uint32_t hash_reg = temporaries;
            uint32_t hash_idx_reg = temporaries + 2;
            uint32_t probe_result_reg = temporaries + 3;
            uint32_t first_tombstone_idx_reg = temporaries + 6;
            uint32_t string_result_reg = kind == DictInsertKind::SetDefault
                                             ? temporaries + 7
                                             : temporaries;

            JumpTarget receiver_ok(&code);
            JumpTarget insert_new(&code);
            JumpTarget hit(&code);

            CL_TRY(code.emit_ldar(0, 0));
            CL_TRY(code.emit_is_instance_of_known_class(0, dict_class_idx));
            CL_TRY(code.emit_jump_if_true(0, receiver_ok));
            CL_TRY(emit_native_error(code, receiver_error_target_idx));
            CL_TRY(receiver_ok.resolve());

            if(kind == DictInsertKind::SetDefault)
            {
                JumpTarget general_path(&code);
                CL_TRY(code.emit_dict_try_string_keyed_setdefault(
                    0, 0, 1, 2, string_result_reg));
                CL_TRY(code.emit_jump_if_false(0, general_path));
                CL_TRY(code.emit_ldar(0, string_result_reg));
                CL_TRY(code.emit_return(0));
                CL_TRY(general_path.resolve());
            }
            else
            {
                CL_TRY(code.emit_dict_promote_string_keyed(0, 0));
            }

            {
                CodeObjectBuilder::TemporaryReg call_args(
                    code, 1, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, call_args, 1));
                CL_TRY(code.emit_call_special_method(
                    0, call_args, hash_name_idx, 0, type_error_idx,
                    unhashable_idx));
            }
            CL_TRY(code.emit_unary_op(0, Bytecode::CanonicalizeHash,
                                      OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_star(0, hash_reg));

            CL_TRY(emit_dict_insert_probe(code, 0, 1, hash_reg, temporaries + 1,
                                          insert_new, hit));

            CL_TRY(insert_new.resolve());
            CL_TRY(code.emit_dict_insert_new(
                0, 0, hash_idx_reg, first_tombstone_idx_reg, hash_reg, 1, 2));
            if(kind == DictInsertKind::SetDefault)
            {
                CL_TRY(code.emit_ldar(0, 2));
            }
            CL_TRY(code.emit_return(0));

            CL_TRY(hit.resolve());
            if(kind == DictInsertKind::SetItem)
            {
                CL_TRY(
                    code.emit_dict_overwrite_entry(0, 0, probe_result_reg, 2));
            }
            else
            {
                CL_TRY(code.emit_ldar(0, probe_result_reg));
                CL_TRY(code.emit_dict_entry_value(0, 0));
            }
            CL_TRY(code.emit_return(0));
        }

        TValue<CodeObject> code_object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        code_object.extract()->trusted_handler_resolver =
            trusted_handler_resolver;
        Optional<TValue<Tuple>> defaults = Optional<TValue<Tuple>>::none();
        if(kind == DictInsertKind::SetDefault)
        {
            defaults = Optional<TValue<Tuple>>::some(
                make_single_default(vm, Value::None()));
        }
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                code_object, Optional<TValue<String>>::none(), defaults));
    }

    static Expected<TValue<Function>> make_dict_delete_function(
        VirtualMachine *vm, DictDeleteKind kind, ClassObject *type_error_class,
        Value pop_missing_sentinel,
        TrustedHandlerResolver trusted_handler_resolver = nullptr)
    {
        const wchar_t *name_text = kind == DictDeleteKind::DelItem
                                       ? L"<dict.__delitem__>"
                                       : L"<dict.pop>";
        IntrinsicFunction0 receiver_error_function =
            kind == DictDeleteKind::DelItem ? dict_delitem_receiver_error
                                            : dict_pop_receiver_error;
        Scope *local_scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), local_scope,
            vm->get_or_create_interned_string_value(name_text));
        code.configure_positional_function(kind == DictDeleteKind::DelItem ? 2
                                                                           : 3);
        if(kind == DictDeleteKind::Pop)
        {
            code.function_signature().first_default_slot = 2;
            code.function_signature().default_presence_mask = 1;
        }

        uint8_t dict_class_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(vm->dict_class())));
        uint8_t hash_name_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"__hash__")));
        uint8_t type_error_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(type_error_class)));
        uint8_t unhashable_idx = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"object is unhashable")));
        uint8_t pop_missing_idx = 0;
        if(kind == DictDeleteKind::Pop)
        {
            pop_missing_idx =
                CL_TRY(code.allocate_constant(pop_missing_sentinel));
        }
        NativeFunctionTarget receiver_error_target;
        receiver_error_target.fixed0 = receiver_error_function;
        uint8_t receiver_error_target_idx =
            CL_TRY(code.add_native_function_target(receiver_error_target));
        NativeFunctionTarget key_error_target;
        key_error_target.fixed2 = dict_method_missing_key_error;
        uint8_t key_error_target_idx =
            CL_TRY(code.add_native_function_target(key_error_target));

        {
            uint32_t temporary_count = kind == DictDeleteKind::DelItem ? 6 : 8;
            CodeObjectBuilder::TemporaryReg temporaries(code, temporary_count);
            uint32_t hash_reg = temporaries;
            uint32_t generation_reg = temporaries + 1;
            uint32_t hash_idx_reg = temporaries + 2;
            uint32_t probe_result_reg = temporaries + 3;
            uint32_t candidate_key_reg = temporaries + 4;
            uint32_t equality_reg = temporaries + 5;
            uint32_t pop_result_reg =
                kind == DictDeleteKind::Pop ? temporaries + 6 : temporaries;
            uint32_t pop_missing_reg =
                kind == DictDeleteKind::Pop ? temporaries + 7 : temporaries;

            JumpTarget receiver_ok(&code);
            JumpTarget restart_probe(&code);
            JumpTarget probe_loop(&code);
            JumpTarget advance_probe(&code);
            JumpTarget hit(&code);
            JumpTarget miss(&code);
            JumpTarget string_hit(&code);

            CL_TRY(code.emit_ldar(0, 0));
            CL_TRY(code.emit_is_instance_of_known_class(0, dict_class_idx));
            CL_TRY(code.emit_jump_if_true(0, receiver_ok));
            CL_TRY(emit_native_error(code, receiver_error_target_idx));
            CL_TRY(receiver_ok.resolve());

            if(kind == DictDeleteKind::Pop)
            {
                CL_TRY(code.emit_dict_try_string_keyed_pop(0, 0, 1,
                                                           pop_result_reg));
                CL_TRY(code.emit_jump_if_equal_smi(
                    0, TrustedDictBytecodeAccess::PopStringHit, string_hit));
                CL_TRY(code.emit_jump_if_equal_smi(
                    0, TrustedDictBytecodeAccess::PopStringMiss, miss));
            }
            else
            {
                CL_TRY(code.emit_dict_promote_string_keyed(0, 0));
            }

            {
                CodeObjectBuilder::TemporaryReg call_args(
                    code, 1, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, call_args, 1));
                CL_TRY(code.emit_call_special_method(
                    0, call_args, hash_name_idx, 0, type_error_idx,
                    unhashable_idx));
            }
            CL_TRY(code.emit_unary_op(0, Bytecode::CanonicalizeHash,
                                      OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_star(0, hash_reg));

            CL_TRY(restart_probe.resolve());
            CL_TRY(code.emit_ldar(0, hash_reg));
            CL_TRY(
                code.emit_dict_probe_start(0, 0, generation_reg, hash_idx_reg));

            CL_TRY(probe_loop.resolve());
            CL_TRY(code.emit_ldar(0, hash_reg));
            CL_TRY(code.emit_dict_probe_for_lookup(0, 0, hash_idx_reg));
            CL_TRY(code.emit_jump_if_equal_smi(
                0, TrustedDictBytecodeAccess::ProbeMiss, miss));
            CL_TRY(code.emit_jump_if_equal_smi(
                0, TrustedDictBytecodeAccess::ProbeContinue, advance_probe));

            CL_TRY(code.emit_star(0, probe_result_reg));
            CL_TRY(code.emit_dict_entry_key(0, 0));
            CL_TRY(code.emit_star(0, candidate_key_reg));
            CL_TRY(code.emit_ldar(0, 1));
            CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs,
                                          candidate_key_reg,
                                          OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_jump_if_true(0, hit));

            CL_TRY(code.emit_ldar(0, 1));
            CL_TRY(code.emit_operator_reg(
                0, Bytecode::TestEqual, candidate_key_reg,
                OperatorBytecodeFormat::WithCacheAndNotImplementedCheck));
            CL_TRY(code.emit_to_bool(0));
            CL_TRY(code.emit_star(0, equality_reg));
            CL_TRY(code.emit_dict_entry_still_matches(
                0, 0, generation_reg, hash_idx_reg, probe_result_reg,
                candidate_key_reg));
            CL_TRY(code.emit_jump_if_false(0, restart_probe));
            CL_TRY(code.emit_ldar(0, equality_reg));
            CL_TRY(code.emit_jump_if_true(0, hit));

            CL_TRY(advance_probe.resolve());
            CL_TRY(code.emit_ldar(0, hash_idx_reg));
            CL_TRY(code.emit_dict_probe_advance(0, 0));
            CL_TRY(code.emit_star(0, hash_idx_reg));
            CL_TRY(code.emit_jump(0, probe_loop));

            CL_TRY(hit.resolve());
            if(kind == DictDeleteKind::Pop)
            {
                CL_TRY(code.emit_ldar(0, probe_result_reg));
                CL_TRY(code.emit_dict_entry_value(0, 0));
                CL_TRY(code.emit_star(0, pop_result_reg));
            }
            CL_TRY(code.emit_dict_delete_entry(0, 0, hash_idx_reg));
            if(kind == DictDeleteKind::Pop)
            {
                CL_TRY(code.emit_ldar(0, pop_result_reg));
            }
            CL_TRY(code.emit_return(0));

            CL_TRY(string_hit.resolve());
            if(kind == DictDeleteKind::Pop)
            {
                CL_TRY(code.emit_ldar(0, pop_result_reg));
                CL_TRY(code.emit_return(0));
            }

            CL_TRY(miss.resolve());
            if(kind == DictDeleteKind::Pop)
            {
                JumpTarget key_error(&code);
                CL_TRY(code.emit_lda_constant(0, pop_missing_idx));
                CL_TRY(code.emit_star(0, pop_missing_reg));
                CL_TRY(code.emit_ldar(0, 2));
                CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs,
                                              pop_missing_reg,
                                              OperatorBytecodeFormat::Plain));
                CL_TRY(code.emit_jump_if_true(0, key_error));
                CL_TRY(code.emit_ldar(0, 2));
                CL_TRY(code.emit_return(0));
                CL_TRY(key_error.resolve());
            }
            CL_TRY(emit_missing_key_error(code, key_error_target_idx));
        }

        TValue<CodeObject> code_object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        code_object.extract()->trusted_handler_resolver =
            trusted_handler_resolver;
        Optional<TValue<Tuple>> defaults = Optional<TValue<Tuple>>::none();
        if(kind == DictDeleteKind::Pop)
        {
            defaults = Optional<TValue<Tuple>>::some(
                make_single_default(vm, pop_missing_sentinel));
        }
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                code_object, Optional<TValue<String>>::none(), defaults));
    }

    Expected<TValue<Function>>
    make_dict_insert_if_absent_known_hash_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(), scope,
                               vm->get_or_create_interned_string_value(
                                   L"<dict.insert_if_absent_known_hash>"));
        // destination, key, value, canonical hash
        code.configure_positional_function(4);
        {
            CodeObjectBuilder::TemporaryReg scratch(code, 7);
            uint32_t hash_idx = scratch + 1;
            uint32_t first_tombstone = scratch + 5;
            uint32_t string_result = scratch + 6;
            JumpTarget general(&code), missing(&code), found(&code);
            CL_TRY(code.emit_dict_try_string_keyed_insert_if_absent(
                0, 0, 1, 2, string_result));
            CL_TRY(code.emit_jump_if_false(0, general));
            CL_TRY(code.emit_ldar(0, string_result));
            CL_TRY(code.emit_return(0));
            CL_TRY(general.resolve());
            CL_TRY(
                emit_dict_insert_probe(code, 0, 1, 3, scratch, missing, found));
            CL_TRY(missing.resolve());
            CL_TRY(code.emit_dict_insert_new(0, 0, hash_idx, first_tombstone, 3,
                                             1, 2));
            CL_TRY(code.emit_lda_true(0));
            CL_TRY(code.emit_return(0));
            CL_TRY(found.resolve());
            CL_TRY(code.emit_lda_false(0));
            CL_TRY(code.emit_return(0));
        }
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::none()));
    }

    static std::wstring call_keyword_callable_name(ThreadState *thread,
                                                   Value callable)
    {
        TValue<String> name = thread->class_of_value(callable)->get_name();
        if(can_convert_to<Function>(callable))
        {
            name = callable.get_ptr<Function>()
                       ->code_object.extract()
                       ->name.value();
        }
        else if(can_convert_to<ClassObject>(callable))
        {
            name = callable.get_ptr<ClassObject>()->get_name();
        }
        return std::wstring(name.extract()->data,
                            name.extract()->count.extract());
    }

    Value raise_call_keyword_mapping_error(ThreadState *thread, Value callable,
                                           Value source)
    {
        std::wstring message = call_keyword_callable_name(thread, callable);
        message += L"() argument after ** must be a mapping, not ";
        TValue<String> source_type = thread->class_of_value(source)->get_name();
        message.append(source_type.extract()->data,
                       source_type.extract()->count.extract());
        return thread->set_pending_builtin_exception_string(
            L"TypeError", thread->make_object_value<String>(message));
    }

    Value raise_duplicate_call_keyword(ThreadState *thread, Value callable,
                                       TValue<String> key)
    {
        std::wstring message = call_keyword_callable_name(thread, callable);
        message += L"() got multiple values for keyword argument '";
        message.append(key.extract()->data, key.extract()->count.extract());
        message += L"'";
        return thread->set_pending_builtin_exception_string(
            L"TypeError", thread->make_object_value<String>(message));
    }

    static Value call_keyword_duplicate_error(ThreadState *thread,
                                              Value callable, Value key_text)
    {
        if(!can_convert_to<String>(key_text))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"__str__ returned non-string");
        }
        return raise_duplicate_call_keyword(
            thread, callable, TValue<String>::from_value_unchecked(key_text));
    }

    static Value call_keyword_source_size(ThreadState *, Value, Value,
                                          Value source)
    {
        assert(can_convert_to<Dict>(source));
        return Value::from_smi(
            static_cast<int64_t>(source.get_ptr<Dict>()->size()));
    }

    static Value call_keyword_source_mutated(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"RuntimeError", L"dict mutated during update");
    }

    static Value call_keyword_add_hash(ThreadState *thread, Value, Value,
                                       Value key, Value)
    {
        assert(is_exact_string(thread, key));
        return string_hash_normalized(TValue<String>::from_value_unchecked(key))
            .raw_value();
    }

    Expected<TValue<Function>>
    make_call_keyword_error_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(), scope,
                               vm->get_or_create_interned_string_value(
                                   L"<duplicate call keyword>"));
        code.configure_positional_function(2);
        uint8_t str_class =
            CL_TRY(code.allocate_constant(Value::from_oop(vm->str_class())));
        NativeFunctionTarget target;
        target.fixed2 = call_keyword_duplicate_error;
        uint8_t error_target = CL_TRY(code.add_native_function_target(target));
        uint8_t str_name = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"__str__")));
        uint8_t type_error = CL_TRY(
            code.allocate_constant(Value::from_oop(vm->builtin_class_by_name(
                vm->get_or_create_interned_string_value(L"TypeError")))));
        uint8_t missing_str = CL_TRY(code.allocate_constant(
            vm->get_or_create_interned_string_value(L"object has no __str__")));
        JumpTarget format(&code);
        CL_TRY(code.emit_ldar(0, 1));
        CL_TRY(code.emit_is_instance_of_known_class(0, str_class));
        CL_TRY(code.emit_jump_if_true(0, format));
        {
            CodeObjectBuilder::TemporaryReg args(code, 1,
                                                 RegisterAlignment::CallFrame);
            CL_TRY(code.emit_mov(0, args, 1));
            CL_TRY(code.emit_call_special_method(0, args, str_name, 0,
                                                 type_error, missing_str));
            CL_TRY(code.emit_star(0, 1));
        }
        CL_TRY(format.resolve());
        CL_TRY(code.emit_call_intrinsic(0, Bytecode::CallIntrinsic2,
                                        error_target));
        CL_TRY(code.emit_return_or_raise_exception(0));
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::none()));
    }

    Expected<TValue<Function>>
    make_call_keyword_merge_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(), scope,
                               vm->get_or_create_interned_string_value(
                                   L"<merge exact call keywords>"));
        code.configure_positional_function(3);  // callable, destination, source
        uint8_t insert = CL_TRY(code.allocate_constant(
            vm->dict_insert_if_absent_known_hash_function().raw_value()));
        uint8_t error = CL_TRY(code.allocate_constant(
            vm->call_keyword_error_function().raw_value()));
        NativeFunctionTarget size_target;
        size_target.fixed3 = call_keyword_source_size;
        uint8_t size_idx = CL_TRY(code.add_native_function_target(size_target));
        NativeFunctionTarget mutated_target;
        mutated_target.fixed0 = call_keyword_source_mutated;
        uint8_t mutated_idx =
            CL_TRY(code.add_native_function_target(mutated_target));
        {
            CodeObjectBuilder::TemporaryReg temps(code, 7);
            uint32_t initial_size = temps;
            uint32_t index = temps + 1;
            uint32_t entry = temps + 2;
            uint32_t insert_function = temps + 5;
            uint32_t error_function = temps + 6;
            CL_TRY(code.emit_lda_constant(0, insert));
            CL_TRY(code.emit_star(0, insert_function));
            CL_TRY(code.emit_lda_constant(0, error));
            CL_TRY(code.emit_star(0, error_function));
            CL_TRY(code.emit_call_intrinsic(0, Bytecode::CallIntrinsic3,
                                            size_idx));
            CL_TRY(code.emit_star(0, initial_size));
            CL_TRY(code.emit_lda_smi(0, 0));
            CL_TRY(code.emit_star(0, index));
            JumpTarget loop(&code), done(&code), duplicate(&code),
                mutated(&code);
            CL_TRY(loop.resolve());
            CL_TRY(code.emit_dict_next_entry(0, 2, index, entry));
            CL_TRY(code.emit_jump_if_false(0, done));
            {
                CodeObjectBuilder::TemporaryReg args(
                    code, 4, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, args, 1));
                CL_TRY(code.emit_mov(0, args + 1, entry));
                CL_TRY(code.emit_mov(0, args + 2, entry + 1));
                CL_TRY(code.emit_mov(0, args + 3, entry + 2));
                CL_TRY(code.emit_call_positional(0, insert_function, args, 4));
            }
            CL_TRY(code.emit_jump_if_false(0, duplicate));
            CL_TRY(code.emit_call_intrinsic(0, Bytecode::CallIntrinsic3,
                                            size_idx));
            CL_TRY(code.emit_operator_reg(0, Bytecode::TestIs, initial_size,
                                          OperatorBytecodeFormat::Plain));
            CL_TRY(code.emit_jump_if_false(0, mutated));
            CL_TRY(code.emit_jump(0, loop));
            CL_TRY(duplicate.resolve());
            {
                CodeObjectBuilder::TemporaryReg args(
                    code, 2, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, args, 0));
                CL_TRY(code.emit_mov(0, args + 1, entry));
                CL_TRY(code.emit_call_positional(0, error_function, args, 2));
            }
            CL_TRY(code.emit_return(0));
            CL_TRY(mutated.resolve());
            CL_TRY(emit_native_error(code, mutated_idx));
            CL_TRY(done.resolve());
            CL_TRY(code.emit_lda_none(0));
            CL_TRY(code.emit_return(0));
        }
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::none()));
    }

    Expected<TValue<Function>>
    make_call_keyword_add_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), scope,
            vm->get_or_create_interned_string_value(L"<add call keyword>"));
        code.configure_positional_function(
            4);  // callable, destination, key, value
        uint8_t insert = CL_TRY(code.allocate_constant(
            vm->dict_insert_if_absent_known_hash_function().raw_value()));
        uint8_t error = CL_TRY(code.allocate_constant(
            vm->call_keyword_error_function().raw_value()));
        NativeFunctionTarget hash_target;
        hash_target.fixed4 = call_keyword_add_hash;
        uint8_t hash_idx = CL_TRY(code.add_native_function_target(hash_target));
        {
            CodeObjectBuilder::TemporaryReg temps(code, 3);
            uint32_t hash = temps;
            uint32_t insert_function = temps + 1;
            uint32_t error_function = temps + 2;
            CL_TRY(code.emit_lda_constant(0, insert));
            CL_TRY(code.emit_star(0, insert_function));
            CL_TRY(code.emit_lda_constant(0, error));
            CL_TRY(code.emit_star(0, error_function));
            CL_TRY(code.emit_call_intrinsic(0, Bytecode::CallIntrinsic4,
                                            hash_idx));
            CL_TRY(code.emit_star(0, hash));
            {
                CodeObjectBuilder::TemporaryReg args(
                    code, 4, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, args, 1));
                CL_TRY(code.emit_mov(0, args + 1, 2));
                CL_TRY(code.emit_mov(0, args + 2, 3));
                CL_TRY(code.emit_mov(0, args + 3, hash));
                CL_TRY(code.emit_call_positional(0, insert_function, args, 4));
            }
            JumpTarget duplicate(&code);
            CL_TRY(code.emit_jump_if_false(0, duplicate));
            CL_TRY(code.emit_lda_none(0));
            CL_TRY(code.emit_return(0));
            CL_TRY(duplicate.resolve());
            {
                CodeObjectBuilder::TemporaryReg args(
                    code, 2, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, args, 0));
                CL_TRY(code.emit_mov(0, args + 1, 2));
                CL_TRY(code.emit_call_positional(0, error_function, args, 2));
            }
            CL_TRY(code.emit_return(0));
        }
        TValue<CodeObject> object =
            TValue<CodeObject>::from_oop(CL_TRY(code.finalize()));
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(
                object, Optional<TValue<String>>::none(),
                Optional<TValue<Tuple>>::none()));
    }

    void install_dict_class_methods(VirtualMachine *vm,
                                    ClassObject *type_error_class)
    {
        Owned<TValue<Tuple>> update_defaults(
            make_single_default(vm, Value::None()));
        Owned<TValue<Tuple>> fromkeys_defaults(
            make_single_default(vm, Value::None()));
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__new__", native_dict_new,
                                     L"Create a dict object."),
            builtin_intrinsic_method(L"__str__", native_dict_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_dict_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_dict_len,
                                     L"Return len(self)."),
            builtin_intrinsic_method(L"clear", native_dict_clear),
            builtin_intrinsic_method(L"copy", native_dict_copy),
            builtin_intrinsic_method(L"keys", native_dict_keys),
            builtin_intrinsic_method(L"values", native_dict_values),
            builtin_intrinsic_method(L"items", native_dict_items),
            builtin_intrinsic_method(L"popitem", native_dict_popitem),
            with_defaults(
                builtin_intrinsic_method(L"update", native_dict_update),
                update_defaults.value()),
            with_defaults(
                builtin_intrinsic_method(L"fromkeys", native_dict_fromkeys),
                fromkeys_defaults.value()),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->dict_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");

        ClassObject *cls = vm->dict_class();
        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        auto install_generated = [&](const wchar_t *name, DictReadKind kind,
                                     TrustedHandlerResolver resolver =
                                         nullptr) {
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(name),
                unwrap_bootstrap_expected(
                    vm,
                    make_dict_read_function(vm, kind, type_error_class,
                                            resolver),
                    "creating generated dict read function")
                    .raw_value(),
                method_flags);
            assert(stored);
            (void)stored;
        };
        auto install_generated_insert =
            [&](const wchar_t *name, DictInsertKind kind,
                TrustedHandlerResolver resolver = nullptr) {
                bool stored = cls->define_own_property(
                    vm->get_or_create_interned_string_value(name),
                    unwrap_bootstrap_expected(
                        vm,
                        make_dict_insert_function(vm, kind, type_error_class,
                                                  resolver),
                        "creating generated dict insertion function")
                        .raw_value(),
                    method_flags);
                assert(stored);
                (void)stored;
            };
        TValue<Function> equal = unwrap_bootstrap_expected(
            vm, make_dict_equal_function(vm), "creating dict equality");
        TValue<Function> not_equal = unwrap_bootstrap_expected(
            vm, make_dict_not_equal_function(vm, equal),
            "creating dict inequality");
        bool equal_stored = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__eq__"),
            equal.raw_value(), method_flags);
        bool not_equal_stored = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__ne__"),
            not_equal.raw_value(), method_flags);
        assert(equal_stored && not_equal_stored);
        (void)equal_stored;
        (void)not_equal_stored;
        install_generated(L"__getitem__", DictReadKind::GetItem,
                          resolve_trusted_dict_getitem_handler);
        install_generated_insert(L"__setitem__", DictInsertKind::SetItem,
                                 resolve_trusted_dict_setitem_handler);
        {
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(L"__delitem__"),
                unwrap_bootstrap_expected(
                    vm,
                    make_dict_delete_function(
                        vm, DictDeleteKind::DelItem, type_error_class,
                        Value::None(), resolve_trusted_dict_delitem_handler),
                    "creating generated dict delitem function")
                    .raw_value(),
                method_flags);
            assert(stored);
            (void)stored;
        }

        install_generated(L"__contains__", DictReadKind::Contains,
                          resolve_trusted_dict_contains_handler);
        install_generated(L"get", DictReadKind::Get);
        TValue<Tuple> pop_missing_sentinel =
            vm->make_immortal_object_value<Tuple>(0);
        {
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(L"pop"),
                unwrap_bootstrap_expected(
                    vm,
                    make_dict_delete_function(vm, DictDeleteKind::Pop,
                                              type_error_class,
                                              pop_missing_sentinel.raw_value()),
                    "creating generated dict pop function")
                    .raw_value(),
                method_flags);
            assert(stored);
            (void)stored;
        }
        install_generated_insert(L"setdefault", DictInsertKind::SetDefault);
    }

    void install_dict_python_methods(VirtualMachine *vm,
                                     ModuleObject *implementation_module)
    {
        ClassObject *dict_class = vm->dict_class();
        assert(dict_class->current_mro_shape_and_contents_validity_cell() ==
               nullptr);
        assert(
            dict_class
                ->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell() ==
            nullptr);
        assert(
            dict_class->attached_mro_shape_and_contents_validity_cell_count() ==
            0);
        assert(dict_class->current_constructor_thunk() == nullptr);
        const wchar_t *method_names[] = {L"update", L"__new__"};
        const wchar_t *helper_names[] = {L"_clover_dict_update",
                                         L"_clover_dict_new"};

        for(size_t idx = 0; idx < std::size(method_names); ++idx)
        {
            TValue<String> helper_name =
                vm->get_or_create_interned_string_value(helper_names[idx]);
            Value function =
                implementation_module->get_own_property(helper_name);
            if(!can_convert_to<Function>(function))
            {
                fatal("trusted builtin_implementations.py did not define dict "
                      "method");
            }

            TValue<String> method_name =
                vm->get_or_create_interned_string_value(method_names[idx]);
            StorageLocation location =
                dict_class->get_shape()->resolve_present_property(method_name);
            assert(location.is_found());
            dict_class->write_storage_location(location, function);
        }
    }

    TValue<Dict> Dict::copy() const { return make_object_value<Dict>(*this); }

    Value Dict::keys()
    {
        return make_object_value<DictKeysView>(TValue<Dict>::from_oop(this))
            .raw_value();
    }

    Value Dict::values()
    {
        return make_object_value<DictValuesView>(TValue<Dict>::from_oop(this))
            .raw_value();
    }

    Value Dict::items()
    {
        return make_object_value<DictItemsView>(TValue<Dict>::from_oop(this))
            .raw_value();
    }

    Value Dict::popitem()
    {
        if(empty())
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }

        int32_t last_entry_idx = -1;
        for(size_t entry_idx = 0; entry_idx < entries.size(); ++entry_idx)
        {
            if(entries[entry_idx].valid())
            {
                last_entry_idx = static_cast<int32_t>(entry_idx);
            }
        }
        assert(last_entry_idx >= 0);
        Entry last = entries[last_entry_idx];
        int64_t last_hash_idx = -1;
        for(size_t hash_idx = 0; hash_idx < hash_table.size(); ++hash_idx)
        {
            if(hash_table[hash_idx] == last_entry_idx)
            {
                last_hash_idx = static_cast<int64_t>(hash_idx);
                break;
            }
        }
        assert(last_hash_idx >= 0);
        Owned<Value> last_key(last.key);
        Owned<Value> last_value(last.value);
        delete_entry_at_slot(static_cast<size_t>(last_hash_idx));
        Owned<TValue<Tuple>> result(make_object_value<Tuple>(2));
        result.extract()->initialize_item_unchecked(0, last_key.value());
        result.extract()->initialize_item_unchecked(1, last_value.value());
        return result.raw_value();
    }

    Expected<void> Dict::update_from_dict(ThreadState *thread,
                                          const Dict *other)
    {
        for(size_t idx = 0; idx < other->entries.size(); ++idx)
        {
            Entry entry = other->entries[idx];
            if(!entry.valid())
            {
                continue;
            }
            CL_TRY(set_item_with_known_hash(thread, entry.key, entry.value,
                                            entry.hash));
        }
        return Expected<void>::ok();
    }

    Value Dict::from_tuple_keys(const Tuple *keys, Value value)
    {
        ThreadState *thread = active_thread();
        Owned<TValue<Dict>> result(make_object_value<Dict>());
        Owned<Value> live_value(value);
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            CL_TRY(result.extract()->set_item(thread, keys->item_unchecked(idx),
                                              live_value.value()));
        }
        return result.raw_value();
    }

    Value Dict::from_list_keys(const List *keys, Value value)
    {
        ThreadState *thread = active_thread();
        Owned<TValue<Dict>> result(make_object_value<Dict>());
        Owned<Value> live_value(value);
        for(size_t idx = 0; idx < keys->size(); ++idx)
        {
            CL_TRY(result.extract()->set_item(thread, keys->item_unchecked(idx),
                                              live_value.value()));
        }
        return result.raw_value();
    }

    Dict::Iterator::Iterator(const Dict *dict, size_t idx)
        : dict(dict), idx(idx)
    {
        skip_dead_entries();
    }

    Dict::EntryView Dict::Iterator::operator*() const
    {
        assert(idx < dict->entries.size());
        const Entry &entry = dict->entries[idx];
        assert(entry.valid());
        return EntryView{entry.key, entry.value};
    }

    Dict::Iterator &Dict::Iterator::operator++()
    {
        assert(idx <= dict->entries.size());
        if(idx < dict->entries.size())
        {
            ++idx;
            skip_dead_entries();
        }
        return *this;
    }

    bool Dict::Iterator::operator==(const Iterator &other) const
    {
        return dict == other.dict && idx == other.idx;
    }

    bool Dict::Iterator::operator!=(const Iterator &other) const
    {
        return !(*this == other);
    }

    void Dict::Iterator::skip_dead_entries()
    {
        while(idx < dict->entries.size() && !dict->entries[idx].valid())
        {
            ++idx;
        }
    }

    Dict::Iterator Dict::begin() const { return Iterator(this, 0); }

    Dict::Iterator Dict::end() const { return Iterator(this, entries.size()); }

    bool Dict::entry_at(size_t idx, EntryView &out) const
    {
        if(idx >= entries.size())
        {
            return false;
        }
        const Entry &entry = entries[idx];
        if(!entry.valid())
        {
            return false;
        }
        out = EntryView{entry.key, entry.value};
        return true;
    }

    Expected<Value> Dict::get_item(ThreadState *thread, Value key)
    {
        Owned<Value> live_key(key);
        ItemResult result =
            CL_TRY(get_item_if_present(thread, live_key.value()));
        if(!result.found)
        {
            return Expected<Value>::from_value_unchecked(
                dict_missing_key_error(thread, live_key.value()));
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<Dict::ItemResult> Dict::get_item_if_present(ThreadState *thread,
                                                         Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key))
        {
            int32_t idx =
                *find_entry(TValue<String>::from_value_unchecked(key));
            if(idx >= 0)
            {
                return Expected<ItemResult>::ok(
                    ItemResult{entries[idx].value, true});
            }
            return Expected<ItemResult>::ok(ItemResult{Value::None(), false});
        }
        maybe_promote_to_general_shape(thread);
        return general_get_item_if_present(thread, key);
    }

    Expected<Value> Dict::get_item_or_default(ThreadState *thread, Value key,
                                              Value default_value)
    {
        default_value.assert_not_vm_sentinel();
        Owned<Value> live_default(default_value);
        ItemResult result = CL_TRY(get_item_if_present(thread, key));
        return Expected<Value>::ok(result.found ? result.value
                                                : live_default.value());
    }

    Expected<void> Dict::set_item(ThreadState *thread, Value key, Value value)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key))
        {
            return set_item_for_str(
                thread, TValue<String>::from_value_unchecked(key), value);
        }
        maybe_promote_to_general_shape(thread);
        return general_set_item(thread, key, value);
    }

    Expected<void> Dict::del_item(ThreadState *thread, Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key))
        {
            return del_item_for_str(thread,
                                    TValue<String>::from_value_unchecked(key));
        }
        maybe_promote_to_general_shape(thread);
        return general_del_item(thread, key);
    }

    Expected<bool> Dict::contains(ThreadState *thread, Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key))
        {
            return contains_for_str(thread,
                                    TValue<String>::from_value_unchecked(key));
        }
        maybe_promote_to_general_shape(thread);
        return general_contains(thread, key);
    }

    Expected<Value> Dict::pop(ThreadState *thread, Value key)
    {
        Owned<Value> live_key(key);
        ItemResult result =
            CL_TRY(pop_item_if_present(thread, live_key.value()));
        if(!result.found)
        {
            return Expected<Value>::from_value_unchecked(
                dict_missing_key_error(thread, live_key.value()));
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<Dict::ItemResult> Dict::pop_item_if_present(ThreadState *thread,
                                                         Value key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key))
        {
            int32_t *entry =
                find_entry(TValue<String>::from_value_unchecked(key));
            int32_t idx = *entry;
            if(idx < 0)
            {
                return Expected<ItemResult>::ok(
                    ItemResult{Value::None(), false});
            }

            Owned<Value> result(entries[idx].value);
            entries.set(idx, Entry(Value::not_present(), Value::None(),
                                   TValue<SMI>::from_smi(0)));
            *entry = tombstone;
            --n_valid_entries;
            return Expected<ItemResult>::ok(ItemResult{result.value(), true});
        }
        maybe_promote_to_general_shape(thread);
        return general_pop_item_if_present(thread, key);
    }

    Expected<Value> Dict::setdefault(ThreadState *thread, Value key,
                                     Value default_value)
    {
        SetDefaultResult result =
            CL_TRY(setdefault_with_presence(thread, key, default_value));
        return Expected<Value>::ok(result.value);
    }

    Expected<Dict::SetDefaultResult>
    Dict::setdefault_with_presence(ThreadState *thread, Value key,
                                   Value default_value)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key))
        {
            TValue<String> string_key =
                TValue<String>::from_value_unchecked(key);
            int32_t idx = *find_entry(string_key);
            if(idx >= 0)
            {
                return Expected<SetDefaultResult>::ok(
                    SetDefaultResult{entries[idx].value, true});
            }
            string_keyed_insert(string_key, default_value);
            return Expected<SetDefaultResult>::ok(
                SetDefaultResult{default_value, false});
        }
        maybe_promote_to_general_shape(thread);
        return general_setdefault_with_presence(thread, key, default_value);
    }

    Expected<Value> Dict::get_item_for_str(ThreadState *thread,
                                           TValue<String> key)
    {
        Owned<TValue<String>> live_key(key);
        ItemResult result =
            CL_TRY(get_item_if_present(thread, live_key.raw_value()));
        if(!result.found)
        {
            return Expected<Value>::from_value_unchecked(
                dict_missing_key_error(thread, live_key.raw_value()));
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<void> Dict::set_item_for_str(ThreadState *thread,
                                          TValue<String> key, Value value)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key.raw_value()))
        {
            string_keyed_insert(key, value);
            return Expected<void>::ok();
        }
        maybe_promote_to_general_shape(thread);
        return general_set_item(thread, key.raw_value(), value);
    }

    Expected<void> Dict::del_item_for_str(ThreadState *thread,
                                          TValue<String> key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key.raw_value()))
        {
            Value result = string_keyed_delete(key);
            if(result.is_exception_marker())
            {
                return Expected<void>::propagate_exception();
            }
            return Expected<void>::ok();
        }
        maybe_promote_to_general_shape(thread);
        return general_del_item(thread, key.raw_value());
    }

    Expected<bool> Dict::contains_for_str(ThreadState *thread,
                                          TValue<String> key)
    {
        if(is_exact_dict_string_key_shape(thread, this) &&
           is_exact_string(thread, key.raw_value()))
        {
            return Expected<bool>::ok(string_keyed_contains(key));
        }
        maybe_promote_to_general_shape(thread);
        return general_contains(thread, key.raw_value());
    }

    Expected<Value> Dict::pop_for_str(ThreadState *thread, TValue<String> key)
    {
        Owned<TValue<String>> live_key(key);
        ItemResult result =
            CL_TRY(pop_item_if_present(thread, live_key.raw_value()));
        if(!result.found)
        {
            return Expected<Value>::from_value_unchecked(
                dict_missing_key_error(thread, live_key.raw_value()));
        }
        return Expected<Value>::ok(result.value);
    }

    Expected<Value> Dict::setdefault_for_str(ThreadState *thread,
                                             TValue<String> key,
                                             Value default_value)
    {
        SetDefaultResult result = CL_TRY(
            setdefault_with_presence(thread, key.raw_value(), default_value));
        return Expected<Value>::ok(result.value);
    }

    void Dict::promote_to_general_shape(ThreadState *thread)
    {
        assert(is_exact_dict_string_key_shape(thread, this));
        set_shape(thread->get_exact_dict_general_shape());
        increment_table_generation();
    }

    void Dict::maybe_promote_to_general_shape(ThreadState *thread)
    {
        if(is_exact_dict_string_key_shape(thread, this))
        {
            promote_to_general_shape(thread);
        }
    }

    Expected<size_t>
    Dict::find_entry_slot_for_general_insert(ThreadState *thread, Value key,
                                             TValue<SMI> hash_smi)
    {
        while(true)
        {
            Probe probe = probe_start(hash_smi);

            while(true)
            {
                int32_t entry_status = hash_table[probe.hash_idx];
                if(entry_status == not_present)
                {
                    return Expected<size_t>::ok(probe_write_slot(probe));
                }
                if(entry_status == tombstone)
                {
                    probe_record_tombstone(probe, entry_status);
                    probe_advance(probe);
                    continue;
                }

                Entry entry = entries[entry_status];
                if(entry.hash == hash_smi)
                {
                    if(entry.key == key)
                    {
                        return Expected<size_t>::ok(probe.hash_idx);
                    }

                    Owned<Value> candidate_key(entry.key);
                    bool equal =
                        CL_TRY(thread->test_equal(candidate_key.value(), key));
                    if(!entry_still_matches(probe.table_generation,
                                            probe.hash_idx, entry_status,
                                            candidate_key.value()))
                    {
                        break;
                    }
                    if(!probe_recorded_tombstone_still_available(probe))
                    {
                        probe_clear_recorded_tombstone(probe);
                    }
                    if(equal)
                    {
                        return Expected<size_t>::ok(probe.hash_idx);
                    }
                }

                probe_advance(probe);
            }
        }
    }

    Expected<int32_t>
    Dict::find_entry_index_for_general_lookup(ThreadState *thread, Value key,
                                              TValue<SMI> hash_smi)
    {
        while(true)
        {
            Probe probe = probe_start(hash_smi);

            while(true)
            {
                int32_t entry_status = hash_table[probe.hash_idx];
                if(entry_status == not_present)
                {
                    return Expected<int32_t>::ok(not_present);
                }
                if(entry_status == tombstone)
                {
                    probe_advance(probe);
                    continue;
                }

                Entry entry = entries[entry_status];
                if(entry.hash == hash_smi)
                {
                    if(entry.key == key)
                    {
                        return Expected<int32_t>::ok(entry_status);
                    }

                    Owned<Value> candidate_key(entry.key);
                    bool equal =
                        CL_TRY(thread->test_equal(candidate_key.value(), key));
                    if(!entry_still_matches(probe.table_generation,
                                            probe.hash_idx, entry_status,
                                            candidate_key.value()))
                    {
                        break;
                    }
                    if(equal)
                    {
                        return Expected<int32_t>::ok(entry_status);
                    }
                }

                probe_advance(probe);
            }
        }
    }

    Expected<int64_t>
    Dict::find_entry_slot_for_general_lookup(ThreadState *thread, Value key,
                                             TValue<SMI> hash_smi)
    {
        while(true)
        {
            Probe probe = probe_start(hash_smi);

            while(true)
            {
                int32_t entry_status = hash_table[probe.hash_idx];
                if(entry_status == not_present)
                {
                    return Expected<int64_t>::ok(-1);
                }
                if(entry_status == tombstone)
                {
                    probe_advance(probe);
                    continue;
                }

                Entry entry = entries[entry_status];
                if(entry.hash == hash_smi)
                {
                    if(entry.key == key)
                    {
                        return Expected<int64_t>::ok(
                            static_cast<int64_t>(probe.hash_idx));
                    }

                    Owned<Value> candidate_key(entry.key);
                    bool equal =
                        CL_TRY(thread->test_equal(candidate_key.value(), key));
                    if(!entry_still_matches(probe.table_generation,
                                            probe.hash_idx, entry_status,
                                            candidate_key.value()))
                    {
                        break;
                    }
                    if(equal)
                    {
                        return Expected<int64_t>::ok(
                            static_cast<int64_t>(probe.hash_idx));
                    }
                }

                probe_advance(probe);
            }
        }
    }

    Expected<Dict::ItemResult>
    Dict::general_get_item_if_present(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int32_t idx = CL_TRY(find_entry_index_for_general_lookup(
            thread, live_key.value(), hash));
        if(idx < 0)
        {
            return Expected<ItemResult>::ok(ItemResult{Value::None(), false});
        }
        return Expected<ItemResult>::ok(ItemResult{entries[idx].value, true});
    }

    Expected<void> Dict::set_item_with_known_hash(ThreadState *thread,
                                                  Value key, Value value,
                                                  TValue<SMI> hash)
    {
        key.assert_not_vm_sentinel();
        value.assert_not_vm_sentinel();

        if(is_exact_dict_string_key_shape(thread, this))
        {
            if(is_exact_string(thread, key))
            {
                string_keyed_insert(TValue<String>::from_value_unchecked(key),
                                    value);
                return Expected<void>::ok();
            }
            promote_to_general_shape(thread);
        }

        Owned<Value> live_key(key);
        Owned<Value> live_value(value);
        resize_general_if_needed();

        size_t entry_slot = CL_TRY(
            find_entry_slot_for_general_insert(thread, live_key.value(), hash));
        int32_t idx = hash_table[entry_slot];
        if(idx < 0)
        {
            write_new_at_slot(entry_slot, hash, live_key.value(),
                              live_value.value());
        }
        else
        {
            write_existing(idx, live_value.value());
        }

        return Expected<void>::ok();
    }

    Expected<void> Dict::general_set_item(ThreadState *thread, Value key,
                                          Value value)
    {
        key.assert_not_vm_sentinel();
        value.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        Owned<Value> live_value(value);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));

        resize_general_if_needed();

        size_t entry_slot = CL_TRY(
            find_entry_slot_for_general_insert(thread, live_key.value(), hash));
        int32_t idx = hash_table[entry_slot];
        if(idx < 0)
        {
            write_new_at_slot(entry_slot, hash, live_key.value(),
                              live_value.value());
        }
        else
        {
            write_existing(idx, live_value.value());
        }

        return Expected<void>::ok();
    }

    Expected<void> Dict::general_del_item(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int64_t entry_slot = CL_TRY(
            find_entry_slot_for_general_lookup(thread, live_key.value(), hash));
        if(entry_slot < 0)
        {
            (void)dict_missing_key_error(thread, live_key.value());
            return Expected<void>::propagate_exception();
        }

        int32_t idx = hash_table[static_cast<size_t>(entry_slot)];
        assert(idx >= 0);
        (void)idx;
        delete_entry_at_slot(static_cast<size_t>(entry_slot));
        return Expected<void>::ok();
    }

    Expected<bool> Dict::general_contains(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int32_t idx = CL_TRY(find_entry_index_for_general_lookup(
            thread, live_key.value(), hash));
        return Expected<bool>::ok(idx >= 0);
    }

    Expected<Dict::ItemResult>
    Dict::general_pop_item_if_present(ThreadState *thread, Value key)
    {
        key.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));
        int64_t entry_slot = CL_TRY(
            find_entry_slot_for_general_lookup(thread, live_key.value(), hash));
        if(entry_slot < 0)
        {
            return Expected<ItemResult>::ok(ItemResult{Value::None(), false});
        }

        int32_t idx = hash_table[static_cast<size_t>(entry_slot)];
        assert(idx >= 0);
        Owned<Value> result(entries[idx].value);
        delete_entry_at_slot(static_cast<size_t>(entry_slot));
        return Expected<ItemResult>::ok(ItemResult{result.value(), true});
    }

    Expected<Dict::SetDefaultResult>
    Dict::general_setdefault_with_presence(ThreadState *thread, Value key,
                                           Value default_value)
    {
        key.assert_not_vm_sentinel();
        default_value.assert_not_vm_sentinel();

        Owned<Value> live_key(key);
        Owned<Value> live_default(default_value);
        TValue<SMI> hash = CL_TRY(thread->hash_value(live_key.value()));

        resize_general_if_needed();

        size_t entry_slot = CL_TRY(
            find_entry_slot_for_general_insert(thread, live_key.value(), hash));
        int32_t slot_value = hash_table[entry_slot];
        if(slot_value >= 0)
        {
            return Expected<SetDefaultResult>::ok(
                SetDefaultResult{entries[slot_value].value, true});
        }

        write_new_at_slot(entry_slot, hash, live_key.value(),
                          live_default.value());
        return Expected<SetDefaultResult>::ok(
            SetDefaultResult{live_default.value(), false});
    }

    const int32_t *Dict::find_entry(TValue<String> key) const
    {
        return find_entry_with_provided_hash(key, string_keyed_hash(key));
    }

    int32_t *Dict::find_entry(TValue<String> key)
    {
        return find_entry_with_provided_hash(key, string_keyed_hash(key));
    }

    int32_t *Dict::find_entry_with_provided_hash(TValue<String> key,
                                                 TValue<SMI> hash_smi)
    {
        const Dict *self = this;
        return const_cast<int32_t *>(
            self->find_entry_with_provided_hash(key, hash_smi));
    }

    const int32_t *
    Dict::find_entry_with_provided_hash(TValue<String> key,
                                        TValue<SMI> hash_smi) const
    {
        uint64_t hash = hash_smi.extract();
        uint32_t hash_table_size_m1 = hash_table.size() - 1;

        uint32_t hash_idx = hash & hash_table_size_m1;
        int32_t tombstone_hash_idx = -1;
        while(true)
        {
            int32_t entry_idx = hash_table[hash_idx];
            if(entry_idx == not_present)
            {
                if(tombstone_hash_idx == -1)
                    tombstone_hash_idx = hash_idx;
                return &hash_table[tombstone_hash_idx];
            }
            if(entry_idx == tombstone)
            {
                if(tombstone_hash_idx == -1)
                {
                    tombstone_hash_idx = hash_idx;
                }
                hash_idx = (hash_idx + 1) & hash_table_size_m1;
                continue;
            }
            if(string_keyed_equal(key, TValue<String>::from_value_unchecked(
                                           entries[entry_idx].key)))
            {
                return &hash_table[hash_idx];
            }

            hash_idx = (hash_idx + 1) & hash_table_size_m1;
        }
    }

    Value Dict::string_keyed_lookup(TValue<String> key) const
    {
        const int32_t *iidx = find_entry(key);
        int32_t idx = *iidx;
        if(idx >= 0)
        {
            const Entry &e = entries[idx];
            if(e.valid())
            {
                return e.value;
            }
        }
        return dict_missing_key_error(active_thread(), key.raw_value());
    }

    Value Dict::string_keyed_delete(TValue<String> key)
    {
        int32_t *iidx = find_entry(key);
        int32_t idx = *iidx;
        if(idx >= 0)
        {
            entries.set(idx, Entry(Value::not_present(), Value::None(),
                                   TValue<SMI>::from_smi(0)));
            *iidx = tombstone;
            --n_valid_entries;
            return Value::None();
        }
        return dict_missing_key_error(active_thread(), key.raw_value());
    }

    void Dict::string_keyed_insert(TValue<String> key, Value value)
    {
        value.assert_not_vm_sentinel();

        if(entries.size() > hash_table.size() * max_load_nom / max_load_denom)
        {
            grow();
        }

        TValue<SMI> hash = string_keyed_hash(key);
        int32_t *entry = find_entry_with_provided_hash(key, hash);
        int32_t idx = *entry;
        if(idx < 0)
        {
            idx = entries.size();
            *entry = idx;
            entries.emplace_back(key.raw_value(), value, hash);
            ++n_valid_entries;
        }
        else
        {
            Entry existing = entries[idx];
            entries.set(idx, Entry(existing.key, value, existing.hash));
        }
    }

    bool Dict::string_keyed_insert_if_absent(TValue<String> key, Value value)
    {
        assert(is_exact_dict_string_key_shape(active_thread(), this));
        value.assert_not_vm_sentinel();

        TValue<SMI> hash = string_keyed_hash(key);
        int32_t *entry = find_entry_with_provided_hash(key, hash);
        if(*entry >= 0)
        {
            return false;
        }
        if(needs_resize_for_insert())
        {
            grow();
            entry = find_entry_with_provided_hash(key, hash);
        }
        write_new_at_slot(static_cast<size_t>(entry - hash_table.data()), hash,
                          key.raw_value(), value);
        return true;
    }

    Optional<TValue<String>> Dict::merge_string_keyed_unique(const Dict *source)
    {
        assert(is_exact_dict_string_key_shape(active_thread(), this));
        assert(is_exact_dict_string_key_shape(active_thread(), source));
        for(EntryView entry: *source)
        {
            TValue<String> key =
                TValue<String>::from_value_unchecked(entry.key);
            if(!string_keyed_insert_if_absent(key, entry.value))
            {
                return Optional<TValue<String>>::some(key);
            }
        }
        return Optional<TValue<String>>::none();
    }

    bool Dict::string_keyed_contains(TValue<String> key) const
    {
        return *find_entry(key) >= 0;
    }

    void Dict::clear()
    {
        entries.clear();
        n_valid_entries = 0;
        increment_table_generation();
        for(int32_t &k: hash_table)
        {
            k = not_present;
        }
    }

    void Dict::grow()
    {
        // make one that's twice the size
        size_t new_size = hash_table.size() * 2;
        increment_table_generation();
        hash_table.resize(0);
        hash_table.resize(new_size, -1);

        size_t write_idx = 0;
        for(size_t read_idx = 0; read_idx < entries.size(); ++read_idx)
        {
            Entry entry = entries[read_idx];
            if(!entry.valid())
            {
                continue;
            }

            if(write_idx != read_idx)
            {
                entries.set(write_idx, entry);
            }
            uint64_t hash = entries[write_idx].hash.extract();
            uint32_t hash_table_size_m1 = hash_table.size() - 1;
            uint32_t hash_idx = hash & hash_table_size_m1;
            while(hash_table[hash_idx] != not_present)
            {
                hash_idx = (hash_idx + 1) & hash_table_size_m1;
            }
            hash_table[hash_idx] = static_cast<int32_t>(write_idx);
            ++write_idx;
        }
        entries.resize(write_idx, Entry(Value::not_present(), Value::None(),
                                        TValue<SMI>::from_smi(0)));
        assert(entries.size() == n_valid_entries);
    }

}  // namespace cl
