#ifndef CL_BYTES_H
#define CL_BYTES_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace cl
{
    class ClassObject;
    class List;
    class String;
    class ThreadState;
    class Tuple;
    class VirtualMachine;
    struct NormalizedNonstridedSlice;
    struct NormalizedGeneralSlice;

    class Bytes : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Bytes;

        Bytes(ClassObject *cls, std::span<const uint8_t> bytes)
            : Object(cls, native_layout),
              count(TValue<SMI>::from_smi(static_cast<int64_t>(bytes.size())))
        {
            copy_bytes(bytes);
        }

        explicit Bytes(ClassObject *cls, TValue<SMI> _count)
            : Object(cls, native_layout), count(_count)
        {
        }

        [[nodiscard]] Value byte_at(ThreadState *thread, int64_t py_idx) const;
        [[nodiscard]] TValue<Bytes>
        get_slice(ThreadState *thread,
                  const NormalizedNonstridedSlice &slice) const;
        [[nodiscard]] TValue<Bytes>
        get_slice(ThreadState *thread,
                  const NormalizedGeneralSlice &slice) const;
        [[nodiscard]] TValue<Bytes> concat(const Bytes *other) const;
        bool startswith(const Bytes *prefix) const;
        bool endswith(const Bytes *suffix) const;
        int64_t find(const Bytes *needle) const;
        int64_t find_byte(uint8_t needle) const;
        [[nodiscard]] Value index(const Bytes *needle) const;
        [[nodiscard]] Value index_byte(uint8_t needle) const;
        int64_t count_subsequence(const Bytes *needle) const;
        int64_t count_byte(uint8_t needle) const;
        bool contains_byte(uint8_t needle) const;

        Member<TValue<SMI>> count;
        uint8_t data[1];

        static size_t size_for(ClassObject *, std::span<const uint8_t> bytes)
        {
            return storage_size_for(bytes.size());
        }
        static size_t size_for(ClassObject *, TValue<SMI> count)
        {
            return storage_size_for(size_t(count.extract()));
        }
        static size_t object_size_in_bytes(const Bytes *bytes)
        {
            return storage_size_for(size_t(bytes->count.extract()));
        }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Bytes, Object, 1);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(Bytes, Bytes::object_size_in_bytes);

    private:
        static size_t storage_count_for(size_t n_bytes)
        {
            return n_bytes == 0 ? 1 : n_bytes;
        }
        static size_t storage_size_for(size_t n_bytes)
        {
            return sizeof(Bytes) + (storage_count_for(n_bytes) - 1);
        }
        void copy_bytes(std::span<const uint8_t> bytes)
        {
            for(size_t idx = 0; idx < bytes.size(); ++idx)
            {
                data[idx] = bytes[idx];
            }
        }
    };

    std::span<const uint8_t> bytes_view(TValue<Bytes> value);
    uint64_t bytes_hash(TValue<Bytes> value);
    TValue<SMI> bytes_hash_normalized(TValue<Bytes> value);
    bool bytes_eq(TValue<Bytes> left, TValue<Bytes> right);
    int bytes_compare(TValue<Bytes> left, TValue<Bytes> right);
    TValue<String> bytes_repr(ThreadState *thread, TValue<Bytes> value);
    BuiltinClassDefinition make_bytes_class(VirtualMachine *vm);
    void install_bytes_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_BYTES_H
