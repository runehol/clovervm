#ifndef CL_FLOAT_H
#define CL_FLOAT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"

#include <cstddef>

namespace cl
{
    class VirtualMachine;

    class Float : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Float;

        Float(ClassObject *cls, double value)
            : Object(cls, native_layout), value_(value)
        {
        }

        double value() const { return value_; }
        static size_t value_offset();

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Float, Object, 0);
        CL_DECLARE_STATIC_OBJECT_SIZE(Float);

    private:
        double value_;
    };

    inline size_t Float::value_offset() { return CL_OFFSETOF(Float, value_); }

    BuiltinClassDefinition make_float_class(VirtualMachine *vm);
    void install_float_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_FLOAT_H
