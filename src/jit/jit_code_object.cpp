#include "jit/jit_code_object.h"

#include "object_model/refcount.h"

#include <cassert>

namespace cl::jit
{
    JitCodeObject::JitCodeObject(CodeSlice code,
                                 MachineAddress interpreter_entry_thunk,
                                 std::span<std::byte> constant_pool,
                                 size_t tagged_value_count,
                                 size_t encoded_code_size)
        : HeapObject(native_layout), code_(code),
          interpreter_entry_thunk_(interpreter_entry_thunk),
          constant_pool_(constant_pool),
          tagged_value_count_(tagged_value_count),
          encoded_code_size_(encoded_code_size)
    {
        assert(encoded_code_size != 0);
        assert(encoded_code_size <= code.capacity());
        assert(tagged_value_count <= constant_pool.size() / sizeof(Value));
        assert(reinterpret_cast<uintptr_t>(constant_pool.data()) %
                   alignof(Value) ==
               0);
        for(Value value: tagged_values())
        {
            incref(value);
        }
    }

    void JitCodeObject::dealloc(HeapObject *obj)
    {
        assert(obj->native_layout_id() == native_layout);
        JitCodeObject *jit_code = static_cast<JitCodeObject *>(obj);
        for(Value &value: jit_code->tagged_values())
        {
            decref(value);
            value = Value::not_present();
        }
        jit_code->~JitCodeObject();
    }

}  // namespace cl::jit
