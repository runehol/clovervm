#include "jit/jit_code_object.h"

#include "object_model/refcount.h"

#include <cassert>

namespace cl::jit
{
    JitCodeObject::JitCodeObject(CodeSlice code, ValuePoolSlice value_pool,
                                 size_t encoded_size)
        : HeapObject(native_layout), code_(code), value_pool_(value_pool),
          encoded_size_(encoded_size)
    {
        assert(encoded_size != 0);
        assert(encoded_size <= code.capacity());
        for(Value value: value_pool_values())
        {
            incref(value);
        }
    }

    void JitCodeObject::dealloc(HeapObject *obj)
    {
        assert(obj->native_layout_id() == native_layout);
        JitCodeObject *jit_code = static_cast<JitCodeObject *>(obj);
        for(Value &value: jit_code->value_pool_values())
        {
            decref(value);
            value = Value::not_present();
        }
        jit_code->~JitCodeObject();
    }

}  // namespace cl::jit
