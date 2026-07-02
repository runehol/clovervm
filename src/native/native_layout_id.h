#ifndef CL_NATIVE_LAYOUT_ID_H
#define CL_NATIVE_LAYOUT_ID_H

#include <cstdint>

namespace cl
{
    enum class NativeLayoutId : uint8_t
    {
        Invalid = 0,

        BigInt,
        String,
        List,
        Tuple,
        Dict,
        GeneralDict,
        SlotDict,
        Slice,
        Float,
        Function,
        RangeIterator,
        TupleIterator,
        ListIterator,
        DictKeysView,
        DictValuesView,
        DictItemsView,
        DictKeyIterator,
        DictValueIterator,
        DictItemIterator,
        ModuleObject,
        ModuleLoaderObject,
        ModuleSpecObject,
        CodeObject,
        ClassObject,
        Exception,
        StopIteration,
        Instance,
        Scope,
        Shape,
        ValidityCell,
        OverflowSlots,
        RawArrayBacking,
        ValueArrayBacking,
        HeapPtrArrayBacking,
        TestOnly,

        Count,
    };

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_ID_H
