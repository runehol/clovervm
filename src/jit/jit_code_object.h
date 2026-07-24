#ifndef CL_JIT_CODE_OBJECT_H
#define CL_JIT_CODE_OBJECT_H

#include "jit/code_cache_types.h"
#include "memory/native_layout_declarations.h"
#include "object_model/heap_object.h"

#include <cstddef>
#include <span>

namespace cl::jit
{
    class JitCodeObject : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::JitCodeObject;

        JitCodeObject(CodeSlice code, ValuePoolSlice value_pool,
                      size_t encoded_size);

        const CodeSlice &code() const { return code_; }
        const ValuePoolSlice &value_pool() const { return value_pool_; }
        MachineAddress entry() const { return code_.execute_address(); }
        size_t encoded_size() const { return encoded_size_; }

        std::span<Value> value_pool_values()
        {
            return {value_pool_.write_pointer(), value_pool_.slot_count()};
        }
        std::span<const Value> value_pool_values() const
        {
            return {value_pool_.write_pointer(), value_pool_.slot_count()};
        }

        static void dealloc(HeapObject *obj);

        CL_DECLARE_CUSTOM_DEALLOC(JitCodeObject, dealloc);
        CL_DECLARE_STATIC_OBJECT_SIZE(JitCodeObject);

    private:
        CodeSlice code_;
        ValuePoolSlice value_pool_;
        size_t encoded_size_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CODE_OBJECT_H
