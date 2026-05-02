#include "interpreter.h"

#include "attr.h"
#include "class_object.h"
#include "code_object.h"
#include "code_object_print.h"
#include "dict.h"
#include "function.h"
#include "instance.h"
#include "list.h"
#include "range_iterator.h"
#include "refcount.h"
#include "runtime_helpers.h"
#include "subscript.h"
#include "tuple.h"
#include "validity_cell.h"
#include "value.h"
#include <cstdint>
#include <fmt/core.h>

namespace cl
{

#define PARAMS                                                                 \
    Value accumulator, Value *fp, const uint8_t *pc, void *dispatch,           \
        CodeObject *code_object
#define ARGS accumulator, fp, pc, dispatch, code_object

    using DispatchTableEntry = Value (*)(PARAMS);

    static constexpr uintptr_t StackFrameAlignmentBytes = 16;

    [[maybe_unused]] static ALWAYSINLINE bool is_stack_frame_aligned(Value *fp)
    {
        return (reinterpret_cast<uintptr_t>(fp) % StackFrameAlignmentBytes) ==
               0;
    }

    struct DispatchTable
    {
        DispatchTableEntry table[BytecodeTableSize];
    };

#define START(len)                                                             \
    static constexpr uint32_t instr_len = len;                                 \
    auto *dispatch_fun =                                                       \
        reinterpret_cast<DispatchTable *>(dispatch)->table[pc[instr_len]]
#define COMPLETE()                                                             \
    pc += instr_len;                                                           \
    MUSTTAIL return dispatch_fun(ARGS)

#define START_BINARY_REG_ACC()                                                 \
    START(2);                                                                  \
    int8_t reg = pc[1];                                                        \
    Value a = fp[reg];                                                         \
    Value b = accumulator

#define START_BINARY_ACC_SMI()                                                 \
    START(2);                                                                  \
    Value a = accumulator;                                                     \
    Value b = Value::from_smi(int8_t(pc[1]))

#define START_UNARY_ACC()                                                      \
    START(1);                                                                  \
    Value a = accumulator

#define A_NOT_SMI() ((a.as.integer & value_not_smi_mask) != 0)
#define A_OR_B_NOT_SMI()                                                       \
    (((a.as.integer | b.as.integer) & value_not_smi_mask) != 0)
#define A_OR_B_REFCOUNTED_PTR()                                                \
    (((a.as.integer | b.as.integer) & value_refcounted_ptr_tag) != 0)

    static uint32_t read_uint32_le(const uint8_t *p)
    {
        return (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }

    static int16_t read_int16_le(const uint8_t *p)
    {
#if 1
        const int16_t *ptr = reinterpret_cast<const int16_t *>(p);
        return *ptr;
#else
        return (p[0] << 0) | (p[1] << 8);
#endif
    }

    NOINLINE Value raise_generic_exception(PARAMS)
    {
        throw std::runtime_error("Clovervm exception");
    }

    NOINLINE Value raise_unknown_opcode_exception(PARAMS)
    {
        throw std::runtime_error("Unknown opcode");
    }

    NOINLINE Value raise_value_error_negative_shift_count(PARAMS)
    {
        throw std::runtime_error("ValueError: negative shift count");
    }

    NOINLINE Value name_error(PARAMS) { throw std::runtime_error("NameError"); }

    NOINLINE Value not_callable_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not callable");
    }

    NOINLINE Value attribute_error(PARAMS)
    {
        throw std::runtime_error("AttributeError");
    }

    NOINLINE Value attribute_assignment_error(PARAMS)
    {
        throw std::runtime_error("AttributeError: cannot assign attribute");
    }

    NOINLINE Value subscript_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not subscriptable");
    }

    NOINLINE Value method_lookup_error(PARAMS)
    {
        throw std::runtime_error("AttributeError");
    }

    NOINLINE Value descriptor_dispatch_error(PARAMS)
    {
        throw std::runtime_error(
            "TypeError: descriptor __get__ requires interpreter dispatch");
    }

    NOINLINE Value wrong_arity_error(PARAMS)
    {
        throw std::runtime_error("TypeError: wrong number of arguments");
    }

    static constexpr uint32_t kDefaultFactoryInlineSlotCount = 4;

    static ALWAYSINLINE void
    initialize_frame_header(Value *new_fp, Value *previous_fp,
                            CodeObject *return_code_object,
                            const uint8_t *return_pc)
    {
        // these aren't really values. we're just going to whack them in and
        // ask the refcounter to ignore them.
        new_fp[FrameHeaderPreviousFpOffset].as.ptr = (Object *)previous_fp;
        new_fp[FrameHeaderReturnCodeObjectOffset] =
            Value::from_oop(return_code_object);
        new_fp[FrameHeaderReturnPcOffset].as.ptr = (Object *)return_pc;
    }

    static ALWAYSINLINE void restore_frame_header(Value *&fp,
                                                  const uint8_t *&pc,
                                                  CodeObject *&code_object)
    {
        pc = (const uint8_t *)fp[FrameHeaderReturnPcOffset].as.ptr;
        code_object =
            fp[FrameHeaderReturnCodeObjectOffset].get_ptr<CodeObject>();
        fp = (Value *)fp[FrameHeaderPreviousFpOffset].as.ptr;
    }

    static void initialize_class_body_frame(Value *fp, CodeObject *body_code)
    {
        Scope *local_scope = body_code->get_local_scope_ptr();
        for(uint32_t slot_idx = 0; slot_idx < local_scope->size(); ++slot_idx)
        {
            if(!local_scope->slot_is_named(slot_idx))
            {
                continue;
            }
            fp[body_code->encode_reg(slot_idx)] =
                local_scope->get_by_slot_index_fastpath_only(slot_idx);
        }
    }

    static void
    reject_set_name_notifications_until_supported(Value *fp,
                                                  CodeObject *body_code)
    {
        TValue<String> set_name_name(interned_string(L"__set_name__"));

        Scope *local_scope = body_code->get_local_scope_ptr();
        for(uint32_t slot_idx = 0; slot_idx < local_scope->size(); ++slot_idx)
        {
            if(!local_scope->slot_is_named(slot_idx))
            {
                continue;
            }

            Value value = fp[body_code->encode_reg(slot_idx)];
            if(value.is_not_present())
            {
                continue;
            }

            Value callable;
            Value self;
            if(load_method(value, set_name_name, callable, self))
            {
                throw std::runtime_error(
                    "TypeError: __set_name__ notifications are not "
                    "implemented yet");
            }
        }
    }

    static constexpr uint32_t ClassBodyNameParameter = 0;
    static constexpr uint32_t ClassBodyBasesParameter = 1;
    static constexpr uint32_t ClassBodyParameterCount = 2;

    static Value build_class_from_frame(Value *fp, CodeObject *body_code)
    {
        TValue<String> class_name(
            fp[body_code->encode_reg(ClassBodyNameParameter)]);
        TValue<Tuple> bases(fp[body_code->encode_reg(ClassBodyBasesParameter)]);

        TValue<ClassObject> cls = make_internal_value<ClassObject>(
            active_vm()->type_class(), class_name,
            kDefaultFactoryInlineSlotCount, bases);
        Scope *local_scope = body_code->get_local_scope_ptr();
        for(uint32_t slot_idx = 0; slot_idx < local_scope->size(); ++slot_idx)
        {
            if(!local_scope->slot_is_named(slot_idx))
            {
                continue;
            }

            Value value = fp[body_code->encode_reg(slot_idx)];
            if(value.is_not_present())
            {
                continue;
            }
            if(!cls.extract()->set_own_property(
                   local_scope->get_name_by_slot_index(slot_idx), value))
            {
                throw std::runtime_error(
                    "TypeError: cannot set read-only class attribute");
            }
        }
        reject_set_name_notifications_until_supported(fp, body_code);
        return Value::from_oop(cls.extract());
    }

    NOINLINE Value not_iterable_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not iterable");
    }

    NOINLINE Value not_iterator_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not an iterator");
    }

    NOINLINE Value range_integer_argument_error(PARAMS)
    {
        throw std::runtime_error(
            "TypeError: range() arguments must be integers");
    }

    NOINLINE Value range_zero_step_error(PARAMS)
    {
        throw std::runtime_error("ValueError: range() arg 3 must not be zero");
    }

    NOINLINE Value init_returned_non_none_error(PARAMS)
    {
        throw std::runtime_error(
            "TypeError: __init__ should return None, not a value");
    }

    NOINLINE Value assertion_error(PARAMS)
    {
        throw std::runtime_error("AssertionError");
    }

    NOINLINE static Value slow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }
    NOINLINE static Value overflow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }

    static ALWAYSINLINE void enter_function_frame_at_new_fp(
        Value *&fp, const uint8_t *&pc, CodeObject *&code_object,
        TValue<Function> fun, Value *new_fp, uint32_t instr_len)
    {
        assert(is_stack_frame_aligned(new_fp));
        pc += instr_len;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = fun.extract()->code_object.extract();
        pc = code_object->code.data();
    }

    static ALWAYSINLINE bool is_fixed_arity_function(TValue<Function> fun)
    {
        return !fun.extract()->has_varargs() &&
               fun.extract()->min_positional_arity ==
                   fun.extract()->max_positional_arity;
    }

    static ALWAYSINLINE FunctionCallAdaptation
    classify_function_call_adaptation(TValue<Function> fun)
    {
        if(fun.extract()->has_varargs())
        {
            return FunctionCallAdaptation::Varargs;
        }
        if(is_fixed_arity_function(fun))
        {
            return FunctionCallAdaptation::FixedArity;
        }
        return FunctionCallAdaptation::Defaults;
    }

    static ALWAYSINLINE void
    populate_function_call_cache(FunctionCallInlineCache &cache,
                                 TValue<Function> fun, uint32_t n_args,
                                 FunctionCallAdaptation adaptation)
    {
        cache.kind = FunctionCallInlineCacheKind::Function;
        cache.guard_value = fun.as_value();
        cache.function = fun.extract();
        cache.code_object = fun.extract()->code_object.extract();
        cache.validity_cell = nullptr;
        cache.n_args = n_args;
        cache.adaptation = adaptation;
    }

    static ALWAYSINLINE void
    populate_constructor_call_cache(FunctionCallInlineCache &cache,
                                    ClassObject *cls, TValue<Function> thunk,
                                    ValidityCell *lookup_cell, uint32_t n_args,
                                    FunctionCallAdaptation adaptation)
    {
        cache.kind = FunctionCallInlineCacheKind::Constructor;
        cache.guard_value = Value::from_oop(cls);
        cache.function = thunk.extract();
        cache.code_object = thunk.extract()->code_object.extract();
        cache.validity_cell = lookup_cell;
        cache.n_args = n_args;
        cache.adaptation = adaptation;
    }

    static ALWAYSINLINE bool
    function_call_cache_matches(const FunctionCallInlineCache &cache, Value fun,
                                uint32_t n_args)
    {
        if(cache.n_args != n_args || cache.guard_value != fun)
        {
            return false;
        }
        if(cache.kind == FunctionCallInlineCacheKind::Function)
        {
            return true;
        }
        if(cache.kind == FunctionCallInlineCacheKind::Constructor)
        {
            return cache.validity_cell != nullptr &&
                   cache.validity_cell->is_valid();
        }
        return false;
    }

    static ALWAYSINLINE void
    initialize_missing_default_arguments(Value *new_fp, TValue<Function> fun,
                                         uint32_t n_args)
    {
        uint32_t n_supplied_positional_args =
            n_args < fun.extract()->n_positional_parameters
                ? n_args
                : fun.extract()->n_positional_parameters;
        uint32_t n_missing_args =
            fun.extract()->n_positional_parameters - n_supplied_positional_args;
        if(likely(n_missing_args == 0))
        {
            return;
        }

        TValue<Tuple> defaults(fun.extract()->default_parameters.as_value());
        uint32_t first_default_idx =
            uint32_t(defaults.extract()->size()) - n_missing_args;
        CodeObject *target_code_object = fun.extract()->code_object.extract();
        for(uint32_t idx = 0; idx < n_missing_args; ++idx)
        {
            uint32_t param_idx = n_args + idx;
            new_fp[target_code_object->encode_reg(param_idx)] =
                defaults.extract()->item_unchecked(first_default_idx + idx);
        }
    }

    static ALWAYSINLINE void initialize_varargs_argument(Value *new_fp,
                                                         TValue<Function> fun,
                                                         uint32_t n_args)
    {
        uint32_t n_positional_parameters =
            fun.extract()->n_positional_parameters;
        uint32_t n_extra_args = n_args > n_positional_parameters
                                    ? n_args - n_positional_parameters
                                    : 0;
        CodeObject *target_code_object = fun.extract()->code_object.extract();
        TValue<Tuple> varargs_tuple = Tuple::from_frame_arguments(
            new_fp, target_code_object->encode_reg(n_positional_parameters),
            n_extra_args);
        new_fp[target_code_object->encode_reg(n_positional_parameters)] =
            varargs_tuple;
    }

    static ALWAYSINLINE Value *
    new_frame_pointer_from_first_arg(Value *fp, TValue<Function> fun,
                                     int32_t first_arg_reg)
    {
        int32_t new_fp_reg = first_arg_reg -
                             int32_t(fun.extract()
                                         ->code_object.extract()
                                         ->get_padded_n_parameters()) +
                             1 - FrameHeaderSizeAboveFp;
        return fp + new_fp_reg;
    }

    static ALWAYSINLINE void enter_function_frame_from_positional_args(
        Value *&fp, const uint8_t *&pc, CodeObject *&code_object,
        TValue<Function> fun, int32_t first_arg_reg, uint32_t n_args,
        uint32_t instr_len, FunctionCallAdaptation adaptation)
    {
        Value *new_fp =
            new_frame_pointer_from_first_arg(fp, fun, first_arg_reg);
        if(likely(adaptation == FunctionCallAdaptation::FixedArity))
        {
            enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                           instr_len);
            return;
        }
        if(adaptation == FunctionCallAdaptation::Defaults)
        {
            initialize_missing_default_arguments(new_fp, fun, n_args);
            enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                           instr_len);
            return;
        }

        assert(adaptation == FunctionCallAdaptation::Varargs);
        initialize_missing_default_arguments(new_fp, fun, n_args);
        initialize_varargs_argument(new_fp, fun, n_args);
        enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                       instr_len);
    }

    static ALWAYSINLINE int32_t prepare_method_call_argument_slots(
        Value *fp, int32_t receiver_reg, uint32_t n_user_args, Value self)
    {
        if(!self.is_not_present())
        {
            fp[receiver_reg] = self;
            return receiver_reg;
        }

        for(uint32_t arg_idx = 0; arg_idx < n_user_args; ++arg_idx)
        {
            int32_t target_reg = receiver_reg - int32_t(arg_idx);
            fp[target_reg] = fp[target_reg - 1];
        }
        return receiver_reg;
    }

    enum class MethodCallTargetStatus : uint8_t
    {
        Ready,
        Missing,
        RequiresDescriptorDispatch,
    };

    enum class MethodCallFastTargetStatus : uint8_t
    {
        Ready,
        Slow,
    };

    enum class AttributeLoadPlanStatus : uint8_t
    {
        Ready,
        Slow,
        RequiresDescriptorDispatch,
    };

    static ALWAYSINLINE const Value *
    object_inline_slot_base(const Object *object)
    {
        return reinterpret_cast<const Value *>(
            reinterpret_cast<const uint64_t *>(object) +
            Object::static_value_offset_in_words());
    }

    static ALWAYSINLINE Value *object_inline_slot_base(Object *object)
    {
        return reinterpret_cast<Value *>(
            reinterpret_cast<uint64_t *>(object) +
            Object::static_value_offset_in_words());
    }

    static ALWAYSINLINE const Object *
    read_plan_storage_owner(Value receiver, const AttributeReadPlan &plan)
    {
        const Object *storage_owner = plan.storage_owner;
        if(storage_owner == nullptr)
        {
            assert(receiver.is_ptr());
            storage_owner = receiver.get_ptr<Object>();
        }
        return storage_owner;
    }

    static ALWAYSINLINE Object *
    mutation_plan_storage_owner(Value receiver,
                                const AttributeMutationPlan &plan)
    {
        Object *storage_owner = plan.storage_owner;
        if(storage_owner == nullptr)
        {
            assert(receiver.is_ptr());
            storage_owner = receiver.get_ptr<Object>();
        }
        return storage_owner;
    }

    static ALWAYSINLINE AttributeLoadPlanStatus load_attr_from_plan_inline(
        Value receiver, const AttributeReadPlan &plan, Value &value_out)
    {
        switch(plan.kind)
        {
            case AttributeReadPlanKind::ReceiverSlot:
                if(plan.storage_location.kind != StorageKind::Inline)
                {
                    value_out = Value::not_present();
                    return AttributeLoadPlanStatus::Slow;
                }
                value_out = object_inline_slot_base(read_plan_storage_owner(
                    receiver, plan))[plan.storage_location.physical_idx];
                return AttributeLoadPlanStatus::Ready;

            case AttributeReadPlanKind::BindFunctionReceiver:
                if(plan.storage_location.kind != StorageKind::Inline)
                {
                    value_out = Value::not_present();
                    return AttributeLoadPlanStatus::Slow;
                }
                value_out = object_inline_slot_base(read_plan_storage_owner(
                    receiver, plan))[plan.storage_location.physical_idx];
                return AttributeLoadPlanStatus::Ready;

            case AttributeReadPlanKind::DataDescriptorGet:
            case AttributeReadPlanKind::NonDataDescriptorGet:
                value_out = Value::not_present();
                return AttributeLoadPlanStatus::RequiresDescriptorDispatch;
        }

        __builtin_unreachable();
    }

    static ALWAYSINLINE bool store_attr_from_plan_inline_fast(
        Value receiver, const AttributeMutationPlan &plan, Value value)
    {
        if(unlikely(plan.kind != AttributeMutationPlanKind::StoreExisting))
        {
            return false;
        }
        Object *storage_owner = mutation_plan_storage_owner(receiver, plan);
        if(plan.storage_kind != StorageKind::Inline)
        {
            return false;
        }
        Shape *shape = storage_owner->get_shape();
        if(shape != nullptr && shape->has_flag(ShapeFlag::IsClassObject))
        {
            return false;
        }

        Value *slots = object_inline_slot_base(storage_owner);
        Value old_value = slots[plan.physical_idx];
        slots[plan.physical_idx] = incref(value);
        decref(old_value);
        return true;
    }

    static ALWAYSINLINE bool store_attr_add_own_property_inline_fast(
        Value receiver, const AttributeMutationPlan &plan, Value value)
    {
        assert(plan.is_add_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        assert(plan.storage_kind == StorageKind::Inline);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_storage_location(plan.storage_location(), value);
        return true;
    }

    static ALWAYSINLINE bool delete_attr_delete_own_property_inline_fast(
        Value receiver, const AttributeMutationPlan &plan)
    {
        assert(plan.is_delete_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_storage_location(plan.storage_location(),
                                       Value::not_present());
        return true;
    }

    static ALWAYSINLINE MethodCallTargetStatus
    prepare_method_call_target_from_plan(Value receiver,
                                         const AttributeReadPlan &plan,
                                         Value &callable_out, Value &self_out)
    {
        self_out = Value::not_present();
        switch(plan.kind)
        {
            case AttributeReadPlanKind::ReceiverSlot:
                callable_out =
                    read_plan_storage_owner(receiver, plan)
                        ->read_storage_location(plan.storage_location);
                return MethodCallTargetStatus::Ready;

            case AttributeReadPlanKind::BindFunctionReceiver:
                callable_out =
                    read_plan_storage_owner(receiver, plan)
                        ->read_storage_location(plan.storage_location);
                // The plan records that a function won when it was created,
                // but the slot may have changed without invalidating this
                // shape-only cache. Bind only if the reloaded value is still a
                // function.
                if(callable_out.is_ptr() &&
                   callable_out.get_ptr()->native_layout_id() ==
                       NativeLayoutId::Function)
                {
                    self_out = receiver;
                }
                return MethodCallTargetStatus::Ready;

            case AttributeReadPlanKind::DataDescriptorGet:
            case AttributeReadPlanKind::NonDataDescriptorGet:
                callable_out = Value::not_present();
                return MethodCallTargetStatus::RequiresDescriptorDispatch;
        }

        __builtin_unreachable();
    }

    static ALWAYSINLINE MethodCallFastTargetStatus
    prepare_method_call_target_from_plan_fast(Value receiver,
                                              const AttributeReadPlan &plan,
                                              Value &callable_out,
                                              Value &self_out)
    {
        self_out = Value::not_present();
        switch(plan.kind)
        {
            case AttributeReadPlanKind::BindFunctionReceiver:
                callable_out =
                    read_plan_storage_owner(receiver, plan)
                        ->read_storage_location(plan.storage_location);
                // The plan survives ordinary class contents writes, so the
                // current slot value decides whether this is still a bound
                // method call target.
                if(callable_out.is_ptr() &&
                   callable_out.get_ptr()->native_layout_id() ==
                       NativeLayoutId::Function)
                {
                    self_out = receiver;
                }
                return MethodCallFastTargetStatus::Ready;

            case AttributeReadPlanKind::ReceiverSlot:
            case AttributeReadPlanKind::DataDescriptorGet:
            case AttributeReadPlanKind::NonDataDescriptorGet:
                callable_out = Value::not_present();
                return MethodCallFastTargetStatus::Slow;
        }

        __builtin_unreachable();
    }

    static ALWAYSINLINE MethodCallTargetStatus
    prepare_method_call_target_from_descriptor(
        Value receiver, const AttributeReadDescriptor &descriptor,
        Value &callable_out, Value &self_out)
    {
        if(!descriptor.is_found())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return MethodCallTargetStatus::Missing;
        }

        return prepare_method_call_target_from_plan(receiver, descriptor.plan,
                                                    callable_out, self_out);
    }

    NOINLINE static Value op_load_attr_cache_miss(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[cache_idx];
        AttributeReadDescriptor descriptor =
            resolve_attr_read_descriptor(receiver, attr_name);
        if(!descriptor.is_found())
        {
            MUSTTAIL return attribute_error(ARGS);
        }
        if(descriptor.is_cacheable())
        {
            cache.populate(receiver, descriptor);
        }
        AttributeLoadPlanStatus plan_status =
            load_attr_from_plan_inline(receiver, descriptor.plan, accumulator);
        if(unlikely(plan_status == AttributeLoadPlanStatus::Slow))
        {
            accumulator = load_attr_from_plan(receiver, descriptor.plan);
            if(unlikely(accumulator.is_not_present()))
            {
                MUSTTAIL return attribute_error(ARGS);
            }
        }
        if(unlikely(plan_status ==
                    AttributeLoadPlanStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }
        COMPLETE();
    }

    NOINLINE static Value op_store_attr_cache_miss(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        AttributeWriteDescriptor descriptor =
            resolve_attr_write_descriptor(receiver, attr_name);
        if(descriptor.is_found())
        {
            if(descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
            store_attr_from_plan(receiver, descriptor.plan, accumulator);
            COMPLETE();
        }
        if(descriptor.status == AttributeWriteStatus::NotFound &&
           receiver.is_ptr())
        {
            Object *receiver_object = receiver.get_ptr<Object>();
            Shape *receiver_shape = receiver_object->get_shape();
            if(receiver_shape != nullptr &&
               !receiver_shape->has_flag(ShapeFlag::IsClassObject) &&
               receiver_shape->allows_attribute_add_delete())
            {
                Shape *next_shape = receiver_shape->derive_transition(
                    attr_name, ShapeTransitionVerb::Add);
                StorageLocation storage_location =
                    next_shape->resolve_present_property(attr_name);
                assert(storage_location.is_found());
                if(storage_location.kind == StorageKind::Inline)
                {
                    AttributeMutationPlan plan = AttributeMutationPlan::add_own_property(
                        next_shape, storage_location,
                        receiver_object->get_class()
                            .extract()
                            ->get_or_create_mro_shape_and_contents_validity_cell());
                    cache.populate(receiver, plan);
                    store_attr_add_own_property_inline_fast(
                        receiver, cache.plan, accumulator);
                    COMPLETE();
                }
            }
            if(likely(
                   receiver_object->add_own_property(attr_name, accumulator)))
            {
                COMPLETE();
            }
        }
        MUSTTAIL return attribute_assignment_error(ARGS);
    }

    NOINLINE static Value op_del_attr_cache_miss(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        AttributeDeleteDescriptor descriptor =
            resolve_attr_delete_descriptor(receiver, attr_name);
        if(descriptor.is_found())
        {
            if(descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
            delete_attr_from_plan(receiver, descriptor.plan);
            COMPLETE();
        }
        MUSTTAIL return attribute_error(ARGS);
    }

    static Value op_lda_constant(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        accumulator = code_object->constant_table[const_offset];
        COMPLETE();
    }

    static Value op_lda_smi(PARAMS)
    {
        START(2);
        int8_t smi = pc[1];
        accumulator = Value::from_smi(smi);
        COMPLETE();
    }

    static Value op_lda_true(PARAMS)
    {
        START(1);
        accumulator = Value::True();
        COMPLETE();
    }

    static Value op_lda_false(PARAMS)
    {
        START(1);
        accumulator = Value::False();
        COMPLETE();
    }

    static Value op_lda_none(PARAMS)
    {
        START(1);
        accumulator = Value::None();
        COMPLETE();
    }

    static Value op_ldar(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = fp[reg];
        COMPLETE();
    }

    static Value op_load_local_checked(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = fp[reg];
        if(unlikely(accumulator.is_not_present()))
        {
            MUSTTAIL return name_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_clear_local(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        fp[reg] = Value::not_present();
        COMPLETE();
    }

    static Value op_star(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        fp[reg] = accumulator;

        COMPLETE();
    }

    static Value op_del_local(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        if(unlikely(fp[reg].is_not_present()))
        {
            MUSTTAIL return name_error(ARGS);
        }
        fp[reg] = Value::not_present();
        COMPLETE();
    }

#define LDAR_STAR_FASTPATH(idx)                                                \
    static Value op_ldar##idx(PARAMS)                                          \
    {                                                                          \
        START(1);                                                              \
        int8_t reg = -idx - cl::FrameHeaderSizeBelowFp - 1;                    \
        accumulator = fp[reg];                                                 \
        COMPLETE();                                                            \
    }                                                                          \
    static Value op_star##idx(PARAMS)                                          \
    {                                                                          \
        START(1);                                                              \
        int8_t reg = -idx - cl::FrameHeaderSizeBelowFp - 1;                    \
        fp[reg] = accumulator;                                                 \
        COMPLETE();                                                            \
    }

    LDAR_STAR_FASTPATH(0);
    LDAR_STAR_FASTPATH(1);
    LDAR_STAR_FASTPATH(2);
    LDAR_STAR_FASTPATH(3);
    LDAR_STAR_FASTPATH(4);
    LDAR_STAR_FASTPATH(5);
    LDAR_STAR_FASTPATH(6);
    LDAR_STAR_FASTPATH(7);
    LDAR_STAR_FASTPATH(8);
    LDAR_STAR_FASTPATH(9);
    LDAR_STAR_FASTPATH(10);
    LDAR_STAR_FASTPATH(11);
    LDAR_STAR_FASTPATH(12);
    LDAR_STAR_FASTPATH(13);
    LDAR_STAR_FASTPATH(14);
    LDAR_STAR_FASTPATH(15);
#undef LDAR_STAR_FASTPATH

    NOINLINE static Value op_lda_global_slow_path(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Value v =
            code_object->module_scope.extract()->get_by_slot_index(slot_idx);
        if(unlikely(v.is_not_present()))
        {
            MUSTTAIL return name_error(ARGS);
        }
        accumulator = v;
        COMPLETE();
    }

    static Value op_lda_global(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Value v = code_object->module_scope.extract()
                      ->get_by_slot_index_fastpath_only(slot_idx);
        if(unlikely(v.is_not_present()))
        {
            MUSTTAIL return op_lda_global_slow_path(ARGS);
        }
        accumulator = v;
        COMPLETE();
    }

    NOINLINE static Value op_sta_global_slow(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        code_object->module_scope.extract()->set_by_slot_index(slot_idx,
                                                               accumulator);
        COMPLETE();
    }

    static Value op_sta_global(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Scope *module_scope = code_object->module_scope.extract();
        if(unlikely(module_scope->set_by_slot_index_needs_slow_path(
               slot_idx, accumulator)))
        {
            MUSTTAIL return op_sta_global_slow(ARGS);
        }
        HeapObject *zct_object =
            module_scope->swap_by_slot_index(slot_idx, accumulator);
        if(unlikely(zct_object != nullptr))
        {
            add_to_active_zero_count_table(zct_object);
        }
        COMPLETE();
    }

    static Value op_del_global(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Scope *module_scope = code_object->module_scope.extract();
        if(unlikely(!module_scope->slot_is_live(slot_idx)))
        {
            MUSTTAIL return name_error(ARGS);
        }
        module_scope->set_by_slot_index(slot_idx, Value::not_present());
        COMPLETE();
    }

    static Value op_load_attr(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_load_attr_cache_miss(ARGS);
        }
        AttributeLoadPlanStatus plan_status =
            load_attr_from_plan_inline(receiver, cache.plan, accumulator);
        if(unlikely(plan_status ==
                    AttributeLoadPlanStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }
        if(unlikely(plan_status == AttributeLoadPlanStatus::Slow))
        {
            MUSTTAIL return op_load_attr_cache_miss(ARGS);
        }
        COMPLETE();
    }

    NOINLINE static Value op_store_attr_non_store_existing_cache_hit(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        if(cache.plan.is_add_own_property())
        {
            store_attr_add_own_property_inline_fast(receiver, cache.plan,
                                                    accumulator);
            COMPLETE();
        }
        MUSTTAIL return op_store_attr_cache_miss(ARGS);
    }

    static Value op_store_attr(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_store_attr_cache_miss(ARGS);
        }
        if(unlikely(!store_attr_from_plan_inline_fast(receiver, cache.plan,
                                                      accumulator)))
        {
            MUSTTAIL return op_store_attr_non_store_existing_cache_hit(ARGS);
        }
        COMPLETE();
    }

    static Value op_del_attr(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_del_attr_cache_miss(ARGS);
        }
        if(unlikely(!cache.plan.is_delete_own_property()))
        {
            MUSTTAIL return op_del_attr_cache_miss(ARGS);
        }
        delete_attr_delete_own_property_inline_fast(receiver, cache.plan);
        COMPLETE();
    }

    static Value op_load_subscript(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = load_subscript(fp[reg], accumulator);
        if(unlikely(accumulator.is_not_present()))
        {
            MUSTTAIL return subscript_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_store_subscript(PARAMS)
    {
        START(3);
        int8_t receiver_reg = pc[1];
        int8_t key_reg = pc[2];
        if(unlikely(
               !store_subscript(fp[receiver_reg], fp[key_reg], accumulator)))
        {
            MUSTTAIL return subscript_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_del_subscript(PARAMS)
    {
        START(3);
        int8_t receiver_reg = pc[1];
        int8_t key_reg = pc[2];
        if(unlikely(!del_subscript(fp[receiver_reg], fp[key_reg])))
        {
            MUSTTAIL return subscript_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_add_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_add_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer -= b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_add(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_add_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer -= a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_sub_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_sub_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer += b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_sub(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_sub_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer += a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_mul(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        Value dest;
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(),
                                              &dest.as.integer)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    static Value op_mul_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        Value dest;
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(),
                                              &dest.as.integer)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    static Value op_left_shift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        int64_t value = a.get_smi();
        int64_t sign_bits = __builtin_clrsbll(value);
        if(unlikely(uint64_t(shift_count) >= 64 ||
                    (value != 0 &&
                     shift_count + int64_t(value_tag_bits) > sign_bits)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator.as.integer =
            static_cast<int64_t>(uint64_t(a.as.integer) << shift_count);

        COMPLETE();
    }

    static Value op_left_shift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        // Bytecode invariant: codegen only emits LeftShiftSmi for shifts 0..63.
        assert(shift_count >= 0 && shift_count < 64);
        int64_t value = a.get_smi();
        int64_t sign_bits = __builtin_clrsbll(value);
        if(unlikely(value != 0 &&
                    shift_count + int64_t(value_tag_bits) > sign_bits))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator.as.integer =
            static_cast<int64_t>(uint64_t(a.as.integer) << shift_count);

        COMPLETE();
    }

    static Value op_right_shift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }

    static Value op_right_shift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }

    static Value op_test_is(PARAMS)
    {
        START_BINARY_REG_ACC();

        accumulator =
            (a.as.integer == b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_is_not(PARAMS)
    {
        START_BINARY_REG_ACC();
        accumulator =
            (a.as.integer != b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_less(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer < b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_less_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer <= b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_greater_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer >= b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_greater(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer > b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_REFCOUNTED_PTR()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        // see if we have a bit difference after we clear the bit that promotes
        // booleans to 0/1 integers
        uint64_t difference =
            (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
        accumulator = (difference == 0) ? Value::True() : Value::False();

        COMPLETE();
    }

    static Value op_test_not_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_REFCOUNTED_PTR()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        // see if we have a bit difference after we clear the bit that promotes
        // booleans to 0/1 integers
        uint64_t difference =
            (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
        accumulator = (difference != 0) ? Value::True() : Value::False();

        COMPLETE();
    }

    static Value op_negate(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        if(unlikely(__builtin_sub_overflow(0, a.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer = -accumulator.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_not(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely((a.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }
        // however, if this is an inlined type, we can simply test for
        // truthiness using a mask and negate

        if((a.as.integer & value_truthy_mask) != 0)
        {
            accumulator = Value::False();
        }
        else
        {
            accumulator = Value::True();
        }
        COMPLETE();
    }

    static Value op_create_function(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        TValue<CodeObject> code_obj(
            code_object->constant_table[const_offset].as_value());

        accumulator = make_object_value<Function>(code_obj);

        COMPLETE();
    }

    static Value op_create_function_with_defaults(PARAMS)
    {
        START(3);
        uint8_t const_offset = pc[1];
        int8_t defaults_reg = pc[2];
        TValue<CodeObject> code_obj(
            code_object->constant_table[const_offset].as_value());
        TValue<Tuple> defaults(fp[defaults_reg]);

        accumulator = make_object_value<Function>(code_obj, defaults);

        COMPLETE();
    }

    static Value op_create_instance_known_class(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        ClassObject *cls = assume_convert_to<ClassObject>(
            code_object->constant_table[const_offset].as_value());
        accumulator = Value::from_oop(make_internal_raw<Instance>(cls));

        COMPLETE();
    }

    static Value op_create_list(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<List> list = make_object_value<List>(n_items);
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            list.extract()->set_item_unchecked(idx, fp[reg - int8_t(idx)]);
        }
        accumulator = list;

        COMPLETE();
    }

    static Value op_create_tuple(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        accumulator = Tuple::from_frame_arguments(fp, reg, n_items);

        COMPLETE();
    }

    static Value op_create_dict(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<Dict> dict = make_object_value<Dict>();
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            Value key = fp[reg - int8_t(idx * 2)];
            Value value = fp[reg - int8_t(idx * 2 + 1)];
            dict.extract()->set_item(key, value);
        }
        accumulator = dict;

        COMPLETE();
    }

    static Value op_create_class(PARAMS)
    {
        static constexpr uint32_t create_class_instr_len = 3;
        uint8_t body_const_offset = pc[1];
        int8_t first_arg_reg = pc[2];
        TValue<CodeObject> body_code(
            code_object->constant_table[body_const_offset].as_value());

        const uint8_t *return_pc = pc + create_class_instr_len;
        Value *new_fp =
            fp + first_arg_reg -
            int32_t(round_up_to_abi_alignment(ClassBodyParameterCount)) + 1 -
            FrameHeaderSizeAboveFp;
        assert(is_stack_frame_aligned(new_fp));
        initialize_frame_header(new_fp, fp, code_object, return_pc);
        initialize_class_body_frame(new_fp, body_code.extract());

        fp = new_fp;
        code_object = body_code.extract();
        pc = code_object->code.data();

        START(0);
        COMPLETE();
    }

    static Value op_build_class(PARAMS)
    {
        accumulator = build_class_from_frame(fp, code_object);

        restore_frame_header(fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    static Value op_check_init_returned_none(PARAMS)
    {
        START(1);
        if(unlikely(accumulator != Value::None()))
        {
            MUSTTAIL return init_returned_non_none_error(ARGS);
        }

        COMPLETE();
    }

    static Value op_jump(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        pc += 3;
        pc += rel_target;

        START(0);
        COMPLETE();
    }

    static Value op_jump_if_true(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(unlikely((accumulator.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }

        pc += 3;
        if(accumulator.is_truthy())
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static Value op_jump_if_false(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(unlikely((accumulator.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }

        pc += 3;
        if(accumulator.is_falsy())
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static Value op_assert(PARAMS)
    {
        START(1);
        if(unlikely(!accumulator.is_ptr() && accumulator.is_falsy()))
        {
            MUSTTAIL return assertion_error(ARGS);
        }
        COMPLETE();
    }

    NOINLINE static Value op_call_simple_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 5;
        int8_t callable_reg = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        uint8_t cache_idx = pc[4];
        Value fun = fp[callable_reg];

        if(unlikely(!fun.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        Object *fun_object = fun.get_ptr();
        if(fun_object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            ClassObject *cls = static_cast<ClassObject *>(fun_object);
            ConstructorThunkLookup constructor =
                cls->get_or_create_constructor_thunk();
            if(unlikely(!constructor.is_found()))
            {
                MUSTTAIL return not_callable_error(ARGS);
            }

            TValue<Function> thunk =
                TValue<Function>::from_oop(constructor.thunk);
            if(unlikely(!thunk.extract()->accepts_arity(n_args)))
            {
                MUSTTAIL return wrong_arity_error(ARGS);
            }
            FunctionCallAdaptation adaptation =
                classify_function_call_adaptation(thunk);
            populate_constructor_call_cache(
                code_object->function_call_caches[cache_idx], cls, thunk,
                constructor.lookup_cell, n_args, adaptation);
            enter_function_frame_from_positional_args(
                fp, pc, code_object, thunk, first_arg_reg, n_args,
                call_instr_len, adaptation);

            START(0);
            COMPLETE();
        }

        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        TValue<Function> function(fun);
        if(unlikely(!function.extract()->accepts_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        FunctionCallAdaptation adaptation =
            classify_function_call_adaptation(function);
        populate_function_call_cache(
            code_object->function_call_caches[cache_idx], function, n_args,
            adaptation);
        enter_function_frame_from_positional_args(fp, pc, code_object, function,
                                                  first_arg_reg, n_args,
                                                  call_instr_len, adaptation);

        START(0);
        COMPLETE();
    }

    NOINLINE static Value op_call_simple_cached_adapt(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 5;
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        uint8_t cache_idx = pc[4];
        FunctionCallInlineCache &cache =
            code_object->function_call_caches[cache_idx];
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        enter_function_frame_from_positional_args(
            fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, cache.adaptation);

        START(0);
        COMPLETE();
    }

    static Value op_enter_prepared_function(PARAMS)
    {
        static constexpr uint32_t enter_instr_len = 4;
        uint8_t const_offset = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        TValue<Function> function(
            code_object->constant_table[const_offset].as_value());
        enter_function_frame_from_positional_args(
            fp, pc, code_object, function, first_arg_reg, n_args,
            enter_instr_len, FunctionCallAdaptation::FixedArity);

        START(0);
        COMPLETE();
    }

    static Value op_call_simple(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 5;
        int8_t callable_reg = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        uint8_t cache_idx = pc[4];
        Value fun = fp[callable_reg];
        FunctionCallInlineCache &cache =
            code_object->function_call_caches[cache_idx];

        if(unlikely(!function_call_cache_matches(cache, fun, n_args)))
        {
            MUSTTAIL return op_call_simple_slow(ARGS);
        }

        if(unlikely(cache.adaptation != FunctionCallAdaptation::FixedArity))
        {
            MUSTTAIL return op_call_simple_cached_adapt(ARGS);
        }

        CodeObject *target_code_object = cache.code_object;
        int32_t new_fp_reg =
            first_arg_reg -
            int32_t(target_code_object->get_padded_n_parameters()) + 1 -
            FrameHeaderSizeAboveFp;
        Value *new_fp = fp + new_fp_reg;
        pc += call_instr_len;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = target_code_object;
        pc = target_code_object->code.data();

        START(0);
        COMPLETE();
    }

    NOINLINE static Value op_call_method_attr_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 6;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t const_offset = pc[2];
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_args = uint8_t(pc[5]);
        Value receiver = fp[receiver_reg];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());

        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[read_cache_idx];
        Value callable;
        Value self;
        MethodCallTargetStatus target_status;
        if(cache.matches(receiver))
        {
            target_status = prepare_method_call_target_from_plan(
                receiver, cache.plan, callable, self);
        }
        else
        {
            AttributeReadDescriptor descriptor =
                resolve_attr_read_descriptor(receiver, attr_name);
            target_status = prepare_method_call_target_from_descriptor(
                receiver, descriptor, callable, self);
            if(target_status == MethodCallTargetStatus::Ready &&
               descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            MUSTTAIL return method_lookup_error(ARGS);
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);

        if(unlikely(!callable.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        Object *fun_object = callable.get_ptr();
        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        TValue<Function> function(callable);
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[call_cache_idx];
        if(function_call_cache_matches(call_cache, callable, n_args))
        {
            int32_t first_arg_reg = prepare_method_call_argument_slots(
                fp, receiver_reg, n_user_args, self);
            TValue<Function> cached_function =
                TValue<Function>::from_oop(call_cache.function);
            enter_function_frame_from_positional_args(
                fp, pc, code_object, cached_function, first_arg_reg, n_args,
                call_instr_len, call_cache.adaptation);

            START(0);
            COMPLETE();
        }
        if(unlikely(!function.extract()->accepts_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_args, self);
        FunctionCallAdaptation adaptation =
            classify_function_call_adaptation(function);
        populate_function_call_cache(call_cache, function, n_args, adaptation);
        enter_function_frame_from_positional_args(fp, pc, code_object, function,
                                                  first_arg_reg, n_args,
                                                  call_instr_len, adaptation);

        {
            START(0);
            COMPLETE();
        }
    }

    static Value op_call_method_attr(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 6;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_args = uint8_t(pc[5]);
        Value receiver = fp[receiver_reg];
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[read_cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_call_method_attr_slow(ARGS);
        }

        Value callable;
        Value self;
        MethodCallFastTargetStatus target_status =
            prepare_method_call_target_from_plan_fast(receiver, cache.plan,
                                                      callable, self);
        if(unlikely(target_status == MethodCallFastTargetStatus::Slow))
        {
            MUSTTAIL return op_call_method_attr_slow(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[call_cache_idx];

        if(likely(function_call_cache_matches(call_cache, callable, n_args)))
        {
            int32_t first_arg_reg = prepare_method_call_argument_slots(
                fp, receiver_reg, n_user_args, self);
            TValue<Function> function =
                TValue<Function>::from_oop(call_cache.function);
            enter_function_frame_from_positional_args(
                fp, pc, code_object, function, first_arg_reg, n_args,
                call_instr_len, call_cache.adaptation);

            START(0);
            COMPLETE();
        }

        if(unlikely(!callable.is_ptr()))
        {
            MUSTTAIL return op_call_method_attr_slow(ARGS);
        }

        Object *fun_object = callable.get_ptr();
        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return op_call_method_attr_slow(ARGS);
        }

        TValue<Function> function(callable);
        if(unlikely(!function.extract()->accepts_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_args, self);
        FunctionCallAdaptation adaptation =
            classify_function_call_adaptation(function);
        populate_function_call_cache(call_cache, function, n_args, adaptation);
        enter_function_frame_from_positional_args(fp, pc, code_object, function,
                                                  first_arg_reg, n_args,
                                                  call_instr_len, adaptation);

        {
            START(0);
            COMPLETE();
        }
    }

    static ALWAYSINLINE Value get_native_arg(Value *fp, CodeObject *code_object,
                                             uint32_t arg_idx)
    {
        int32_t reg = int32_t(code_object->get_padded_n_parameters()) - 1 +
                      FrameHeaderSizeAboveFp - int32_t(arg_idx);
        return fp[reg];
    }

    static Value op_call_native0(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        accumulator = code_object->native_function_targets[target_idx].fixed0();
        COMPLETE();
    }

    static Value op_call_native1(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        accumulator = code_object->native_function_targets[target_idx].fixed1(
            get_native_arg(fp, code_object, 0));
        COMPLETE();
    }

    static Value op_call_native2(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        accumulator = code_object->native_function_targets[target_idx].fixed2(
            get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1));
        COMPLETE();
    }

    static Value op_call_native3(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        accumulator = code_object->native_function_targets[target_idx].fixed3(
            get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1),
            get_native_arg(fp, code_object, 2));
        COMPLETE();
    }

    static Value op_get_iter(PARAMS)
    {
        START(1);
        if(unlikely(!accumulator.is_ptr()))
        {
            MUSTTAIL return not_iterable_error(ARGS);
        }

        Object *iterator_object = accumulator.get_ptr();
        if(unlikely(iterator_object->native_layout_id() !=
                    NativeLayoutId::RangeIterator))
        {
            MUSTTAIL return not_iterable_error(ARGS);
        }

        COMPLETE();
    }

    static Value op_for_iter(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value iterator_value = fp[reg];

        if(unlikely(!iterator_value.is_ptr()))
        {
            MUSTTAIL return not_iterator_error(ARGS);
        }

        Object *iterator_object = iterator_value.get_ptr();
        if(unlikely(iterator_object->native_layout_id() !=
                    NativeLayoutId::RangeIterator))
        {
            MUSTTAIL return not_iterator_error(ARGS);
        }

        RangeIterator *iterator = static_cast<RangeIterator *>(iterator_object);
        Value current = iterator->current;
        Value stop = iterator->stop;
        Value step = iterator->step;

        if(unlikely(!current.is_smi() || !stop.is_smi() || !step.is_smi()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();

        if(unlikely(step_smi == 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        bool exhausted =
            step_smi > 0 ? current_smi >= stop_smi : current_smi <= stop_smi;
        pc += 4;
        if(exhausted)
        {
            pc += rel_target;
        }
        else
        {
            int64_t next_smi = 0;
            if(unlikely(
                   __builtin_add_overflow(current_smi, step_smi, &next_smi)))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator = current;
            iterator->current = Value::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_prep_range1(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable != active_vm()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value stop = fp[reg - 1];
            if(unlikely(!stop.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            fp[reg] = Value::from_smi(0);
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_prep_range2(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable != active_vm()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value start = fp[reg - 1];
            Value stop = fp[reg - 2];
            if(unlikely(!start.is_integer() || !stop.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            fp[reg] = start;
            fp[reg - 1] = stop;
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_prep_range3(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable != active_vm()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value start = fp[reg - 1];
            Value stop = fp[reg - 2];
            Value step = fp[reg - 3];
            if(unlikely(!start.is_integer() || !stop.is_integer() ||
                        !step.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            if(unlikely(step.get_smi() == 0))
            {
                MUSTTAIL return range_zero_step_error(ARGS);
            }
            fp[reg] = start;
            fp[reg - 1] = stop;
            fp[reg - 2] = step;
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_iter_range1(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value current = fp[reg];
        Value stop = fp[reg - 1];

        if(unlikely(!current.is_smi() || !stop.is_smi()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        pc += 4;
        if(current_smi >= stop_smi)
        {
            pc += rel_target;
        }
        else
        {
            int64_t next_smi = 0;
            if(unlikely(__builtin_add_overflow(current_smi, 1, &next_smi)))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator = current;
            fp[reg] = Value::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_iter_range_step(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value current = fp[reg];
        Value stop = fp[reg - 1];
        Value step = fp[reg - 2];

        if(unlikely(!current.is_smi() || !stop.is_smi() || !step.is_smi()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();
        if(unlikely(step_smi == 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        bool exhausted =
            step_smi > 0 ? current_smi >= stop_smi : current_smi <= stop_smi;
        pc += 4;
        if(exhausted)
        {
            pc += rel_target;
        }
        else
        {
            int64_t next_smi = 0;
            if(unlikely(
                   __builtin_add_overflow(current_smi, step_smi, &next_smi)))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator = current;
            fp[reg] = Value::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static Value op_return(PARAMS)
    {
        restore_frame_header(fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    static Value op_halt(PARAMS)
    {
        START(1);
        return accumulator;
        COMPLETE();
    }

    DispatchTable make_dispatch_table()
    {
        DispatchTable tbl;
        for(size_t i = 0; i < BytecodeTableSize; ++i)
        {
            tbl.table[i] = raise_unknown_opcode_exception;
        }
#define SET_TABLE_ENTRY(bytecode, fun) tbl.table[size_t(bytecode)] = fun
        SET_TABLE_ENTRY(Bytecode::Ldar, op_ldar);
        SET_TABLE_ENTRY(Bytecode::LoadLocalChecked, op_load_local_checked);
        SET_TABLE_ENTRY(Bytecode::ClearLocal, op_clear_local);
        SET_TABLE_ENTRY(Bytecode::Star, op_star);
        SET_TABLE_ENTRY(Bytecode::LdaConstant, op_lda_constant);
        SET_TABLE_ENTRY(Bytecode::LdaSmi, op_lda_smi);
        SET_TABLE_ENTRY(Bytecode::LdaTrue, op_lda_true);
        SET_TABLE_ENTRY(Bytecode::LdaFalse, op_lda_false);
        SET_TABLE_ENTRY(Bytecode::LdaNone, op_lda_none);
        SET_TABLE_ENTRY(Bytecode::Add, op_add);
        SET_TABLE_ENTRY(Bytecode::AddSmi, op_add_smi);
        SET_TABLE_ENTRY(Bytecode::Sub, op_sub);
        SET_TABLE_ENTRY(Bytecode::SubSmi, op_sub_smi);
        SET_TABLE_ENTRY(Bytecode::Mul, op_mul);
        SET_TABLE_ENTRY(Bytecode::MulSmi, op_mul_smi);

        SET_TABLE_ENTRY(Bytecode::LeftShift, op_left_shift);
        SET_TABLE_ENTRY(Bytecode::LeftShiftSmi, op_left_shift_smi);
        SET_TABLE_ENTRY(Bytecode::RightShift, op_right_shift);
        SET_TABLE_ENTRY(Bytecode::RightShiftSmi, op_right_shift_smi);

        SET_TABLE_ENTRY(Bytecode::TestIs, op_test_is);
        SET_TABLE_ENTRY(Bytecode::TestIsNot, op_test_is_not);
        SET_TABLE_ENTRY(Bytecode::TestEqual, op_test_equal);
        SET_TABLE_ENTRY(Bytecode::TestNotEqual, op_test_not_equal);
        SET_TABLE_ENTRY(Bytecode::TestLess, op_test_less);
        SET_TABLE_ENTRY(Bytecode::TestLessEqual, op_test_less_equal);
        SET_TABLE_ENTRY(Bytecode::TestGreaterEqual, op_test_greater_equal);
        SET_TABLE_ENTRY(Bytecode::TestGreater, op_test_greater);

        SET_TABLE_ENTRY(Bytecode::LdaGlobal, op_lda_global);
        SET_TABLE_ENTRY(Bytecode::StaGlobal, op_sta_global);
        SET_TABLE_ENTRY(Bytecode::DelGlobal, op_del_global);
        SET_TABLE_ENTRY(Bytecode::DelLocal, op_del_local);
        SET_TABLE_ENTRY(Bytecode::LoadAttr, op_load_attr);
        SET_TABLE_ENTRY(Bytecode::StoreAttr, op_store_attr);
        SET_TABLE_ENTRY(Bytecode::DelAttr, op_del_attr);
        SET_TABLE_ENTRY(Bytecode::LoadSubscript, op_load_subscript);
        SET_TABLE_ENTRY(Bytecode::StoreSubscript, op_store_subscript);
        SET_TABLE_ENTRY(Bytecode::DelSubscript, op_del_subscript);
        SET_TABLE_ENTRY(Bytecode::CallMethodAttr, op_call_method_attr);

        SET_TABLE_ENTRY(Bytecode::Negate, op_negate);
        SET_TABLE_ENTRY(Bytecode::Not, op_not);

        SET_TABLE_ENTRY(Bytecode::CreateDict, op_create_dict);
        SET_TABLE_ENTRY(Bytecode::CreateList, op_create_list);
        SET_TABLE_ENTRY(Bytecode::CreateTuple, op_create_tuple);
        SET_TABLE_ENTRY(Bytecode::CreateFunction, op_create_function);
        SET_TABLE_ENTRY(Bytecode::CreateFunctionWithDefaults,
                        op_create_function_with_defaults);
        SET_TABLE_ENTRY(Bytecode::CreateInstanceKnownClass,
                        op_create_instance_known_class);
        SET_TABLE_ENTRY(Bytecode::CreateClass, op_create_class);
        SET_TABLE_ENTRY(Bytecode::BuildClass, op_build_class);
        SET_TABLE_ENTRY(Bytecode::CheckInitReturnedNone,
                        op_check_init_returned_none);
        SET_TABLE_ENTRY(Bytecode::Assert, op_assert);

        SET_TABLE_ENTRY(Bytecode::CallSimple, op_call_simple);
        SET_TABLE_ENTRY(Bytecode::CallNative0, op_call_native0);
        SET_TABLE_ENTRY(Bytecode::CallNative1, op_call_native1);
        SET_TABLE_ENTRY(Bytecode::CallNative2, op_call_native2);
        SET_TABLE_ENTRY(Bytecode::CallNative3, op_call_native3);
        SET_TABLE_ENTRY(Bytecode::EnterPreparedFunction,
                        op_enter_prepared_function);
        SET_TABLE_ENTRY(Bytecode::GetIter, op_get_iter);
        SET_TABLE_ENTRY(Bytecode::ForIter, op_for_iter);
        SET_TABLE_ENTRY(Bytecode::ForPrepRange1, op_for_prep_range1);
        SET_TABLE_ENTRY(Bytecode::ForPrepRange2, op_for_prep_range2);
        SET_TABLE_ENTRY(Bytecode::ForPrepRange3, op_for_prep_range3);
        SET_TABLE_ENTRY(Bytecode::ForIterRange1, op_for_iter_range1);
        SET_TABLE_ENTRY(Bytecode::ForIterRangeStep, op_for_iter_range_step);

        SET_TABLE_ENTRY(Bytecode::Jump, op_jump);
        SET_TABLE_ENTRY(Bytecode::JumpIfTrue, op_jump_if_true);
        SET_TABLE_ENTRY(Bytecode::JumpIfFalse, op_jump_if_false);
        SET_TABLE_ENTRY(Bytecode::Return, op_return);
        SET_TABLE_ENTRY(Bytecode::Halt, op_halt);

#define REGISTER_LDAR_STAR_FASTPATH(idx)                                       \
    SET_TABLE_ENTRY(Bytecode::Ldar##idx, op_ldar##idx);                        \
    SET_TABLE_ENTRY(Bytecode::Star##idx, op_star##idx);

        REGISTER_LDAR_STAR_FASTPATH(0);
        REGISTER_LDAR_STAR_FASTPATH(1);
        REGISTER_LDAR_STAR_FASTPATH(2);
        REGISTER_LDAR_STAR_FASTPATH(3);
        REGISTER_LDAR_STAR_FASTPATH(4);
        REGISTER_LDAR_STAR_FASTPATH(5);
        REGISTER_LDAR_STAR_FASTPATH(6);
        REGISTER_LDAR_STAR_FASTPATH(7);
        REGISTER_LDAR_STAR_FASTPATH(8);
        REGISTER_LDAR_STAR_FASTPATH(9);
        REGISTER_LDAR_STAR_FASTPATH(10);
        REGISTER_LDAR_STAR_FASTPATH(11);
        REGISTER_LDAR_STAR_FASTPATH(12);
        REGISTER_LDAR_STAR_FASTPATH(13);
        REGISTER_LDAR_STAR_FASTPATH(14);
        REGISTER_LDAR_STAR_FASTPATH(15);
#undef REGISTER_LDAR_STAR_FASTPATH

        return tbl;
    }

    DispatchTable dispatch_table = make_dispatch_table();

    Value run_interpreter(Value *fp, CodeObject *code_object, uint32_t start_pc)
    {
        assert(is_stack_frame_aligned(fp));
        const uint8_t *pc = &code_object->code[start_pc];
        void *dispatch = reinterpret_cast<void *>(&dispatch_table);
        Value accumulator = Value::from_smi(0);  // init accumulator to 0

        // do the initial dispatch
        auto *dispatch_fun = dispatch_table.table[*pc];
        return dispatch_fun(ARGS);
    }

}  // namespace cl
