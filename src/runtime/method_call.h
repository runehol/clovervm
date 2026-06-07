#ifndef CL_METHOD_CALL_H
#define CL_METHOD_CALL_H

#include "bytecode/code_object.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/function.h"
#include "object_model/object.h"
#include "object_model/value.h"
#include <cassert>
#include <cstdint>

namespace cl
{
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

    static ALWAYSINLINE bool is_fixed_arity_function(TValue<Function> fun)
    {
        return !fun.extract()->has_varargs() &&
               fun.extract()->call_signature.min_positional_arity ==
                   fun.extract()->call_signature.max_positional_arity;
    }

    static ALWAYSINLINE FunctionCallAdaptation
    function_call_adaptation_for_positional_call(TValue<Function> fun,
                                                 uint32_t n_args)
    {
        if(fun.extract()->has_varargs())
        {
            return FunctionCallAdaptation::Varargs;
        }
        if(fun.extract()->default_parameters.value().has_value())
        {
            return n_args == fun.extract()->call_signature.function.n_parameters
                       ? FunctionCallAdaptation::FixedArity
                       : FunctionCallAdaptation::Defaults;
        }
        return is_fixed_arity_function(fun) ? FunctionCallAdaptation::FixedArity
                                            : FunctionCallAdaptation::Defaults;
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

    static ALWAYSINLINE MethodCallTargetStatus
    prepare_method_call_target_from_plan(Value receiver,
                                         const AttributeReadPlan &plan,
                                         Value &callable_out, Value &self_out)
    {
        self_out = Value::not_present();
        switch(plan.kind)
        {
            case AttributeReadPlanKind::ConstantValue:
                callable_out = plan.binding.self;
                return MethodCallTargetStatus::Ready;

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
        if(likely(plan.kind == AttributeReadPlanKind::BindFunctionReceiver))
        {
            callable_out = read_plan_storage_owner(receiver, plan)
                               ->read_storage_location(plan.storage_location);
            // The plan survives ordinary class contents writes, so the current
            // slot value decides whether this is still a bound method call
            // target.
            if(likely(callable_out.is_ptr() &&
                      callable_out.get_ptr()->native_layout_id() ==
                          NativeLayoutId::Function))
            {
                self_out = receiver;
                return MethodCallFastTargetStatus::Ready;
            }
        }

        return MethodCallFastTargetStatus::Slow;
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
}  // namespace cl

#endif  // CL_METHOD_CALL_H
