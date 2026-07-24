#ifndef CL_JIT_CODE_OBJECT_H
#define CL_JIT_CODE_OBJECT_H

#include "jit/code_cache_types.h"
#include "memory/native_layout_declarations.h"
#include "object_model/heap_object.h"
#include "object_model/value.h"

#include <cstddef>
#include <span>

namespace cl::jit
{
    class JitCodeObject : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::JitCodeObject;

        JitCodeObject(CodeSlice code, std::span<Value> pool_values,
                      size_t encoded_code_size);

        const CodeSlice &code() const { return code_; }
        MachineAddress entry() const { return code_.execute_address(); }
        size_t encoded_code_size() const { return encoded_code_size_; }

        std::span<Value> value_pool_values() { return value_pool_values_; }
        std::span<const Value> value_pool_values() const
        {
            return value_pool_values_;
        }

        static void dealloc(HeapObject *obj);

        CL_DECLARE_CUSTOM_DEALLOC(JitCodeObject, dealloc);
        CL_DECLARE_STATIC_OBJECT_SIZE(JitCodeObject);

    private:
        CodeSlice code_;
        std::span<Value> value_pool_values_;
        size_t encoded_code_size_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CODE_OBJECT_H
