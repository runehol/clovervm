#include "runtime/operator_walk.h"

#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/object.h"
#include "runtime/method_call.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cassert>

namespace cl
{
    OperatorWalkDescriptor OperatorWalkDescriptor::native_result(Value result)
    {
        OperatorWalkDescriptor descriptor;
        descriptor.status = OperatorWalkStatus::NativeResult;
        descriptor.result = result;
        return descriptor;
    }

    OperatorWalkDescriptor OperatorWalkDescriptor::propagate_pending_exception()
    {
        OperatorWalkDescriptor descriptor;
        descriptor.status = OperatorWalkStatus::PropagatePendingException;
        return descriptor;
    }

    OperatorWalkDescriptor OperatorWalkDescriptor::call_untrusted_function(
        OperatorStepAction action, uint32_t resume_index,
        OperatorOperandOrder operand_order, ShapeKey operand0_shape_key,
        ShapeKey operand1_shape_key, ShapeKey operand2_shape_key,
        TValue<Function> function, uint32_t n_args,
        FunctionCallAdaptation adaptation, bool has_self,
        ValidityCell *operand0_lookup_validity_cell,
        ValidityCell *operand1_lookup_validity_cell)
    {
        OperatorWalkDescriptor descriptor;
        descriptor.status = OperatorWalkStatus::CallUntrustedFunction;
        descriptor.action = action;
        descriptor.resume_index = resume_index;
        descriptor.operand_order = operand_order;
        descriptor.cache_entry = OperatorInlineCache::untrusted_function_call(
            operand0_shape_key, operand1_shape_key, operand2_shape_key,
            function.extract(), function.extract()->code_object.extract(),
            n_args, resume_index,
            operand_order == OperatorOperandOrder::Reflected, has_self,
            adaptation, operand0_lookup_validity_cell,
            operand1_lookup_validity_cell);
        return descriptor;
    }

    OperatorWalkDescriptor OperatorWalkDescriptor::call_trusted_handler(
        OperatorStepAction action, ShapeKey operand0_shape_key,
        ShapeKey operand1_shape_key, ShapeKey operand2_shape_key,
        TrustedResolution resolution,
        ValidityCell *operand0_lookup_validity_cell,
        ValidityCell *operand1_lookup_validity_cell)
    {
        OperatorWalkDescriptor descriptor;
        descriptor.status = OperatorWalkStatus::CallTrustedHandler;
        descriptor.action = action;
        descriptor.cache_entry = OperatorInlineCache::trusted_handler_call(
            operand0_shape_key, operand1_shape_key, operand2_shape_key,
            resolution, operand0_lookup_validity_cell,
            operand1_lookup_validity_cell);
        return descriptor;
    }

    static bool operator_step_is_reflected(OperatorStepAction action)
    {
        return operator_step_action_is_reflected(action);
    }

    static TrustedHandlerOperandOrder
    trusted_handler_operand_order_for(OperatorOperandOrder order)
    {
        return order == OperatorOperandOrder::Reflected
                   ? TrustedHandlerOperandOrder::Reflected
                   : TrustedHandlerOperandOrder::Normal;
    }

    static ClassObject *class_of_operand(ThreadState *thread, Value operand)
    {
        return thread->class_of_value(operand);
    }

    static ValidityCell *
    operator_lookup_validity_cell_for_operand(ThreadState *thread,
                                              Value operand)
    {
        return class_of_operand(thread, operand)
            ->get_or_create_mro_shape_and_contents_validity_cell();
    }

    [[noreturn]] static void debug_operator_table_exhausted()
    {
        assert(false && "operator dispatch table exhausted");
        __builtin_unreachable();
    }

    static OperatorWalkDescriptor
    operator_walk_raise_type_error(ThreadState *thread, const wchar_t *message)
    {
        (void)thread->set_pending_builtin_exception_string(L"TypeError",
                                                           message);
        return OperatorWalkDescriptor::propagate_pending_exception();
    }

    static OperatorWalkDescriptor
    operator_walk_raise_unsupported(ThreadState *thread,
                                    OperatorDispatchTableId table_id)
    {
        switch(table_id)
        {
            case OperatorDispatchTableId::Add:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for +");

            case OperatorDispatchTableId::Sub:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for -");

            case OperatorDispatchTableId::Mul:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for *");

            case OperatorDispatchTableId::BinaryPow:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for **");

            case OperatorDispatchTableId::TrueDiv:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for /");

            case OperatorDispatchTableId::FloorDiv:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for //");

            case OperatorDispatchTableId::Mod:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for %");

            case OperatorDispatchTableId::LShift:
            case OperatorDispatchTableId::RShift:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for shift");

            case OperatorDispatchTableId::And:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for &");

            case OperatorDispatchTableId::Xor:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for ^");

            case OperatorDispatchTableId::Or:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type(s) for |");

            case OperatorDispatchTableId::Neg:
            case OperatorDispatchTableId::Pos:
            case OperatorDispatchTableId::Invert:
                return operator_walk_raise_type_error(
                    thread, L"unsupported operand type for unary arithmetic");

            case OperatorDispatchTableId::CompareEq:
            case OperatorDispatchTableId::CompareNe:
            case OperatorDispatchTableId::CompareLt:
            case OperatorDispatchTableId::CompareLe:
            case OperatorDispatchTableId::CompareGt:
            case OperatorDispatchTableId::CompareGe:
            case OperatorDispatchTableId::Count:
                break;
        }

        assert(false && "unsupported operator fallback for table");
        __builtin_unreachable();
    }

    static bool resolve_applicable_operator_method(
        ThreadState *thread, const OperatorStep &step, Value receiver,
        Value operand0, Value operand1, TValue<String> method_name,
        AttributeReadDescriptor &method_descriptor)
    {
        method_descriptor = AttributeReadDescriptor::not_found();
        switch(step.applicability)
        {
            case OperatorStepApplicability::Always:
            case OperatorStepApplicability::IfMethodFound:
                method_descriptor = resolve_special_method_read_descriptor(
                    receiver, method_name);
                return method_descriptor.is_found();

            case OperatorStepApplicability::
                IfMethodFoundAndOperands01TypesDiffer:
                if(class_of_operand(thread, operand0) ==
                   class_of_operand(thread, operand1))
                {
                    return false;
                }
                method_descriptor = resolve_special_method_read_descriptor(
                    receiver, method_name);
                return method_descriptor.is_found();

            case OperatorStepApplicability::IfArithmeticReflectedPriority:
                assert(operator_step_is_reflected(step.action));
                method_descriptor =
                    resolve_reflected_priority_special_method_read_descriptor(
                        receiver, operand0, method_name);
                return method_descriptor.is_found();

            case OperatorStepApplicability::IfRichComparisonReflectedPriority:
                {
                    ClassObject *left_class =
                        class_of_operand(thread, operand0);
                    ClassObject *right_class =
                        class_of_operand(thread, operand1);
                    if(left_class == right_class ||
                       !is_subclass_of(right_class, left_class))
                    {
                        return false;
                    }
                    method_descriptor = resolve_special_method_read_descriptor(
                        receiver, method_name);
                    return method_descriptor.is_found();
                }
        }

        __builtin_unreachable();
    }

    OperatorWalkDescriptor
    walk_operator_table(ThreadState *thread, OperatorDispatchTableId table_id,
                        uint32_t start_index, OperatorCacheability cacheability,
                        Value operand0, Value operand1, Value operand2)
    {
        VirtualMachine *vm = thread->get_machine();
        const OperatorDispatchTable &table =
            vm->operator_dispatch_table(table_id);
        ShapeKey operand0_shape_key = ShapeKey::from_value(operand0);
        ShapeKey operand1_shape_key = ShapeKey::from_value(operand1);
        ShapeKey operand2_shape_key = ShapeKey::from_value(operand2);

        uint32_t index = start_index;
        while(index < table.n_steps)
        {
            const OperatorStep &step = table.step(uint8_t(index));
            uint32_t failed_applicability_index = index + 1 + step.else_skip;

            switch(step.action)
            {
                case OperatorStepAction::IdentityEq:
                    assert(step.applicability ==
                           OperatorStepApplicability::Always);
                    return OperatorWalkDescriptor::native_result(
                        operand0 == operand1 ? Value::True() : Value::False());

                case OperatorStepAction::IdentityNe:
                    assert(step.applicability ==
                           OperatorStepApplicability::Always);
                    return OperatorWalkDescriptor::native_result(
                        operand0 != operand1 ? Value::True() : Value::False());

                case OperatorStepAction::RaiseOrdering:
                    assert(step.applicability ==
                           OperatorStepApplicability::Always);
                    return operator_walk_raise_type_error(
                        thread, L"unsupported operand type(s) for comparison");

                case OperatorStepAction::RaiseUnsupported:
                    assert(step.applicability ==
                           OperatorStepApplicability::Always);
                    return operator_walk_raise_unsupported(thread, table_id);

                case OperatorStepAction::CallUnary:
                    {
                        assert(step.dunder_name != nullptr);

                        Value receiver = operand0;
                        AttributeReadDescriptor method_descriptor =
                            AttributeReadDescriptor::not_found();
                        TValue<String> method_name =
                            TValue<String>::from_oop(step.dunder_name);
                        if(!resolve_applicable_operator_method(
                               thread, step, receiver, operand0, operand1,
                               method_name, method_descriptor))
                        {
                            index = failed_applicability_index;
                            continue;
                        }

                        Value callable;
                        Value self;
                        MethodCallTargetStatus target_status =
                            prepare_method_call_target_from_descriptor(
                                receiver, method_descriptor, callable, self);
                        if(target_status ==
                           MethodCallTargetStatus::RequiresDescriptorDispatch)
                        {
                            return operator_walk_raise_type_error(
                                thread, L"descriptor __get__ requires "
                                        L"interpreter dispatch");
                        }
                        if(target_status == MethodCallTargetStatus::Missing)
                        {
                            assert(false && "applicable operator method "
                                            "missing after selection");
                            __builtin_unreachable();
                        }
                        if(!callable.is_ptr())
                        {
                            return operator_walk_raise_type_error(
                                thread, L"object is not callable");
                        }

                        Object *callable_object = callable.get_ptr();
                        if(callable_object->native_layout_id() !=
                           NativeLayoutId::Function)
                        {
                            return operator_walk_raise_type_error(
                                thread, L"object is not callable");
                        }

                        TValue<Function> function =
                            TValue<Function>::from_value_assumed(callable);
                        bool has_self = !self.is_not_present();
                        uint32_t n_args = has_self ? 1 : 0;
                        if(!function.extract()
                                ->accepts_positional_only_call_arity(n_args))
                        {
                            return operator_walk_raise_type_error(
                                thread, L"wrong number of arguments");
                        }

                        FunctionCallAdaptation adaptation =
                            function_call_adaptation_for_positional_call(
                                function, n_args);
                        TrustedResolution trusted_resolution =
                            TrustedResolution::
                                no_trusted_handler_call_untrusted();
                        CodeObject *target_code_object =
                            function.extract()->code_object.extract();
                        if(target_code_object->trusted_handler_resolver !=
                           nullptr)
                        {
                            TrustedResolution resolution =
                                target_code_object->trusted_handler_resolver(
                                    vm, operand0_shape_key, operand1_shape_key,
                                    operand2_shape_key,
                                    TrustedHandlerOperandOrder::Normal,
                                    TrustedHandlerArity::Unary);
                            switch(resolution.kind)
                            {
                                case TrustedResolutionKind::
                                    NoTrustedHandlerCallUntrusted:
                                    break;

                                case TrustedResolutionKind::TrustedHandler:
                                    assert(resolution.arity ==
                                           TrustedHandlerArity::Unary);
                                    trusted_resolution = resolution;
                                    break;

                                case TrustedResolutionKind::
                                    KnownNotImplementedSkipMethod:
                                    index = failed_applicability_index;
                                    continue;
                            }
                        }

                        ValidityCell *operand0_lookup_validity_cell = nullptr;
                        if(cacheability != OperatorCacheability::Uncacheable)
                        {
                            operand0_lookup_validity_cell =
                                operator_lookup_validity_cell_for_operand(
                                    thread, operand0);
                        }

                        if(trusted_resolution.has_trusted_handler())
                        {
                            return OperatorWalkDescriptor::call_trusted_handler(
                                step.action, operand0_shape_key,
                                operand1_shape_key, operand2_shape_key,
                                trusted_resolution,
                                operand0_lookup_validity_cell, nullptr);
                        }

                        return OperatorWalkDescriptor::call_untrusted_function(
                            step.action, index + 1,
                            OperatorOperandOrder::Normal, operand0_shape_key,
                            operand1_shape_key, operand2_shape_key, function,
                            n_args, adaptation, has_self,
                            operand0_lookup_validity_cell, nullptr);
                    }

                case OperatorStepAction::CallBinary:
                case OperatorStepAction::CallBinaryReflected:
                    {
                        assert(step.dunder_name != nullptr);

                        bool reflected =
                            operator_step_is_reflected(step.action);
                        OperatorOperandOrder operand_order =
                            reflected ? OperatorOperandOrder::Reflected
                                      : OperatorOperandOrder::Normal;
                        Value receiver = reflected ? operand1 : operand0;

                        AttributeReadDescriptor method_descriptor =
                            AttributeReadDescriptor::not_found();
                        TValue<String> method_name =
                            TValue<String>::from_oop(step.dunder_name);
                        if(!resolve_applicable_operator_method(
                               thread, step, receiver, operand0, operand1,
                               method_name, method_descriptor))
                        {
                            index = failed_applicability_index;
                            continue;
                        }

                        Value callable;
                        Value self;
                        MethodCallTargetStatus target_status =
                            prepare_method_call_target_from_descriptor(
                                receiver, method_descriptor, callable, self);
                        if(target_status ==
                           MethodCallTargetStatus::RequiresDescriptorDispatch)
                        {
                            return operator_walk_raise_type_error(
                                thread, L"descriptor __get__ requires "
                                        L"interpreter dispatch");
                        }
                        if(target_status == MethodCallTargetStatus::Missing)
                        {
                            assert(false && "applicable operator method "
                                            "missing after selection");
                            __builtin_unreachable();
                        }
                        if(!callable.is_ptr())
                        {
                            return operator_walk_raise_type_error(
                                thread, L"object is not callable");
                        }

                        Object *callable_object = callable.get_ptr();
                        if(callable_object->native_layout_id() !=
                           NativeLayoutId::Function)
                        {
                            return operator_walk_raise_type_error(
                                thread, L"object is not callable");
                        }

                        TValue<Function> function =
                            TValue<Function>::from_value_assumed(callable);
                        bool has_self = !self.is_not_present();
                        uint32_t n_args = 1 + (has_self ? 1 : 0);
                        if(!function.extract()
                                ->accepts_positional_only_call_arity(n_args))
                        {
                            return operator_walk_raise_type_error(
                                thread, L"wrong number of arguments");
                        }

                        FunctionCallAdaptation adaptation =
                            function_call_adaptation_for_positional_call(
                                function, n_args);
                        TrustedResolution trusted_resolution =
                            TrustedResolution::
                                no_trusted_handler_call_untrusted();
                        CodeObject *target_code_object =
                            function.extract()->code_object.extract();
                        if(target_code_object->trusted_handler_resolver !=
                           nullptr)
                        {
                            TrustedResolution resolution =
                                target_code_object->trusted_handler_resolver(
                                    vm, operand0_shape_key, operand1_shape_key,
                                    operand2_shape_key,
                                    trusted_handler_operand_order_for(
                                        operand_order),
                                    TrustedHandlerArity::Binary);
                            switch(resolution.kind)
                            {
                                case TrustedResolutionKind::
                                    NoTrustedHandlerCallUntrusted:
                                    break;

                                case TrustedResolutionKind::TrustedHandler:
                                    assert(resolution.arity ==
                                           TrustedHandlerArity::Binary);
                                    trusted_resolution = resolution;
                                    break;

                                case TrustedResolutionKind::
                                    KnownNotImplementedSkipMethod:
                                    index = failed_applicability_index;
                                    continue;
                            }
                        }

                        ValidityCell *operand0_lookup_validity_cell = nullptr;
                        ValidityCell *operand1_lookup_validity_cell = nullptr;
                        if(cacheability != OperatorCacheability::Uncacheable)
                        {
                            operand0_lookup_validity_cell =
                                operator_lookup_validity_cell_for_operand(
                                    thread, operand0);
                            if(cacheability ==
                               OperatorCacheability::CacheableMaybeReflected)
                            {
                                operand1_lookup_validity_cell =
                                    operator_lookup_validity_cell_for_operand(
                                        thread, operand1);
                            }
                        }

                        if(trusted_resolution.has_trusted_handler())
                        {
                            return OperatorWalkDescriptor::call_trusted_handler(
                                step.action, operand0_shape_key,
                                operand1_shape_key, operand2_shape_key,
                                trusted_resolution,
                                operand0_lookup_validity_cell,
                                operand1_lookup_validity_cell);
                        }

                        return OperatorWalkDescriptor::call_untrusted_function(
                            step.action, index + 1, operand_order,
                            operand0_shape_key, operand1_shape_key,
                            operand2_shape_key, function, n_args, adaptation,
                            has_self, operand0_lookup_validity_cell,
                            operand1_lookup_validity_cell);
                    }
            }
        }

        debug_operator_table_exhausted();
    }

}  // namespace cl
