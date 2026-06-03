#ifndef CL_FLOAT_H
#define CL_FLOAT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"

namespace cl
{
    class VirtualMachine;

    class Float : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Float;

        Float(ClassObject *cls, double _value)
            : Object(cls, native_layout), value(_value)
        {
        }

        double value;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Float, Object, 0);
        CL_DECLARE_STATIC_OBJECT_SIZE(Float);
    };

    BuiltinClassDefinition make_float_class(VirtualMachine *vm);
    void install_float_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_FLOAT_H
