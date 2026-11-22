#include "runtime/call_target.h"

#include "object_model/class_object.h"
#include "object_model/function.h"
#include "util/result.h"

namespace cl
{
    Expected<CallTargetDescriptor> resolve_call_target(Value callable)
    {
        if(callable.is_ptr())
        {
            Object *object = callable.get_ptr<Object>();
            if(object->native_layout_id() == NativeLayoutId::Function)
            {
                return Expected<CallTargetDescriptor>::ok(
                    {TValue<Function>::from_value_assumed(callable),
                     CallTargetGuard::exact(object)});
            }
            if(object->native_layout_id() == NativeLayoutId::ClassObject)
            {
                ConstructorThunkLookup constructor =
                    CL_TRY(static_cast<ClassObject *>(object)
                               ->get_or_create_constructor_thunk());
                if(constructor.is_found())
                {
                    return Expected<CallTargetDescriptor>::ok(
                        {TValue<Function>::from_oop(constructor.thunk),
                         CallTargetGuard::exact(object,
                                                constructor.lookup_cell)});
                }
            }
        }
        return Expected<CallTargetDescriptor>::raise_exception(
            L"TypeError", L"object is not callable");
    }
}  // namespace cl
