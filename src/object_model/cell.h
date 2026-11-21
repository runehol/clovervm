#ifndef CL_CELL_H
#define CL_CELL_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/value.h"

namespace cl
{
    class VirtualMachine;

    class Cell : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Cell;

        Cell(ClassObject *cls, Value value)
            : Object(cls, native_layout), value_(value)
        {
        }

        Value value() const { return value_.value(); }
        void set_value(Value value) { value_ = value; }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Cell, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(Cell);

    private:
        Member<Value> value_;
    };

    BuiltinClassDefinition make_cell_class(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_CELL_H
