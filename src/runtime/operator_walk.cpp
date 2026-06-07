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

    OperatorWalkDescriptor OperatorWalkDescriptor::call_python_function(
        OperatorStepAction action, uint32_t resume_index,
        OperatorOperandOrder operand_order, Value receiver,
        const AttributeReadDescriptor &method_descriptor,
        ShapeKey arg_shape_key, TValue<Function> function, uint32_t n_args,
        FunctionCallAdaptation adaptation, bool has_self)
    {
        OperatorWalkDescriptor descriptor;
        descriptor.status = OperatorWalkStatus::CallPythonFunction;
        descriptor.action = action;
        descriptor.resume_index = resume_index;
        descriptor.operand_order = operand_order;
        descriptor.cache_entry = OperatorInlineCache::python_function_call(
            receiver, method_descriptor, arg_shape_key, function.extract(),
            function.extract()->code_object.extract(), n_args, has_self,
            adaptation);
        return descriptor;
    }

    OperatorWalkDescriptor OperatorWalkDescriptor::call_trusted_handler(
        OperatorStepAction action, Value receiver,
        const AttributeReadDescriptor &method_descriptor,
        ShapeKey arg_shape_key, TrustedHandler handler)
    {
        OperatorWalkDescriptor descriptor;
        descriptor.status = OperatorWalkStatus::CallTrustedHandler;
        descriptor.action = action;
        descriptor.cache_entry = OperatorInlineCache::trusted_handler_call(
            receiver, method_descriptor, arg_shape_key, handler);
        return descriptor;
    }

    static bool operator_step_is_reflected(OperatorStepAction action)
    {
        return action == OperatorStepAction::CallBinaryReflected;
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

    OperatorWalkDescriptor walk_operator_table(ThreadState *thread,
                                               OperatorDispatchTableId table_id,
                                               uint32_t start_index,
                                               Value operand0, Value operand1)
    {
        VirtualMachine *vm = thread->get_machine();
        const OperatorDispatchTable &table =
            vm->operator_dispatch_table(table_id);
        ShapeKey operand0_shape_key = ShapeKey::from_value(operand0);
        ShapeKey operand1_shape_key = ShapeKey::from_value(operand1);

        uint32_t index = start_index;
        while(index < table.n_steps)
        {
            const OperatorStep &step = table.step(uint8_t(index));
            uint32_t failed_applicability_index = index + 1 + step.else_skip;

            if(step.action == OperatorStepAction::IdentityEq)
            {
                assert(step.applicability == OperatorStepApplicability::Always);
                return OperatorWalkDescriptor::native_result(
                    operand0 == operand1 ? Value::True() : Value::False());
            }

            assert(step.action == OperatorStepAction::CallBinary ||
                   step.action == OperatorStepAction::CallBinaryReflected);
            assert(step.dunder_name != nullptr);

            bool reflected = operator_step_is_reflected(step.action);
            OperatorOperandOrder operand_order =
                reflected ? OperatorOperandOrder::Reflected
                          : OperatorOperandOrder::Normal;
            Value receiver = reflected ? operand1 : operand0;
            ShapeKey arg_shape_key =
                reflected ? operand0_shape_key : operand1_shape_key;

            AttributeReadDescriptor method_descriptor =
                AttributeReadDescriptor::not_found();
            TValue<String> method_name =
                TValue<String>::from_oop(step.dunder_name);
            if(!resolve_applicable_operator_method(
                   thread, step, receiver, operand0, operand1, method_name,
                   method_descriptor))
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
                    thread,
                    L"descriptor __get__ requires interpreter dispatch");
            }
            if(target_status == MethodCallTargetStatus::Missing)
            {
                assert(false &&
                       "applicable operator method missing after selection");
                __builtin_unreachable();
            }
            if(!callable.is_ptr())
            {
                return operator_walk_raise_type_error(
                    thread, L"object is not callable");
            }

            Object *callable_object = callable.get_ptr();
            if(callable_object->native_layout_id() != NativeLayoutId::Function)
            {
                return operator_walk_raise_type_error(
                    thread, L"object is not callable");
            }

            TValue<Function> function =
                TValue<Function>::from_value_assumed(callable);
            bool has_self = !self.is_not_present();
            uint32_t n_args = 1 + (has_self ? 1 : 0);
            if(!function.extract()->accepts_positional_only_call_arity(n_args))
            {
                return operator_walk_raise_type_error(
                    thread, L"wrong number of arguments");
            }

            FunctionCallAdaptation adaptation =
                function_call_adaptation_for_positional_call(function, n_args);
            TrustedHandler handler;
            CodeObject *target_code_object =
                function.extract()->code_object.extract();
            if(target_code_object->trusted_handler_resolver != nullptr)
            {
                TrustedHandler resolved_handler =
                    target_code_object->trusted_handler_resolver(
                        vm, operand0_shape_key, operand1_shape_key, ShapeKey{},
                        trusted_handler_operand_order_for(operand_order));
                if(!resolved_handler.is_none())
                {
                    handler = resolved_handler;
                }
            }

            if(handler.arity == TrustedHandlerArity::Binary)
            {
                return OperatorWalkDescriptor::call_trusted_handler(
                    step.action, receiver, method_descriptor, arg_shape_key,
                    handler);
            }
            return OperatorWalkDescriptor::call_python_function(
                step.action, index + 1, operand_order, receiver,
                method_descriptor, arg_shape_key, function, n_args, adaptation,
                has_self);
        }

        debug_operator_table_exhausted();
    }

}  // namespace cl
