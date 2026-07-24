#include "jit/jit_code_object.h"

#include "object_model/refcount.h"

#include <cassert>

namespace cl::jit
{
    JitCodeObject::JitCodeObject(CodeSlice code, std::span<Value> pool_values,
                                 size_t encoded_code_size)
        : HeapObject(native_layout), code_(code),
          value_pool_values_(pool_values), encoded_code_size_(encoded_code_size)
    {
        assert(encoded_code_size != 0);
        assert(encoded_code_size <= code.capacity());
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
