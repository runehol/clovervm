#ifndef CL_TUPLE_H
#define CL_TUPLE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/refcount.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "runtime/runtime_helpers.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;
    struct NormalizedNonstridedSlice;
    struct NormalizedGeneralSlice;

    struct TupleFromFrameArgumentsTag
    {
    };

    class Tuple : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Tuple;

        Tuple(BootstrapObjectTag, size_t size);
        Tuple(ClassObject *cls, size_t size);
        Tuple(ClassObject *cls, TupleFromFrameArgumentsTag, Value *fp,
              int8_t first_arg_reg, uint32_t n_args)
            : Object(cls, native_layout),
              size_value(TValue<SMI>::from_smi(static_cast<int64_t>(n_args)))
        {
            for(uint32_t idx = 0; idx < n_args; ++idx)
            {
                Value value = fp[int32_t(first_arg_reg) - int32_t(idx)];
                value.assert_not_vm_sentinel();
                elements[idx] = incref(value);
            }
        }

        size_t size() const
        {
            return static_cast<size_t>(size_value.extract());
        }
        bool empty() const { return size() == 0; }

        Value item_unchecked(size_t idx) const
        {
            assert(idx < size());
            return elements[idx];
        }
        ALWAYSINLINE void initialize_item_unchecked(size_t idx, Value value)
        {
            assert(idx < size());
            assert(elements[idx] == Value::not_present());
            value.assert_not_vm_sentinel();
            elements[idx] = incref(value);
        }
        [[nodiscard]] Value get_item(int64_t py_idx) const;
        [[nodiscard]] TValue<Tuple>
        get_slice(const NormalizedNonstridedSlice &slice) const;
        [[nodiscard]] TValue<Tuple>
        get_slice(const NormalizedGeneralSlice &slice) const;
        [[nodiscard]] TValue<Tuple> concat(const Tuple *other) const;
        int64_t count(Value needle) const;
        [[nodiscard]] Value index(Value needle, int64_t start_py_idx,
                                  int64_t stop_py_idx) const;

        static ALWAYSINLINE TValue<Tuple>
        from_frame_arguments(ThreadState *thread, Value *fp,
                             int8_t first_arg_reg, uint32_t n_args)
        {
            return thread->make_object_value<Tuple>(
                TupleFromFrameArgumentsTag{}, fp, first_arg_reg, n_args);
        }

        static size_t size_for(size_t size)
        {
            size_t storage_count = storage_count_for(size);
            return sizeof(Tuple) + (storage_count - 1) * sizeof(Value);
        }
        static size_t size_for(BootstrapObjectTag, size_t size)
        {
            return size_for(size);
        }
        static size_t size_for(ClassObject *, size_t size)
        {
            return size_for(size);
        }
        static size_t size_for(ClassObject *, TupleFromFrameArgumentsTag,
                               Value *, int8_t, uint32_t n_args)
        {
            return size_for(n_args);
        }
        static size_t object_size_in_bytes(const Tuple *tuple)
        {
            return size_for(tuple->size());
        }

        CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN_EXTENDS(Tuple, Object, size_value, 1);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(Tuple, Tuple::object_size_in_bytes);

    private:
        static size_t storage_count_for(size_t size)
        {
            return size == 0 ? 1 : size;
        }

        size_t wrap_index(int64_t py_idx) const;
        [[nodiscard]] Value check_index(size_t idx) const;
        void initialize_items(size_t size);

        Member<TValue<SMI>> size_value;
        Value elements[1];
    };

    class VirtualMachine;

    template <typename T> std::vector<T *> vector_from_tuple(const Tuple *tuple)
    {
        std::vector<T *> result;
        result.reserve(tuple->size());
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            result.push_back(assume_convert_to<T>(tuple->item_unchecked(idx)));
        }
        return result;
    }

    template <typename T>
    Value tuple_from_vector(const std::vector<T *> &values)
    {
        Tuple *tuple =
            active_vm()->tuple_class() == nullptr
                ? make_internal_raw<Tuple>(BootstrapObjectTag{}, values.size())
                : make_object_raw<Tuple>(values.size());
        for(size_t idx = 0; idx < values.size(); ++idx)
        {
            tuple->initialize_item_unchecked(idx, Value::from_oop(values[idx]));
        }
        return Value::from_oop(tuple);
    }

    BuiltinClassDefinition make_tuple_class(VirtualMachine *vm);
    void install_tuple_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_TUPLE_H
