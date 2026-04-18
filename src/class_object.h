#ifndef CL_CLASS_OBJECT_H
#define CL_CLASS_OBJECT_H

#include "klass.h"
#include "object.h"
#include "owned_typed_value.h"
#include "shape.h"
#include "typed_value.h"

namespace cl
{
    class ClassObject : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Class", nullptr);

        ClassObject(TValue<String> name, uint32_t inline_slot_capacity);

        TValue<String> get_name() const { return name; }
        uint32_t get_inline_slot_capacity() const
        {
            return inline_slot_capacity;
        }
        Shape *get_initial_shape() const;

    private:
        MemberTValue<String> name;
        uint32_t inline_slot_capacity;
        MemberValue initial_shape;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(ClassObject, name, 2);
    };

}  // namespace cl

#endif  // CL_CLASS_OBJECT_H
